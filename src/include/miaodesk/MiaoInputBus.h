#pragma once

// MiaoDesk Scene input bus: the channel contract and the platform-independent
// analysis that feeds it.
//
// The declarative half of this bus already exists in MiaoSceneRuntimeModel /
// MiaoSceneRuntime (BindingSourceKind::Input, InputChannelDefinition,
// AnimationTriggerMode::InputChange / InputRisingEdge). What was missing was a
// producer and a stated contract, so `input://audio/bass` appeared only in a unit
// test stub and nothing could ever drive a wallpaper from sound or the cursor.
//
// This header owns the contract (which channel ids exist, what type and range each
// carries) plus the pure analysis. Platform capture stays elsewhere: WASAPI loopback
// for audio and the desktop host for the pointer. Splitting it this way keeps the
// hard part — the signal processing — testable off Windows.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::content::inputbus {

// ---------------------------------------------------------------------------
// Channel contract
// ---------------------------------------------------------------------------
//
// Every channel is one of three shapes:
//   * Float01  — continuously sampled, in [0, 1]
//   * BoolState — a level that persists until it changes (button held)
//   * BoolEdge  — true for exactly one frame on a transition
//
// Bindings read them with BindingSourceKind::Input and the matching sourceId.
// BoolEdge channels are what AnimationTriggerMode::InputRisingEdge is for;
// BoolState channels pair with AnimationTriggerMode::InputChange.

inline constexpr std::wstring_view kFrameTimeInput = L"input://frame/time";
inline constexpr std::wstring_view kEventPulse = L"input://event/pulse";

// Audio. Values are normalised to [0, 1]; 0 means silence for that band.
inline constexpr std::wstring_view kAudioLevel = L"input://audio/level";
inline constexpr std::wstring_view kAudioBass = L"input://audio/bass";
inline constexpr std::wstring_view kAudioLowMid = L"input://audio/lowmid";
inline constexpr std::wstring_view kAudioMid = L"input://audio/mid";
inline constexpr std::wstring_view kAudioHighMid = L"input://audio/highmid";
inline constexpr std::wstring_view kAudioTreble = L"input://audio/treble";
// Beat detector: true for the single frame on which an onset is detected.
inline constexpr std::wstring_view kAudioBeat = L"input://audio/beat";

// Pointer. Positions are monitor-relative in [0, 1] with (0, 0) at the top-left of
// the monitor the wallpaper occupies, NOT screen coordinates — a wallpaper must not
// learn the desktop layout.
inline constexpr std::wstring_view kPointerX = L"input://pointer/x";
inline constexpr std::wstring_view kPointerY = L"input://pointer/y";
inline constexpr std::wstring_view kPointerInside = L"input://pointer/inside";
inline constexpr std::wstring_view kPointerDown = L"input://pointer/down";
inline constexpr std::wstring_view kPointerClick = L"input://event/pointer/click";
inline constexpr std::wstring_view kPointerEnter = L"input://event/pointer/enter";
inline constexpr std::wstring_view kPointerLeave = L"input://event/pointer/leave";

inline constexpr std::wstring_view kFloat01AudioChannels[] = {
    kAudioLevel, kAudioBass, kAudioLowMid, kAudioMid, kAudioHighMid, kAudioTreble,
};
inline constexpr std::wstring_view kBoolEdgeAudioChannels[] = {kAudioBeat};
// Position is a continuum; containment is a state. Keeping them in different shapes
// is what lets a binding smooth one and threshold the other.
inline constexpr std::wstring_view kFloat01PointerChannels[] = {kPointerX, kPointerY};
inline constexpr std::wstring_view kBoolStatePointerChannels[] = {kPointerInside, kPointerDown};
inline constexpr std::wstring_view kBoolEdgePointerChannels[] = {kPointerClick, kPointerEnter,
                                                                 kPointerLeave};

// Channels a wallpaper may consume without opting out of click-through.
// Pointer *position* is invisible to the shell: the host tracks the cursor globally
// while the wallpaper window still ignores mouse messages, so parallax and glow do
// not steal desktop clicks. Containment, enter and leave are derivable from that same
// position stream, so they stay available too. Press/click DO disable click-through
// and are therefore opt-in per wallpaper.
inline constexpr std::wstring_view kPositionOnlyChannels[] = {
    kAudioLevel, kAudioBass,  kAudioLowMid, kAudioMid,  kAudioHighMid, kAudioTreble,
    kAudioBeat,  kPointerX,   kPointerY,    kPointerInside,
    kPointerEnter, kPointerLeave,
    kFrameTimeInput, kEventPulse,
};
// These require the wallpaper to stop being click-through.
inline constexpr std::wstring_view kInteractivePointerChannels[] = {kPointerDown, kPointerClick};

