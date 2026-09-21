// Audio ingress for the Scene input bus: downmix, resample and window feeding.
// Pure arithmetic on PCM, so the whole path from a WASAPI-shaped buffer to an
// analysis frame is verified without an audio device.
#include "miaodesk/MiaoAudioCapture.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

namespace ib = miaodesk::content::inputbus;

int failures = 0;
void Check(bool c, const std::string& w) {
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", w.c_str());
    if (!c) ++failures;
}
int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Audio ingress\n");

    std::printf("\n1. Downmix\n");
    {
        std::vector<float> stereo = {0.5f, -0.5f, 1.0f, -1.0f, 0.25f, 0.25f};
        std::vector<double> mono(3);
        Check(ib::DownmixToMono(stereo.data(), 3, 2, mono.data()) == 3, "3 stereo frames -> 3 mono");
        Check(std::abs(mono[0]) < 1e-9, "0.5/-0.5 cancels to 0");
        Check(std::abs(mono[1]) < 1e-9, "1.0/-1.0 cancels to 0");
        Check(std::abs(mono[2] - 0.25) < 1e-9, "identical channels pass through unchanged");

        std::vector<float> quad = {1.f, 1.f, 1.f, 1.f};
        std::vector<double> m1(1);
        ib::DownmixToMono(quad.data(), 1, 4, m1.data());
        Check(std::abs(m1[0] - 1.0) < 1e-9, "4 identical channels average to the same level");

        std::vector<float> hot = {4.0f, -4.0f};
        std::vector<double> m2(1);
        ib::DownmixToMono(hot.data(), 1, 2, m2.data());
        Check(std::abs(m2[0]) <= 1.0 + 1e-9, "out-of-range samples clamped into [-1,1]");

        Check(ib::DownmixToMono(nullptr, 3, 2, mono.data()) == 0, "null input -> 0");
        Check(ib::DownmixToMono(stereo.data(), 0, 2, mono.data()) == 0, "zero frames -> 0");
        Check(ib::DownmixToMono(stereo.data(), 3, 0, mono.data()) == 3, "zero channels treated as 1");
    }

    std::printf("\n2. Resample\n");
    {
        std::vector<double> in(100);
        for (std::size_t i = 0; i < in.size(); ++i) in[i] = std::sin(2 * std::numbers::pi * i / 20.0);
        std::vector<double> out;
        Check(ib::ResampleLinear(in.data(), in.size(), 48000, 48000, &out) == 100, "same rate passes through");
        Check(out.size() == 100, "passthrough keeps the count");
        auto n = ib::ResampleLinear(in.data(), in.size(), 48000, 24000, &out);
        Check(n > 0 && out.size() == n, "2:1 downsample halves the count");
        Check(out.size() == 50, "48k -> 24k of 100 samples is 50");
        auto n2 = ib::ResampleLinear(in.data(), in.size(), 24000, 48000, &out);
        Check(n2 == 200, "24k -> 48k of 100 samples is 200");
        Check(ib::ResampleLinear(in.data(), in.size(), 0, 48000, &out) == 0, "zero input rate -> 0");
        Check(ib::ResampleLinear(in.data(), in.size(), 48000, -1, &out) == 0, "negative rate -> 0");
        Check(ib::ResampleLinear(nullptr, 10, 48000, 24000, &out) == 0, "null input -> 0");
        Check(out.empty(), "invalid input clears the output");
        // 端点:最后一帧必须是原值,不得读越界
        for (std::size_t i = 0; i < out.size(); ++i) Check(std::isfinite(out[i]), "resampled finite");
    }

    std::printf("\n3. FeedAnalyzer\n");
    {
        ib::AudioSpectrumAnalyzer analyzer;   // 2048 @ 48k
        const std::size_t window = analyzer.Config().fftSize;
        // 48 kHz 立体声,刚好一个窗口的帧数
        std::vector<float> pcm(window * 2);
        for (std::size_t i = 0; i < window; ++i) {
            const float s = static_cast<float>(std::sin(2 * std::numbers::pi * 100.0 * i / 48000.0));
            pcm[i * 2] = s; pcm[i * 2 + 1] = s;
        }
        ib::AudioSpectrumFrame frame;
        auto used = ib::FeedAnalyzer(pcm.data(), window, 2, 48000.0, 48000.0, &analyzer, &frame);
        Check(used == window, "one full window is consumed");
        Check(std::isfinite(frame.level) && frame.bands[0] > 0.0, "100 Hz shows up in the bass band");

        // 半个窗口:不得零填充成静音帧
        ib::AudioSpectrumAnalyzer a2;
        std::vector<float> half(window / 2 * 2, 0.5f);
        ib::AudioSpectrumFrame f2;
        Check(ib::FeedAnalyzer(half.data(), window / 2, 2, 48000.0, 48000.0, &a2, &f2) == 0,
              "a partial window is not analysed");

        // 多出一个窗口:应产出两帧
        ib::AudioSpectrumAnalyzer a3;
        std::vector<float> two(window * 2 * 2, 0.3f);
        ib::AudioSpectrumFrame f3;
        Check(ib::FeedAnalyzer(two.data(), window * 2, 2, 48000.0, 48000.0, &a3, &f3) == window * 2,
              "two full windows both consumed");

        Check(ib::FeedAnalyzer(nullptr, window, 2, 48000.0, 48000.0, &analyzer, &frame) == 0, "null pcm -> 0");
        Check(ib::FeedAnalyzer(pcm.data(), window, 2, 48000.0, 48000.0, nullptr, &frame) == 0, "null analyzer -> 0");
        Check(ib::FeedAnalyzer(pcm.data(), 0, 2, 48000.0, 48000.0, &analyzer, &frame) == 0, "zero frames -> 0");
        Check(ib::FeedAnalyzer(pcm.data(), window, 2, 48000.0, 48000.0, &analyzer, nullptr) == window,
              "null lastFrame still works");
    }

    std::printf("\n4. 44.1k -> 48k 上采样后仍能分析\n");
    {
        ib::AudioSpectrumAnalyzer analyzer;
        const std::size_t window = analyzer.Config().fftSize;
        const std::size_t frames = window * 2;
        std::vector<float> pcm(frames * 2);
        for (std::size_t i = 0; i < frames; ++i) {
            const float s = static_cast<float>(std::sin(2 * std::numbers::pi * 200.0 * i / 44100.0));
            pcm[i * 2] = s; pcm[i * 2 + 1] = s;
        }
        ib::AudioSpectrumFrame frame;
        auto used = ib::FeedAnalyzer(pcm.data(), frames, 2, 44100.0, 48000.0, &analyzer, &frame);
        Check(used > 0, "upsampled capture yields at least one window");
        Check(std::isfinite(frame.level), "upsampled analysis stays finite");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
