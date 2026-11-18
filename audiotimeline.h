#ifndef AUDIOTIMELINE_H
#define AUDIOTIMELINE_H

#include <cstdint>

// 描述本包的写入顺序：先补静音，再跳过包头重叠部分，最后写保留的数据。
struct AudioWritePlan
{
    std::uint64_t silenceFrames = 0; // 本包前面需要补的静音帧数。
    std::uint32_t skipFrames = 0;    // 本包开头已经被覆盖、需要跳过的帧数。
    std::uint32_t audioFrames = 0;   // 本包剩余保留的帧数，包含原有静音帧。
};

class AudioTimeline
{
public:
    explicit AudioTimeline(std::uint32_t sampleRate) : m_sampleRate(sampleRate) {}

    // 首包处理前指定时间原点，已有原点或采样率无效时拒绝修改
    bool setOrigin(std::uint64_t qpc100ns);
    // 返回这包在截止时刻前已经完整采集的帧数。
    std::uint32_t framesBeforeStop(std::uint64_t packetQpc, std::uint32_t frames,
                                   std::uint64_t stopQpc) const;
    // 首个有效包建立原点；成功后推进计划游标，失败则清空输出且不改变状态。
    bool planPacket(std::uint64_t qpc100ns, std::uint32_t frames,
                    AudioWritePlan &plan);
    std::uint64_t totalFrames() const { return m_nextFrame; }

private:
    std::uint32_t m_sampleRate;
    bool m_hasOrigin = false;
    std::uint64_t m_originQpc = 0;
    // 已计划输出的总帧数，也是下一帧的位置；例如排完 0～479 帧后，这里为 480。
    std::uint64_t m_nextFrame = 0;
};

#endif // AUDIOTIMELINE_H
