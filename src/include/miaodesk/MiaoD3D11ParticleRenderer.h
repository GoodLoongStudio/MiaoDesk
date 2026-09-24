#pragma once

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "miaodesk/MiaoAnalyticParticleField.h"
#include "miaodesk/MiaoParticleRuntime.h"

namespace miaodesk::content {

// M4 v1 particle GPU backend. Simulation stays in MiaoParticleRuntime while
// this class owns only GPU upload/compositing resources. The pass copies the
// incoming scene color into a distinct RenderGraph target and then overlays a
// bounded set of instanced soft particles.
class MiaoD3D11ParticleRenderer {
public:
    // One analytic dot is one ellipse instance; a comet head additionally expands to
    // two thin rectangle instances for its cross. Keep that expansion budget separate
    // from the simulated-particle pool.
    static constexpr std::uint32_t kMaxGpuInstances =
        MiaoSceneRuntimeModel::kMaxParticlesPerScene +
        MiaoSceneRuntimeModel::kMaxAnalyticSamplesPerScene * 3u;

    MiaoD3D11ParticleRenderer();
    ~MiaoD3D11ParticleRenderer();

    MiaoD3D11ParticleRenderer(const MiaoD3D11ParticleRenderer&) = delete;
    MiaoD3D11ParticleRenderer& operator=(const MiaoD3D11ParticleRenderer&) = delete;

    bool Initialize(
        ID3D11Device* device,
        std::uint32_t maxParticles = MiaoSceneRuntimeModel::kMaxParticlesPerScene,
        std::wstring* error = nullptr);

    bool Draw(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* output,
        ID3D11ShaderResourceView* sceneColor,
        std::uint32_t width,
        std::uint32_t height,
        const MiaoParticleRuntime& particles,
        std::span<const AnalyticParticleSample> analyticSamples = {},
        std::wstring* error = nullptr);

    void Reset() noexcept;
    bool Initialized() const noexcept;
    std::uint32_t Capacity() const noexcept;

    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::content
