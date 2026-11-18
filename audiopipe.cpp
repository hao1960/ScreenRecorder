#include "audiopipe.h"
#include <new>
#include <stdexcept>

AudioPipe::~AudioPipe()
{
    close();
}

DWORD AudioPipe::create(const wchar_t *name)
{
    // 1. 每次创建新实例前释放旧句柄;此方法与等待操作在同一线程调用
    close();
    m_pipe = CreateNamedPipeW(
    name,
    PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
    1,65536,0,0,nullptr);

    return m_pipe == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
}
DWORD AudioPipe::waitForClient(const QThread &worker)
{
    if(m_pipe == INVALID_HANDLE_VALUE)
        return ERROR_INVALID_HANDLE;
    if(worker.isInterruptionRequested())
        return ERROR_OPERATION_ABORTED;

    // 2. windows 在连接请求完成时通知这个时间；并非qt信号
    HANDLE event = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!event)
        return GetLastError();
    // OVERLAPPED保存一次异步I/O请求的状态
    OVERLAPPED operation{};
    operation.hEvent = event;
    DWORD error = ERROR_SUCCESS;
    bool pending = false;

    // 3. 异步发起连接；返回FALSE 时需要继续检查具体状态
    if(!ConnectNamedPipe(m_pipe,&operation)){
        error = GetLastError();
        if(error == ERROR_PIPE_CONNECTED)
            error = ERROR_SUCCESS;// 客户端可能已在调用前连接
        else if(error == ERROR_IO_PENDING)
            pending = true;// 请求已提交，等待完成
    }

    // 4. 每次只等待20毫秒，给工作线程检查停止请求的机会
    while(pending){
        if(worker.isInterruptionRequested()){
            error = ERROR_OPERATION_ABORTED;
            break;
        }

        const DWORD state = WaitForSingleObject(event,20);
        if(state==WAIT_TIMEOUT)
            continue;
        if(state==WAIT_OBJECT_0){
            DWORD unused = 0;
            error = GetOverlappedResult(m_pipe,&operation,&unused,TRUE)?ERROR_SUCCESS:GetLastError();
            pending = false;
            break;
        }
        error = state == WAIT_FAILED ? GetLastError():ERROR_GEN_FAILURE;
        break;
    }
    if(pending){
        // 5.取消只是提出请求；确认操作结束后才能释放operation和事件
        // 请求有可能恰好已完成，因此CancelIoEx返回失败后仍要收尾
        CancelIoEx(m_pipe,&operation);// 请求取消正在进行的I/O
        DWORD unused = 0;
        GetOverlappedResult(m_pipe,&operation,&unused,TRUE);
    }
    CloseHandle(event);
    return error;
}
void AudioPipe::close()
{
    // waitForClient 返回前已完成异步数据收尾；不要从其他线程调用close
    if(m_pipe != INVALID_HANDLE_VALUE){
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
}
DWORD AudioPipe::writePacket(const std::vector<BYTE> &packet,const QThread &worker)
{
    if(m_pipe == INVALID_HANDLE_VALUE)
        return ERROR_INVALID_HANDLE;
    if(worker.isInterruptionRequested())
        return ERROR_OPERATION_ABORTED;
    if(packet.empty())
        return ERROR_SUCCESS;

    HANDLE event = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!event)
        return GetLastError();

    DWORD error = ERROR_SUCCESS;
    std::size_t offset = 0;
    while(offset<packet.size()){
        // 1.每次提交新写入前检查停止；offset记录已经写完的位置
        if(worker.isInterruptionRequested()){
            error = ERROR_OPERATION_ABORTED;
            break;
        }

        const std::size_t remaining = packet.size()-offset;
        // 每次最多提交64kiB，避免一次提交过大的I/O请求
        const DWORD count = remaining > 65536?65536:static_cast<DWORD>(remaining);

        OVERLAPPED operation{};
        operation.hEvent = event;
        // 2.异步写入的实际字节数稍后通过GetOverLappedResult 获取
        bool completed = WriteFile(m_pipe,packet.data()+offset,count,nullptr,&operation);
        if(!completed){
            const DWORD startError = GetLastError();
            if(startError!=ERROR_IO_PENDING){
                error = startError;
                break;
            }
        }

        // 3.请求未完成时短暂等待，并允许主线程请求停止
        while(!completed){
            if(worker.isInterruptionRequested()){
                error=ERROR_OPERATION_ABORTED;
                break;
            }

            const DWORD state = WaitForSingleObject(event,20);
            if(state == WAIT_OBJECT_0)
                completed = true;// 完成不代表成功，下面还需读取结果
            else if(state!=WAIT_TIMEOUT){
                error = state==WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
                break;
            }
        }
        if(!completed){
            // 4.停止或等待失败：先取消并等操作收尾，才允许复用packet
            CancelIoEx(m_pipe,&operation);
            DWORD unused = 0;
            GetOverlappedResult(m_pipe,&operation,&unused,TRUE);
            break;
        }
        DWORD written = 0;
        if(!GetOverlappedResult(m_pipe,&operation,&written,FALSE)){
            error = GetLastError();
            break;
        }
        // 5.必须有进展且不能超出提交长度，避免死循环或偏移越界
        if(written==0||written>count){
            error = ERROR_WRITE_FAULT;
            break;
        }
        offset+=written;
    }
    CloseHandle(event);
    return error;
}

DWORD AudioPipe::writeSilence(UINT64 frames, WORD blockAlign, const QThread &worker)
{
    if(blockAlign == 0|| blockAlign > 655536)
        return ERROR_INVALID_PARAMETER;
    if (m_pipe == INVALID_HANDLE_VALUE)
        return ERROR_INVALID_HANDLE;
    // 极端格式下 chunkFrames 会算成 0；入口直接拒绝，避免 while 死循环。
    if (blockAlign == 0 || blockAlign > 65536)
        return ERROR_INVALID_PARAMETER;
    if (worker.isInterruptionRequested())
        return ERROR_OPERATION_ABORTED;

    // 单次请求不超过 64 KiB；按整帧截取，避免拆开一个多声道音频帧。
    const UINT32 chunkFrames = 65536 / blockAlign;
    std::vector<BYTE> zeros;
    while (frames > 0) {
        if (worker.isInterruptionRequested())
            return ERROR_OPERATION_ABORTED;
        const UINT32 count = frames > chunkFrames
            ? chunkFrames : static_cast<UINT32>(frames);
        try {
            zeros.assign(static_cast<std::size_t>(count) * blockAlign, 0);
        } catch (const std::bad_alloc &) {
            return ERROR_NOT_ENOUGH_MEMORY;
        } catch (const std::length_error &) {
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        // writePacket 返回前已结束本次异步请求，之后才允许复用 zeros。
        const DWORD error = writePacket(zeros, worker);
        if (error != ERROR_SUCCESS)
            return error;
        frames -= count;
    }
    return ERROR_SUCCESS;
}
