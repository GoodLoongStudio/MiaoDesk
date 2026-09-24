#include "miaodesk/MiaoAnalyticParticleField.h"

#include <cmath>

namespace miaodesk::content {

namespace {

// Verbatim from LayeredSceneRenderer.h:97-104. The constants are part of the
// *visual*: a different hash relocates every sparkle and petal, so this is copied
// rather than "improved". Changing it would silently redesign three wallpapers.
inline double Hash01(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<double>(value & 0x00ffffffu) /
           static_cast<double>(0x01000000u);
}

inline double Wrap01(double value) {
    const double wrapped = std::fmod(value, 1.0);
    return wrapped < 0.0 ? wrapped + 1.0 : wrapped;
}

inline double Clamp01(double value) {
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

// The legacy renderer works in float; keeping that here means a migrated field
// lands on the same pixels the legacy path produced, to the last bit the old
// float arithmetic reached. Widening to double would be "more accurate" and
// therefore different.
using LegacyFloat = float;

void Tint(Color4* color, const Color4& base, double alphaScale) {
    color->r = base.r;
    color->g = base.g;
    color->b = base.b;
    color->a = Clamp01(base.a * alphaScale);
}

} // namespace

bool IsAnalyticEmitterMode(ParticleEmitterMode mode) noexcept {
    return mode == ParticleEmitterMode::Sparkle ||
           mode == ParticleEmitterMode::CometTrail ||
           mode == ParticleEmitterMode::PetalFall;
}

std::vector<AnalyticParticleSample> EvaluateAnalyticParticleField(
    const ParticleEmitterDefinition& emitter,
    double designWidth,
    double designHeight,
    double timeSeconds,
    const AnalyticParticleLimits& limits) {
    std::vector<AnalyticParticleSample> samples;
    if (!emitter.enabled || !IsAnalyticEmitterMode(emitter.mode)) return samples;
    if (!(designWidth > 0.0) || !(designHeight > 0.0)) return samples;

    const double width = designWidth;
    const double height = designHeight;
    const double t = timeSeconds;
    const std::uint32_t count = emitter.analyticCount;

    if (emitter.mode == ParticleEmitterMode::Sparkle) {
        // LayeredSceneRenderer.h:270-277.
        const std::uint32_t n = count > limits.maxSparkles ? limits.maxSparkles : count;
        samples.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            AnalyticParticleSample s;
            s.x = Hash01(i * 79u + 19u) * width;
            s.y = Hash01(i * 101u + 31u) * height * 0.78;
            // All-float, exactly as the legacy line computes it. Doing `t * 0.7`
            // in double and casting the product is *not* the same rounding, and the
            // whole claim of this module is that a migrated field lands where the
            // legacy path landed. The constants are float literals on purpose.
            const LegacyFloat frequency = 0.7f + static_cast<LegacyFloat>(i % 5u) * 0.13f;
            const LegacyFloat argument =
                static_cast<LegacyFloat>(t) * frequency + static_cast<LegacyFloat>(i);
            const LegacyFloat pulse = 0.22f + (0.5f + 0.5f * std::sin(argument)) * 0.78f;
            const LegacyFloat radius = 0.8f + static_cast<LegacyFloat>(i % 4u) * 0.42f;
            s.radiusX = radius;
            s.radiusY = radius;
            Tint(&s.color, emitter.analyticColor, emitter.analyticOpacity * pulse);
            samples.push_back(s);
        }
        return samples;
    }

