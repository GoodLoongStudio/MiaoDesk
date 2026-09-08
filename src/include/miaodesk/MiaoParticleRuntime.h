#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

struct MiaoParticleState {
    std::uint32_t emitterIndex{};
    Vec2 position{};
    Vec2 velocity{};
    double ageSeconds{};
    double lifetimeSeconds{1.0};
    double size{};
    Color4 color{1.0, 1.0, 1.0, 1.0};
};

// M4 CPU simulation baseline. Definitions stay renderer-independent and the
// live state vector is intentionally bounded so the next D3D11 slice can upload
// it directly into an instance buffer without introducing a second particle model.
class MiaoParticleRuntime {
public:
    static constexpr double kMaxSimulationStepSeconds = 0.25;

    bool Initialize(const SceneRuntimeDefinition& definition, std::wstring* error = nullptr);
    bool Advance(double deltaSeconds, std::wstring* error = nullptr);
    void Reset() noexcept;

    bool Initialized() const noexcept;
    std::size_t LiveCount() const noexcept;
    const std::vector<MiaoParticleState>& Particles() const noexcept;

    static bool SelfTest();

private:
    struct EmitterState {
        ParticleEmitterDefinition definition;
        double spawnCarry{};
        std::uint32_t randomState{1};
    };

    static std::uint32_t NextRandom(std::uint32_t* state) noexcept;
    static double Random01(std::uint32_t* state) noexcept;
    static double RandomSigned(std::uint32_t* state) noexcept;
    static double Lerp(double from, double to, double t) noexcept;
    static Color4 LerpColor(const Color4& from, const Color4& to, double t) noexcept;

    void Spawn(std::size_t emitterIndex);

    std::vector<EmitterState> emitters_;
    std::vector<MiaoParticleState> particles_;
    bool initialized_{};
};

} // namespace miaodesk::content
