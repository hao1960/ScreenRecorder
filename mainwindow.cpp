#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QFileInfo>
#include <QstandardPaths>
#include <QCloseEvent>
#include "regionselector.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->stopButton->setEnabled(false);
    // 点击开始录制按钮，找到FFmpeg，准备参数并启动录制
    connect(&m_ffmpeg,&QProcess::started,this,[this](){
        ui->statusLabel->setText(QStringLiteral("状态：录制中"));
        ui->stopButton->setEnabled(true);
    });
    // FFmpeg 启动成功：把界面状态改为“录制中”
    connect(&m_ffmpeg,&QProcess::errorOccurred, this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){
            ui->startButton->setEnabled(true);
            ui->statusLabel->setText(QStringLiteral("状态：启动失败"));
            ui->statusbar->showMessage(m_ffmpeg.errorString());
        }
    });
    // FFmpeg 启动出错：恢复按钮并显示错误
    connect(&m_ffmpeg,&QProcess::finished, this,[this](int exitcode,QProcess::ExitStatus exitStatus){
        ui->startButton->setEnabled(true);
        ui->stopButton->setEnabled(false);
        // 设置可选区域可用
        ui->regionButton->setEnabled(true);

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
    // FFmpeg 运行结束：检查视频文件，显示成功或失败
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

        const QStringList args{
            "-hide_banner", "-loglevel", "error", "-n",
            "-f", "gdigrab", "-framerate", "10", "-i", "desktop",
            "-c:v", "libx264", "-preset", "ultrafast",
            "-pix_fmt", "yuv420p", m_outputFile
        };

        ui->startButton->setEnabled(false);
        ui->statusLabel->setText(QStringLiteral("状态：准备中"));
        // 录制前禁用区域选择，避免遮罩进入录制
        ui->regionButton->setEnabled(false);

        m_ffmpeg.start(ffmpeg, args);

    });
    // 检查ffmpeg是否在运行，是则停止录制并保存
    connect(ui->stopButton,&QPushButton::clicked,this,[this](){
        if(m_ffmpeg.state()!= QProcess::Running)
            return;

        ui->stopButton->setEnabled(false);
        ui->statusLabel->setText(QStringLiteral("状态：正在保存"));
        m_ffmpeg.write("q\n");
    });

    // 选择录制区域
    connect(ui->regionButton,&QPushButton::clicked,this,[this](){
        m_selector.show();
        m_selector.raise();
        m_selector.activateWindow();
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