    if (emitter.mode == ParticleEmitterMode::CometTrail) {
        // LayeredSceneRenderer.h:282-308. Two loops: comets, then trail steps.
        const std::uint32_t comets = count > limits.maxComets ? limits.maxComets : count;
        const std::uint32_t steps = limits.maxTrailSteps;
        const double speed = emitter.analyticSpeed > 0.0 ? emitter.analyticSpeed : 0.10;
        samples.reserve(static_cast<std::size_t>(comets) * steps);
        constexpr LegacyFloat kPi = 3.14159265359f;
        for (std::uint32_t comet = 0; comet < comets; ++comet) {
            const LegacyFloat phase =
                comets > 0 ? static_cast<LegacyFloat>(comet) / static_cast<LegacyFloat>(comets)
                           : 0.0f;
            const LegacyFloat head = static_cast<LegacyFloat>(
                Wrap01(static_cast<double>(static_cast<LegacyFloat>(t) * speed) +
                       static_cast<double>(phase)));
            for (std::uint32_t trail = 0; trail < steps; ++trail) {
                const LegacyFloat p = head - static_cast<LegacyFloat>(trail) * 0.014f;
                if (p < 0.0f || p > 1.0f) continue;
                const LegacyFloat fade = 1.0f - static_cast<LegacyFloat>(trail) /
                                                   static_cast<LegacyFloat>(steps);
                const LegacyFloat arch = std::sin(p * kPi);
                AnalyticParticleSample s;
                s.x = width * (0.48 + static_cast<double>(p) * 0.46);
                const LegacyFloat wobble =
                    std::sin(static_cast<LegacyFloat>(t) * 0.72f +
                             static_cast<LegacyFloat>(comet) * 1.91f + p * 8.0f) *
                    static_cast<LegacyFloat>(height) * 0.005f;
                s.y = static_cast<double>(static_cast<LegacyFloat>(height) *
                                          (0.315f - arch * 0.145f)) +
                      static_cast<double>(wobble);
                const LegacyFloat radius = 1.1f + fade * 2.8f;
                s.radiusX = radius;
                s.radiusY = radius;
                Tint(&s.color, emitter.analyticColor, emitter.analyticOpacity * fade * fade);
                // The legacy head strokes a cross (LayeredSceneRenderer.h:300-305).
                s.cross = (trail == 0);
                samples.push_back(s);
            }
        }
        return samples;
    }

    // PetalFall. LayeredSceneRenderer.h:310-321.
    const std::uint32_t n = count > limits.maxPetals ? limits.maxPetals : count;
    samples.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const LegacyFloat speed = 0.010f + static_cast<LegacyFloat>(i % 5u) * 0.0025f;
        const double p = Wrap01(static_cast<double>(Hash01(i * 43u + 7u) +
                                                    static_cast<LegacyFloat>(t) * speed));
        const double baseX = Hash01(i * 61u + 23u) * width;
        AnalyticParticleSample s;
        s.x = baseX + std::sin(static_cast<LegacyFloat>(t) * 0.34f +
                               static_cast<LegacyFloat>(i) * 1.17f) *
                          width * 0.018;
        s.y = -height * 0.05 + p * height * 1.10;
        s.radiusX = width * (0.0018 + static_cast<double>(i % 3u) * 0.0006);
        s.radiusY = height * (0.0042 + static_cast<double>(i % 4u) * 0.0008);
        const LegacyFloat fade = 0.35f + (1.0f - static_cast<LegacyFloat>(p)) * 0.45f;
        Tint(&s.color, emitter.analyticColor, emitter.analyticOpacity * fade);
        samples.push_back(s);
    }
    return samples;
}

namespace {

bool Near(double a, double b, double tolerance) noexcept {
    return std::fabs(a - b) <= tolerance;
}

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("    [FAIL] %s\n", what);
        ++gFailures;
    }
}

ParticleEmitterDefinition AnalyticEmitter(ParticleEmitterMode mode, std::uint32_t count,
                                          double opacity = 1.0, double speed = 0.10) {
    ParticleEmitterDefinition emitter;
    emitter.id = L"particle://self-test/field";
    emitter.mode = mode;
    emitter.analyticCount = count;
    emitter.analyticOpacity = opacity;
    emitter.analyticSpeed = speed;
    return emitter;
}

} // namespace

