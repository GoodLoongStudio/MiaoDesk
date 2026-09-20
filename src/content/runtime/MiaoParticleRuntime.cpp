#include "miaodesk/MiaoParticleRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace

bool MiaoParticleRuntime::Initialize(const SceneRuntimeDefinition& definition, std::wstring* error) {
    Reset();
    std::wstring validationError;
    if (!MiaoSceneRuntimeModel::Validate(definition, &validationError))
        return Fail(error, L"Cannot initialize particle runtime from invalid scene: " + validationError);

    std::size_t reserveCount = 0;
    emitters_.reserve(definition.particleEmitters.size());
    for (const auto& item : definition.particleEmitters) {
        EmitterState state;
        state.definition = item;
        state.randomState = item.seed == 0 ? 0x6d2b79f5u : item.seed;
        emitters_.push_back(std::move(state));
        reserveCount += item.maxParticles;
    }
    particles_.reserve(reserveCount);
    initialized_ = true;
    if (error) error->clear();
    return true;
}

bool MiaoParticleRuntime::Advance(double deltaSeconds, std::wstring* error) {
    if (!initialized_) return Fail(error, L"Particle runtime is not initialized.");
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
        return Fail(error, L"Particle simulation delta must be finite and non-negative.");

    const double step = std::min(deltaSeconds, kMaxSimulationStepSeconds);
    if (step == 0.0) {
        if (error) error->clear();
        return true;
    }

    for (auto& particle : particles_) {
        if (particle.emitterIndex >= emitters_.size())
            return Fail(error, L"Particle runtime contains an invalid emitter index.");
        const auto& emitter = emitters_[particle.emitterIndex].definition;
        particle.velocity.x += emitter.acceleration.x * step;
        particle.velocity.y += emitter.acceleration.y * step;
        particle.position.x += particle.velocity.x * step;
        particle.position.y += particle.velocity.y * step;
        particle.ageSeconds += step;
        const double normalizedAge = particle.lifetimeSeconds > 0.0
            ? std::clamp(particle.ageSeconds / particle.lifetimeSeconds, 0.0, 1.0)
            : 1.0;
        particle.size = Lerp(emitter.sizeStart, emitter.sizeEnd, normalizedAge);
        particle.color = LerpColor(emitter.colorStart, emitter.colorEnd, normalizedAge);
    }

    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(), [](const MiaoParticleState& particle) {
            return particle.ageSeconds >= particle.lifetimeSeconds;
        }),
        particles_.end());

    std::vector<std::uint32_t> liveByEmitter(emitters_.size(), 0);
    for (const auto& particle : particles_) {
        if (particle.emitterIndex < liveByEmitter.size()) ++liveByEmitter[particle.emitterIndex];
    }

    for (std::size_t index = 0; index < emitters_.size(); ++index) {
        auto& emitter = emitters_[index];
        if (!emitter.definition.enabled || emitter.definition.spawnRate <= 0.0) continue;

        emitter.spawnCarry += emitter.definition.spawnRate * step;
        const double integral = std::floor(emitter.spawnCarry);
        if (integral <= 0.0) continue;
        const auto requested = static_cast<std::uint64_t>(std::min(
            integral,
            static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
        emitter.spawnCarry -= static_cast<double>(requested);

        const auto alive = liveByEmitter[index];
        const auto capacity = emitter.definition.maxParticles > alive
            ? emitter.definition.maxParticles - alive
            : 0u;
        const auto toSpawn = static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, capacity));
        for (std::uint32_t count = 0; count < toSpawn; ++count) Spawn(index);
    }

    if (particles_.size() > MiaoSceneRuntimeModel::kMaxParticlesPerScene)
        return Fail(error, L"Particle runtime exceeded the scene particle budget.");

    if (error) error->clear();
    return true;
}

void MiaoParticleRuntime::Reset() noexcept {
    emitters_.clear();
    particles_.clear();
    initialized_ = false;
}

bool MiaoParticleRuntime::Initialized() const noexcept { return initialized_; }
std::size_t MiaoParticleRuntime::LiveCount() const noexcept { return particles_.size(); }
const std::vector<MiaoParticleState>& MiaoParticleRuntime::Particles() const noexcept { return particles_; }

