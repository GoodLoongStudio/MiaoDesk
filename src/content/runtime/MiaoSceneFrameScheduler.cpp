#include "miaodesk/MiaoSceneFrameScheduler.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace miaodesk::content {

std::uint32_t MiaoSceneFrameScheduler::IntervalForFps(std::uint32_t fps) noexcept {
    fps = std::clamp(fps, kMinimumAnimationFps, kMaximumAnimationFps);
    return std::max<std::uint32_t>(1u, (1000u + fps - 1u) / fps);
}

MiaoSceneFrameDemand MiaoSceneFrameScheduler::Evaluate(
    const MiaoSceneRuntime& runtime,
    double timeSeconds,
    std::uint64_t lastRenderedGeneration,
    std::uint32_t animationFps) noexcept {
    MiaoSceneFrameDemand demand;
    const auto& state = runtime.State();
    if (!state.loaded) return demand;

    demand.contentDirty = !state.paintReady || state.generation != lastRenderedGeneration;
    demand.continuousAnimation = std::isfinite(timeSeconds) && runtime.NeedsContinuousAnimation(timeSeconds);
    demand.render = demand.contentDirty || demand.continuousAnimation;
    demand.intervalMs = demand.continuousAnimation ? IntervalForFps(animationFps) : 0u;
    return demand;
}

bool MiaoSceneFrameScheduler::SelfTest() {
    SceneRuntimeDefinition definition;
    definition.scene.id = L"scene://frame-scheduler-self-test";
    definition.scene.kind = ContentKind::Widget;
    definition.scene.rootNodeId = L"node://root";

    SceneNodeDefinition root;
    root.id = L"node://root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform",
        ComponentKind::Transform,
        {PropertyDefinition{L"opacity", PropertyType::Float, 0.2}},
    });
    definition.scene.nodes.push_back(std::move(root));
    definition.profile = RuntimeProfile::Widget;
    definition.inputs.push_back(InputChannelDefinition{L"input://frame/time", PropertyType::Float, 0.0});
    definition.inputs.push_back(InputChannelDefinition{L"input://event/pulse", PropertyType::Bool, false});
    definition.animations.push_back(AnimationTrackDefinition{
        L"animation://event-pulse",
        PropertyAddress{L"component://root/transform", L"opacity"},
        true,
        AnimationLoopMode::Once,
        0.5,
        {
            AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::EaseOut},
            AnimationKeyframeDefinition{0.5, 1.0, AnimationEasing::Linear},
        },
        AnimationTriggerMode::InputRisingEdge,
        L"input://event/pulse",
    });

    MiaoSceneRuntime runtime;
    std::wstring error;
    if (!runtime.Initialize(std::move(definition), &error)) return false;

    auto demand = Evaluate(runtime, 0.0, runtime.State().generation, 60);
    if (!demand.render || !demand.contentDirty || demand.continuousAnimation || demand.intervalMs != 0) return false;

    runtime.MarkPaintReady();
    const auto renderedGeneration = runtime.State().generation;
    demand = Evaluate(runtime, 0.0, renderedGeneration, 60);
    if (demand.render || demand.contentDirty || demand.continuousAnimation || demand.intervalMs != 0) return false;

    if (!runtime.SetInput(L"input://event/pulse", true, &error)) return false;
    demand = Evaluate(runtime, 0.0, renderedGeneration, 60);
    if (!demand.render || !demand.continuousAnimation || demand.intervalMs != 17) return false;

    if (!runtime.SetInput(L"input://frame/time", 0.25, &error)) return false;
    const auto activeGeneration = runtime.State().generation;
    demand = Evaluate(runtime, 0.25, activeGeneration, 120);
    if (!demand.render || demand.contentDirty || !demand.continuousAnimation || demand.intervalMs != 9) return false;

    runtime.MarkPaintReady();
    demand = Evaluate(runtime, 0.75, runtime.State().generation, 60);
    if (demand.render || demand.continuousAnimation || demand.intervalMs != 0) return false;

    if (IntervalForFps(0) != 1000 || IntervalForFps(1000) != 5 || IntervalForFps(30) != 34) return false;
    return true;
}

} // namespace miaodesk::content
