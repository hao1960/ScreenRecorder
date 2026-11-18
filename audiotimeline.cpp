#include "audiotimeline.h"
#include <limits>
bool AudioTimeline::setOrigin(std::uint64_t qpc100ns)
{
    // 原点一旦确定就不能修改，否则已计划的数据会失去统一的时间基准
    if(m_hasOrigin||m_sampleRate==0)
        return false;
    m_originQpc = qpc100ns;
    m_hasOrigin = true;
    return true;
}
std::uint32_t AudioTimeline::framesBeforeStop(std::uint64_t packetQpc, std::uint32_t frames,
                                             std::uint64_t stopQpc) const
{
    if (stopQpc <= packetQpc || m_sampleRate == 0)
        return 0;
    constexpr std::uint64_t unitsPerSecond = 10000000;
    const std::uint64_t elapsed = stopQpc - packetQpc;
    // 先判断整包能否写入，只有截止点落在包内时才将时间差乘以采样率。
    const std::uint64_t packetDuration = (frames * unitsPerSecond + m_sampleRate - 1) / m_sampleRate;
    if (elapsed >= packetDuration)
        return frames;
    return static_cast<std::uint32_t>(elapsed * m_sampleRate / unitsPerSecond);
}

bool AudioTimeline::planPacket(std::uint64_t qpc100ns, std::uint32_t frames,
                               AudioWritePlan &plan)
{
    plan = {};
    if (m_sampleRate == 0 || frames == 0)
        return false;

    const std::uint64_t origin = m_hasOrigin ? m_originQpc : qpc100ns;
    if (qpc100ns < origin)
        return false; // 原点之前的时间戳不属于本阶段支持的输入。

    // 1. 目标位置约等于：距原点的秒数 × 采样率；拆算整秒与余数避免乘法溢出。
    constexpr std::uint64_t unitsPerSecond = 10000000;
    const std::uint64_t elapsed = qpc100ns - origin;
    const std::uint64_t seconds = elapsed / unitsPerSecond;
    const std::uint64_t fraction =
        ((elapsed % unitsPerSecond) * m_sampleRate + unitsPerSecond / 2)
        / unitsPerSecond; // 四舍五入到最近的一帧。
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    if (seconds > maximum / m_sampleRate)
        return false;
    std::uint64_t target = seconds * m_sampleRate;
    if (fraction > maximum - target)
        return false;
    target += fraction;

    // target 是本包首帧应放的位置，frames 是本包的长度；两者含义不同。
    // 2. 约 1 毫秒以内的偏差先视为抖动，避免频繁插帧或裁帧。
    const std::uint64_t tolerance = m_sampleRate / 1000 > 0
        ? m_sampleRate / 1000 : 1;
    AudioWritePlan next{};
    if (target > m_nextFrame) {
        // 输出末尾尚未到达本包应开始的位置，中间缺口先用静音占位。
        const std::uint64_t gap = target - m_nextFrame;
        if (gap > tolerance)
            next.silenceFrames = gap;
    } else {
        // 输出末尾已越过本包的开头，跳过已经覆盖的部分，避免重复。
        const std::uint64_t overlap = m_nextFrame - target;
        if (overlap > tolerance)
            next.skipFrames = overlap >= frames
                ? frames : static_cast<std::uint32_t>(overlap);
    }
    next.audioFrames = frames - next.skipFrames;

    // 3. 新位置 = 原末尾 + 新增静音帧 + 保留的包帧；检查后再提交状态。
    if (next.silenceFrames > maximum - m_nextFrame)
        return false;
    const std::uint64_t afterSilence = m_nextFrame + next.silenceFrames;
    if (next.audioFrames > maximum - afterSilence)
        return false;

    m_originQpc = origin;
    m_hasOrigin = true;
    m_nextFrame = afterSilence + next.audioFrames;
    plan = next;
    return true;
}
