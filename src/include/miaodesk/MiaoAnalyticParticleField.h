#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

// One dot of an analytic particle field, in the scene's **design space** — not in
// output pixels and not in normalised units. Renderers already map design space to
// the render target for sprites, so reusing that mapping here means a particle field
// lands in the same place a sprite at the same design coordinates would.
//
// Design space rather than normalised [0,1] because the legacy formulas are
// *asymmetric* between the axes (a petal's radius is width*0.0018 by
// height*0.0042; a sparkle's field is height*0.78 tall). Collapsing those to one
// scalar would change the shape, which is the one thing a migration must not do.
struct AnalyticParticleSample {
    double x{};
    double y{};
    double radiusX{};
    double radiusY{};
    Color4 color{1.0, 1.0, 1.0, 1.0};
    // The legacy comet head draws a small cross through itself so motion stays
    // obvious at normal viewing distance. Expressed as a flag rather than dropped,
    // and rather than baked into the position maths: a sample is a dot, and this
    // says the renderer should add the cross too.
    bool cross{false};
};

// Per-frame caps. These mirror the clamps LayeredSceneRenderer applies when it
// reads [Particles], and they exist for the same reason: a package declaring
// sparkle_count=100000 must not be able to produce a 100000-iteration frame.
struct AnalyticParticleLimits {
    std::uint32_t maxSparkles{96};
    std::uint32_t maxPetals{64};
    std::uint32_t maxComets{12};
    std::uint32_t maxTrailSteps{11};
};

bool IsAnalyticEmitterMode(ParticleEmitterMode mode) noexcept;

// Verifies the field reproduces the legacy formulas and stays bounded. Runs from
// MiaoDeskContentSelfTests, so it runs on every machine rather than only on Windows
// — the point being that this module is platform-independent by construction.
bool MiaoAnalyticParticleFieldSelfTest() noexcept;

// Evaluate one analytic emitter for one frame. Pure: no state is kept between
// calls, which is the whole difference from MiaoParticleRuntime's simulation.
//
// `designWidth` / `designHeight` are the scene's design space, matching the sprite
// geometry's own frame of reference. A zero or negative size yields no samples
// rather than a division by zero — an unsceneable frame should draw nothing, not
// NaN coordinates that a renderer would then reject.
std::vector<AnalyticParticleSample> EvaluateAnalyticParticleField(
    const ParticleEmitterDefinition& emitter,
    double designWidth,
    double designHeight,
    double timeSeconds,
    const AnalyticParticleLimits& limits = {});

} // namespace miaodesk::content
