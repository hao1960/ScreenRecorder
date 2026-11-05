#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
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
#include "regionselector.h"

namespace{
// 在目标目录试建一个临时文件，离开函数时自动删除
bool canWriteDirectory(const QString &path)
{
    if(!QFileInfo(path).isDir())
        return false;

    QTemporaryFile check(QDir(path).filePath("screenRecorder-check-XXXXXX"));
    return check.open();
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
// 用 ffprobe 核对 MP4 的视频编码、尺寸和时长；返回空字符串表示通过。
QString checkVideoFile(const QString &path)
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
    if(codecName != "h264")
        return QStringLiteral("视频编码未通过，探测到：%1").arg(codecName.isEmpty()?QStringLiteral("未识别"):codecName);

    if(!widthOk || !heightOk || width <= 0 || height <= 0)
        return QStringLiteral("视频尺寸无效");

    if(!durationOk || duration <= 0)
        return QStringLiteral("视频时长无效");
    return {}; // 空字符串表示检查通过
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
    // 保存文件配置；读取顺序：上次目录-系统视频目录-用户主目录。旧目录被删时自动回退。无效模式值按全屏处理
    QSettings settings;
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
            ui->startButton->setEnabled(true);
            ui->statusLabel->setText(QStringLiteral("状态：启动失败"));
            ui->statusbar->showMessage(m_ffmpeg.errorString());
            ui->regionButton->setEnabled(true);
            ui->usingRegionCheckBox->setEnabled(true);
            ui->chooseOutputDirectoryButton->setEnabled(true);
        }
    });
    // 进程结束后恢复界面，并区分停止超时、FFmpeg 失败与视频验证结果。
    connect(&m_ffmpeg,&QProcess::finished, this,[this](int exitcode,QProcess::ExitStatus exitStatus){
        // 停止计时器，并记住本次是否因超时被强制结束。
        m_stopTimer.stop();
        const bool timedOut = m_stopTimedOut;
        m_stopTimedOut = false;

        ui->startButton->setEnabled(true);
        ui->stopButton->setEnabled(false);
        // 录制结束后允许再次选择区域。
        ui->regionButton->setEnabled(true);
        // 录制结束后允许切换全屏/区域模式，即使当前尚未选区。
        ui->usingRegionCheckBox->setEnabled(true);
        ui->chooseOutputDirectoryButton->setEnabled(true);
        if(timedOut){
            ui->statusLabel->setText(QStringLiteral("状态：停止超时"));
            ui->statusbar->showMessage(QStringLiteral("FFmpeg 已被强制结束，文件可能无法播放：%1").arg(m_outputFile));
            return;
        }

        // 仅在 FFmpeg 正常退出且文件非空时，才用 ffprobe 验证视频流。
        const bool ffmpegSucceeded = exitStatus == QProcess::NormalExit && exitcode ==0 && QFileInfo(m_outputFile).size() > 0;

        const QString videoError = ffmpegSucceeded ? checkVideoFile(m_outputFile): QString{};

        const bool success = ffmpegSucceeded && videoError.isEmpty();

        if(success){
            ui->statusLabel->setText(QStringLiteral("状态：录制完成"));
            ui->statusbar->showMessage(QStringLiteral("文件：")+ m_outputFile);
        }
        else if(ffmpegSucceeded){
            ui->statusLabel->setText(QStringLiteral("状态：视频验证未通过"));
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
                       || error.contains("Could not find video device", Qt::CaseInsensitive)) {
                message = QStringLiteral("无法打开屏幕录制源，请检查选区和屏幕状态");
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
        if(m_ffmpeg.state() != QProcess::NotRunning)
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

        const QString encoderError = checkFFmpegFeature(
            ffmpeg,"encoder=libx264","Encoder libx264 [",
            QStringLiteral("H.264 编码器 libx264"));

        if(!encoderError.isEmpty()){
            ui->statusLabel->setText(QStringLiteral("状态：环境检查失败"));
            ui->statusbar->showMessage(encoderError);
            return;
        }
        QStringList args{
            "-hide_banner", "-loglevel", "error", "-n",
            "-f", "gdigrab", "-framerate", "10"
        };
        // 仅在区域模式下，把 Qt 逻辑坐标换算成 FFmpeg 所需的屏幕像素参数。
        if(ui->usingRegionCheckBox->isChecked()){
            if(!m_recordingRegion.isValid()){
                ui->statusLabel->setText(QStringLiteral("状态：请先选择区域"));
                return;
            }

            QScreen *screen = QGuiApplication::primaryScreen();
            const QRect screenRect = screen->geometry();
            const qreal ratio = screen->devicePixelRatio();

            const int x =qRound((m_recordingRegion.x() - screenRect.x())* ratio);
            const int y =qRound((m_recordingRegion.y() - screenRect.y())* ratio);
            int width = qRound(m_recordingRegion.width()*ratio);
            int height = qRound(m_recordingRegion.height()*ratio);

            // yuv420p 编码时，让宽高保持偶数；最多缩小右边和下边各1像素
            width -= width % 2;
            height -= height % 2;

            args<< "-offset_x" << QString::number(x)
                << "-offset_y"<<QString::number(y)
                <<"-video_size"
                <<QStringLiteral("%1x%2").arg(width).arg(height);
        }
        // 启动前检查目录能否写入，并确定本次录像的完整文件路径。
        if(!canWriteDirectory(m_outputDirectory)){
            ui->statusLabel->setText(QStringLiteral("状态：保存目录不可写，请重新选择"));
            return;
        }
        m_outputFile = nextOutputFilePath();

        args << "-i" << "desktop"
             << "-c:v"<< "libx264"
             << "-preset"<<"ultrafast"
             << "-pix_fmt"<<"yuv420p"
             << m_outputFile;

        ui->startButton->setEnabled(false);
        ui->statusLabel->setText(QStringLiteral("状态：准备中"));
        // 录制前禁用区域选择，避免遮罩进入录制
        ui->regionButton->setEnabled(false);
        ui->usingRegionCheckBox->setEnabled(false);
        // 开始录制时，禁用选择保存目录地址
        ui->chooseOutputDirectoryButton->setEnabled(false);
        m_ffmpeg.start(ffmpeg, args);
    });
    // 点击停止：发送 q 给 FFmpeg，等待 finished 信号确认文件已保存。
    connect(ui->stopButton,&QPushButton::clicked,this,[this](){
        if(m_ffmpeg.state()!= QProcess::Running)
            return;

        ui->stopButton->setEnabled(false);
        ui->statusLabel->setText(QStringLiteral("状态：正在保存"));
        // 请求 FFmpeg 完成 MP4 封装并正常退出。
        m_ffmpeg.write("q\n");
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
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent*event){
    if(m_ffmpeg.state() != QProcess::NotRunning){
        event->ignore();
        ui->statusLabel->setText(QStringLiteral("状态：请先停止录制，等待保存完成后退出"));
        return;
    }
    QMainWindow::closeEvent(event);
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