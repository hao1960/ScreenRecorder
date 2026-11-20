#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "windowtitlebar.h"

#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
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
// 用 QPainter 画一个 24×24 坐标系的“显示器 + 红色录制点”，
// 不依赖 QtSvg 或外部图片；最后按 size 缩放成 QPixmap。
QPixmap makeAppIconPixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);

    painter.setPen(QPen(QColor(0x1F, 0x29, 0x37), 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(3, 4, 18, 13), 2, 2);
    painter.drawLine(QPointF(9, 21), QPointF(15, 21));
    painter.drawLine(QPointF(12, 17), QPointF(12, 21));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xE5, 0x48, 0x4D));
    painter.drawEllipse(QPointF(12, 10.5), 3.0, 3.0);

    return pixmap;
}

// 单面板里用 1px 分隔线分段，避免堆太多卡片。
QFrame *makeDivider()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("divider"));
    line->setFixedHeight(1);
    return line;
}

QLabel *makeSectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("ScreenRecorder"));
    // 无边框窗口：标题栏、移动、缩放由于我们自己接管
    setWindowFlag(Qt::FramelessWindowHint,true);
    setMinimumSize(900,620);
    resize(900,620);
    ui->menubar->hide();
    buildUi();

    setModeButtonsEnabled(true);
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
            setStateText(QStringLiteral("状态：%1 秒后开始录制").arg(m_countdownRemaining));
            return;
        }
        m_countdownTimer.stop();
        ui->cancelCountdownButton->setEnabled(false);
        ui->cancelCountdownButton->setVisible(false);
        setStateText(QStringLiteral("状态：准备中"));
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
        setStateText(QStringLiteral("状态：倒计时已取消"));
        ui->cancelCountdownButton->setEnabled(false);
        ui->cancelCountdownButton->setVisible(false);
        ui->startButton->setEnabled(true);
        ui->regionButton->setEnabled(true);
        setModeButtonsEnabled(true);
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
    for (int index = 0; index < frameRateItems; ++index) {
        const int rate = rec::kFrameRates[static_cast<std::size_t>(index)];
        ui->frameRateComboBox->setItemData(index, rate);
        ui->frameRateComboBox->setItemText(index, QStringLiteral("%1 fps").arg(rate));
    }
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
        showStatusMessage(QStringLiteral("已保存的编码器不可用，请选择可用编码器"));
    connect(ui->encoderComboBox, &QComboBox::currentIndexChanged, this, [this](int) {
        if (ui->encoderComboBox->currentData().isValid()) {
            QSettings settings;
            settings.setValue("videoEncoder", ui->encoderComboBox->currentData());
        }
    });

    ui->qualityComboBox->clear();
    ui->qualityComboBox->addItem(QStringLiteral("较高"), QStringLiteral("high"));
    ui->qualityComboBox->addItem(QStringLiteral("均衡"), QStringLiteral("balanced"));
    ui->qualityComboBox->addItem(QStringLiteral("较小文件"), QStringLiteral("small"));
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
            showStatusMessage(error);
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
    updateOutputPathLabel();

    const QString mode = settings.value("recordingMode","full").toString();
    ui->usingRegionCheckBox->setChecked(mode == "region");
    if (mode == "region") {
        setStateText(QStringLiteral("状态：待机"));
        showStatusMessage(QStringLiteral("请先选择区域"));
    }


    // FFmpeg 真正启动后，更新状态并允许停止。
    connect(&m_ffmpeg,&QProcess::started,this,[this](){
        setStateText(QStringLiteral("状态：录制中"));
        ui->stopButton->setEnabled(true);
    });
    // FFmpeg 启动失败时恢复按钮，并显示错误原因。
    connect(&m_ffmpeg,&QProcess::errorOccurred, this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){
            m_waitingAudioStop = false;
            cleanupAudioThread();
            ui->startButton->setEnabled(true);
            setStateText(QStringLiteral("状态：启动失败"));
            showStatusMessage(m_ffmpeg.errorString());
            ui->regionButton->setEnabled(true);
            setModeButtonsEnabled(true);
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
        ui->cancelCountdownButton->setEnabled(false);
        ui->cancelCountdownButton->setVisible(false);
        // 录制结束后允许再次选择区域。
        ui->regionButton->setEnabled(true);
        // 录制结束后允许切换全屏/区域模式，即使当前尚未选区。
        setModeButtonsEnabled(true);
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
            setStateText(QStringLiteral("状态：停止超时"));
            showStatusMessage(QStringLiteral("FFmpeg 已被强制结束，文件可能无法播放：%1").arg(m_outputFile));
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
            setStateText(QStringLiteral("状态：录制完成"));
            showStatusMessage(QStringLiteral("文件：")+ m_outputFile);
        }
        else if(ffmpegSucceeded){
            setStateText(QStringLiteral("状态：文件验证未通过"));
            showStatusMessage(videoError + QStringLiteral("；文件：")+m_outputFile);
        }
        else{
            setStateText(QStringLiteral("状态：录制失败"));
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
            showStatusMessage(message);
        }
    });
    // 点击开始：定位 FFmpeg、构造参数，然后异步启动录制进程。
    connect(ui->startButton,&QPushButton::clicked,this,[this](){
        if(m_ffmpeg.state() != QProcess::NotRunning || m_countdownTimer.isActive())
            return;

        const QString ffmpeg = QStandardPaths::findExecutable(
            QStringLiteral("ffmpeg"));
        if(ffmpeg.isEmpty()){
            setStateText(QStringLiteral("状态：找不到FFmpeg"));
            ui->regionButton->setEnabled(true);
            return;
        }
        const QString captureError = checkFFmpegFeature(ffmpeg,"demuxer=gdigrab","Demuxer gdigrab [",QStringLiteral("Windows 屏幕捕获 gdigrab"));
        if(!captureError.isEmpty()){
            setStateText(QStringLiteral("状态：环境检查失败"));
            showStatusMessage(captureError);
            return;
        }

        const QString encoder = ui->encoderComboBox->currentData().toString();
        if (encoder.isEmpty()) {
            setStateText(QStringLiteral("状态：请选择可用编码器"));
            return;
        }
        const QString encoderError = checkFFmpegFeature(
            ffmpeg, QStringLiteral("encoder=") + encoder,
            QByteArray("Encoder ") + encoder.toUtf8() + " [", encoder);

        if(!encoderError.isEmpty()){
            setStateText(QStringLiteral("状态：环境检查失败"));
            showStatusMessage(encoderError);
            return;
        }
        QStringList args{
            "-hide_banner", "-loglevel", "error", "-n",
            "-f", "gdigrab", "-framerate", ui->frameRateComboBox->currentData().toString()
        };
        // 仅在区域模式下，把 Qt 逻辑坐标换算成 FFmpeg 所需的屏幕像素参数。
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) {
            setStateText(QStringLiteral("状态：找不到主显示器"));
            return;
        }
        const QRect screenRect = screen->geometry();
        const bool useRegion = ui->usingRegionCheckBox->isChecked();
        if (useRegion && !m_recordingRegion.isValid()) {
            setStateText(QStringLiteral("状态：请先选择区域"));
            return;
        }
        // 全屏也显式指定主屏矩形：desktop 默认可能覆盖整个虚拟桌面，
        // 多显示器时会录进副屏；显式 offset/video_size 才能保证首版只录主屏。
        const QRect logicalCapture = useRegion ? m_recordingRegion : screenRect;
        const CaptureRect capture = nativeCaptureRect(logicalCapture, screenRect,
                                                      screen->devicePixelRatio());
        if (capture.width <= 0 || capture.height <= 0) {
            setStateText(QStringLiteral("状态：选区太小，无法录制"));
            return;
        }
        args << "-offset_x" << QString::number(capture.x)
             << "-offset_y" << QString::number(capture.y)
             << "-video_size" << QStringLiteral("%1x%2").arg(capture.width).arg(capture.height);
        m_expectedVideoWidth = capture.width;
        m_expectedVideoHeight = capture.height;
        // 启动前检查目录能否写入，并确定本次录像的完整文件路径。
        if(!canWriteDirectory(m_outputDirectory)){
            setStateText(QStringLiteral("状态：保存目录不可写，请重新选择"));
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
                setStateText(QStringLiteral("状态：请选择可用麦克风"));
                showStatusMessage(QStringLiteral("重新勾选麦克风可刷新设备列表"));
                return;
            }
            args << "-f" << "dshow" << "-i" << (QStringLiteral("audio=") + device);
            microphoneAdded = true;
        }
        // 编码器与画质先解析成枚举：CRF 数值和 ffprobe 期望的编码名都由 recordingparams.h 决定。
        const std::optional<rec::Encoder> parsedEncoder = rec::encoderFromName(encoder.toStdString());
        if (!parsedEncoder) {
            setStateText(QStringLiteral("状态：编码器名称无法识别"));
            showStatusMessage(QStringLiteral("请重新选择可用编码器"));
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
        setStateText(QStringLiteral("状态：准备中"));
        clearStatusMessage();
        // 录制前禁用区域选择，避免遮罩进入录制
        ui->regionButton->setEnabled(false);
        setModeButtonsEnabled(false);
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
                showStatusMessage(QStringLiteral("系统声音采集失败：%1").arg(message));
                if (!recording) {
                    // FFmpeg 尚未启动：整体中止本次录制，恢复界面。
                    setStateText(QStringLiteral("状态：启动失败"));
                    ui->startButton->setEnabled(true);
                    ui->regionButton->setEnabled(true);
                    setModeButtonsEnabled(true);
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
            setStateText(QStringLiteral("状态：准备中（启动系统声音）"));
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
        setStateText(QStringLiteral("状态：正在保存"));
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
        m_regionChip->setText(QStringLiteral("%1 × %2").arg(region.width()).arg(region.height()));
        m_regionMeta->setText(QStringLiteral("x=%1, y=%2").arg(region.x()).arg(region.y()));
        ui->regionButton->setText(QStringLiteral("重新选择"));
        setStateText(QStringLiteral("状态：待机"));
        updateOutputPathLabel();
    });


    // 选择目录：取消时保留旧目录；成功后更新显示与持久化设置。
    connect(ui->chooseOutputDirectoryButton,&QPushButton::clicked,this,[this](){
        // 弹出目录选择框，并从当前保存目录开始浏览；取消时返回空字符串。
        const QString selected = QFileDialog::getExistingDirectory(this,QStringLiteral("选择保存目录"),m_outputDirectory);
        if(selected.isEmpty())
            return;// 用户取消

        if(!canWriteDirectory(selected)){
            setStateText(QStringLiteral("状态：该目录不可写"));
            return;
        }

        m_outputDirectory = QDir(selected).absolutePath();
        updateOutputPathLabel();

        QSettings settings;
        settings.setValue("outputDirectory",m_outputDirectory);
        // 重新更新状态文字
        setStateText(QStringLiteral("状态：待机"));
        if (ui->usingRegionCheckBox->isChecked() && !m_recordingRegion.isValid())
            showStatusMessage(QStringLiteral("请先选择区域"));
        else
            updateOutputPathLabel();
    });
    // toggled 发出新的勾选状态；useRegion=true 代表区域模式，false 代表全屏模式。
    connect(ui->usingRegionCheckBox,&QCheckBox::toggled,this,[this](bool useRegion){
        QSettings settings;
        settings.setValue("recordingMode",useRegion?"region":"full");
        setStateText(QStringLiteral("状态：待机"));
        if (useRegion && !m_recordingRegion.isValid())
            showStatusMessage(QStringLiteral("请先选择区域"));
        else
            updateOutputPathLabel();
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
        setStateText(QStringLiteral("状态：请先停止录制，等待保存完成后退出"));
        return;
    }
    if(m_audioThread){
        // 采集线程可能在等倒计时或管道连接，直接关闭会遗留工作线程。
        event->ignore();
        setStateText(QStringLiteral("状态：请先取消或完成本次录制"));
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
        setStateText(QStringLiteral("状态：%1 秒后开始录制").arg(m_countdownRemaining));
        ui->cancelCountdownButton->setEnabled(true);
        ui->cancelCountdownButton->setVisible(true);
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
void MainWindow::buildUi()
{
    // 旧界面先取下来，不立即删除；页面会把需要的控件 reparent 过去。
    QWidget *oldCentral = takeCentralWidget();
    auto *newCentral = new QWidget(this);
    setCentralWidget(newCentral);

    auto *root = new QVBoxLayout(newCentral);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = createTitleBar();
    root->addWidget(m_titleBar);

    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("pageStack"));
    m_pages->addWidget(createBasicPage());
    m_pages->addWidget(createAdvancedPage());
    root->addWidget(m_pages, 1);

    root->addWidget(createBottomBar());

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_pages && index >= 0 && index < m_pages->count())
            m_pages->setCurrentIndex(index);
    });
    connect(ui->usingRegionCheckBox, &QCheckBox::toggled, this, &MainWindow::updateModeUi);

    setModeButtonsEnabled(true);
    updateModeUi();
    updateOutputPathLabel();
    setStateText(QStringLiteral("状态：待机"));
    ui->statusbar->hide();

    // 所有需要的控件都已经迁移到新页面；旧容器现在可以安全删除。
    if (oldCentral)
        oldCentral->deleteLater();

    applyStyles();
}

