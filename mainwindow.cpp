#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QFileInfo>
#include <QstandardPaths>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QtMath>
#include "regionselector.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->usingRegionCheckBox->setEnabled(false);
    ui->stopButton->setEnabled(false);
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
            ui->usingRegionCheckBox->setEnabled(m_recordingRegion.isValid());
        }
    });
    // 进程结束后检查退出状态和文件大小，再显示保存结果。
    connect(&m_ffmpeg,&QProcess::finished, this,[this](int exitcode,QProcess::ExitStatus exitStatus){
        ui->startButton->setEnabled(true);
        ui->stopButton->setEnabled(false);
        // 录制结束后允许再次选择区域。
        ui->regionButton->setEnabled(true);
        // 同时更新区域选中状态
        ui->usingRegionCheckBox->setEnabled(m_recordingRegion.isValid());
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

        m_outputFile = QStringLiteral("E:/QT_app/capture-")
                       +QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+QStringLiteral(".mp4");

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
        // 勾选“录制选中区域box”
        ui->usingRegionCheckBox->setEnabled(true);
        ui->usingRegionCheckBox->setChecked(true);
        ui->statusLabel->setText(
            QStringLiteral("已选区域，x=%1 y=%2 宽=%3 高=%4")
            .arg(region.x()).arg(region.y())
            .arg(region.width()).arg(region.height()));
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