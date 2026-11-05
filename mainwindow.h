#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "QProcess"
#include "QString"
#include "regionselector.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent*event) override;

private:
    Ui::MainWindow *ui;
    QProcess m_ffmpeg;
    QString m_outputFile;
    RegionSelector m_selector;
    QRect m_recordingRegion;

    QString m_outputDirectory;
    QString nextOutputFilePath() const;

    QTimer m_stopTimer; // 等待 FFmpeg 正常退出的计时器
    bool m_stopTimedOut = false; // finished 回调据此识别强制结束

};
#endif // MAINWINDOW_H
