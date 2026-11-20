#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QstandardPaths>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QtMath>
#include <QDir>
#include <QFileDialog>
#include <QSettings>
#include <QTemporaryFile>
#include <QComboBox>
#include <QRegularExpression>
#include <optional>
#include <string>
#include <string_view>
#include "regionselector.h"
#include "recordingparams.h"

namespace{
// 在目标目录试建一个临时文件，离开函数时自动删除
bool canWriteDirectory(const QString &path)
{
    if(!QFileInfo(path).isDir())
        return false;

    QTemporaryFile check(QDir(path).filePath("screenRecorder-check-XXXXXX"));
    return check.open();
}
struct CaptureRect{
    int x=0;
    int y=0;
    int width=0;
    int height=0;
};

// 把Qt逻辑坐标下的捕获矩形换算程gdigrab参数
// gdigrab的offset 以主显示器左上角为原点，所以先减去主屏逻辑原点，再乘缩放比
// 宽高取偶，避免yuv420p因奇数尺寸编码失败
CaptureRect nativeCaptureRect(const QRect &logicalRect,const QRect &primaryRect, qreal dpr)
{
    CaptureRect rect;
    rect.x=qRound((logicalRect.x()-primaryRect.x())*dpr);
    rect.y=qRound((logicalRect.y()-primaryRect.y())*dpr);
    rect.width=rec::evenDimension(qRound(logicalRect.width()*dpr));
    rect.height=rec::evenDimension(qRound(logicalRect.height()*dpr));
    return rect;
}
// 查询 FFmpeg 对指定设备或编码器的支持情况。
// program 是可执行文件路径；topic 是要查询的 -h 主题；marker 是成功输出标记，即预期在FFmpeg输出中找到的文字；name 给用户看的功能名称，用于拼接错误提示。
QString checkFFmpegFeature(const QString &program,const QString &topic,const QByteArray &marker,const QString &name)
{
    // 使用独立进程检查能力，避免触发录制进程 m_ffmpeg 的状态回调。
    QProcess probe;
    // MergedChannels把标准错误合并到标准输出
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(program,{"-hide_banner","-h",topic});

    // shortcut: 两次帮助查询会短暂阻塞界面；若实际出现卡顿，再改为异步检查
    if(!probe.waitForFinished(2000)){
        const QString error = probe.errorString();
        if(probe.state() != QProcess::NotRunning){
            probe.kill();
            probe.waitForFinished(1000);
        }
        return QStringLiteral("检查 %1 失败：%2").arg(name,error);
    }

    const QByteArray output = probe.readAllStandardOutput();
    // FFmpeg 对未知 -h 主题也可能返回退出码 0，所以还需检查成功输出标记。
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode()!=0||!output.contains(marker)){
        return QStringLiteral("当前 FFmpeg 不支持 %1").arg(name);
    }
    return {};
}

// FFmpeg 的设备列表写在标准错误中，列举结束时可能返回非零退出码。
QStringList microphoneDevices(const QString &ffmpeg, QString *error)
{
    if (ffmpeg.isEmpty()) {
        *error = QStringLiteral("找不到 FFmpeg，无法加载麦克风");
        return {};
    }
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(ffmpeg, {"-hide_banner", "-list_devices", "true", "-f", "dshow", "-i", "dummy"});
    // shortcut: 设备查询最多阻塞界面 3 秒；若出现卡顿再改为异步查询。
    if (!probe.waitForFinished(3000)) {
        if (probe.state() != QProcess::NotRunning) {
            probe.kill();
            probe.waitForFinished(1000);
        }
        *error = QStringLiteral("麦克风设备查询失败或超时");
        return {};
    }
    const QString output = QString::fromUtf8(probe.readAllStandardOutput());
    const QRegularExpression pattern(QStringLiteral("\\\"([^\\\"]+)\\\"\\s+\\(audio\\)"));
    QStringList devices;
    auto matches = pattern.globalMatch(output);
    while (matches.hasNext())
        devices.append(matches.next().captured(1));
    if (devices.isEmpty()) {
        *error = output.contains("Could not enumerate audio only devices")
            ? QStringLiteral("未检测到麦克风，请检查设备或权限")
            : QStringLiteral("无法获取麦克风列表，请检查 FFmpeg 的 DirectShow 支持");
    }
    return devices;
}
// 用 ffprobe 核对 MP4 的视频编码、尺寸和时长；返回空字符串表示通过。
QString checkVideoFile(const QString &path, const QString &expectedCodec,int expectedWidth, int expectedHeight)
{
    const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
    if(ffprobe.isEmpty())
        return QStringLiteral("找不到 ffprobe，无法验证视频");
    QProcess probe;
    probe.start(ffprobe,{
        "-v","error",
        "-select_streams","v:0",
        "-show_entries","stream=codec_name,width,height:format=duration",
        "-of","default=noprint_wrappers=1",
        path
    });

    // shortcut: 本地文件探测最多阻塞界面 3 秒；后续可改为异步进程。
    if(!probe.waitForFinished(3000)){
        if(probe.state() != QProcess::NotRunning){
            probe.kill();
            probe.waitForFinished(1000);
        }
        return QStringLiteral("ffprobe 检查超时或启动失败");
    }

    if(probe.exitStatus()!= QProcess::NormalExit || probe.exitCode() != 0)
        return QStringLiteral("ffprobe 无法读取视频：%1").arg(QString::fromUtf8(probe.readAllStandardError()).trimmed().left(160));

    const QString output = QString::fromUtf8(probe.readAllStandardOutput());
    // ffprobe 每行输出 key=value；field("width") 从 width=2560 中取出 "2560"。
    // [&output] 让这个小函数读取上面的 output，不复制整份输出。
    auto field = [&output](const QString &key){
        for(const QString &line : output.split('\n')){
            if(line.startsWith(key+'='))
                return line.mid(key.size()+1).trimmed();
        }
        return QString();
    };

    // toInt/toDouble 通过布尔指针回填“转换是否成功”，数值本身由返回值取得。
    bool widthOk = false, heightOk = false, durationOk = false;
    const int width = field("width").toInt(&widthOk);
    const int height = field("height").toInt(&heightOk);
    const double duration = field("duration").toDouble(&durationOk);

    // 错误原因分类
    const QString codecName = field("codec_name");
    if(codecName != expectedCodec)
        return QStringLiteral("视频编码未通过，探测到：%1").arg(codecName.isEmpty()?QStringLiteral("未识别"):codecName);

    if(!widthOk || !heightOk || width <= 0 || height <= 0)
        return QStringLiteral("视频尺寸无效");
    if(expectedWidth>0&&width!=expectedWidth)
        return QStringLiteral("视频宽度不符：期望%1，实际%2").arg(expectedWidth).arg(width);
    if(expectedHeight>0&&height!=expectedHeight)
        return QStringLiteral("视频高度不符：期望%1，实际%2").arg(expectedHeight).arg(height);
    if(!durationOk || duration <= 0)
        return QStringLiteral("视频时长无效");
    return {}; // 空字符串表示检查通过
}