WindowTitleBar *MainWindow::createTitleBar()
{
    auto *bar = new WindowTitleBar;
    auto *layout = new QGridLayout(bar);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setHorizontalSpacing(12);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(2, 1);

    // 左侧：图标 + 应用名 + 版本徽标。
    auto *brand = new QWidget;
    auto *brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(8);

    auto *icon = new QLabel;
    icon->setPixmap(makeAppIconPixmap(23));
    icon->setFixedSize(23, 23);
    brandLayout->addWidget(icon);

    auto *name = new QLabel(QStringLiteral("ScreenRecorder"));
    QFont nameFont = name->font();
    nameFont.setWeight(QFont::DemiBold);
    name->setFont(nameFont);
    brandLayout->addWidget(name);

    auto *version = new QLabel(QStringLiteral("V0.3"));
    version->setObjectName(QStringLiteral("versionBadge"));
    brandLayout->addWidget(version);

    layout->addWidget(brand, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    // 中间：基本设置 / 高级设置。
    m_tabs = new QTabBar;
    m_tabs->addTab(QStringLiteral("基本设置"));
    m_tabs->addTab(QStringLiteral("高级设置"));
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_tabs, 0, 1, Qt::AlignCenter);

    // 右侧：最小化 / 最大化 / 关闭。
    auto *controls = new QWidget;
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(6);

    auto addControl = [&](const QString &text, const QString &objectName) {
        auto *button = new QToolButton;
        button->setText(text);
        button->setObjectName(objectName);
        button->setFixedSize(30, 26);
        button->setFocusPolicy(Qt::NoFocus);
        controlsLayout->addWidget(button);
        return button;
    };

    auto *minButton = addControl(QStringLiteral("—"), QStringLiteral("windowMinButton"));
    auto *maxButton = addControl(QStringLiteral("▢"), QStringLiteral("windowMaxButton"));
    auto *closeButton = addControl(QStringLiteral("✕"), QStringLiteral("windowCloseButton"));

    connect(minButton, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maxButton, &QToolButton::clicked, this, [this] {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    });
    connect(closeButton, &QToolButton::clicked, this, [this] {
        close();
    });

    layout->addWidget(controls, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    return bar;
}

QWidget *MainWindow::createBasicPage()
{
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(24, 24, 24, 24);
    pageLayout->setSpacing(0);

    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("settingsPanel"));
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(24, 8, 24, 8);
    panelLayout->setSpacing(0);

    // 录制范围：三选一；窗口按钮先禁用，等下一步接入捕获。
    auto *rangeSection = new QWidget;
    auto *rangeLayout = new QVBoxLayout(rangeSection);
    rangeLayout->setContentsMargins(0, 16, 0, 16);
    rangeLayout->setSpacing(0);
    rangeLayout->addWidget(makeSectionTitle(QStringLiteral("录制范围")));
    rangeLayout->addSpacing(8);

    auto *segmented = new QWidget;
    segmented->setObjectName(QStringLiteral("segmented"));
    segmented->setAttribute(Qt::WA_StyledBackground, true);
    auto *segmentedLayout = new QHBoxLayout(segmented);
    segmentedLayout->setContentsMargins(3, 3, 3, 3);
    segmentedLayout->setSpacing(3);

    auto makeModeButton = [](const QString &text, const QString &objectName) {
        auto *button = new QPushButton(text);
        button->setObjectName(objectName);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setFixedHeight(34);
        return button;
    };

    m_fullModeButton = makeModeButton(QStringLiteral("全屏"), QStringLiteral("fullModeButton"));
    m_regionModeButton = makeModeButton(QStringLiteral("区域"), QStringLiteral("regionModeButton"));
    m_windowModeButton = makeModeButton(QStringLiteral("窗口"), QStringLiteral("windowModeButton"));
    m_windowModeButton->setEnabled(false);
    m_windowModeButton->setToolTip(QStringLiteral("窗口录制将在下一步接入"));
    m_fullModeButton->setChecked(true);

    segmentedLayout->addWidget(m_fullModeButton);
    segmentedLayout->addWidget(m_regionModeButton);
    segmentedLayout->addWidget(m_windowModeButton);

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_fullModeButton);
    modeGroup->addButton(m_regionModeButton);
    modeGroup->addButton(m_windowModeButton);

    connect(m_fullModeButton, &QPushButton::clicked, this, [this] {
        ui->usingRegionCheckBox->setChecked(false);
    });
    connect(m_regionModeButton, &QPushButton::clicked, this, [this] {
        ui->usingRegionCheckBox->setChecked(true);
    });

    rangeLayout->addWidget(segmented);

    m_regionSelectionRow = new QWidget;
    m_regionSelectionRow->setObjectName(QStringLiteral("selectionRow"));
    m_regionSelectionRow->setAttribute(Qt::WA_StyledBackground, true);
    m_regionSelectionRow->setFixedHeight(38);
    auto *selectionLayout = new QHBoxLayout(m_regionSelectionRow);
    selectionLayout->setContentsMargins(10, 0, 10, 0);
    selectionLayout->setSpacing(8);

    auto *selectionTitle = new QLabel(QStringLiteral("已选区域"));
    selectionTitle->setObjectName(QStringLiteral("selectionLabel"));
    m_regionChip = new QLabel(QStringLiteral("未选择"));
    m_regionChip->setObjectName(QStringLiteral("selectionChip"));
    m_regionMeta = new QLabel;
    m_regionMeta->setObjectName(QStringLiteral("selectionMeta"));

    selectionLayout->addWidget(selectionTitle);
    selectionLayout->addWidget(m_regionChip);
    selectionLayout->addWidget(m_regionMeta);
    selectionLayout->addStretch();

    ui->regionButton->setText(QStringLiteral("选择区域"));
    selectionLayout->addWidget(ui->regionButton);

    rangeLayout->addSpacing(8);
    rangeLayout->addWidget(m_regionSelectionRow);
    panelLayout->addWidget(rangeSection);
    panelLayout->addWidget(makeDivider());

    // 保存位置。
    auto *saveSection = new QWidget;
    auto *saveLayout = new QVBoxLayout(saveSection);
    saveLayout->setContentsMargins(0, 16, 0, 16);
    saveLayout->setSpacing(0);
    saveLayout->addWidget(makeSectionTitle(QStringLiteral("保存位置")));
    saveLayout->addSpacing(8);

    auto *saveRow = new QWidget;
    auto *saveRowLayout = new QHBoxLayout(saveRow);
    saveRowLayout->setContentsMargins(0, 0, 0, 0);
    saveRowLayout->setSpacing(9);

    ui->outputDirectoryLabel->setObjectName(QStringLiteral("pathField"));
    ui->outputDirectoryLabel->setWordWrap(false);
    ui->outputDirectoryLabel->setMinimumHeight(34);
    ui->outputDirectoryLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->outputDirectoryLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->chooseOutputDirectoryButton->setText(QStringLiteral("更改"));

    saveRowLayout->addWidget(ui->outputDirectoryLabel, 1);
    saveRowLayout->addWidget(ui->chooseOutputDirectoryButton);
    saveLayout->addWidget(saveRow);
    panelLayout->addWidget(saveSection);
    panelLayout->addWidget(makeDivider());

    // 音频。
    auto *audioSection = new QWidget;
    auto *audioLayout = new QVBoxLayout(audioSection);
    audioLayout->setContentsMargins(0, 16, 0, 16);
    audioLayout->setSpacing(0);
    audioLayout->addWidget(makeSectionTitle(QStringLiteral("音频")));
    audioLayout->addSpacing(8);

    ui->microphoneCheckBox->setText(QStringLiteral("麦克风"));
    ui->systemAudioCheckBox->setText(QStringLiteral("系统声音"));
    ui->countdownCheckBox->setText(QStringLiteral("开始前倒计时"));

    auto *microphoneRow = new QWidget;
    auto *microphoneLayout = new QHBoxLayout(microphoneRow);
    microphoneLayout->setContentsMargins(0, 0, 0, 0);
    microphoneLayout->setSpacing(8);
    microphoneLayout->addWidget(ui->microphoneCheckBox);
    microphoneLayout->addStretch();
    ui->microphoneComboBox->setMinimumWidth(220);
    microphoneLayout->addWidget(ui->microphoneComboBox);
    audioLayout->addWidget(microphoneRow);
    audioLayout->addSpacing(8);

    auto *systemAudioRow = new QWidget;
    auto *systemAudioLayout = new QHBoxLayout(systemAudioRow);
    systemAudioLayout->setContentsMargins(0, 0, 0, 0);
    systemAudioLayout->addWidget(ui->systemAudioCheckBox);
    systemAudioLayout->addStretch();
    auto *systemAudioHint = new QLabel(QStringLiteral("WASAPI 回环"));
    systemAudioHint->setObjectName(QStringLiteral("mutedLabel"));
    systemAudioLayout->addWidget(systemAudioHint);
    audioLayout->addWidget(systemAudioRow);
    panelLayout->addWidget(audioSection);
    panelLayout->addWidget(makeDivider());

    // 倒计时。
    auto *countdownSection = new QWidget;
    auto *countdownLayout = new QVBoxLayout(countdownSection);
    countdownLayout->setContentsMargins(0, 16, 0, 16);
    countdownLayout->setSpacing(0);
    countdownLayout->addWidget(makeSectionTitle(QStringLiteral("倒计时")));
    countdownLayout->addSpacing(8);

    auto *countdownRow = new QWidget;
    auto *countdownRowLayout = new QHBoxLayout(countdownRow);
    countdownRowLayout->setContentsMargins(0, 0, 0, 0);
    countdownRowLayout->addWidget(ui->countdownCheckBox);
    countdownRowLayout->addStretch();
    ui->countdownSecondsComboBox->setFixedWidth(104);
    countdownRowLayout->addWidget(ui->countdownSecondsComboBox);
    countdownLayout->addWidget(countdownRow);
    panelLayout->addWidget(countdownSection);

    pageLayout->addWidget(panel);
    pageLayout->addStretch(1);

    // 隐藏的状态承载控件仍用于 full/region 判断和 QSettings。
    ui->usingRegionCheckBox->setParent(page);
    ui->usingRegionCheckBox->hide();
    return page;
}

