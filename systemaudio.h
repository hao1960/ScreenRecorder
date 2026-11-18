#ifndef SYSTEMAUDIO_H
#define SYSTEMAUDIO_H
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>
// 描述一包音频的时间位置：仅在readpacket返回S_OK时有效
struct AudioPacketInfo
{
    UINT32 frames = 0; // 本包包含多少音频帧
    UINT64 devicePosition = 0; // 本包首帧在设备中的帧位置
    UINT64 qpcPosition100ns = 0;// 本包首帧的时间戳，单位为100纳秒
    DWORD flags = 0;
};

class SystemAudio
{
public:
    SystemAudio() = default;
    ~SystemAudio();

    // COM 接口不能简单复制，避免多个对象重复释放
    SystemAudio(const SystemAudio &)=delete;
    SystemAudio &operator=(const SystemAudio &)=delete;

    // 所有操作及析构需在同一线程执行；open 准备流，start 才开始采集。
    HRESULT open();
    void close();
    HRESULT start();
    HRESULT stop();

    // 返回的指针由本类管理，close后不能继续使用
    const WAVEFORMATEX *format() const;

    // 成功返回S_OK；暂无数据返回S_FALSE；失败返回错误码
    HRESULT readPacket(std::vector<BYTE> &packet, AudioPacketInfo *info = nullptr);
private:
    // 记录本对象是否成功初始化COM环境
    bool m_comInitialized = false;
    // 用于查找音频设备（以下都是接口指针）
    IMMDeviceEnumerator *m_enumerator = nullptr;
    // 指向找到的音频设备
    IMMDevice *m_device = nullptr;
    // 用于与设备建立音频流、读取混合格式
    IAudioClient *m_client = nullptr;
    // 读取回环音频数据，并记录流是否已启动。
    IAudioCaptureClient *m_capture = nullptr;
    bool m_started = false;
    // 指向音频格式信息，如采样率、声道数和位数（结构体指针）
    WAVEFORMATEX *m_format = nullptr;
};

#endif // SYSTEMAUDIO_H