// 用户选择麦克风时，还需确认输出文件包含音频流。
QString checkAudioFile(const QString &path)
{
    const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
    if (ffprobe.isEmpty())
        return QStringLiteral("找不到 ffprobe，无法验证音频");
    QProcess probe;
    probe.start(ffprobe, {"-v", "error", "-select_streams", "a:0", "-show_entries",
                          "stream=codec_type", "-of", "default=noprint_wrappers=1", path});
    // shortcut: 音频流检查最多阻塞界面 3 秒；若出现卡顿再改为异步查询。
    if (!probe.waitForFinished(3000)) {
        if (probe.state() != QProcess::NotRunning) {
            probe.kill();
            probe.waitForFinished(1000);
        }
        return QStringLiteral("音频验证失败或超时");
    }
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0 ||
        !probe.readAllStandardOutput().contains("codec_type=audio"))
        return QStringLiteral("未检测到有效音频流，请检查音频设备");
    return {};
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->usingRegionCheckBox->setEnabled(true);
    ui->stopButton->setEnabled(false);
    ui->chooseOutputDirectoryButton->setEnabled(true);
    ui->cancelCountdownButton->setEnabled(false);
    for (int i = 0; i < ui->countdownSecondsComboBox->count(); ++i)
        ui->countdownSecondsComboBox->setItemData(i, i + 1);
    ui->countdownSecondsComboBox->setEnabled(ui->countdownCheckBox->isChecked());
    connect(ui->countdownCheckBox, &QCheckBox::toggled, ui->countdownSecondsComboBox, &QComboBox::setEnabled);

    // 倒计时结束后才启动已准备好的录制进程。
    m_countdownTimer.setInterval(1000);
    m_countdownTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        if (--m_countdownRemaining > 0) {
            ui->statusLabel->setText(QStringLiteral("状态：%1 秒后开始录制").arg(m_countdownRemaining));
            return;
        }
        m_countdownTimer.stop();
        ui->cancelCountdownButton->setEnabled(false);
        ui->statusLabel->setText(QStringLiteral("状态：准备中"));
        m_ffmpeg.start(m_pendingProgram, m_pendingArgs);
        m_pendingProgram.clear();
        m_pendingArgs.clear();
    });
    connect(ui->cancelCountdownButton, &QPushButton::clicked, this, [this] {
        if (!m_countdownTimer.isActive())
            return;
        m_countdownTimer.stop();
        m_pendingProgram.clear();
        m_pendingArgs.clear();
        m_outputFile.clear();
        m_waitingAudioStop = false;
        cleanupAudioThread(); // 倒计时取消时管道尚未连接，线程会被取消并等待退出。
        ui->statusLabel->setText(QStringLiteral("状态：倒计时已取消"));
        ui->cancelCountdownButton->setEnabled(false);
        ui->startButton->setEnabled(true);
        ui->regionButton->setEnabled(true);
        ui->usingRegionCheckBox->setEnabled(true);
        ui->chooseOutputDirectoryButton->setEnabled(true);
        ui->frameRateComboBox->setEnabled(true);
        ui->encoderComboBox->setEnabled(true);
        ui->qualityComboBox->setEnabled(true);
        ui->microphoneCheckBox->setEnabled(true);
        ui->microphoneComboBox->setEnabled(ui->microphoneCheckBox->isChecked() && ui->microphoneComboBox->count() > 1);
        ui->systemAudioCheckBox->setEnabled(true);
        ui->countdownCheckBox->setEnabled(true);
        ui->countdownSecondsComboBox->setEnabled(ui->countdownCheckBox->isChecked());
    });
    // 保存文件配置；读取顺序：上次目录-系统视频目录-用户主目录。旧目录被删时自动回退。无效模式值按全屏处理
    QSettings settings;
    // 显示文字由 mainwindow.ui 提供，这里按帧率表填入实际数值，避免两处各写一份。
    const int frameRateItems = qMin(ui->frameRateComboBox->count(),
                                    static_cast<int>(rec::kFrameRates.size()));
    for (int index = 0; index < frameRateItems; ++index)
        ui->frameRateComboBox->setItemData(index, rec::kFrameRates[static_cast<std::size_t>(index)]);
    const int savedFrameRate = settings.value("frameRate", rec::kFrameRates.front()).toInt();
    const int frameRateIndex = ui->frameRateComboBox->findData(savedFrameRate);
    ui->frameRateComboBox->setCurrentIndex(frameRateIndex >= 0 ? frameRateIndex : 0);
    connect(ui->frameRateComboBox, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings settings;
        settings.setValue("frameRate", ui->frameRateComboBox->currentData());
    });
    // 只展示当前 FFmpeg 支持的编码器，保存的编码器失效时要求用户重新选择。
    ui->encoderComboBox->clear();
    ui->encoderComboBox->addItem(QStringLiteral("请选择可用编码器"));
    const QString ffmpegPath = QStandardPaths::findExecutable("ffmpeg");
    if (!ffmpegPath.isEmpty()) {
        if (checkFFmpegFeature(ffmpegPath, "encoder=libx264", "Encoder libx264 [", QStringLiteral("H.264")).isEmpty())
            ui->encoderComboBox->addItem(QStringLiteral("H.264（libx264）"), QStringLiteral("libx264"));
        if (checkFFmpegFeature(ffmpegPath, "encoder=libx265", "Encoder libx265 [", QStringLiteral("H.265")).isEmpty())
            ui->encoderComboBox->addItem(QStringLiteral("H.265（libx265）"), QStringLiteral("libx265"));
    }
    const int encoderIndex = ui->encoderComboBox->findData(settings.value("videoEncoder", "libx264"));
    ui->encoderComboBox->setCurrentIndex(encoderIndex >= 0 ? encoderIndex : 0);
    if (encoderIndex < 0)
        ui->statusbar->showMessage(QStringLiteral("已保存的编码器不可用，请选择可用编码器"));
    connect(ui->encoderComboBox, &QComboBox::currentIndexChanged, this, [this](int) {
        if (ui->encoderComboBox->currentData().isValid()) {
            QSettings settings;
            settings.setValue("videoEncoder", ui->encoderComboBox->currentData());
        }
    });

    ui->qualityComboBox->clear();
    ui->qualityComboBox->addItem(QStringLiteral("画质：较高"), QStringLiteral("high"));
    ui->qualityComboBox->addItem(QStringLiteral("画质：均衡"), QStringLiteral("balanced"));
    ui->qualityComboBox->addItem(QStringLiteral("画质：较小文件"), QStringLiteral("small"));
    const int qualityIndex = ui->qualityComboBox->findData(settings.value("videoQuality", "balanced"));
    ui->qualityComboBox->setCurrentIndex(qualityIndex >= 0 ? qualityIndex : 1);
    connect(ui->qualityComboBox, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings settings;
        settings.setValue("videoQuality", ui->qualityComboBox->currentData());
    });
    ui->microphoneComboBox->clear();
    ui->microphoneComboBox->addItem(QStringLiteral("请先启用麦克风"));
    ui->microphoneComboBox->setEnabled(false);
    connect(ui->microphoneCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings settings;
        settings.setValue("microphoneEnabled", enabled);
        ui->microphoneComboBox->clear();
        if (!enabled) {
            ui->microphoneComboBox->addItem(QStringLiteral("请先启用麦克风"));
            ui->microphoneComboBox->setEnabled(false);
            return;
        }
        QString error;
        const QStringList devices = microphoneDevices(QStandardPaths::findExecutable("ffmpeg"), &error);
        if (devices.isEmpty()) {
            ui->microphoneComboBox->addItem(QStringLiteral("无可用麦克风"));
            ui->microphoneComboBox->setEnabled(false);
            ui->statusbar->showMessage(error);
            return;
        }
        const QString savedDevice = settings.value("microphoneDevice").toString();
        // 旧设备消失时要求重新选择，避免悄悄切换录音来源。
        ui->microphoneComboBox->addItem(QStringLiteral("请选择麦克风"));
        for (const QString &device : devices)
            ui->microphoneComboBox->addItem(device, device);
        const int index = ui->microphoneComboBox->findData(savedDevice);
        ui->microphoneComboBox->setCurrentIndex(index >= 0 ? index : 0);
        ui->microphoneComboBox->setEnabled(true);
    });
    connect(ui->microphoneComboBox, &QComboBox::currentIndexChanged, this, [this](int) {
        if (ui->microphoneComboBox->currentData().isValid()) {
            QSettings settings;
            settings.setValue("microphoneDevice", ui->microphoneComboBox->currentData());
        }
    });
    ui->microphoneCheckBox->setChecked(settings.value("microphoneEnabled", false).toBool());
    // 原生系统声音采集（WASAPI 回环 + 命名管道）已接入，勾选状态随设置持久化。
    connect(ui->systemAudioCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings settings;
        settings.setValue("systemAudioEnabled", enabled);
    });
    ui->systemAudioCheckBox->setChecked(settings.value("systemAudioEnabled", false).toBool());
    const QString savedDir = settings.value("outputDirectory").toString();
    // 上次目录不可用时，尝试系统默认的“视频”目录。
    const QString moviesDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    for(const QString &candidate:{savedDir,moviesDir,QDir::homePath()}){
        if(canWriteDirectory(candidate)){
            m_outputDirectory = candidate;
            break;
        }
    }
    // 显示最终选用的保存目录；仅展示时换成 Windows 常见的反斜杠。
    ui->outputDirectoryLabel->setText(
        m_outputDirectory.isEmpty()
            ?QStringLiteral("保存目录：未设置")
            :QStringLiteral("保存目录：%1").arg(QDir::toNativeSeparators(m_outputDirectory)));

    const QString mode = settings.value("recordingMode","full").toString();
    ui->usingRegionCheckBox->setChecked(mode == "region");
    if(mode == "region")
        ui->statusLabel->setText(QStringLiteral("状态：请重新选择区域"));


    // FFmpeg 真正启动后，更新状态并允许停止。
    connect(&m_ffmpeg,&QProcess::started,this,[this](){
        ui->statusLabel->setText(QStringLiteral("状态：录制中"));
        ui->stopButton->setEnabled(true);
    });
    // FFmpeg 启动失败时恢复按钮，并显示错误原因。
    connect(&m_ffmpeg,&QProcess::errorOccurred, this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){
            m_waitingAudioStop = false;
            cleanupAudioThread();
            ui->startButton->setEnabled(true);
            ui->statusLabel->setText(QStringLiteral("状态：启动失败"));
            ui->statusbar->showMessage(m_ffmpeg.errorString());
            ui->regionButton->setEnabled(true);
            ui->usingRegionCheckBox->setEnabled(true);
            ui->chooseOutputDirectoryButton->setEnabled(true);
            ui->frameRateComboBox->setEnabled(true);
            ui->encoderComboBox->setEnabled(true);
            ui->qualityComboBox->setEnabled(true);
            ui->microphoneCheckBox->setEnabled(true);
            ui->microphoneComboBox->setEnabled(ui->microphoneCheckBox->isChecked() && ui->microphoneComboBox->count() > 1);
            ui->systemAudioCheckBox->setEnabled(true);
            ui->countdownCheckBox->setEnabled(true);
            ui->countdownSecondsComboBox->setEnabled(ui->countdownCheckBox->isChecked());
        }
    });
    // 进程结束后恢复界面，并区分停止超时、FFmpeg 失败与视频验证结果。
    connect(&m_ffmpeg,&QProcess::finished, this,[this](int exitcode,QProcess::ExitStatus exitStatus){
        if (m_stopElapsed.isValid()) {
            qInfo() << "[stop] click_to_finished_ms =" << m_stopElapsed.elapsed();
            m_stopElapsed.invalidate();
        }
        // 停止计时器，并记住本次是否因超时被强制结束。
        m_stopTimer.stop();
        const bool timedOut = m_stopTimedOut;
        m_stopTimedOut = false;
        m_waitingAudioStop = false;
        cleanupAudioThread(); // FFmpeg 已退出，采集线程必然已收尾或被取消。

        ui->startButton->setEnabled(true);
        ui->stopButton->setEnabled(false);
        // 录制结束后允许再次选择区域。
        ui->regionButton->setEnabled(true);
        // 录制结束后允许切换全屏/区域模式，即使当前尚未选区。
        ui->usingRegionCheckBox->setEnabled(true);
        ui->chooseOutputDirectoryButton->setEnabled(true);
        ui->frameRateComboBox->setEnabled(true);
        ui->encoderComboBox->setEnabled(true);
        ui->qualityComboBox->setEnabled(true);
        ui->microphoneCheckBox->setEnabled(true);
        ui->microphoneComboBox->setEnabled(ui->microphoneCheckBox->isChecked() && ui->microphoneComboBox->count() > 1);
        ui->systemAudioCheckBox->setEnabled(true);
        ui->countdownCheckBox->setEnabled(true);
        ui->countdownSecondsComboBox->setEnabled(ui->countdownCheckBox->isChecked());
        if(timedOut){
            ui->statusLabel->setText(QStringLiteral("状态：停止超时"));
            ui->statusbar->showMessage(QStringLiteral("FFmpeg 已被强制结束，文件可能无法播放：%1").arg(m_outputFile));
            return;
        }

        // 仅在 FFmpeg 正常退出且文件非空时，才用 ffprobe 验证视频流。
        const bool ffmpegSucceeded = exitStatus == QProcess::NormalExit && exitcode ==0 && QFileInfo(m_outputFile).size() > 0;

        QString videoError = ffmpegSucceeded
                                 ? checkVideoFile(m_outputFile, m_expectedVideoCodec,
                                                  m_expectedVideoWidth, m_expectedVideoHeight)
                                 : QString{};
        // 勾选任一音频来源时，都要求输出包含音频流。
        if (ffmpegSucceeded && videoError.isEmpty() &&
            (ui->microphoneCheckBox->isChecked() || ui->systemAudioCheckBox->isChecked()))
            videoError = checkAudioFile(m_outputFile);

        const bool success = ffmpegSucceeded && videoError.isEmpty();

        if(success){
            ui->statusLabel->setText(QStringLiteral("状态：录制完成"));
            ui->statusbar->showMessage(QStringLiteral("文件：")+ m_outputFile);
        }
        else if(ffmpegSucceeded){
            ui->statusLabel->setText(QStringLiteral("状态：文件验证未通过"));
            ui->statusbar->showMessage(videoError + QStringLiteral("；文件：")+m_outputFile);
        }
        else{
            ui->statusLabel->setText(QStringLiteral("状态：录制失败"));
            const QString error = QString::fromUtf8(
                m_ffmpeg.readAllStandardError()).trimmed();
            const QString firstLine = error.section('\n', 0, 0).trimmed();
            QString message;

            // FFmpeg 返回错误码时也可能被标为 CrashExit；先读取它自己的错误输出。
            if (error.contains("No space left on device", Qt::CaseInsensitive)
                       || error.contains("not enough space", Qt::CaseInsensitive)) {
                message = QStringLiteral("磁盘空间不足，请清理空间或更换保存目录");
            } else if (error.contains("Permission denied", Qt::CaseInsensitive)
                       || error.contains("Access is denied", Qt::CaseInsensitive)) {
                message = QStringLiteral("无法写入视频文件，请检查保存目录权限");
            } else if (error.contains("Error opening input", Qt::CaseInsensitive)
                       || error.contains("Could not find video device", Qt::CaseInsensitive)
                       || error.contains("Could not find audio", Qt::CaseInsensitive)) {
                message = ui->microphoneCheckBox->isChecked()
                    ? QStringLiteral("无法打开录制源，请检查屏幕、音频设备及权限")
                    : QStringLiteral("无法打开屏幕录制源，请检查选区和屏幕状态");
            } else if (error.contains("Invalid argument", Qt::CaseInsensitive)) {
                message = QStringLiteral("FFmpeg 拒绝录制参数，请检查选区和保存路径");
            } else if (error.isEmpty() && exitStatus != QProcess::NormalExit) {
                message = QStringLiteral("FFmpeg 意外终止，且没有提供错误信息");
            } else if (error.isEmpty()) {
                message = QStringLiteral("FFmpeg 已退出，但没有生成可用视频");
            } else {
                message = QStringLiteral("FFmpeg 报错：%1").arg(firstLine.left(180));
            }

            if (QFileInfo::exists(m_outputFile)) {
                message += QStringLiteral("；文件可能不完整：%1").arg(m_outputFile);
            } else {
                message += QStringLiteral("；未生成视频文件");
            }
            ui->statusbar->showMessage(message);
        }
    });
    // 点击开始：定位 FFmpeg、构造参数，然后异步启动录制进程。
    connect(ui->startButton,&QPushButton::clicked,this,[this](){
        if(m_ffmpeg.state() != QProcess::NotRunning || m_countdownTimer.isActive())
            return;

        const QString ffmpeg = QStandardPaths::findExecutable(
            QStringLiteral("ffmpeg"));
        if(ffmpeg.isEmpty()){
            ui->statusLabel->setText(QStringLiteral("状态：找不到FFmpeg"));
            ui->regionButton->setEnabled(true);
            return;
        }
        const QString captureError = checkFFmpegFeature(ffmpeg,"demuxer=gdigrab","Demuxer gdigrab [",QStringLiteral("Windows 屏幕捕获 gdigrab"));
        if(!captureError.isEmpty()){
            ui->statusLabel->setText(QStringLiteral("状态：环境检查失败"));
            ui->statusbar->showMessage(captureError);
            return;
        }

        const QString encoder = ui->encoderComboBox->currentData().toString();
        if (encoder.isEmpty()) {
            ui->statusLabel->setText(QStringLiteral("状态：请选择可用编码器"));
            return;
        }
        const QString encoderError = checkFFmpegFeature(
            ffmpeg, QStringLiteral("encoder=") + encoder,
            QByteArray("Encoder ") + encoder.toUtf8() + " [", encoder);

        if(!encoderError.isEmpty()){
            ui->statusLabel->setText(QStringLiteral("状态：环境检查失败"));
            ui->statusbar->showMessage(encoderError);
            return;
        }
        QStringList args{
            "-hide_banner", "-loglevel", "error", "-n",
            "-f", "gdigrab", "-framerate", ui->frameRateComboBox->currentData().toString()
        };
        // 仅在区域模式下，把 Qt 逻辑坐标换算成 FFmpeg 所需的屏幕像素参数。
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) {
            ui->statusLabel->setText(QStringLiteral("状态：找不到主显示器"));
            return;
        }
        const QRect screenRect = screen->geometry();
        const bool useRegion = ui->usingRegionCheckBox->isChecked();
        if (useRegion && !m_recordingRegion.isValid()) {
            ui->statusLabel->setText(QStringLiteral("状态：请先选择区域"));
            return;
        }
        // 全屏也显式指定主屏矩形：desktop 默认可能覆盖整个虚拟桌面，
        // 多显示器时会录进副屏；显式 offset/video_size 才能保证首版只录主屏。
        const QRect logicalCapture = useRegion ? m_recordingRegion : screenRect;
        const CaptureRect capture = nativeCaptureRect(logicalCapture, screenRect,
                                                      screen->devicePixelRatio());
        if (capture.width <= 0 || capture.height <= 0) {
            ui->statusLabel->setText(QStringLiteral("状态：选区太小，无法录制"));
            return;
        }
        args << "-offset_x" << QString::number(capture.x)
             << "-offset_y" << QString::number(capture.y)
             << "-video_size" << QStringLiteral("%1x%2").arg(capture.width).arg(capture.height);
        m_expectedVideoWidth = capture.width;
        m_expectedVideoHeight = capture.height;
        // 启动前检查目录能否写入，并确定本次录像的完整文件路径。
        if(!canWriteDirectory(m_outputDirectory)){
            ui->statusLabel->setText(QStringLiteral("状态：保存目录不可写，请重新选择"));
            return;
        }
        m_outputFile = nextOutputFilePath();

        args << "-i" << "desktop";
        // 麦克风输入先只占位输入索引；映射和音频编码参数在最后统一拼接，
        // 否则系统声音的管道输入插进来会打乱流索引。
        bool microphoneAdded = false;
        if (ui->microphoneCheckBox->isChecked()) {
            const QString device = ui->microphoneComboBox->currentData().toString();
            if (device.isEmpty()) {
                ui->statusLabel->setText(QStringLiteral("状态：请选择可用麦克风"));
                ui->statusbar->showMessage(QStringLiteral("重新勾选麦克风可刷新设备列表"));
                return;
            }
            args << "-f" << "dshow" << "-i" << (QStringLiteral("audio=") + device);
            microphoneAdded = true;
        }
        // 编码器与画质先解析成枚举：CRF 数值和 ffprobe 期望的编码名都由 recordingparams.h 决定。
        const std::optional<rec::Encoder> parsedEncoder = rec::encoderFromName(encoder.toStdString());
        if (!parsedEncoder) {
            ui->statusLabel->setText(QStringLiteral("状态：编码器名称无法识别"));
            ui->statusbar->showMessage(QStringLiteral("请重新选择可用编码器"));
            return;
        }
        // 画质选项缺失时沿用旧实现的默认分支，按均衡档处理。
        const rec::Quality quality = rec::qualityFromName(
            ui->qualityComboBox->currentData().toString().toStdString())
            .value_or(rec::Quality::Balanced);
        // x264 与 x265 各自使用质量参数，数值不代表跨编码器的相同画质。
        const int crf = rec::qualityCrf(*parsedEncoder, quality);
        const std::string_view codecName = rec::expectedVideoCodec(*parsedEncoder);
        m_expectedVideoCodec = QString::fromLatin1(codecName.data(), static_cast<int>(codecName.size()));

        // 映射与编码参数（输出文件除外）：视频恒为输入 0，麦克风为 1，管道音频顺延。
        const bool systemAudioWanted = ui->systemAudioCheckBox->isChecked();
        QStringList mapAndEncode{ "-map", "0:v:0" };
        int nextAudioInput = 1;
        if (microphoneAdded) {
            mapAndEncode << "-map" << "1:a:0";
            nextAudioInput = 2;
        }
        if (systemAudioWanted)
            mapAndEncode << "-map" << QStringLiteral("%1:a:0").arg(nextAudioInput);
        if (microphoneAdded || systemAudioWanted)
            mapAndEncode << "-c:a" << "aac";
        mapAndEncode << "-c:v"<< encoder
                     << "-preset"<<"ultrafast"
                     << "-crf" << QString::number(crf)
                     << "-pix_fmt"<<"yuv420p";

        ui->startButton->setEnabled(false);
        ui->statusLabel->setText(QStringLiteral("状态：准备中"));
        ui->statusbar->clearMessage();
        // 录制前禁用区域选择，避免遮罩进入录制
        ui->regionButton->setEnabled(false);
        ui->usingRegionCheckBox->setEnabled(false);
        // 开始录制时，禁用选择保存目录地址
        ui->chooseOutputDirectoryButton->setEnabled(false);
        ui->frameRateComboBox->setEnabled(false);
        ui->encoderComboBox->setEnabled(false);
        ui->qualityComboBox->setEnabled(false);
        ui->microphoneCheckBox->setEnabled(false);
        ui->microphoneComboBox->setEnabled(false);
        ui->systemAudioCheckBox->setEnabled(false);
        ui->countdownCheckBox->setEnabled(false);
        ui->countdownSecondsComboBox->setEnabled(false);

        if (systemAudioWanted) {
            // 系统声音：先让采集线程建好命名管道，FFmpeg 的真实音频格式
            // （采样率/声道/布局）由 pipeReady 带回后再补齐参数并启动。
            const QString pipeName = QStringLiteral("\\\\.\\pipe\\ScreenRecorder_%1_%2")
                .arg(QCoreApplication::applicationPid())
                .arg(QDateTime::currentMSecsSinceEpoch());
            m_audioThread = new SystemAudioThread(pipeName);
            connect(m_audioThread, &SystemAudioThread::pipeReady, this,
                [this, ffmpeg, args, mapAndEncode](const QString &name, quint32 sampleRate,
                                                   quint16 channels, quint32 channelMask) mutable {
                // 输入参数必须与采集线程确认过的 float32 格式完全一致。
                args << "-f" << "f32le" << "-ar" << QString::number(sampleRate)
                     << "-ac" << QString::number(channels)
                     << "-channel_layout" << "0x" + QString::number(channelMask, 16)
                     << "-probesize" << "32" << "-analyzeduration" << "0"
                     << "-i" << name
                     << mapAndEncode << m_outputFile;
                launchRecording(ffmpeg, args);
            });
            connect(m_audioThread, &SystemAudioThread::captureStopped, this, [this](quint64) {
                // 采集线程收尾（管道已关闭，FFmpeg 音频流自然结束）后再发 q，
                // 直接发 q 会打断管道写入并让线程报错。
                if (m_waitingAudioStop) {
                    m_waitingAudioStop = false;
                    if (m_ffmpeg.state() == QProcess::Running)
                        sendStopToFFmpeg();
                }
            });
            connect(m_audioThread, &SystemAudioThread::captureFailed, this,
                [this](const QString &message) {
                const bool wasWaitingStop = m_waitingAudioStop;
                m_waitingAudioStop = false;
                const bool recording = m_ffmpeg.state() == QProcess::Running;
                cleanupAudioThread();
                ui->statusbar->showMessage(QStringLiteral("系统声音采集失败：%1").arg(message));
                if (!recording) {
                    // FFmpeg 尚未启动：整体中止本次录制，恢复界面。
                    ui->statusLabel->setText(QStringLiteral("状态：启动失败"));
                    ui->startButton->setEnabled(true);
                    ui->regionButton->setEnabled(true);
                    ui->usingRegionCheckBox->setEnabled(true);
                    ui->chooseOutputDirectoryButton->setEnabled(true);
                    ui->frameRateComboBox->setEnabled(true);
                    ui->encoderComboBox->setEnabled(true);
                    ui->qualityComboBox->setEnabled(true);
                    ui->microphoneCheckBox->setEnabled(true);
                    ui->microphoneComboBox->setEnabled(ui->microphoneCheckBox->isChecked() && ui->microphoneComboBox->count() > 1);
                    ui->systemAudioCheckBox->setEnabled(true);
                    ui->countdownCheckBox->setEnabled(true);
                    ui->countdownSecondsComboBox->setEnabled(ui->countdownCheckBox->isChecked());
                    return;
                }
                // 采集中途失败：视频继续录制并在状态栏提示；若正在等停止则直接发 q。
                if (wasWaitingStop && m_ffmpeg.state() == QProcess::Running)
                    sendStopToFFmpeg();
            });
            ui->statusLabel->setText(QStringLiteral("状态：准备中（启动系统声音）"));
            m_audioThread->startCapture();
            return;
        }

        args << mapAndEncode << m_outputFile;
        launchRecording(ffmpeg, args);
    });
    // 点击停止：勾选系统声音时先等采集线程收尾，再发 q 给 FFmpeg 完成封装。
    connect(ui->stopButton,&QPushButton::clicked,this,[this](){
        if(m_ffmpeg.state()!= QProcess::Running)
            return;

        m_stopElapsed.start();
        ui->stopButton->setEnabled(false);
        ui->statusLabel->setText(QStringLiteral("状态：正在保存"));
        if (m_audioThread && m_audioThread->isRunning()) {
            // 先写完停止时刻之前的音频，再由 captureStopped 回调给 FFmpeg 发 q。
            m_waitingAudioStop = true;
            m_audioThread->requestStop();
        } else {
            // 请求 FFmpeg 完成 MP4 封装并正常退出。
            sendStopToFFmpeg();
        }
        // 15 秒后仍未退出，则由计时器强制结束进程。
        m_stopTimer.start(15000);
    });


    // 打开遮罩；选区操作由 RegionSelector 自己处理。
    connect(ui->regionButton,&QPushButton::clicked,this,[this](){
        m_selector.beginSelection();
    });

    // 只在用户确认后保存选区，取消时保留上一次的区域。
    connect(&m_selector,&RegionSelector::regionConfirmed,this,[this](const QRect &region){
        m_recordingRegion = region;
        // 用户确认新选区后，切换到区域模式；选区本身保存在 m_recordingRegion。
        ui->usingRegionCheckBox->setChecked(true);
        ui->statusLabel->setText(
            QStringLiteral("已选区域，x=%1 y=%2 宽=%3 高=%4")
            .arg(region.x()).arg(region.y())
            .arg(region.width()).arg(region.height()));
    });


    // 选择目录：取消时保留旧目录；成功后更新显示与持久化设置。
    connect(ui->chooseOutputDirectoryButton,&QPushButton::clicked,this,[this](){
        // 弹出目录选择框，并从当前保存目录开始浏览；取消时返回空字符串。
        const QString selected = QFileDialog::getExistingDirectory(this,QStringLiteral("选择保存目录"),m_outputDirectory);
        if(selected.isEmpty())
            return;// 用户取消

        if(!canWriteDirectory(selected)){
            ui->statusLabel->setText(QStringLiteral("状态：该目录不可写"));
            return;
        }

        m_outputDirectory = QDir(selected).absolutePath();
        // 只把显示给用户的路径改成 Windows 常见的 \，例如 E:/video → E:\video
        ui->outputDirectoryLabel->setText(QStringLiteral("保存目录：%1").arg(QDir::toNativeSeparators(m_outputDirectory)));

        QSettings settings;
        settings.setValue("outputDirectory",m_outputDirectory);
        // 重新更新状态文字
        ui->statusLabel->setText(
            // 勾选区域录制&&还没有有效录制矩形
            ui->usingRegionCheckBox->isChecked()&&!m_recordingRegion.isValid()
                ?QStringLiteral("状态：请先选择区域")
                :QStringLiteral("状态：待机"));
    });
    // toggled 发出新的勾选状态；useRegion=true 代表区域模式，false 代表全屏模式。
    connect(ui->usingRegionCheckBox,&QCheckBox::toggled,this,[this](bool useRegion){
        QSettings settings;
        settings.setValue("recordingMode",useRegion?"region":"full");
        ui->statusLabel->setText(useRegion && !m_recordingRegion.isValid()?QStringLiteral("状态：请先选择区域"):QStringLiteral("状态：待机"));
    });

    // 避免 FFmpeg 无响应时界面一直停留在“正在保存”。
    m_stopTimer.setSingleShot(true); // 一次停止只触发一次超时事件。
    connect(&m_stopTimer,&QTimer::timeout,this,[this]{
        if(m_ffmpeg.state() == QProcess::NotRunning){
            return;
        }

        m_stopTimedOut = true;
        m_ffmpeg.kill(); // 等 finished 信号到来后再恢复界面。
    });
}