enum class InputChannelShape {
    Float01,
    BoolState,
    BoolEdge,
};

// Returns the declared shape for a channel id, or false when the id is not part of
// the contract. Anything outside this table is not a channel: a producer must not
// write it to the InputBus, and a scene that declares it is invalid.
inline bool ChannelShape(std::wstring_view id, InputChannelShape* shape) {
    for (const auto channel : kFloat01AudioChannels) {
        if (channel == id) {
            *shape = InputChannelShape::Float01;
            return true;
        }
    }
    for (const auto channel : kFloat01PointerChannels) {
        if (channel == id) {
            *shape = InputChannelShape::Float01;
            return true;
        }
    }
    for (const auto channel : kBoolEdgeAudioChannels) {
        if (channel == id) {
            *shape = InputChannelShape::BoolEdge;
            return true;
        }
    }
    for (const auto channel : kBoolEdgePointerChannels) {
        if (channel == id) {
            *shape = InputChannelShape::BoolEdge;
            return true;
        }
    }
    for (const auto channel : kBoolStatePointerChannels) {
        if (channel == id) {
            *shape = InputChannelShape::BoolState;
            return true;
        }
    }
    if (id == kFrameTimeInput) {
        *shape = InputChannelShape::Float01;
        return true;
    }
    if (id == kEventPulse) {
        *shape = InputChannelShape::BoolEdge;
        return true;
    }
    return false;
}

// True when consuming this channel forces the wallpaper to stop being
// click-through. Widgets are always interactive and are exempt.
inline bool ChannelRequiresInteraction(std::wstring_view id) {
    for (const auto channel : kInteractivePointerChannels) {
        if (channel == id) return true;
    }
    return false;
}

// True when the scene declared at least one channel that requires click-through to be
// disabled. The host asks this rather than hardcoding channel names, so adding a new
// interactive channel changes behaviour in exactly one place.
//
// Takes a range of ids so a caller can hand over SceneRuntimeDefinition::inputs without
// first projecting it into a container of a particular type.
template <typename Range>
bool DeclaresInteractiveInput(const Range& declaredIds) {
    for (const auto& id : declaredIds) {
        if (ChannelRequiresInteraction(id)) return true;
    }
    return false;
}


// ---------------------------------------------------------------------------
// Audio analysis
// ---------------------------------------------------------------------------

// Number of log-spaced spectrum bins exposed alongside the named bands.
inline constexpr std::size_t kSpectrumBins = 16;

struct AudioSpectrumConfig {
    // FFT window. Must be a power of two; 2048 at 48 kHz gives ~23 Hz resolution
    // and covers a frame at up to ~23 analysis updates per second.
    std::size_t fftSize{2048};
    double sampleRate{48000.0};

    // Band edges in Hz. Order is ascending and the array must not be empty.
    std::array<double, 5> bandEdgesHz{{20.0, 160.0, 500.0, 2000.0, 5000.0}};
    // Upper edge of the whole analysis range. Energy above this is ignored.
    double spectrumMaxHz{16000.0};
    // Lowest bin centre, so the log spiral does not collapse at the bottom.
    double spectrumMinHz{40.0};

    // dB floor/ceiling used to map magnitude onto [0, 1]. A wide range keeps quiet
    // detail visible; a narrow one makes the result punchier.
    double floorDb{-72.0};
    double ceilingDb{0.0};

    // Asymmetric smoothing in seconds. Fast attack / slow decay reads as musical.
    double attackSeconds{0.02};
    double decaySeconds{0.28};
};

// Result of one analysis frame. All floats are in [0, 1].
struct AudioSpectrumFrame {
    std::array<double, 5> bands{};
    std::array<double, kSpectrumBins> spectrum{};
    double level{};
    bool beat{};
};

