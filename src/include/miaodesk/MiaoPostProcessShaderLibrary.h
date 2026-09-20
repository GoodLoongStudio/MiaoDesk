#pragma once

#include <string>
#include <string_view>

#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

struct alignas(16) MiaoPostProcessConstants {
    float amount{1.0f};
    float radius{0.75f};
    float softness{0.25f};
    float time{};
    float texelSize[2]{};
    float reserved[2]{};
};

static_assert(sizeof(MiaoPostProcessConstants) == 32);

// Source generator for engine-owned post-process effects. These shaders use
// the same fullscreen input texture contract as programmable materials, while
// effect parameters live in a dedicated b3 constant buffer.
class MiaoPostProcessShaderLibrary {
public:
    static constexpr unsigned kPostProcessCBufferRegister = 3;
    static constexpr std::string_view kEntryPoint = "MiaoPostProcessMain";

    static std::string PixelShaderSource(PostProcessEffectKind effect);
    static std::string_view EffectKey(PostProcessEffectKind effect) noexcept;
    static bool ParseEffectKey(std::string_view key, PostProcessEffectKind* effect) noexcept;
    static MiaoPostProcessConstants MakeConstants(
        const PostProcessDefinition& definition,
        unsigned width,
        unsigned height,
        float timeSeconds) noexcept;

    static bool SelfTest();
};

} // namespace miaodesk::content