QWidget *MainWindow::createAdvancedPage()
{
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(24, 24, 24, 24);
    pageLayout->setSpacing(0);

    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("settingsPanel"));
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(24, 8, 24, 8);
    panelLayout->setSpacing(0);

    auto *videoSection = new QWidget;
    auto *videoLayout = new QVBoxLayout(videoSection);
    videoLayout->setContentsMargins(0, 16, 0, 16);
    videoLayout->setSpacing(0);
    videoLayout->addWidget(makeSectionTitle(QStringLiteral("视频参数")));
    videoLayout->addSpacing(8);

    auto addSettingRow = [&](const QString &text, QWidget *control) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(12);
        auto *label = new QLabel(text);
        label->setObjectName(QStringLiteral("rowLabel"));
        rowLayout->addWidget(label);
        rowLayout->addStretch();
        control->setMinimumWidth(220);
        rowLayout->addWidget(control);
        videoLayout->addWidget(row);
        videoLayout->addSpacing(8);
    };

    addSettingRow(QStringLiteral("帧率"), ui->frameRateComboBox);
    addSettingRow(QStringLiteral("编码器"), ui->encoderComboBox);
    addSettingRow(QStringLiteral("画质"), ui->qualityComboBox);
    panelLayout->addWidget(videoSection);
    panelLayout->addWidget(makeDivider());

    auto *outputSection = new QWidget;
    auto *outputLayout = new QVBoxLayout(outputSection);
    outputLayout->setContentsMargins(0, 16, 0, 16);
    outputLayout->setSpacing(0);
    outputLayout->addWidget(makeSectionTitle(QStringLiteral("输出")));
    outputLayout->addSpacing(8);

    auto *checkRow = new QWidget;
    auto *checkRowLayout = new QHBoxLayout(checkRow);
    checkRowLayout->setContentsMargins(0, 0, 0, 0);
    auto *checkLabel = new QLabel(QStringLiteral("停止后校验"));
    checkLabel->setObjectName(QStringLiteral("rowLabel"));
    auto *checkPill = new QLabel(QStringLiteral("已开启"));
    checkPill->setObjectName(QStringLiteral("enabledPill"));
    checkRowLayout->addWidget(checkLabel);
    checkRowLayout->addStretch();
    checkRowLayout->addWidget(checkPill);
    outputLayout->addWidget(checkRow);
    outputLayout->addSpacing(8);

    auto *nameRow = new QWidget;
    auto *nameRowLayout = new QHBoxLayout(nameRow);
    nameRowLayout->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(QStringLiteral("文件命名"));
    nameLabel->setObjectName(QStringLiteral("rowLabel"));
    auto *nameValue = new QLabel(QStringLiteral("capture-时间戳.mp4"));
    nameValue->setObjectName(QStringLiteral("mutedLabel"));
    nameRowLayout->addWidget(nameLabel);
    nameRowLayout->addStretch();
    nameRowLayout->addWidget(nameValue);
    outputLayout->addWidget(nameRow);
    panelLayout->addWidget(outputSection);

    auto *note = new QLabel(QStringLiteral(
        "固定参数：yuv420p · AAC · MP4 · ultrafast；窗口模式会在编码前裁到偶数尺寸。"));
    note->setObjectName(QStringLiteral("noteLabel"));
    note->setWordWrap(true);
    panelLayout->addWidget(note);
    panelLayout->addSpacing(4);

    pageLayout->addWidget(panel);
    pageLayout->addStretch(1);
    return page;
}