// Radix-2 complex FFT plus the banding and smoothing policy. Deterministic and
// allocation-free after construction, so a host can call it once per captured
// buffer without churning the heap.
class AudioSpectrumAnalyzer {
public:
    explicit AudioSpectrumAnalyzer(AudioSpectrumConfig config = {}) : config_(Normalize(std::move(config))) {
        window_.resize(config_.fftSize);
        for (std::size_t i = 0; i < config_.fftSize; ++i) {
            // Hann window: without it, spectral leakage smears bass energy across
            // the whole spectrum and every band reads as "loud".
            const double t = static_cast<double>(i) / static_cast<double>(config_.fftSize - 1);
            window_[i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * t);
        }
        real_.assign(config_.fftSize, 0.0);
        imag_.assign(config_.fftSize, 0.0);
        magnitudes_.assign(config_.fftSize / 2 + 1, 0.0);
        smoothedBands_.fill(0.0);
        smoothedSpectrum_.fill(0.0);
        smoothedLevel_ = 0.0;
        beatHistory_.fill(0.0);
    }

    const AudioSpectrumConfig& Config() const noexcept { return config_; }

    // `mono` must contain at least Config().fftSize samples. Extra samples are
    // ignored; a short buffer is rejected rather than silently zero-padded, because
    // a silent buffer would otherwise look like real silence.
    bool Analyze(const std::vector<double>& mono, double deltaSeconds, AudioSpectrumFrame* out) {
        return Analyze(mono.data(), mono.size(), deltaSeconds, out);
    }

    bool Analyze(const double* mono, std::size_t count, double deltaSeconds, AudioSpectrumFrame* out) {
        if (!out || !mono || count < config_.fftSize) return false;
        const double dt = std::isfinite(deltaSeconds) && deltaSeconds > 0.0 ? deltaSeconds : 1.0 / 60.0;

        for (std::size_t i = 0; i < config_.fftSize; ++i) {
            real_[i] = mono[i] * window_[i];
            imag_[i] = 0.0;
        }
        Transform(real_.data(), imag_.data(), config_.fftSize);

        const std::size_t binCount = config_.fftSize / 2 + 1;
        const double hzPerBin = config_.sampleRate / static_cast<double>(config_.fftSize);
        double peak = 0.0;
        for (std::size_t bin = 0; bin < binCount; ++bin) {
            const double re = real_[bin];
            const double im = imag_[bin];
            const double magnitude = std::sqrt(re * re + im * im) / static_cast<double>(config_.fftSize);
            magnitudes_[bin] = magnitude;
            peak = std::max(peak, magnitude);
        }

        // Named bands: average the bins whose centre frequency falls in each range.
        for (std::size_t band = 0; band < 5; ++band) {
            const double lowHz = config_.bandEdgesHz[band];
            const double highHz = band + 1 < config_.bandEdgesHz.size() ? config_.bandEdgesHz[band + 1]
                                                                       : config_.spectrumMaxHz;
            double sum = 0.0;
            std::size_t count = 0;
            for (std::size_t bin = 1; bin + 1 < binCount; ++bin) {
                const double hz = static_cast<double>(bin) * hzPerBin;
                if (hz < lowHz || hz >= highHz) continue;
                sum += magnitudes_[bin];
                ++count;
            }
            const double mean = count > 0 ? sum / static_cast<double>(count) : 0.0;
            out->bands[band] = Smooth(smoothedBands_[band], Normalize(mean), dt);
        }

        // Log-spaced spectrum bins.
        for (std::size_t index = 0; index < kSpectrumBins; ++index) {
            const double ratio = static_cast<double>(index) / static_cast<double>(kSpectrumBins);
            const double nextRatio = static_cast<double>(index + 1) / static_cast<double>(kSpectrumBins);
            const double lowHz = LogFrequency(ratio);
            const double highHz = LogFrequency(nextRatio);
            double sum = 0.0;
            std::size_t count = 0;
            for (std::size_t bin = 1; bin + 1 < binCount; ++bin) {
                const double hz = static_cast<double>(bin) * hzPerBin;
                if (hz < lowHz || hz >= highHz) continue;
                sum += magnitudes_[bin];
                ++count;
            }
            const double mean = count > 0 ? sum / static_cast<double>(count) : 0.0;
            out->spectrum[index] = Smooth(smoothedSpectrum_[index], Normalize(mean), dt);
        }

        const double rmsSum = [&] {
            double acc = 0.0;
            for (std::size_t i = 0; i < config_.fftSize; ++i) acc += mono[i] * mono[i];
            return acc;
        }();
        const double rms = std::sqrt(rmsSum / static_cast<double>(config_.fftSize));
        // 0 dBFS full-scale sine is ~0.707 RMS, so scale back into [0, 1].
        out->level = Smooth(smoothedLevel_, std::clamp(rms / 0.7071, 0.0, 1.0), dt);

        out->beat = DetectBeat(out->bands[0], dt);
        return true;
    }