MainWindow::~MainWindow()
{
    cleanupAudioThread();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent*event){
    if(m_ffmpeg.state() != QProcess::NotRunning){
        event->ignore();
        ui->statusLabel->setText(QStringLiteral("状态：请先停止录制，等待保存完成后退出"));
        return;
    }
    if(m_audioThread){
        // 采集线程可能在等倒计时或管道连接，直接关闭会遗留工作线程。
        event->ignore();
        ui->statusLabel->setText(QStringLiteral("状态：请先取消或完成本次录制"));
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::sendStopToFFmpeg()
{
    // write 的返回值表示进入写缓冲的字节数，FFmpeg 实际处理 q 可能更晚。
    const qint64 queued = m_ffmpeg.write("q\n");
    qInfo() << "[stop] click_to_q_ms =" << m_stopElapsed.elapsed()
            << "queued_bytes =" << queued;
}

void MainWindow::cleanupAudioThread()
{
    if(!m_audioThread)
        return;
    if(m_audioThread->isRunning()){
        // 线程内所有阻塞 I/O（等连接、写管道）都能被 requestInterruption 取消，
        // 因此 wait 有设计上界；正常路径下线程早已自行退出。
        m_audioThread->requestInterruption();
        m_audioThread->wait();
    }
    delete m_audioThread;
    m_audioThread = nullptr;
}

void MainWindow::launchRecording(const QString &program, const QStringList &args)
{
    // 倒计时结束后由计时器回调启动；否则立即启动 FFmpeg。
    if (ui->countdownCheckBox->isChecked()) {
        m_pendingProgram = program;
        m_pendingArgs = args;
        m_countdownRemaining = ui->countdownSecondsComboBox->currentData().toInt();
        ui->statusLabel->setText(QStringLiteral("状态：%1 秒后开始录制").arg(m_countdownRemaining));
        ui->cancelCountdownButton->setEnabled(true);
        m_countdownTimer.start();
    } else {
        m_ffmpeg.start(program, args);
    }
}

QString MainWindow::nextOutputFilePath() const {
    const QDir dir(m_outputDirectory);
    // 毫秒时间戳减少重名
    const QString base = QStringLiteral("capture-") + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");

    QString path = dir.filePath(base + ".mp4");
    // 若仍重名，就追加序号
    for(int number = 1;QFileInfo::exists(path);++number){
        path = dir.filePath(QStringLiteral("%1-%2.mp4").arg(base).arg(number));
    }
    return path;
}
