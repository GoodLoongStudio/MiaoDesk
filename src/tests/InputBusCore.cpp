// The Scene input bus contract and the platform-independent analysis behind it.
// This gate deliberately has no Windows dependency: the channel table, the FFT
// banding and the pointer normalisation are all pure logic, so they can be
// verified on every build instead of only on a machine with audio hardware.
#include "miaodesk/MiaoInputBus.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace miaodesk::content::inputbus;

int failures = 0;
void Check(bool c, const std::string& what) {
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", what.c_str());
    if (!c) ++failures;
}

std::vector<double> Sine(double hz, double rate, std::size_t n, double amp = 1.0) {
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate);
    return v;
}

int wmain() {
    const double rate = 48000.0;
    const std::size_t N = 2048;

    std::printf("1. 通道契约\n");
    {
        InputChannelShape shape{};
        Check(ChannelShape(kAudioBass, &shape) && shape == InputChannelShape::Float01, "bass is Float01");
        Check(ChannelShape(kPointerX, &shape) && shape == InputChannelShape::Float01, "pointer/x is Float01");
        Check(ChannelShape(kPointerClick, &shape) && shape == InputChannelShape::BoolEdge, "pointer click is BoolEdge");
        Check(ChannelShape(kPointerDown, &shape) && shape == InputChannelShape::BoolState, "pointer down is BoolState (held), not an edge");
        Check(ChannelShape(kAudioBeat, &shape) && shape == InputChannelShape::BoolEdge, "audio beat is BoolEdge (not Float01)");
        Check(ChannelShape(kFrameTimeInput, &shape) && shape == InputChannelShape::Float01, "frame time is Float01");
        Check(ChannelShape(kEventPulse, &shape) && shape == InputChannelShape::BoolEdge, "event pulse is BoolEdge");
        Check(!ChannelShape(L"input://audio/nope", &shape), "unknown channel rejected");
        Check(!ChannelShape(L"input://audio/", &shape), "trailing-slash channel rejected");
        Check(!ChannelShape(L"", &shape), "empty channel rejected");
        Check(!ChannelShape(L"input://frame/", &shape), "prefix-only channel rejected");
        Check(!ChannelShape(L"INPUT://AUDIO/BASS", &shape), "channel ids are case-sensitive");

        Check(ChannelRequiresInteraction(kPointerDown), "pointer down requires interaction");
        Check(ChannelRequiresInteraction(kPointerClick), "pointer click requires interaction");
        Check(!ChannelRequiresInteraction(kPointerEnter), "pointer enter is position-derived, no interaction needed");
        Check(!ChannelRequiresInteraction(kPointerLeave), "pointer leave is position-derived, no interaction needed");
        Check(!ChannelRequiresInteraction(kPointerX), "pointer position does NOT require interaction");
        Check(!ChannelRequiresInteraction(kPointerY), "pointer y does NOT require interaction");
        Check(!ChannelRequiresInteraction(kPointerInside), "pointer inside does NOT require interaction");
        Check(!ChannelRequiresInteraction(kAudioBass), "audio bass does NOT require interaction");
        Check(!ChannelRequiresInteraction(kAudioBeat), "audio beat does NOT require interaction");
        Check(!ChannelRequiresInteraction(kFrameTimeInput), "frame time does NOT require interaction");
    }

    std::printf("\n2. FFT 正确性(纯音应落在对应频段)\n");
    {
        for (const double hz : {50.0, 300.0, 1000.0, 3500.0, 9000.0}) {
            AudioSpectrumAnalyzer analyzer;   // 全新实例:平滑状态不得跨频率串台
            auto buf = Sine(hz, rate, N);
            AudioSpectrumFrame frame;
            Check(analyzer.Analyze(buf, 1.0 / 60.0, &frame), "analyze " + std::to_string((int)hz) + "Hz");
            std::size_t best = 0;
            for (std::size_t i = 1; i < frame.bands.size(); ++i)
                if (frame.bands[i] > frame.bands[best]) best = i;
            const std::size_t expect = hz < 160 ? 0 : hz < 500 ? 1 : hz < 2000 ? 2 : hz < 5000 ? 3 : 4;
            Check(best == expect,
                  std::to_string((int)hz) + "Hz peaks in band " + std::to_string(best) +
                      " (expected " + std::to_string(expect) + ")");
        }
    }

    std::printf("\n3. 静音必须读作静音\n");
    {
        AudioSpectrumAnalyzer analyzer;
        AudioSpectrumFrame frame;
        std::vector<double> silence(N, 0.0);
        for (int i = 0; i < 60; ++i) analyzer.Analyze(silence, 1.0 / 60.0, &frame);
        Check(frame.level < 1e-6, "level is 0 for silence");
        for (auto b : frame.bands) Check(b < 1e-6, "band is 0 for silence");
        for (auto s : frame.spectrum) Check(s < 1e-6, "spectrum bin is 0 for silence");
        Check(!frame.beat, "no beat for silence");
    }

    std::printf("\n4. 边界输入\n");
    {
        AudioSpectrumAnalyzer analyzer;
        AudioSpectrumFrame frame;
        std::vector<double> shortBuf(128, 1.0);
        Check(!analyzer.Analyze(shortBuf, 1.0 / 60.0, &frame), "short buffer rejected");
        Check(!analyzer.Analyze(nullptr, N, 1.0 / 60.0, &frame), "null buffer rejected");
        Check(!analyzer.Analyze(Sine(100, rate, N), 0.0, nullptr), "null output rejected");
        Check(!analyzer.Analyze(Sine(100, rate, N), 1.0 / 60.0, nullptr), "null output rejected (data)");
        Check(analyzer.Analyze(Sine(100, rate, N), -1.0, &frame), "negative dt falls back, not rejected");
        Check(analyzer.Analyze(Sine(100, rate, N), 1e9, &frame), "huge dt does not produce NaN");
        Check(std::isfinite(frame.level) && std::isfinite(frame.bands[0]), "outputs stay finite");
        for (auto b : frame.bands) Check(b >= 0.0 && b <= 1.0, "band in [0,1]");
        for (auto s : frame.spectrum) Check(s >= 0.0 && s <= 1.0, "spectrum bin in [0,1]");
        Check(frame.level >= 0.0 && frame.level <= 1.0, "level in [0,1]");
    }

    std::printf("\n5. 配置规范化\n");
    {
        AudioSpectrumConfig c;
        c.fftSize = 1000;  // 非 2 的幂 -> 应上取到 1024
        AudioSpectrumAnalyzer a(c);
        Check(a.Config().fftSize == 1024, "non-power-of-two fftSize rounded up to 1024");
        AudioSpectrumConfig c2;
        c2.fftSize = 0;
        AudioSpectrumAnalyzer a2(c2);
        Check(a2.Config().fftSize >= 64, "zero fftSize clamped to >= 64");
        AudioSpectrumConfig c3;
        c3.sampleRate = 0;
        AudioSpectrumAnalyzer a3(c3);
        Check(a3.Config().sampleRate == 48000.0, "invalid sampleRate falls back to 48 kHz");
        AudioSpectrumConfig c4;
        c4.floorDb = 0; c4.ceilingDb = -72;
        AudioSpectrumAnalyzer a4(c4);
        Check(a4.Config().floorDb < a4.Config().ceilingDb, "inverted dB range corrected");
        AudioSpectrumConfig c5;
        c5.spectrumMaxHz = 10; c5.spectrumMinHz = 40;
        AudioSpectrumAnalyzer a5(c5);
        Check(a5.Config().spectrumMaxHz > a5.Config().spectrumMinHz, "inverted spectrum range corrected");
    }

    std::printf("\n6. 缓动与复位\n");
    {
        AudioSpectrumAnalyzer analyzer;
        auto loud = Sine(100.0, rate, N, 1.0);
        auto quiet = Sine(100.0, rate, N, 0.02);
        AudioSpectrumFrame a, b;
        analyzer.Analyze(loud, 1.0 / 60.0, &a);
        analyzer.Analyze(quiet, 1.0 / 60.0, &b);
        Check(b.bands[0] > 0.0, "value survives one frame of decay");
        analyzer.Reset();
        analyzer.Analyze(quiet, 1.0 / 60.0, &b);
        Check(b.bands[0] < a.bands[0], "quiet reads lower than loud");
    }

    std::printf("\n7. 节拍检测:不该把持续低音当节拍\n");
    {
        AudioSpectrumAnalyzer analyzer;
        auto bass = Sine(60.0, rate, N, 0.9);
        AudioSpectrumFrame frame;
        int beats = 0;
        for (int i = 0; i < 120; ++i) {
            analyzer.Analyze(bass, 1.0 / 60.0, &frame);
            if (frame.beat) ++beats;
        }
        Check(beats < 20, "sustained bass yields few beats (got " + std::to_string(beats) + " over 120 frames)");
    }

    std::printf("\n8. 指针归一化\n");
    {
        PointerNormalizer p;
        p.Configure(1920, 1080);
        auto r = p.Apply({960, 540, true, false}, 1.0 / 60.0);
        Check(std::abs(r.x - 0.5) < 0.2 && std::abs(r.y - 0.5) < 0.2, "centre maps near centre");
        r = p.Apply({0, 0, true, false}, 1.0 / 60.0);
        Check(r.x < 0.5 && r.y < 0.5, "top-left corner maps low");
        r = p.Apply({1919, 1079, true, false}, 1.0 / 60.0);
        Check(r.x > 0.5 && r.y > 0.5, "bottom-right corner maps high");
        r = p.Apply({-500, 5000, true, false}, 1.0 / 60.0);
        Check(r.x >= 0.0 && r.y <= 1.0, "out-of-range clamped into [0,1]");
    }

    std::printf("\n9. 指针边沿\n");
    {
        PointerNormalizer p;
        p.Configure(100, 100);
        auto r = p.Apply({10, 10, false, false}, 1.0 / 60.0);
        Check(!r.entered && !r.left, "first frame with outside=false fires no edge");
        r = p.Apply({10, 10, true, false}, 1.0 / 60.0);
        Check(r.entered && !r.left, "enter fires exactly once");
        r = p.Apply({20, 20, true, false}, 1.0 / 60.0);
        Check(!r.entered && !r.left, "no repeated enter while inside");
        r = p.Apply({20, 20, false, false}, 1.0 / 60.0);
        Check(r.left && !r.entered, "leave fires exactly once");
        r = p.Apply({20, 20, true, true}, 1.0 / 60.0);
        Check(r.entered && r.down && r.clicked, "enter+down on the same frame fires both edges");
        r = p.Apply({20, 20, true, true}, 1.0 / 60.0);
        Check(!r.clicked, "held button does not re-fire click");
        r = p.Apply({20, 20, true, false}, 1.0 / 60.0);
        r = p.Apply({20, 20, true, true}, 1.0 / 60.0);
        Check(r.clicked, "click fires again after release");
    }

    std::printf("\n10. 指针复位与配置\n");
    {
        PointerNormalizer p;
        p.Configure(100, 100);
        p.Apply({90, 90, true, true}, 1.0 / 60.0);
        p.Reset();
        auto r = p.Apply({50, 50, false, false}, 1.0 / 60.0);
        Check(!r.entered && !r.clicked && !r.left, "reset clears edge state");
        PointerNormalizer bad;
        bad.Configure(0, 0);
        auto r2 = bad.Apply({0, 0, true, false}, 1.0 / 60.0);
        Check(std::isfinite(r2.x) && std::isfinite(r2.y), "zero-size monitor does not divide by zero");
    }

    std::printf("\n11. 长时间运行稳定性\n");
    {
        AudioSpectrumAnalyzer analyzer;
        std::mt19937 rng(12345);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        std::vector<double> noise(N);
        AudioSpectrumFrame frame;
        bool finite = true;
        for (int i = 0; i < 2000; ++i) {
            for (auto& s : noise) s = dist(rng);
            if (!analyzer.Analyze(noise, 1.0 / 60.0, &frame)) { finite = false; break; }
            if (!std::isfinite(frame.level) || !std::isfinite(frame.bands[0])) { finite = false; break; }
        }
        Check(finite, "2000 frames of noise stay finite");
        Check(frame.level >= 0.0 && frame.level <= 1.0, "noise level in [0,1]");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