bool MiaoAnalyticParticleFieldSelfTest() noexcept {
    gFailures = 0;
    const double width = 1672.0;
    const double height = 941.0;

    // Mode gating. A Simulated emitter must produce nothing here — its particles come
    // from MiaoParticleRuntime, and a module that also answered for it would mean two
    // owners for the same visual.
    Check(EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Simulated, 8),
                                        width, height, 1.0)
              .empty(),
          "Simulated mode yields no analytic samples");

    Check(IsAnalyticEmitterMode(ParticleEmitterMode::Sparkle) &&
              IsAnalyticEmitterMode(ParticleEmitterMode::CometTrail) &&
              IsAnalyticEmitterMode(ParticleEmitterMode::PetalFall) &&
              !IsAnalyticEmitterMode(ParticleEmitterMode::Simulated),
          "IsAnalyticEmitterMode covers exactly the three analytic modes");

    // An unsceneable frame draws nothing rather than NaN coordinates that a renderer
    // would then reject somewhere far from here.
    Check(EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Sparkle, 8),
                                        0.0, height, 1.0)
              .empty(),
          "zero design width yields nothing");
    Check(EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Sparkle, 8),
                                        width, 0.0, 1.0)
              .empty(),
          "zero design height yields nothing");
    Check(EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Sparkle, 0),
                                        width, height, 1.0)
              .empty(),
          "zero count yields nothing");

    // Disabled yields nothing even with a valid count.
    {
        ParticleEmitterDefinition emitter = AnalyticEmitter(ParticleEmitterMode::Sparkle, 8);
        emitter.enabled = false;
        Check(EvaluateAnalyticParticleField(emitter, width, height, 1.0).empty(),
              "disabled emitter yields nothing");
    }

    // Sparkle positions must land where the legacy hash puts them. The reference hash
    // below is an independent transcription of LayeredSceneRenderer.h:97-104 — the
    // constants are part of the visual, so this pins them rather than trusting the
    // copy inside the module.
    auto referenceHash = [](std::uint32_t value) {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return static_cast<double>(value & 0x00ffffffu) /
               static_cast<double>(0x01000000u);
    };
    {
        const auto samples =
            EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Sparkle, 12),
                                          width, height, 0.0);
        Check(samples.size() == 12, "sparkle count is honoured");
        bool positionsMatch = true;
        bool insideField = true;
        bool alphaInRange = true;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const std::uint32_t index = static_cast<std::uint32_t>(i);
            const double wantX = referenceHash(index * 79u + 19u) * width;
            const double wantY = referenceHash(index * 101u + 31u) * height * 0.78;
            if (!Near(samples[i].x, wantX, 1e-9) || !Near(samples[i].y, wantY, 1e-9)) {
                positionsMatch = false;
            }
            if (samples[i].x < 0.0 || samples[i].x > width || samples[i].y < 0.0 ||
                samples[i].y > height * 0.78)
                insideField = false;
            if (samples[i].color.a < 0.0 || samples[i].color.a > 1.0) alphaInRange = false;
            // Spheres, not ellipses: the legacy sparkle uses one radius.
            if (!Near(samples[i].radiusX, samples[i].radiusY, 1e-12)) insideField = false;
        }
        Check(positionsMatch, "sparkle positions match the legacy hash");
        Check(insideField, "sparkles stay inside the legacy field bounds");
        Check(alphaInRange, "sparkle alpha stays in [0, 1]");
        Check(!samples[0].cross, "sparkles do not carry the comet cross flag");
    }

    // Pulse periodicity, per index. Sparkle i brightens at 0.7 + (i%5)*0.13 rad/s,
    // so each index has its *own* period and must be advanced by that one. My first
    // version advanced every sample by 2pi/0.7 and compared all five — which would
    // only ever have held for i=0.
    //
    // The tolerance is 1e-4 rather than 1e-9 because the period is reconstructed in
    // double and the argument re-evaluated in float: a value of ~7 carries ~1e-7 of
    // float rounding, which through sin() is ~1e-7 of alpha. A *wrong* frequency
    // multiplier moves the value by whole percentage points, so this still catches
    // the drift it exists to catch.
    {
        const auto emitter = AnalyticEmitter(ParticleEmitterMode::Sparkle, 5);
        bool periodic = true;
        for (std::uint32_t i = 0; i < 5; ++i) {
            const double frequency = 0.7 + static_cast<double>(i % 5u) * 0.13;
            const double period = 2.0 * 3.14159265358979323846 / frequency;
            const auto a = EvaluateAnalyticParticleField(emitter, width, height, 1.0);
            const auto b = EvaluateAnalyticParticleField(emitter, width, height, 1.0 + period);
            if (!Near(a[i].color.a, b[i].color.a, 1e-4)) {
                std::printf("    sparkle %u period mismatch: %.9f vs %.9f\n", i,
                            a[i].color.a, b[i].color.a);
                periodic = false;
            }
        }
        Check(periodic, "sparkle pulse repeats after its own per-index period");
    }

    // Comet trail. The per-frame count is *not* a fixed comets × steps: a trail step
    // is skipped when the path parameter leaves [0, 1], so the count is time
    // dependent. That is the legacy behaviour, not a bug — but it means the assertion
    // has to be the bound, plus one time at which every step really is in range.
    {
        AnalyticParticleLimits limits;
        const auto emitter = AnalyticEmitter(ParticleEmitterMode::CometTrail, 4);
        // t = 2 with speed 0.10 puts each comet's head at 0.20 / 0.45 / 0.70 / 0.95,
        // all >= 10*0.014, so all 11 trail steps of all 4 comets survive the range
        // test and the count is exactly comets × steps.
        const auto full = EvaluateAnalyticParticleField(emitter, width, height, 2.0, limits);
        Check(full.size() == 4 * static_cast<std::size_t>(limits.maxTrailSteps),
              "comet trail emits comets × trail steps when every step is in range");
        bool bounded = true;
        bool headHasCross = false;
        std::size_t crosses = 0;
        bool insideField = true;
        for (double t = 0.0; t < 12.0; t += 0.37) {
            const auto samples = EvaluateAnalyticParticleField(emitter, width, height, t, limits);
            if (samples.size() > 4 * static_cast<std::size_t>(limits.maxTrailSteps)) {
                bounded = false;
            }
            for (const auto& sample : samples) {
                if (sample.cross) {
                    headHasCross = true;
                    ++crosses;
                }
                if (sample.x < 0.0 || sample.x > width || sample.color.a < 0.0 ||
                    sample.color.a > 1.0)
                    insideField = false;
            }
        }
        Check(bounded, "comet trail never exceeds comets × trail steps");
        Check(headHasCross, "the comet head carries the cross flag");
        Check(insideField, "comet samples stay in bounds with alpha in [0, 1]");
        // Exactly one head per comet at t=0, where head = comet/comets.
        crosses = 0;
        for (const auto& sample : EvaluateAnalyticParticleField(emitter, width, height, 0.0)) {
            if (sample.cross) ++crosses;
        }
        Check(crosses == 4, "exactly one cross per comet at t=0");
    }

    // Petals: elliptical, i.e. the two radii differ — which is what separates them
    // from the sparkles' single radius. Whether radiusX happens to be *smaller*
    // depends on the design aspect ratio (at 1672×941 it flips at i=4), so asserting
    // a direction would be asserting the wallpaper's dimensions, not the shape.
    {
        const auto samples =
            EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::PetalFall, 9),
                                          width, height, 0.0);
        Check(samples.size() == 9, "petal count is honoured");
        bool elliptical = true;
        bool inBand = true;
        for (const auto& sample : samples) {
            if (Near(sample.radiusX, sample.radiusY, 1e-12)) elliptical = false;
            if (sample.y < -height * 0.05 || sample.y > height * 1.05) inBand = false;
        }
        Check(elliptical, "petals are elliptical (the two radii differ), unlike sparkles");
        Check(inBand, "petals stay inside the legacy fall band");
    }

    // Caps. A package declaring a huge count must not be able to build a huge frame;
    // the limits mirror LayeredSceneRenderer's own clamps.
    //
    // The comet case asserts the *bound*, not an exact product: at count=100000 the
    // clamped 12 comets have heads at 0.2 + comet/12, and the last two wrap past 1.0
    // to 0.033 / 0.117 — below 10*0.014, so those comets legitimately emit fewer than
    // 11 steps. Asserting exactly 12*11 there would be asserting a time at which no
    // wrap happens, which is a different claim. The exact product is asserted below
    // with a count small enough that nothing wraps.
    {
        AnalyticParticleLimits limits;
        const auto samples =
            EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Sparkle, 100000),
                                          width, height, 1.0, limits);
        Check(samples.size() == limits.maxSparkles,
              "sparkle count is clamped to the per-frame limit");
        const auto pets = EvaluateAnalyticParticleField(
            AnalyticEmitter(ParticleEmitterMode::PetalFall, 100000), width, height, 1.0, limits);
        Check(pets.size() == limits.maxPetals, "petal count is clamped to the per-frame limit");
        const auto comets = EvaluateAnalyticParticleField(
            AnalyticEmitter(ParticleEmitterMode::CometTrail, 100000), width, height, 2.0, limits);
        Check(comets.size() <=
                  static_cast<std::size_t>(limits.maxComets) * limits.maxTrailSteps,
              "comet count is clamped to the per-frame limit");
        Check(!comets.empty(), "clamped comets still emit something");
        // 3 comets at t=2: heads 0.20 / 0.53 / 0.87, none wrapping, so the exact
        // comets × steps product holds.
        const auto three = EvaluateAnalyticParticleField(
            AnalyticEmitter(ParticleEmitterMode::CometTrail, 3), width, height, 2.0, limits);
        Check(three.size() == 3 * static_cast<std::size_t>(limits.maxTrailSteps),
              "three non-wrapping comets emit exactly 3 × trail steps");
    }

    // Opacity scales the whole field, so [Particles] `opacity` is a real knob and not
    // a decorative number.
    {
        const auto full =
            EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Sparkle, 6, 1.0),
                                          width, height, 0.0);
        const auto half =
            EvaluateAnalyticParticleField(AnalyticEmitter(ParticleEmitterMode::Sparkle, 6, 0.5),
                                          width, height, 0.0);
        bool scaled = full.size() == half.size();
        if (scaled) {
            for (std::size_t i = 0; i < full.size(); ++i) {
                if (!Near(full[i].color.a, half[i].color.a * 2.0, 1e-6)) scaled = false;
            }
        }
        Check(scaled, "analyticOpacity scales the field linearly");
    }

    // Determinism: a stateless field must be a pure function of its arguments.
    {
        const auto emitter = AnalyticEmitter(ParticleEmitterMode::PetalFall, 16);
        const auto a = EvaluateAnalyticParticleField(emitter, width, height, 3.25);
        const auto b = EvaluateAnalyticParticleField(emitter, width, height, 3.25);
        Check(a.size() == b.size(), "field size is deterministic");
        if (a.size() == b.size()) {
            bool same = true;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (!Near(a[i].x, b[i].x, 0.0) || !Near(a[i].y, b[i].y, 0.0) ||
                    !Near(a[i].color.a, b[i].color.a, 0.0))
                    same = false;
            }
            Check(same, "field is a pure function of its arguments");
        }
    }

    if (gFailures) {
        std::printf("  ❌ MiaoAnalyticParticleField SelfTest:%d 项失败\n", gFailures);
        return false;
    }
    std::printf("  ✅ MiaoAnalyticParticleField SelfTest 通过\n");
    return true;
}

} // namespace miaodesk::content
