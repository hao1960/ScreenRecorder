#include "systemaudio.h"
#include <cstring>
#include <new>
#include <stdexcept>
SystemAudio::~SystemAudio()
{
    // 对象销毁时自动清理；需与 open 在同一线程执行。
    close();
}

HRESULT SystemAudio::open()
{
    // 1. 重复打开前先释放上次资源，避免丢失旧指针。
    close();
    // 2. 为当前线程初始化 COM；hr 记录每一步的结果。
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return hr;

    // 包括 S_FALSE 在内的成功初始化都需要配对 CoUninitialize。
    m_comInitialized = true;
    // 请Windows创建音频设备枚举器，并把接口指针写入m_enumerator；
    /*
     * 1.__uuidof()取得要创建的 COM 类标识，不执行创建本身
     * 2.nullptr是否作为另一个COM对象的一部分创建，表示不使用COM聚合
     * 3.CLSCTX_ALL 允许对象在哪种运行环境中创建：允许COM使用该类支持的上下文，例如进程内或本地服务
     * 4.需要什么接口：请求音频设备枚举器的操作接口
     * 5.结果放在哪里：把m_enumerator的地址传进去，让函数写入接口指针
    */
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void **>(&m_enumerator));
    // 3. 找默认播放设备；前一步失败时，不使用未取得的指针。
    if(SUCCEEDED(hr))
        hr = m_enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&m_device);
    // 4. 取得此设备的音频客户端接口，之后才能配置流。
    if(SUCCEEDED(hr))
        hr = m_device->Activate(
            __uuidof(IAudioClient),CLSCTX_ALL,nullptr,
            reinterpret_cast<void **>(&m_client));
    // 5. 取得 Windows 分配的格式描述，保存到 m_format。
    if(SUCCEEDED(hr))
        hr = m_client->GetMixFormat(&m_format);
    // 6. 配置共享模式回环流；这里只准备流，尚未 Start。
    if(SUCCEEDED(hr))
        hr=m_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            10000000, // 请求 1 秒缓冲区，单位为 100 纳秒。
            0,
            m_format,
            nullptr);
    // 7. 从已初始化的流取得读取接口。
    if(SUCCEEDED(hr))
        hr = m_client->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void **>(&m_capture));
    // 8. 任一步失败都清理已取得的资源，并保留原来的错误码。
    if(FAILED(hr))
        close();

    return hr;
}

void SystemAudio::close()
{
    // 1. 先停流，再释放依赖该流的采集接口。
    if(m_started){
        m_client->Stop();
        m_started=false;
    }
    if(m_capture){
        m_capture->Release();
        m_capture=nullptr;
    }
    // 2. 格式是 Windows 分配的内存，不能使用 delete 或 Release。
    CoTaskMemFree(m_format);
    m_format = nullptr;

    // 3. 释放 COM 接口并置空，重复 close 不会再次释放旧对象。
    if(m_client)
    {
        m_client->Release();
        m_client=nullptr;
    }
    if(m_device)
    {
        m_device->Release();
        m_device=nullptr;
    }
    if(m_enumerator)
    {
        m_enumerator->Release();
        m_enumerator=nullptr;
    }
    // 4. 最后撤销本对象在当前线程上的 COM 初始化。
    if(m_comInitialized)
    {
        CoUninitialize();
        m_comInitialized=false;
    }
}

const WAVEFORMATEX *SystemAudio::format() const
{
    // 借用指针：调用者只读取，不能释放；close/open 后旧指针失效。
    return m_format;
}

HRESULT SystemAudio::start()
{
    // 必须先完成 open；已启动时不再次调用 Windows。
    if(!m_client||!m_capture)
        return E_UNEXPECTED;
    // S_FALSE表示重复启动或停止时没有执行新操作；它仍属于成功状态
    if(m_started)
        return S_FALSE;

    const HRESULT hr = m_client->Start();
    if(SUCCEEDED(hr))
        m_started=true;
    return hr;
}
HRESULT SystemAudio::stop()
{
    // 重复停止返回 S_FALSE；它仍属于成功状态。
    if(!m_started)
        return S_FALSE;

    // 停止失败时保留 started 状态，close 还会尝试清理。
    const HRESULT hr = m_client->Stop();
    if(SUCCEEDED(hr))
        m_started = false;
    return hr;
}

HRESULT SystemAudio::readPacket(std::vector<BYTE> &packet, AudioPacketInfo *info)
{
    // 1. 清空上次输出；packet 是引用，直接修改调用者自己的容器。
    packet.clear();
    // 暂无包或失效时不能保留上一次时间信息，避免误判时间线
    if(info)
        *info = {};
    if(!m_started || !m_capture || !m_format || m_format->nBlockAlign==0)
        return E_UNEXPECTED;
    /* 2. 查询下一个包有多大，此时还没有取得包的内存地址。
     * GetNextPacketSize()把下一个数据包包含的音频帧数写入nextFrames
     * 查询失败：返回错误码
     * 帧数为0：当前没有数据包，返回S_FALSE（仍属于成功状态）
    */
    UINT32 nextFrames=0;
    HRESULT hr = m_capture->GetNextPacketSize(&nextFrames);

    if(FAILED(hr))
        return hr;
    if(nextFrames==0)
        return S_FALSE;
    /* 3. 从 Windows 借用数据包，后续必须配对 ReleaseBuffer。
     * data:数据包起始地址
     * frames：这次取得的音频帧数
     * flags：数据包状态，例如是否被标记为静音
    */
    BYTE *data=nullptr;
    UINT32 frames=0;
    DWORD flags=0;

    // 后两个空参数表示本阶段不获取音频设备位置和时间信息。
    AudioPacketInfo current{};
    hr = m_capture->GetBuffer(&data,&frames,&flags,&current.devicePosition,&current.qpcPosition100ns);

    if(hr==AUDCLNT_S_BUFFER_EMPTY)
        return S_FALSE;
    if(FAILED(hr))
        return hr;

    // 4. 检查复制条件，按实际格式计算大小，静音包填零。
    HRESULT copyError = S_OK;
    // 先除后比较，避免计算 frames * nBlockAlign 时溢出。
    if(frames>packet.max_size()/m_format->nBlockAlign){// nBlockAlign表示一个音频帧占多少字节
        copyError=E_OUTOFMEMORY;
    }else if(!(flags & AUDCLNT_BUFFERFLAGS_SILENT)&&!data){
        copyError = E_POINTER;
    }else{
        const size_t byteCount = static_cast<size_t>(frames)*m_format->nBlockAlign;

        try{
            // 计算字节数并复制：字节数=帧数*每帧字节数
            packet.resize(byteCount,0);
            // 静音包：保留resize填入的0；普通包用memcpy把data指向的字节复制到packet。data指向的是windows的缓冲区，释放后不能再依赖这个指针
            if(!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
                std::memcpy(packet.data(),data,byteCount);
        }catch (const std::bad_alloc &){
            copyError = E_OUTOFMEMORY;
        }catch (const std::length_error &){
            copyError = E_OUTOFMEMORY;
        }
    }

    // 5. 复制失败也要释放借用；0 表示未消费此包，随后返回错误。
    if(FAILED(copyError)){
        packet.clear();
        const HRESULT releaseHr = m_capture->ReleaseBuffer(0);
        return FAILED(releaseHr)?releaseHr:copyError;
    }

    // 6. 复制完成后确认消费全部帧；之后只使用自己的 packet。
    hr = m_capture->ReleaseBuffer(frames);
    if(FAILED(hr)){
        packet.clear();
    }else if(info){
        current.frames = frames;
        current.flags = flags;
        *info = current;
    }
    return hr;
}
