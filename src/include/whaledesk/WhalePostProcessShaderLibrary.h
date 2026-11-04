#pragma once

#include <string>
#include <string_view>

#include "whaledesk/WhaleSceneRuntimeModel.h"

namespace whaledesk::content {

struct alignas(16) WhalePostProcessConstants {
    float amount{1.0f};
    float radius{0.75f};
    float softness{0.25f};
    float time{};
    float texelSize[2]{};
    float reserved[2]{};
};

static_assert(sizeof(WhalePostProcessConstants) == 32);

// Source generator for engine-owned post-process effects. These shaders use
// the same fullscreen input texture contract as programmable materials, while
// effect parameters live in a dedicated b3 constant buffer.
class WhalePostProcessShaderLibrary {
public:
    static constexpr unsigned kPostProcessCBufferRegister = 3;
    static constexpr std::string_view kEntryPoint = "WhalePostProcessMain";

    static std::string PixelShaderSource(PostProcessEffectKind effect);
    static std::string_view EffectKey(PostProcessEffectKind effect) noexcept;
    static bool ParseEffectKey(std::string_view key, PostProcessEffectKind* effect) noexcept;
    static WhalePostProcessConstants MakeConstants(
        const PostProcessDefinition& definition,
        unsigned width,
        unsigned height,
        float timeSeconds) noexcept;

    static bool SelfTest();
};

} // namespace whaledesk::content
