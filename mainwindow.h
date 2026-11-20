#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include "QProcess"
#include "QString"
#include "regionselector.h"
#include "systemaudiothread.h"
#include "windowenumerator.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class WindowTitleBar;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent*event) override;

private:
    enum class RecordMode { Full, Region, Window };

    void buildUi();
    WindowTitleBar *createTitleBar();
    QWidget *createBasicPage();
    QWidget *createAdvancedPage();
    QWidget *createBottomBar();
    void applyStyles();
    void setStateText(const QString &text);
    void showStatusMessage(const QString &message, int timeout = 0);
    void clearStatusMessage();
    void setModeButtonsEnabled(bool enabled);
    void updateModeUi();
    void updateOutputPathLabel();
    void setRecordMode(RecordMode mode);
    void selectWindow();
    void updateWindowUi();
    bool validateSelectedWindow(QString *error) const;
    void requestStop(const QString &reason);

    WindowTitleBar *m_titleBar = nullptr;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_messageLabel = nullptr;
    QPushButton *m_fullModeButton = nullptr;
    QPushButton *m_regionModeButton = nullptr;
    QPushButton *m_windowModeButton = nullptr;
    QWidget *m_regionSelectionRow = nullptr;
    QLabel *m_regionChip = nullptr;
    QLabel *m_regionMeta = nullptr;

    RecordMode m_recordMode = RecordMode::Full;
    WindowTarget m_selectedWindow;
    QWidget *m_windowSelectionRow = nullptr;
    QLabel *m_windowChip = nullptr;
    QLabel *m_windowMeta = nullptr;
    QPushButton *m_selectWindowButton = nullptr;
    QTimer m_windowMonitor;
    QString m_stopReason;

    Ui::MainWindow *ui;
    QProcess m_ffmpeg;
    QString m_outputFile;
    QString m_expectedVideoCodec;
    // 本次录制期望的视频尺寸（原生像素），用于ffprobe校验画面范围
    int m_expectedVideoWidth = 0;
    int m_expectedVideoHeight = 0;
    QString m_pendingProgram;
    QStringList m_pendingArgs;
    int m_countdownRemaining = 0;
    QTimer m_countdownTimer;
    RegionSelector m_selector;
    QRect m_recordingRegion;

    QString m_outputDirectory;
    QString nextOutputFilePath() const;

    QTimer m_stopTimer; // 等待 FFmpeg 正常退出的计时器
    QElapsedTimer m_stopElapsed; // 以点击停止为起点，对照发出 q 和进程退出的耗时。
    void sendStopToFFmpeg();
    bool m_stopTimedOut = false; // finished 回调据此识别强制结束

    // 系统声音采集线程：仅勾选系统声音且启动录制后创建，收尾后销毁。
    SystemAudioThread *m_audioThread = nullptr;
    // 停止时是否正在等待采集线程收尾（captureStopped 后才允许给 FFmpeg 发 q）。
    bool m_waitingAudioStop = false;

    // 等待采集线程退出并释放对象；线程内 I/O 均可被 requestInterruption 取消。
    void cleanupAudioThread();
    // 参数就绪后的统一起点：按设置决定倒计时启动或直接启动 FFmpeg。
    void launchRecording(const QString &program, const QStringList &args);

};
#endif // MAINWINDOW_H
