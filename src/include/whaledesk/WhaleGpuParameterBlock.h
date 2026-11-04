#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "whaledesk/WhaleSceneRuntime.h"

namespace whaledesk::content {

struct alignas(16) WhaleGpuParameterBlock {
    static constexpr std::size_t kMaxSlots = 16;
    std::array<std::array<float, 4>, kMaxSlots> slots{};
};

class WhaleGpuParameterPacker {
public:
    // Shader Contract v1: parameter order in parameters.json is ABI-significant.
    // Each supported parameter consumes exactly one float4 slot.
    static bool Pack(
        const SceneRuntimeDefinition& definition,
        const WhaleSceneRuntime& runtime,
        WhaleGpuParameterBlock* block,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace whaledesk::content
