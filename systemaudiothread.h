#ifndef SYSTEMAUDIOTHREAD_H
#define SYSTEMAUDIOTHREAD_H
#include <QThread>
#include <QString>
#include <atomic>
class SystemAudioThread:public QThread
{
    Q_OBJECT
public:
    // 不传管道名时保留原来的计数模式，供旧测试继续使用
    explicit SystemAudioThread(QObject *parent = nullptr):QThread(parent){}
    explicit SystemAudioThread(const QString &pipeName, QObject *parent = nullptr):QThread(parent),m_pipeName(pipeName){}
    ~SystemAudioThread() override;
    // 管道就绪后、启动FFmpeg前调用；时间戳和WASAPI 音频包使用同一时钟
    void setTimelineOrigin(qint64 qpc100ns);
    // 由主线程调用：空闲时重置正常停止标志，再启动工作线程。
    void startCapture();
    // 正常停止保留截至请求时刻的音频；等待连接或需要强制退出时用 requestInterruption。
    void requestStop();
signals:
    void pipeReady(const QString &name,quint32 sampleRate,quint16 channels,quint32 channelMask);
    void captureStarted();
    void captureStopped(quint64 totalBytes);
    // 正常结束时报告已补入的静音帧和已跳过的重叠帧。
    void timelineStats(quint64 silenceFrames, quint64 skippedFrames);
    void captureFailed(const QString &message);
protected:
    void run() override;

private:
    QString m_pipeName;
    // 主线程写入，采集线程读取。-1表示未指定，0仍然是合法时间戳
    std::atomic<qint64> m_originQpc100ns{-1};
    // -1 表示仍在采集；0 表示取时失败、直接收尾；正值是首次停止请求的 QPC（100ns）。
    std::atomic<qint64> m_stopQpc100ns{-1};
};

#endif // SYSTEMAUDIOTHREAD_H