    void Reset() {
        smoothedBands_.fill(0.0);
        smoothedSpectrum_.fill(0.0);
        smoothedLevel_ = 0.0;
        beatHistory_.fill(0.0);
        beatHistoryIndex_ = 0;
        beatCooldown_ = 0.0;
        std::fill(magnitudes_.begin(), magnitudes_.end(), 0.0);
    }

private:
    static AudioSpectrumConfig Normalize(AudioSpectrumConfig config) {
        auto size = config.fftSize;
        if (size < 64) size = 64;
        if (size > 16384) size = 16384;
        // Round to the next power of two; the transform is radix-2 only.
        std::size_t po2 = 64;
        while (po2 < size) po2 <<= 1;
        config.fftSize = po2;
        if (!(config.sampleRate >= 8000.0) || config.sampleRate > 384000.0) config.sampleRate = 48000.0;
        for (auto& edge : config.bandEdgesHz) {
            if (!std::isfinite(edge) || edge <= 0.0) edge = 1.0;
        }
        if (!(config.spectrumMaxHz > config.spectrumMinHz) || config.spectrumMinHz <= 0.0) {
            config.spectrumMinHz = 40.0;
            config.spectrumMaxHz = std::max(16000.0, config.spectrumMinHz * 2.0);
        }
        config.spectrumMaxHz = std::min(config.spectrumMaxHz, config.sampleRate * 0.5);
        if (!(config.floorDb < config.ceilingDb)) {
            config.floorDb = -72.0;
            config.ceilingDb = 0.0;
        }
        if (!(config.attackSeconds > 0.0) || !std::isfinite(config.attackSeconds)) config.attackSeconds = 0.02;
        if (!(config.decaySeconds > 0.0) || !std::isfinite(config.decaySeconds)) config.decaySeconds = 0.28;
        return config;
    }

    // Maps a linear magnitude onto [0, 1] through the dB range.
    double Normalize(double magnitude) const {
        if (!(magnitude > 0.0)) return 0.0;
        const double db = 20.0 * std::log10(magnitude);
        const double t = (db - config_.floorDb) / (config_.ceilingDb - config_.floorDb);
        return std::clamp(t, 0.0, 1.0);
    }

    double Smooth(double& state, double target, double dt) const {
        const bool rising = target > state;
        const double tau = rising ? config_.attackSeconds : config_.decaySeconds;
        const double alpha = 1.0 - std::exp(-dt / tau);
        state += (target - state) * std::clamp(alpha, 0.0, 1.0);
        return std::clamp(state, 0.0, 1.0);
    }

    double LogFrequency(double ratio) const {
        const double t = std::clamp(ratio, 0.0, 1.0);
        return config_.spectrumMinHz * std::pow(config_.spectrumMaxHz / config_.spectrumMinHz, t);
    }

    // Onset detection against a short rolling mean of the bass band. A beat must
    // exceed the local average by a margin and respect a refractory period, otherwise
    // a sustained bass note reports a beat on every frame.
    bool DetectBeat(double bass, double dt) {
        double mean = 0.0;
        for (const double value : beatHistory_) mean += value;
        mean /= static_cast<double>(kBeatHistorySize);

        beatCooldown_ = std::max(0.0, beatCooldown_ - dt);
        constexpr double kThreshold = 1.35;
        constexpr double kMinBass = 0.18;
        constexpr double kMinInterval = 0.18;
        bool beat = false;
        if (beatCooldown_ <= 0.0 && bass > kMinBass && bass > mean * kThreshold && mean > 0.02) {
            beat = true;
            beatCooldown_ = kMinInterval;
        }

        beatHistory_[beatHistoryIndex_] = bass;
        beatHistoryIndex_ = (beatHistoryIndex_ + 1) % beatHistory_.size();
        return beat;
    }

