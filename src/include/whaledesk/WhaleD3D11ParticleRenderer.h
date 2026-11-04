#pragma once

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "whaledesk/WhaleAnalyticParticleField.h"
#include "whaledesk/WhaleParticleRuntime.h"

namespace whaledesk::content {

// M4 v1 particle GPU backend. Simulation stays in WhaleParticleRuntime while
// this class owns only GPU upload/compositing resources. The pass copies the
// incoming scene color into a distinct RenderGraph target and then overlays a
// bounded set of instanced soft particles.
class WhaleD3D11ParticleRenderer {
public:
    // One analytic dot is one ellipse instance; a comet head additionally expands to
    // two thin rectangle instances for its cross. Keep that expansion budget separate
    // from the simulated-particle pool.
    static constexpr std::uint32_t kMaxGpuInstances =
        WhaleSceneRuntimeModel::kMaxParticlesPerScene +
        WhaleSceneRuntimeModel::kMaxAnalyticSamplesPerScene * 3u;

    WhaleD3D11ParticleRenderer();
    ~WhaleD3D11ParticleRenderer();

    WhaleD3D11ParticleRenderer(const WhaleD3D11ParticleRenderer&) = delete;
    WhaleD3D11ParticleRenderer& operator=(const WhaleD3D11ParticleRenderer&) = delete;

    bool Initialize(
        ID3D11Device* device,
        std::uint32_t maxParticles = WhaleSceneRuntimeModel::kMaxParticlesPerScene,
        std::wstring* error = nullptr);

    bool Draw(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* output,
        ID3D11ShaderResourceView* sceneColor,
        std::uint32_t width,
        std::uint32_t height,
        const WhaleParticleRuntime& particles,
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

} // namespace whaledesk::content
