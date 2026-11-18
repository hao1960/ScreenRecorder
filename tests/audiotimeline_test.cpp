#include "../audiotimeline.h"
#include <iostream>
#include <limits>

int main()
{
    int checks = 0;
    int failures = 0;
    auto check = [&](bool okay, const char *name) {
        ++checks;
        if (!okay) {
            ++failures;
            std::cerr << "FAIL: " << name << '\n';
        }
    };
    constexpr std::uint64_t base = 1000000000;
    AudioWritePlan plan{};

    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 480, plan) &&
              plan.silenceFrames == 0 && plan.skipFrames == 0 &&
              plan.audioFrames == 480 && timeline.totalFrames() == 480,
              "first packet establishes origin");
        check(timeline.planPacket(base + 100000, 480, plan) &&
              plan.silenceFrames == 0 && plan.skipFrames == 0 &&
              plan.audioFrames == 480 && timeline.totalFrames() == 960,
              "contiguous packets need no padding");
    }
    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 480, plan) &&
              timeline.planPacket(base + 3000000, 480, plan) &&
              plan.silenceFrames == 13920 && plan.skipFrames == 0 &&
              plan.audioFrames == 480 && timeline.totalFrames() == 14880,
              "300 ms packet position preserves 290 ms gap");
    }
    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 480, plan) &&
              timeline.planPacket(base + 20000000, 480, plan) &&
              plan.silenceFrames == 95520 && timeline.totalFrames() == 96480,
              "whole-second timestamp conversion");
    }
    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 480, plan) &&
              timeline.planPacket(base + 100200, 480, plan) &&
              plan.silenceFrames == 0 && plan.skipFrames == 0 &&
              timeline.totalFrames() == 960,
              "small positive timestamp jitter does not add silence");
    }
    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 480, plan) &&
              timeline.planPacket(base + 99800, 480, plan) &&
              plan.silenceFrames == 0 && plan.skipFrames == 0 &&
              timeline.totalFrames() == 960,
              "small negative timestamp jitter does not trim audio");
    }
    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 960, plan) &&
              timeline.planPacket(base + 100000, 960, plan) &&
              plan.skipFrames == 480 && plan.audioFrames == 480 &&
              plan.silenceFrames == 0 && timeline.totalFrames() == 1440,
              "overlapping packet keeps only its new tail");
    }
    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 960, plan) &&
              timeline.planPacket(base + 50000, 480, plan) &&
              plan.skipFrames == 480 && plan.audioFrames == 0 &&
              timeline.totalFrames() == 960,
              "fully covered packet does not replay audio");
    }
    {
        AudioTimeline timeline(44100);
        check(timeline.planPacket(base, 441, plan) &&
              timeline.planPacket(base + 100000, 441, plan) &&
              plan.silenceFrames == 0 && plan.audioFrames == 441 &&
              timeline.totalFrames() == 882,
              "44100 Hz uses its own frame rate");
    }
    {
        AudioTimeline timeline(0);
        plan = {1, 2, 3};
        check(!timeline.planPacket(base, 480, plan) &&
              plan.silenceFrames == 0 && plan.skipFrames == 0 &&
              plan.audioFrames == 0 && timeline.totalFrames() == 0,
              "zero sample rate fails with empty output");
    }
    {
        AudioTimeline timeline(48000);
        check(!timeline.planPacket(base, 0, plan) &&
              timeline.totalFrames() == 0 &&
              timeline.planPacket(base + 100000, 480, plan) &&
              plan.silenceFrames == 0 && timeline.totalFrames() == 480,
              "empty packet does not establish origin");
    }
    {
        AudioTimeline timeline(48000);
        check(timeline.planPacket(base, 480, plan) &&
              !timeline.planPacket(base - 1, 480, plan) &&
              plan.silenceFrames == 0 && plan.audioFrames == 0 &&
              timeline.totalFrames() == 480,
              "timestamp before origin fails without changing state");
    }
    {
        AudioTimeline timeline(std::numeric_limits<std::uint32_t>::max());
        check(timeline.planPacket(0, 1, plan) &&
              !timeline.planPacket(std::numeric_limits<std::uint64_t>::max(), 1, plan) &&
              plan.silenceFrames == 0 && plan.audioFrames == 0 &&
              timeline.totalFrames() == 1,
              "unrepresentable frame position is rejected");
    }
    {
        AudioTimeline timeline(48000);

        // 设定原点不产生音频，也不推进输出游标。
        check(timeline.setOrigin(base) && timeline.totalFrames() == 0,
              "external origin accepted without advancing cursor");

        check(!timeline.setOrigin(base + 100000) &&
                  timeline.totalFrames() == 0,
              "second external origin rejected");

        // 首包晚到 10 ms：先补 480 帧，再写入本包的 480 帧。
        check(timeline.planPacket(base + 100000, 480, plan) &&
                  plan.silenceFrames == 480 &&
                  plan.skipFrames == 0 &&
                  plan.audioFrames == 480 &&
                  timeline.totalFrames() == 960,
              "external origin preserves leading gap");

        // 下一包紧接前包，不能再次补入开头的静音。
        check(timeline.planPacket(base + 200000, 480, plan) &&
                  plan.silenceFrames == 0 &&
                  plan.skipFrames == 0 &&
                  plan.audioFrames == 480 &&
                  timeline.totalFrames() == 1440,
              "packet after leading gap remains contiguous");
    }
    {
        AudioTimeline timeline(48000);

        // 自动建立的原点同样不可修改；拒绝后仍按原来的基准计算。
        check(timeline.planPacket(base, 480, plan) &&
                  !timeline.setOrigin(base + 100000) &&
                  timeline.planPacket(base + 100000, 480, plan) &&
                  plan.silenceFrames == 0 &&
                  plan.skipFrames == 0 &&
                  plan.audioFrames == 480 &&
                  timeline.totalFrames() == 960,
              "automatic origin cannot be overwritten");
    }
    {
        AudioTimeline timeline(0);
        check(!timeline.setOrigin(base) && timeline.totalFrames() == 0,
              "zero sample rate rejects external origin");
    }
    {
        AudioTimeline timeline(48000);

        // 零是合法的时间坐标，不能同时充当“尚未设置”的标记。
        check(timeline.setOrigin(0) &&
                  timeline.planPacket(100000, 480, plan) &&
                  plan.silenceFrames == 480 &&
                  plan.skipFrames == 0 &&
                  plan.audioFrames == 480 &&
                  timeline.totalFrames() == 960,
              "zero timestamp is a valid external origin");
    }
    {
        AudioTimeline timeline(48000);

        // 本阶段仍拒绝原点之前的包；失败不能破坏后续有效包的计算。
        check(timeline.setOrigin(base) &&
                  !timeline.planPacket(base - 1, 480, plan) &&
                  plan.silenceFrames == 0 &&
                  plan.skipFrames == 0 &&
                  plan.audioFrames == 0 &&
                  timeline.totalFrames() == 0 &&
                  timeline.planPacket(base + 100000, 480, plan) &&
                  plan.silenceFrames == 480 &&
                  plan.skipFrames == 0 &&
                  plan.audioFrames == 480 &&
                  timeline.totalFrames() == 960,
              "packet before external origin fails without changing state");
    }

    {
        AudioTimeline timeline(48000);
        // 模拟点击时还有两个 10ms 的包未处理，截止点落在第二个包的中间。
        const auto cutoff = base + 150000;
        const auto first = timeline.framesBeforeStop(base, 480, cutoff);
        const auto second = timeline.framesBeforeStop(base + 100000, 480, cutoff);
        check(first == 480 && second == 240 &&
              timeline.planPacket(base, first, plan) &&
              timeline.planPacket(base + 100000, second, plan) &&
              timeline.totalFrames() == 720,
              "stop drains buffered audio up to the cutoff");
        check(timeline.framesBeforeStop(cutoff, 480, cutoff) == 0 &&
              timeline.framesBeforeStop(cutoff + 1, 480, cutoff) == 0,
              "packets starting at or after stop are dropped");
        check(timeline.framesBeforeStop(base, 480, base + 100000) == 480 &&
              timeline.framesBeforeStop(base, 480, base + 99999) == 479,
              "stop never includes an incomplete sample frame");
        check(timeline.framesBeforeStop(base, 480, std::numeric_limits<std::uint64_t>::max()) == 480,
              "distant cutoff keeps the whole packet without overflow");
    }

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks << " checks\n";
    return 0;
}