QWidget *MainWindow::createBottomBar()
{
    auto *bar = new QFrame;
    bar->setObjectName(QStringLiteral("bottomBar"));
    bar->setFixedHeight(64);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(24, 0, 24, 0);
    layout->setSpacing(8);

    auto *statusText = new QWidget;
    auto *statusLayout = new QVBoxLayout(statusText);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(2);

    auto *stateRow = new QWidget;
    auto *stateRowLayout = new QHBoxLayout(stateRow);
    stateRowLayout->setContentsMargins(0, 0, 0, 0);
    stateRowLayout->setSpacing(9);

    auto *statusDot = new QLabel;
    statusDot->setObjectName(QStringLiteral("statusDot"));
    statusDot->setAttribute(Qt::WA_StyledBackground, true);
    statusDot->setFixedSize(8, 8);
    stateRowLayout->addWidget(statusDot);

    ui->statusLabel->setObjectName(QStringLiteral("statusLabel"));
    stateRowLayout->addWidget(ui->statusLabel);
    stateRowLayout->addStretch();
    statusLayout->addWidget(stateRow);

    m_messageLabel = new QLabel;
    m_messageLabel->setObjectName(QStringLiteral("statusMessage"));
    m_messageLabel->setMinimumWidth(0);
    m_messageLabel->setContentsMargins(17, 0, 0, 0);
    m_messageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLayout->addWidget(m_messageLabel);
    layout->addWidget(statusText, 1);

    ui->cancelCountdownButton->setText(QStringLiteral("取消倒计时"));
    ui->cancelCountdownButton->setVisible(false);
    ui->startButton->setText(QStringLiteral("开始录制"));
    ui->stopButton->setText(QStringLiteral("停止录制"));

    layout->addWidget(ui->cancelCountdownButton);
    layout->addWidget(ui->startButton);
    layout->addWidget(ui->stopButton);

    auto *grip = new WindowResizeGrip(bar);
    grip->setFixedSize(16, 16);
    layout->addWidget(grip);
    return bar;
}

