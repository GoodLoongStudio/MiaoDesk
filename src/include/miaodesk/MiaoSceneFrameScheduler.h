#pragma once

#include <cstdint>

#include "miaodesk/MiaoSceneRuntime.h"

namespace miaodesk::content {

struct MiaoSceneFrameDemand {
    bool render{};
    bool contentDirty{};
    bool continuousAnimation{};
    std::uint32_t intervalMs{};
};

class MiaoSceneFrameScheduler {
public:
    static constexpr std::uint32_t kDefaultAnimationFps = 60;
    static constexpr std::uint32_t kMinimumAnimationFps = 1;
    static constexpr std::uint32_t kMaximumAnimationFps = 240;

    static MiaoSceneFrameDemand Evaluate(
        const MiaoSceneRuntime& runtime,
        double timeSeconds,
        std::uint64_t lastRenderedGeneration,
        std::uint32_t animationFps = kDefaultAnimationFps) noexcept;

    static std::uint32_t IntervalForFps(std::uint32_t fps) noexcept;
    static bool SelfTest();
};

} // namespace miaodesk::content
