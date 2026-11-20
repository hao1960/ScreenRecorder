#ifndef RECORDINGPARAMS_H
#define RECORDINGPARAMS_H

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

// 录制参数表：把帧率、画质档位与编码器的对应关系集中成编译期常量。
// 界面代码和 FFmpeg 参数都从这里取值，改错数值时由 static_assert 直接编译失败，
// 而不是等到录完才发现画质不对。
namespace rec {

// 编码器的枚举值只在代码内部使用；对外的字符串由 encoderName() 提供。
enum class Encoder
{
    LibX264,
    LibX265
};

// 画质档位；字符串值与 QSettings 里保存的 videoQuality 一致。
enum class Quality
{
    High,
    Balanced,
    Small
};

// 可选的录制帧率；显示文字仍由 mainwindow.ui 提供，这里只给出实际数值。
inline constexpr std::array<int, 3> kFrameRates{10, 15, 30};

namespace detail {

// 画质档位在 CRF 表中的列号。新增档位时这里会因为缺少返回值而报警，
// 提醒同时补上表格的每一行。
[[nodiscard]] constexpr std::size_t qualityRank(Quality quality) noexcept
{
    switch (quality) {
    case Quality::High:
        return 0;
    case Quality::Balanced:
        return 1;
    case Quality::Small:
        return 2;
    }
    return 1;
}

// 编码器在 CRF 表中的行号。
[[nodiscard]] constexpr std::size_t encoderRank(Encoder encoder) noexcept
{
    return encoder == Encoder::LibX265 ? 1u : 0u;
}

// 行是编码器，列是画质档位（较高 / 均衡 / 较小文件）。
// x264 与 x265 使用各自的取值范围，同一个数字不代表相同画质，不能互相换算。
constexpr std::array<std::array<int, 3>, 2> kCrfTable{{
    {18, 23, 28}, // libx264
    {23, 28, 33}, // libx265
}};

} // namespace detail

// 画质档位对应的 CRF 值。
[[nodiscard]] constexpr int qualityCrf(Encoder encoder, Quality quality) noexcept
{
    return detail::kCrfTable[detail::encoderRank(encoder)][detail::qualityRank(quality)];
}

// 把保存下来的编码器名称解析成枚举；无法识别时返回 nullopt，调用方负责提示用户。
[[nodiscard]] constexpr std::optional<Encoder> encoderFromName(std::string_view name) noexcept
{
    if (name == "libx264")
        return Encoder::LibX264;
    if (name == "libx265")
        return Encoder::LibX265;
    return std::nullopt;
}

// FFmpeg -c:v 使用的编码器名称。
[[nodiscard]] constexpr std::string_view encoderName(Encoder encoder) noexcept
{
    return encoder == Encoder::LibX265 ? "libx265" : "libx264";
}

// 输出文件里应该出现的视频流编码名，用于 ffprobe 校验。
[[nodiscard]] constexpr std::string_view expectedVideoCodec(Encoder encoder) noexcept
{
    return encoder == Encoder::LibX265 ? "hevc" : "h264";
}

// 把保存下来的画质名称解析成枚举；无法识别时返回 nullopt，调用方按均衡档处理。
[[nodiscard]] constexpr std::optional<Quality> qualityFromName(std::string_view name) noexcept
{
    if (name == "high")
        return Quality::High;
    if (name == "balanced")
        return Quality::Balanced;
    if (name == "small")
        return Quality::Small;
    return std::nullopt;
}

// QSettings 保存画质时使用的名称。
[[nodiscard]] constexpr std::string_view qualityName(Quality quality) noexcept
{
    switch (quality) {
    case Quality::High:
        return "high";
    case Quality::Balanced:
        return "balanced";
    case Quality::Small:
        return "small";
    }
    return "balanced";
}

// yuv420p 编码要求宽高为偶数，这里向下取偶。
// 非正数原样返回：录制宽高不会是负数，真出现时交给调用方的校验去报错。
[[nodiscard]] constexpr int evenDimension(int value) noexcept
{
    return value > 0 ? value - value % 2 : value;
}

// 六个画质档位必须与改造前 mainwindow.cpp 里的三元表达式完全一致。
static_assert(qualityCrf(Encoder::LibX264, Quality::High) == 18);
static_assert(qualityCrf(Encoder::LibX264, Quality::Balanced) == 23);
static_assert(qualityCrf(Encoder::LibX264, Quality::Small) == 28);
static_assert(qualityCrf(Encoder::LibX265, Quality::High) == 23);
static_assert(qualityCrf(Encoder::LibX265, Quality::Balanced) == 28);
static_assert(qualityCrf(Encoder::LibX265, Quality::Small) == 33);

// 名称与枚举必须一一对应，避免保存值和显示值脱节。
static_assert(encoderFromName("libx264") == Encoder::LibX264);
static_assert(encoderFromName("libx265") == Encoder::LibX265);
static_assert(!encoderFromName("libvpx").has_value());
static_assert(encoderName(Encoder::LibX265) == "libx265");
static_assert(expectedVideoCodec(Encoder::LibX265) == "hevc");
static_assert(expectedVideoCodec(Encoder::LibX264) == "h264");
static_assert(qualityFromName("balanced") == Quality::Balanced);
static_assert(!qualityFromName("best").has_value());
static_assert(qualityName(Quality::Small) == "small");

// 取偶在编译期就能验证，不需要运行时测试。
static_assert(evenDimension(1920) == 1920);
static_assert(evenDimension(1921) == 1920);
static_assert(evenDimension(1080) == 1080);
static_assert(evenDimension(3) == 2);
static_assert(evenDimension(0) == 0);
static_assert(evenDimension(-7) == -7);

// 帧率表按从小到大排列，界面和设置都依赖第一个值作为默认值。
static_assert(kFrameRates.front() == 10);
static_assert(kFrameRates.back() == 30);

} // namespace rec

#endif // RECORDINGPARAMS_H