void MainWindow::applyStyles()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #FFFFFF; }"
        "QFrame#windowTitleBar { background: #FFFFFF; border-bottom: 1px solid #E4E8EF; }"
        "QLabel#versionBadge { color: #1F2937; background: #FFFFFF;"
        " border: 1px solid #D8DEE8; border-radius: 4px;"
        " padding: 3px 6px; font-size: 9px; font-weight: bold; }"
        "QTabBar::tab { background: transparent; color: #6B7280; padding: 8px 17px;"
        " border: none; border-bottom: 2px solid transparent; font-weight: 600; }"
        "QTabBar::tab:selected { color: #2F6FED; border-bottom: 2px solid #2F6FED; }"
        "QToolButton#windowMinButton, QToolButton#windowMaxButton, QToolButton#windowCloseButton {"
        " border: none; background: transparent; color: #98A1AE; font-size: 12px; }"
        "QToolButton#windowMinButton:hover, QToolButton#windowMaxButton:hover {"
        " background: #F1F3F7; border-radius: 4px; }"
        "QToolButton#windowCloseButton:hover { background: #FDE8E8; color: #E5484D; border-radius: 4px; }"
        "QStackedWidget#pageStack { background: #F7F8FA; }"
        "QFrame#settingsPanel { background: #FFFFFF; border: 1px solid #E4E8EF; border-radius: 12px; }"
        "QLabel#sectionTitle { color: #6B7280; font-size: 11px; font-weight: bold; }"
        "QFrame#divider { background: #EEF1F5; border: none; }"
        "QWidget#segmented { background: #F1F3F7; border: 1px solid #E4E8EF; border-radius: 9px; }"
        "QPushButton#fullModeButton, QPushButton#regionModeButton, QPushButton#windowModeButton {"
        " border: 1px solid transparent; background: transparent; color: #6B7280;"
        " border-radius: 6px; font-weight: 600; }"
        "QPushButton#fullModeButton:checked, QPushButton#regionModeButton:checked,"
        " QPushButton#windowModeButton:checked {"
        " background: #FFFFFF; color: #2F6FED; border: 1px solid #C5D7FB; }"
        "QPushButton#regionButton { border: none; background: transparent; color: #2F6FED;"
        " font-weight: 600; padding: 4px 2px; }"
        "QWidget#selectionRow { background: #F7F8FA; border: 1px solid #E4E8EF; border-radius: 8px; }"
        "QLabel#selectionLabel { font-size: 11px; color: #6B7280; }"
        "QLabel#selectionChip { font-size: 11px; font-weight: bold; color: #2F6FED;"
        " background: #EEF4FF; border: 1px solid #C5D7FB; border-radius: 5px; padding: 2px 7px; }"
        "QLabel#selectionMeta { font-size: 10px; color: #98A1AE; }"
        "QLabel#pathField { background: #F7F8FA; border: 1px solid #D8DEE8;"
        " border-radius: 7px; padding: 0 10px; color: #1F2937; }"
        "QComboBox { background: #F7F8FA; border: 1px solid #D8DEE8; border-radius: 7px;"
        " padding: 0 9px; min-height: 34px; color: #1F2937; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QCheckBox { color: #1F2937; font-size: 12px; spacing: 8px; }"
        "QLabel#mutedLabel, QLabel#noteLabel { color: #98A1AE; font-size: 10px; }"
        "QLabel#rowLabel { color: #1F2937; font-size: 12px; }"
        "QLabel#enabledPill { color: #1F9D63; background: #EAF8F0; border-radius: 999px;"
        " padding: 3px 8px; font-size: 10px; font-weight: bold; }"
        "QFrame#bottomBar { background: #FFFFFF; border-top: 1px solid #E4E8EF; }"
        "QLabel#statusDot { background: #1F9D63; border-radius: 4px; }"
        "QLabel#statusLabel { color: #1F2937; font-size: 12px; font-weight: bold; }"
        "QLabel#statusMessage { color: #6B7280; font-size: 10px; }"
        "QPushButton#startButton { background: #2F6FED; color: #FFFFFF; border: none;"
        " border-radius: 7px; padding: 0 17px; min-height: 34px; font-weight: 600; }"
        "QPushButton#startButton:hover { background: #245FD4; }"
        "QPushButton#startButton:disabled { background: #B8CFFB; color: #FFFFFF; }"
        "QPushButton#stopButton { background: #FFFFFF; color: #E5484D; border: 1px solid #EFC0C2;"
        " border-radius: 7px; padding: 0 14px; min-height: 34px; font-weight: 600; }"
        "QPushButton#stopButton:disabled { color: #D8A7A9; border-color: #F0D6D7; }"
        "QPushButton#cancelCountdownButton { background: #F7F8FA; color: #6B7280;"
        " border: 1px solid #E4E8EF; border-radius: 7px; padding: 0 12px; min-height: 34px; }"
        "QPushButton#chooseOutputDirectoryButton { background: #FFFFFF; color: #1F2937;"
        " border: 1px solid #D8DEE8; border-radius: 7px; padding: 0 14px; min-height: 34px;"
        " font-weight: 600; }"
        "QPushButton[disabled] { color: #A0A7B4; }"
    ));
}