    // In-place iterative radix-2 Cooley-Tukey FFT. `n` must be a power of two.
    static void Transform(double* real, double* imag, std::size_t n) {
        for (std::size_t i = 1, j = 0; i < n; ++i) {
            std::size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(real[i], real[j]);
                std::swap(imag[i], imag[j]);
            }
        }
        for (std::size_t length = 2; length <= n; length <<= 1) {
            const double angle = -2.0 * 3.14159265358979323846 / static_cast<double>(length);
            const double wStepRe = std::cos(angle);
            const double wStepIm = std::sin(angle);
            for (std::size_t i = 0; i < n; i += length) {
                double wRe = 1.0;
                double wIm = 0.0;
                for (std::size_t k = 0; k < length / 2; ++k) {
                    const std::size_t a = i + k;
                    const std::size_t b = i + k + length / 2;
                    const double tRe = wRe * real[b] - wIm * imag[b];
                    const double tIm = wRe * imag[b] + wIm * real[b];
                    real[b] = real[a] - tRe;
                    imag[b] = imag[a] - tIm;
                    real[a] += tRe;
                    imag[a] += tIm;
                    const double nextRe = wRe * wStepRe - wIm * wStepIm;
                    wIm = wRe * wStepIm + wIm * wStepRe;
                    wRe = nextRe;
                }
            }
        }
    }

    AudioSpectrumConfig config_;
    std::vector<double> window_;
    std::vector<double> real_;
    std::vector<double> imag_;
    std::vector<double> magnitudes_;
    std::array<double, 5> smoothedBands_{};
    std::array<double, kSpectrumBins> smoothedSpectrum_{};
    double smoothedLevel_{};
    static constexpr std::size_t kBeatHistorySize = 43; // ~0.7 s at 60 fps
    std::array<double, kBeatHistorySize> beatHistory_{};
    std::size_t beatHistoryIndex_{};
    double beatCooldown_{};
};

// ---------------------------------------------------------------------------
// Pointer normalisation
// ---------------------------------------------------------------------------

// A raw cursor position in physical pixels for one monitor.
struct PointerSample {
    long x{};
    long y{};
    bool inside{};
    bool down{};
};

// Converts physical cursor coordinates into the monitor-relative [0, 1] space the
// input bus exposes, with the same asymmetric smoothing as the audio path so a
// parallax layer follows the cursor without vibrating.
class PointerNormalizer {
public:
    struct Result {
        double x{0.5};
        double y{0.5};
        bool inside{};
        bool down{};
        bool entered{};
        bool left{};
        bool clicked{};
    };

    // `width` / `height` are the monitor's physical pixel dimensions.
    void Configure(long width, long height) {
        width_ = width > 0 ? width : 1;
        height_ = height > 0 ? height : 1;
    }

    Result Apply(const PointerSample& sample, double deltaSeconds) {
        const double dt = std::isfinite(deltaSeconds) && deltaSeconds > 0.0 ? deltaSeconds : 1.0 / 60.0;
        const double targetX = Clamp01(static_cast<double>(sample.x) / static_cast<double>(width_));
        const double targetY = Clamp01(static_cast<double>(sample.y) / static_cast<double>(height_));

        Result result;
        result.x = Smooth(x_, targetX, dt);
        result.y = Smooth(y_, targetY, dt);
        result.inside = sample.inside;
        result.down = sample.down;

        // Edges are derived from the transition, not from the smoothed value: a
        // smoothed position would fire "enter" a frame late or twice.
        result.entered = sample.inside && !previousInside_;
        result.left = !sample.inside && previousInside_;
        result.clicked = sample.down && !previousDown_;
        previousInside_ = sample.inside;
        previousDown_ = sample.down;
        return result;
    }

    void Reset() {
        x_ = 0.5;
        y_ = 0.5;
        previousInside_ = false;
        previousDown_ = false;
    }

private:
    static double Clamp01(double value) { return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value); }

    static double Smooth(double& state, double target, double dt) {
        const bool rising = std::abs(target - state) > 0.0 && target > state;
        const double tau = rising ? kAttackSeconds : kDecaySeconds;
        const double alpha = 1.0 - std::exp(-dt / tau);
        state += (target - state) * Clamp01(alpha);
        return Clamp01(state);
    }

    static constexpr double kAttackSeconds = 0.05;
    static constexpr double kDecaySeconds = 0.18;

    long width_{1};
    long height_{1};
    double x_{0.5};
    double y_{0.5};
    bool previousInside_{};
    bool previousDown_{};
};

} // namespace miaodesk::content::inputbus
