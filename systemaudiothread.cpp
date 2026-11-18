#include "systemaudiothread.h"
#include "systemaudio.h"
#include "audiopipe.h"
#include "audiotimeline.h"
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <QDebug>
SystemAudioThread::~SystemAudioThread()
{
    // 此线程对象由主线程持有，销毁前请求停止并等待清理完成
    requestInterruption();
    wait();
}

void SystemAudioThread::startCapture()
{
    // 重复启动无效：只在开始新录制时清除上次的停止时刻。
    if(isRunning())
        return;
    m_stopQpc100ns.store(-1);
    m_originQpc100ns.store(-1); // 每次启动前都清除上次录制的原点
    start();
}
void SystemAudioThread::setTimelineOrigin(qint64 qpc100ns)
{
    // 调用方先发布原点，再允许FFmpeg 连接管道
    m_originQpc100ns.store(qpc100ns);
}
void SystemAudioThread::requestStop()
{
    // 在发出停止请求前取 QPC 时间，与 WASAPI 最后一包的结束时间对照。
    LARGE_INTEGER frequency{}, ticks{};
    const bool clockOk = QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 &&
                         QueryPerformanceCounter(&ticks);
    const qint64 cutoff = clockOk
        ? std::llround(static_cast<long double>(ticks.QuadPart) * 10000000.0L / frequency.QuadPart)
        : 0;
    qint64 running = -1;
    if (!m_stopQpc100ns.compare_exchange_strong(running, cutoff))
        return; // 重复停止不能推迟截止点。
    if (clockOk) {
        qInfo("[stop] request_qpc_ms = %.3f", cutoff / 10000.0);
    }
}
void SystemAudioThread::run()
{
    // 1. COM 与管道资源都在工作线程中创建、使用和销毁。
    SystemAudio audio;
    AudioPipe pipe;
    HRESULT hr = audio.open();
    if(FAILED(hr)){
        emit captureFailed(QStringLiteral("open failed: HRESULT=0x")+QString::number(static_cast<quint32>(hr),16));
        return;
    }
    if (isInterruptionRequested() || m_stopQpc100ns.load() >= 0) {
        emit captureStopped(0);
        return;
    }

    const WAVEFORMATEX *format = audio.format();
    if (!m_pipeName.isEmpty()) {
        // 2. 原始音频没有格式头；必须确认 float32，并传递真实声道布局。
        bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        quint32 channelMask = 0;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
            const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
            const GUID floatSubtype = {
                WAVE_FORMAT_IEEE_FLOAT, 0x0000, 0x0010,
                {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
            };
            isFloat = IsEqualGUID(ext->SubFormat, floatSubtype);
            channelMask = ext->dwChannelMask;
        }
        // 普通单声道、双声道可采用标准布局；多声道不猜测排列。
        if (channelMask == 0 && format->nChannels == 1)
            channelMask = 0x4; // 前中：mono。
        if (channelMask == 0 && format->nChannels == 2)
            channelMask = 0x3; // 前左、前右：stereo。
        if (!isFloat || format->wBitsPerSample != 32 ||
            format->nSamplesPerSec == 0 || format->nChannels == 0 ||
            format->nBlockAlign != format->nChannels * 4 || channelMask == 0) {
            emit captureFailed(QStringLiteral(
                "管道模式需要布局已知的 float32：rate=%1 channels=%2 bits=%3")
                .arg(format->nSamplesPerSec).arg(format->nChannels)
                .arg(format->wBitsPerSample));
            return;
        }

        const std::wstring name = m_pipeName.toStdWString();
        DWORD error = pipe.create(name.c_str());
        if (error != ERROR_SUCCESS) {
            emit captureFailed(QStringLiteral("create pipe failed: Win32=%1").arg(error));
            return;
        }
        // 3. 先通知主线程启动 FFmpeg，再等待它连接，避免互相等待。
        emit pipeReady(m_pipeName, format->nSamplesPerSec,
                       format->nChannels, channelMask);
        error = pipe.waitForClient(*this);
        if (error == ERROR_OPERATION_ABORTED && isInterruptionRequested()) {
            emit captureStopped(0); // 连接前取消，尚未开始采集。
            return;
        }
        if (error != ERROR_SUCCESS) {
            emit captureFailed(QStringLiteral("connect pipe failed: Win32=%1").arg(error));
            return;
        }
    }

    if (isInterruptionRequested() || m_stopQpc100ns.load() >= 0) {
        emit captureStopped(0);
        return;
    }
    // 4. 接收端连接后才开始采集，避免等待连接期间积压音频。
    AudioTimeline timeline(format->nSamplesPerSec);
    const qint64 origin = m_originQpc100ns.load();

    // 管道已经连接，此时可以一次性确定本次录制的原点。
    if (!m_pipeName.isEmpty() && origin >= 0) {
        if (!timeline.setOrigin(static_cast<std::uint64_t>(origin))) {
            emit captureFailed(QStringLiteral("无法设置系统声音时间线原点"));
            return;
        }
    }
    hr = audio.start();
    if(FAILED(hr)){
        emit captureFailed(QStringLiteral("start failed: HRESULT=0x")+QString::number(static_cast<quint32>(hr),16));
        return;
    }
    emit captureStarted();

    std::vector<BYTE> packet;
    AudioPacketInfo info{};

    const WORD blockAlign = format->nBlockAlign;
    quint64 totalBytes = 0;
    double lastPacketEndMs = -1.0;
    quint64 insertedSilenceFrames = 0;
    quint64 skippedFrames = 0;
    QString failureMessage;
    bool waitedForFinalPacket = false;
    while (!isInterruptionRequested()) {
        hr = audio.readPacket(packet, &info);
        if (FAILED(hr))
            break;
        const qint64 cutoff = m_stopQpc100ns.load();
        if (cutoff == 0 || (cutoff >= 0 && m_pipeName.isEmpty()))
            break;
        if (hr == S_FALSE) {
            // 允许最后一个设备包晚一个轮询周期；静音设备没有新包时也能结束。
            if (cutoff >= 0 && waitedForFinalPacket)
                break;
            waitedForFinalPacket = cutoff >= 0;
            msleep(20);
            continue;
        }

        bool reachedCutoff = false;
        if (!m_pipeName.isEmpty()) {
            // 时间信息不可靠时先报错退出，不把未知间隙猜成静音。
            if (info.flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
                failureMessage = QStringLiteral("音频时间戳不可靠，无法继续对齐时间线");
                hr = E_FAIL;
                break;
            }
            if (info.frames == 0 || packet.size() !=
                static_cast<quint64>(info.frames) * blockAlign) {
                failureMessage = QStringLiteral("音频包的帧数与字节数不匹配");
                hr = E_INVALIDARG;
                break;
            }

            if (cutoff > 0) {
                // 按包时间戳排空截止点以前的数据，不把保存期间的声音写进成片。
                const auto keep = timeline.framesBeforeStop(info.qpcPosition100ns, info.frames,
                                                            static_cast<quint64>(cutoff));
                if (keep == 0)
                    break;
                if (keep < info.frames) {
                    info.frames = keep;
                    packet.resize(static_cast<std::size_t>(keep) * blockAlign);
                    reachedCutoff = true;
                }
            }

            AudioWritePlan plan{};
            const quint64 maximumBytes = (std::numeric_limits<quint64>::max)();
            if (!timeline.planPacket(info.qpcPosition100ns, info.frames, plan) ||
                timeline.totalFrames() > maximumBytes / blockAlign) {
                failureMessage = QStringLiteral("音频时间戳无法映射到有效的输出位置");
                hr = E_INVALIDARG;
                break;
            }

            // 6. 先补时间缺口。S_FALSE 不触发补零，只有新包的时间位置才提供依据。
            DWORD error = pipe.writeSilence(plan.silenceFrames, blockAlign, *this);
            if (error != ERROR_SUCCESS) {
                hr = HRESULT_FROM_WIN32(error);
                break;
            }
            totalBytes += plan.silenceFrames * blockAlign;
            insertedSilenceFrames += plan.silenceFrames;

            // 7. 跳过已覆盖的包头；SystemAudio 已复制并释放原生缓冲区，可以改自己的副本。
            const std::size_t skipBytes =
                static_cast<std::size_t>(plan.skipFrames) * blockAlign;
            if (skipBytes != 0)
                packet.erase(packet.begin(), packet.begin() +
                    static_cast<std::vector<BYTE>::difference_type>(skipBytes));
            if (!packet.empty()) {
                error = pipe.writePacket(packet, *this);
                if (error != ERROR_SUCCESS) {
                    hr = HRESULT_FROM_WIN32(error);
                    break;
                }
                // QPC 标记原包首帧；即使跳过重叠的包头，成功写入的包尾仍在原结束位置。
                lastPacketEndMs = info.qpcPosition100ns / 10000.0
                                + 1000.0 * info.frames / format->nSamplesPerSec;
            }
            skippedFrames += plan.skipFrames;
        }
        // 管道模式含已补入的静音；计数模式仍然统计原始读取量。
        totalBytes += packet.size();
        if (reachedCutoff)
            break;
    }

    // 8. 保留先前的读取或写入错误；停流后由局部对象关闭管道和释放 COM。
    const HRESULT stopHr = audio.stop();
    if(SUCCEEDED(hr))
        hr = stopHr;
    if (FAILED(hr)) {
        if (failureMessage.isEmpty())
            failureMessage = QStringLiteral("read/write/stop failed: HRESULT=0x") +
                             QString::number(static_cast<quint32>(hr), 16);
        emit captureFailed(failureMessage);
    } else {
        if (!m_pipeName.isEmpty()){
            // totalBytes 包含写入成功的音频和补零；先换算为帧，再换算为毫秒。
            const quint64 writtenFrames = totalBytes / blockAlign;
            const double pcmMs = writtenFrames * 1000.0 / format->nSamplesPerSec;
            qInfo("[stop] pipe_pcm_ms = %.2f", pcmMs);
            if (lastPacketEndMs >= 0.0)
                qInfo("[stop] last_packet_end_qpc_ms = %.3f", lastPacketEndMs);
            emit timelineStats(insertedSilenceFrames, skippedFrames);
        }
        emit captureStopped(totalBytes);
    }
    // run 返回后才有 finished；音频文件完成还要等待 FFmpeg 的 finished。
}