std::uint32_t MiaoParticleRuntime::NextRandom(std::uint32_t* state) noexcept {
    if (!state) return 0;
    std::uint32_t value = *state;
    if (value == 0) value = 0x6d2b79f5u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

double MiaoParticleRuntime::Random01(std::uint32_t* state) noexcept {
    return static_cast<double>(NextRandom(state) & 0x00ffffffu) /
           static_cast<double>(0x01000000u);
}

double MiaoParticleRuntime::RandomSigned(std::uint32_t* state) noexcept {
    return Random01(state) * 2.0 - 1.0;
}

double MiaoParticleRuntime::Lerp(double from, double to, double t) noexcept {
    const double u = std::clamp(t, 0.0, 1.0);
    return from + (to - from) * u;
}

Color4 MiaoParticleRuntime::LerpColor(const Color4& from, const Color4& to, double t) noexcept {
    const double u = std::clamp(t, 0.0, 1.0);
    return Color4{
        Lerp(from.r, to.r, u),
        Lerp(from.g, to.g, u),
        Lerp(from.b, to.b, u),
        Lerp(from.a, to.a, u),
    };
}

void MiaoParticleRuntime::Spawn(std::size_t emitterIndex) {
    if (emitterIndex >= emitters_.size()) return;
    auto& state = emitters_[emitterIndex];
    const auto& emitter = state.definition;

    MiaoParticleState particle;
    particle.emitterIndex = static_cast<std::uint32_t>(emitterIndex);
    particle.position = Vec2{
        emitter.position.x + emitter.positionSpread.x * RandomSigned(&state.randomState),
        emitter.position.y + emitter.positionSpread.y * RandomSigned(&state.randomState),
    };
    particle.velocity = Vec2{
        emitter.velocity.x + emitter.velocitySpread.x * RandomSigned(&state.randomState),
        emitter.velocity.y + emitter.velocitySpread.y * RandomSigned(&state.randomState),
    };
    particle.lifetimeSeconds = Lerp(
        emitter.lifetimeMinSeconds,
        emitter.lifetimeMaxSeconds,
        Random01(&state.randomState));
    particle.size = emitter.sizeStart;
    particle.color = emitter.colorStart;
    particles_.push_back(std::move(particle));
}

bool MiaoParticleRuntime::SelfTest() {
    SceneRuntimeDefinition definition;
    definition.scene.id = L"scene://particle-runtime-self-test";
    definition.scene.kind = ContentKind::Wallpaper;
    definition.scene.rootNodeId = L"node://root";
    SceneNodeDefinition root;
    root.id = L"node://root";
    definition.scene.nodes.push_back(std::move(root));
    definition.profile = RuntimeProfile::Wallpaper;

    ParticleEmitterDefinition emitter;
    emitter.id = L"particle://self-test";
    emitter.maxParticles = 4;
    emitter.spawnRate = 4.0;
    emitter.lifetimeMinSeconds = 1.0;
    emitter.lifetimeMaxSeconds = 1.0;
    emitter.position = Vec2{0.0, 0.0};
    emitter.positionSpread = Vec2{0.0, 0.0};
    emitter.velocity = Vec2{10.0, 0.0};
    emitter.velocitySpread = Vec2{0.0, 0.0};
    emitter.acceleration = Vec2{0.0, 0.0};
    emitter.sizeStart = 4.0;
    emitter.sizeEnd = 2.0;
    emitter.colorStart = Color4{1.0, 1.0, 1.0, 1.0};
    emitter.colorEnd = Color4{1.0, 1.0, 1.0, 0.0};
    emitter.seed = 42;
    definition.particleEmitters.push_back(emitter);

    MiaoParticleRuntime runtime;
    std::wstring error;
    if (!runtime.Initialize(definition, &error) || !runtime.Initialized() || runtime.LiveCount() != 0) return false;
    if (!runtime.Advance(0.25, &error) || runtime.LiveCount() != 1) return false;
    if (std::abs(runtime.Particles().front().position.x) > 0.000001) return false;

    if (!runtime.Advance(0.25, &error) || runtime.LiveCount() != 2) return false;
    const auto& first = runtime.Particles().front();
    if (std::abs(first.position.x - 2.5) > 0.000001) return false;
    if (std::abs(first.size - 3.5) > 0.000001) return false;
    if (std::abs(first.color.a - 0.75) > 0.000001) return false;

    for (int i = 0; i < 12; ++i) {
        if (!runtime.Advance(0.25, &error)) return false;
        if (runtime.LiveCount() > emitter.maxParticles) return false;
    }
    if (runtime.Advance(-0.1, &error)) return false;

    runtime.Reset();
    return !runtime.Initialized() && runtime.LiveCount() == 0;
}

} // namespace miaodesk::content
