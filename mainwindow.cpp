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
    // 进程结束后检查退出状态和文件大小，再显示保存结果。
    connect(&m_ffmpeg,&QProcess::finished, this,[this](int exitcode,QProcess::ExitStatus exitStatus){
        ui->startButton->setEnabled(true);
        ui->stopButton->setEnabled(false);
        // 录制结束后允许再次选择区域。
        ui->regionButton->setEnabled(true);
        // 录制结束后允许切换全屏/区域模式，即使当前尚未选区。
        ui->usingRegionCheckBox->setEnabled(true);
        ui->chooseOutputDirectoryButton->setEnabled(true);
        const bool success = exitStatus == QProcess::NormalExit && exitcode==0 && QFileInfo(m_outputFile).size()>0;

        ui->statusLabel->setText(
            success?QStringLiteral("状态：录制完成"):QStringLiteral("状态：录制失败"));

        if(success){
            ui->statusbar->showMessage(QStringLiteral("文件：")+ m_outputFile);
        }else{
            const QString error = QString::fromUtf8(
                m_ffmpeg.readAllStandardError()).trimmed();
            ui->statusbar->showMessage(error.isEmpty()? QStringLiteral("FFmpeg 未生成视频"): error.right(200));
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
        m_ffmpeg.write("q\n");
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