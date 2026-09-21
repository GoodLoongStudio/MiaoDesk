#pragma once

// Platform-independent audio ingress for the Scene input bus.
//
// Windows capture (WASAPI loopback) produces interleaved float PCM at whatever rate
// the device runs; the spectrum analyzer wants a fixed-size mono window at a known
// rate. This header declares only the conversion, so it can be verified without an
// audio device. The WASAPI half belongs to the desktop host, not here.

#include "miaodesk/MiaoInputBus.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace miaodesk::content::inputbus {

// Averages `channels` interleaved float channels into `mono`, clamping each sample to
// [-1, 1]. Averaging rather than taking channel 0 keeps a centred stereo cue that
// carries a mono-compatible bass line from being halved or lost.
std::size_t DownmixToMono(const float* interleaved, std::size_t frameCount, std::size_t channels,
                          double* mono);

// Linear resample from `inputRate` to `outputRate`. A passthrough copy when the rates
// match, and an empty result on invalid input rather than a divide-by-zero.
std::size_t ResampleLinear(const double* input, std::size_t inputCount, double inputRate,
                           double outputRate, std::vector<double>* output);

// Downmix, resample and feed complete analyzer windows from one captured buffer.
// Returns the number of resampled samples consumed; a partial trailing window is left
// for the next call rather than zero-padded, because silence reads as real silence.
// `lastFrame` receives the most recent complete analysis.
std::size_t FeedAnalyzer(const float* interleaved, std::size_t frameCount, std::size_t channels,
                         double inputRate, double analysisRate, AudioSpectrumAnalyzer* analyzer,
                         AudioSpectrumFrame* lastFrame);

} // namespace miaodesk::content::inputbus
