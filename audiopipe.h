#ifndef AUDIOPIPE_H
#define AUDIOPIPE_H

#include <QThread>
#include <windows.h>
#include <cstddef>
#include <vector>

// 只在采集工作线程内使用;主线程通过 requestInterruption()请求停止
class AudioPipe
{
public:
    AudioPipe() = default;
    ~AudioPipe();

    // 句柄由本对象独占，避免重复关闭
    AudioPipe(const AudioPipe &) = delete;
    AudioPipe &operator=(const AudioPipe &)=delete;
    // 创建命名管道
    DWORD create(const wchar_t *name);
    // 等待客户端，期间允许请求停止
    DWORD waitForClient(const QThread &worker);
    // 关闭句柄，释放资源
    void close();
    // 成功时发送完整包；取消或失败时，可能已有部分数据发送出去
    DWORD writePacket(const std::vector<BYTE> &packet,const QThread &worker);
    // 当前 float32 格式的静音由全零表示；按整帧分块发送，保留取消能力。
    DWORD writeSilence(UINT64 frames, WORD blockAlign, const QThread &worker);
private:
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
};

#endif // AUDIOPIPE_H