void MainWindow::setStateText(const QString &text)
{
    QString value = text;
    if (value.startsWith(QStringLiteral("状态：")))
        value.remove(0, 3);
    ui->statusLabel->setText(value);
}

void MainWindow::showStatusMessage(const QString &message, int timeout)
{
    if (m_messageLabel) {
        m_messageLabel->setText(message);
        m_messageLabel->setToolTip(message);
    }
    // 旧状态栏保持兼容；界面已经隐藏它。
    ui->statusbar->showMessage(message, timeout);
}

void MainWindow::clearStatusMessage()
{
    if (m_messageLabel) {
        m_messageLabel->clear();
        m_messageLabel->setToolTip({});
    }
    ui->statusbar->clearMessage();
}

void MainWindow::setModeButtonsEnabled(bool enabled)
{
    ui->usingRegionCheckBox->setEnabled(enabled);
    if (m_fullModeButton)
        m_fullModeButton->setEnabled(enabled);
    if (m_regionModeButton)
        m_regionModeButton->setEnabled(enabled);
    if (m_windowModeButton)
        m_windowModeButton->setEnabled(false);
}

void MainWindow::updateModeUi()
{
    const bool region = ui->usingRegionCheckBox->isChecked();
    if (m_fullModeButton)
        m_fullModeButton->setChecked(!region);
    if (m_regionModeButton)
        m_regionModeButton->setChecked(region);
    if (m_windowModeButton)
        m_windowModeButton->setChecked(false);
    if (m_regionSelectionRow)
        m_regionSelectionRow->setVisible(region);
}

void MainWindow::updateOutputPathLabel()
{
    const QString path = m_outputDirectory.isEmpty()
        ? QStringLiteral("未设置")
        : QDir::toNativeSeparators(m_outputDirectory);
    if (ui->outputDirectoryLabel) {
        ui->outputDirectoryLabel->setText(path);
        ui->outputDirectoryLabel->setToolTip(path);
    }
    if (m_messageLabel)
        m_messageLabel->setText(QStringLiteral("保存到 %1").arg(path));
}