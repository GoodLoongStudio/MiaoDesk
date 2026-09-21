#include "miaodesk/MiaoAudioCapture.h"

#include <algorithm>
#include <cmath>

namespace miaodesk::content::inputbus {

std::size_t DownmixToMono(const float* interleaved, std::size_t frameCount, std::size_t channels,
                          double* mono) {
    if (!interleaved || !mono || frameCount == 0) return 0;
    if (channels == 0) channels = 1;
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        // Average the channels rather than taking the left one: a centred stereo cue
        // that also carries a mono-compatible bass line would otherwise be halved or
        // lost entirely, and audio-reactive wallpapers read almost entirely as bass.
        double sum = 0.0;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const double sample = static_cast<double>(interleaved[frame * channels + channel]);
            // WASAPI shared-mode capture can hand back samples outside [-1, 1] when a
            // device reports a hot level; clamping here keeps the FFT input bounded.
            sum += std::clamp(sample, -1.0, 1.0);
        }
        mono[frame] = sum / static_cast<double>(channels);
    }
    return frameCount;
}

std::size_t ResampleLinear(const double* input, std::size_t inputCount, double inputRate,
                           double outputRate, std::vector<double>* output) {
    if (!input || !output || inputCount == 0 || !(inputRate > 0.0) || !(outputRate > 0.0)) {
        if (output) output->clear();
        return 0;
    }
    if (std::abs(inputRate - outputRate) < 1e-9) {
        output->assign(input, input + inputCount);
        return inputCount;
    }

    const double ratio = inputRate / outputRate;
    const auto outputCount = static_cast<std::size_t>(
        std::max(1.0, std::floor(static_cast<double>(inputCount) / ratio)));
    output->assign(outputCount, 0.0);
    for (std::size_t i = 0; i < outputCount; ++i) {
        const double position = static_cast<double>(i) * ratio;
        const auto left = static_cast<std::size_t>(position);
        if (left + 1 >= inputCount) {
            (*output)[i] = input[inputCount - 1];
            continue;
        }
        const double fraction = position - static_cast<double>(left);
        (*output)[i] = input[left] * (1.0 - fraction) + input[left + 1] * fraction;
    }
    return outputCount;
}

std::size_t FeedAnalyzer(const float* interleaved, std::size_t frameCount, std::size_t channels,
                         double inputRate, double analysisRate, AudioSpectrumAnalyzer* analyzer,
                         AudioSpectrumFrame* lastFrame) {
    if (!interleaved || !analyzer || frameCount == 0) return 0;
    const auto mixed = std::make_unique<double[]>(frameCount);
    const std::size_t monoCount = DownmixToMono(interleaved, frameCount, channels, mixed.get());
    if (monoCount == 0) return 0;

    std::vector<double> resampled;
    const std::size_t analysisCount =
        ResampleLinear(mixed.get(), monoCount, inputRate, analysisRate, &resampled);
    if (analysisCount == 0) return 0;

    const std::size_t window = analyzer->Config().fftSize;
    if (analysisCount < window) return 0;

    std::size_t consumed = 0;
    while (consumed + window <= analysisCount) {
        AudioSpectrumFrame frame;
        if (!analyzer->Analyze(resampled.data() + consumed, window, 1.0 / 60.0, &frame)) break;
        if (lastFrame) *lastFrame = frame;
        consumed += window;
    }
    return consumed;
}

} // namespace miaodesk::content::inputbus
