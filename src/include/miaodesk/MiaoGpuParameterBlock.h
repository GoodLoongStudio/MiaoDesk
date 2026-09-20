#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "miaodesk/MiaoSceneRuntime.h"

namespace miaodesk::content {

struct alignas(16) MiaoGpuParameterBlock {
    static constexpr std::size_t kMaxSlots = 16;
    std::array<std::array<float, 4>, kMaxSlots> slots{};
};

class MiaoGpuParameterPacker {
public:
    // Shader Contract v1: parameter order in parameters.json is ABI-significant.
    // Each supported parameter consumes exactly one float4 slot.
    static bool Pack(
        const SceneRuntimeDefinition& definition,
        const MiaoSceneRuntime& runtime,
        MiaoGpuParameterBlock* block,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace miaodesk::content
