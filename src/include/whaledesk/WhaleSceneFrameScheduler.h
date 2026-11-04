#pragma once

#include <cstdint>
#include <string>

#include "whaledesk/WhaleSceneRuntime.h"

namespace whaledesk::content {

struct WhaleSceneFrameDemand {
    bool render{};
    bool contentDirty{};
    bool continuousAnimation{};
    std::uint32_t intervalMs{};
};

class WhaleSceneFrameScheduler {
public:
    static constexpr std::uint32_t kDefaultAnimationFps = 60;
    static constexpr std::uint32_t kMinimumAnimationFps = 1;
    static constexpr std::uint32_t kMaximumAnimationFps = 240;

    // Snapshot-only demand query. Callers that schedule a future frame should
    // prefer AdvanceAndEvaluate so a Once track is first evaluated through its
    // terminal keyframe before the scheduler decides that it can sleep.
    static WhaleSceneFrameDemand Evaluate(
        const WhaleSceneRuntime& runtime,
        double timeSeconds,
        std::uint64_t lastRenderedGeneration,
        std::uint32_t animationFps = kDefaultAnimationFps) noexcept;

    // Advances animation state to the requested clock before calculating frame
    // demand. This is the canonical scheduler entry point for Wallpaper/Widget
    // hosts and guarantees that a Once animation cannot skip its final frame.
    static bool AdvanceAndEvaluate(
        WhaleSceneRuntime& runtime,
        double timeSeconds,
        std::uint64_t lastRenderedGeneration,
        WhaleSceneFrameDemand* demand,
        std::uint32_t animationFps = kDefaultAnimationFps,
        std::wstring* error = nullptr);

    static std::uint32_t IntervalForFps(std::uint32_t fps) noexcept;
    static bool SelfTest();
};

} // namespace whaledesk::content
