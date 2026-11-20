#include "../recordingparams.h"
#include <iostream>

// 录制参数表的回归测试：数值都来自改造前的 mainwindow.cpp，
// 一旦有人改了档位或取偶规则，这里会先失败。
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

    // 1. 画质矩阵：x264 与 x265 各自六个取值，必须与旧的三元表达式一致。
    struct CrfCase
    {
        rec::Encoder encoder;
        rec::Quality quality;
        int crf;
        const char *name;
    };
    const CrfCase crfCases[] = {
        {rec::Encoder::LibX264, rec::Quality::High, 18, "x264 high is crf 18"},
        {rec::Encoder::LibX264, rec::Quality::Balanced, 23, "x264 balanced is crf 23"},
        {rec::Encoder::LibX264, rec::Quality::Small, 28, "x264 small is crf 28"},
        {rec::Encoder::LibX265, rec::Quality::High, 23, "x265 high is crf 23"},
        {rec::Encoder::LibX265, rec::Quality::Balanced, 28, "x265 balanced is crf 28"},
        {rec::Encoder::LibX265, rec::Quality::Small, 33, "x265 small is crf 33"},
    };
    for (const CrfCase &item : crfCases)
        check(rec::qualityCrf(item.encoder, item.quality) == item.crf, item.name);

    // 2. 编码器名称解析：QSettings 里保存的就是这两个字符串。
    check(rec::encoderFromName("libx264") == rec::Encoder::LibX264, "parse libx264");
    check(rec::encoderFromName("libx265") == rec::Encoder::LibX265, "parse libx265");
    check(!rec::encoderFromName("libvpx").has_value(), "unknown encoder is rejected");
    check(!rec::encoderFromName("").has_value(), "empty encoder is rejected");
    check(rec::encoderFromName(rec::encoderName(rec::Encoder::LibX265)) == rec::Encoder::LibX265,
          "encoder name round trip");

    // 3. 输出校验用的编码名：ffprobe 返回的是 hevc/h264，不是 libx264/libx265。
    check(rec::expectedVideoCodec(rec::Encoder::LibX264) == "h264", "x264 outputs h264");
    check(rec::expectedVideoCodec(rec::Encoder::LibX265) == "hevc", "x265 outputs hevc");

    // 4. 画质名称：与界面下拉框的 itemData 一致，未知值交给调用方回退。
    check(rec::qualityFromName("high") == rec::Quality::High, "parse high");
    check(rec::qualityFromName("balanced") == rec::Quality::Balanced, "parse balanced");
    check(rec::qualityFromName("small") == rec::Quality::Small, "parse small");
    check(!rec::qualityFromName("best").has_value(), "unknown quality is rejected");
    check(rec::qualityName(rec::Quality::High) == "high", "quality name round trip");

    // 5. 宽高取偶：yuv420p 要求偶数，正数向下取偶，非正数原样返回。
    check(rec::evenDimension(1920) == 1920, "even width is unchanged");
    check(rec::evenDimension(1921) == 1920, "odd width rounds down");
    check(rec::evenDimension(1080) == 1080, "even height is unchanged");
    check(rec::evenDimension(1079) == 1078, "odd height rounds down");
    check(rec::evenDimension(1) == 0, "one pixel rounds to zero");
    check(rec::evenDimension(0) == 0, "zero is unchanged");
    check(rec::evenDimension(-7) == -7, "negative value is returned as is");

    // 6. 帧率表：界面下拉框有三项，默认值是第一个。
    check(rec::kFrameRates.size() == 3, "three frame rate entries match the combo box");
    check(rec::kFrameRates.front() == 10, "default frame rate is 10 fps");
    check(rec::kFrameRates.back() == 30, "highest frame rate is 30 fps");
    for (std::size_t index = 1; index < rec::kFrameRates.size(); ++index)
        check(rec::kFrameRates[index - 1] < rec::kFrameRates[index], "frame rates ascend");

    std::cout << "recordingparams: " << (checks - failures) << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
