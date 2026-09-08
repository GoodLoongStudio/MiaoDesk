#include "miaodesk/MiaoSceneFrameScheduler.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace miaodesk::content {
namespace {

constexpr std::wstring_view kFrameTimeInput = L"input://frame/time";

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool HasContinuousParticles(const SceneRuntimeDefinition* definition) noexcept {
    if (!definition) return false;
    for (const auto& emitter : definition->particleEmitters) {
        if (emitter.enabled && emitter.maxParticles > 0 && emitter.spawnRate > 0.0) return true;
    }
    return false;
}

} // namespace

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
    if (!state.loaded || !std::isfinite(timeSeconds)) return demand;

    demand.contentDirty = !state.paintReady || state.generation != lastRenderedGeneration;
    demand.continuousAnimation = runtime.NeedsContinuousAnimation(timeSeconds) ||
                                 HasContinuousParticles(runtime.Definition());

    // A Once track can cross its duration between two scheduler ticks. The
    // runtime does not mutate to the terminal keyframe until the host advances
    // frame time, so compare against the last frame clock as well. If the last
    // presented clock still required animation but the requested clock no
    // longer does, request exactly one terminal render. Draw/AdvanceAndEvaluate
    // then stores the new frame clock and the extra demand disappears.
    bool terminalFrame = false;
    if (!demand.continuousAnimation) {
        const auto* previousValue = runtime.GetInput(kFrameTimeInput);
        const auto* previousTime = previousValue ? std::get_if<double>(previousValue) : nullptr;
        if (previousTime && std::isfinite(*previousTime) && *previousTime < timeSeconds)
            terminalFrame = runtime.NeedsContinuousAnimation(*previousTime);
    }

    demand.render = demand.contentDirty || demand.continuousAnimation || terminalFrame;
    demand.intervalMs = demand.continuousAnimation ? IntervalForFps(animationFps) : 0u;
    return demand;
}

bool MiaoSceneFrameScheduler::AdvanceAndEvaluate(
    MiaoSceneRuntime& runtime,
    double timeSeconds,
    std::uint64_t lastRenderedGeneration,
    MiaoSceneFrameDemand* demand,
    std::uint32_t animationFps,
    std::wstring* error) {
    if (!demand) return Fail(error, L"Scene frame demand output is null.");
    *demand = {};
    if (!runtime.State().loaded) {
        if (error) error->clear();
        return true;
    }
    if (!std::isfinite(timeSeconds)) return Fail(error, L"Scene frame scheduler time must be finite.");

    // Advance first. A Once track may already be past its nominal duration at
    // this scheduler tick, but its terminal keyframe still has to mutate the
    // PropertyStore. That generation change then requests exactly one final
    // render before the runtime is allowed to become idle. Use the canonical
    // InputBus frame clock when the scene declares it so shader/runtime clocks
    // remain in sync with scheduler state.
    std::wstring runtimeError;
    if (MiaoSceneRuntimeModel::FindInput(*runtime.Definition(), kFrameTimeInput)) {
        if (!runtime.SetInput(kFrameTimeInput, timeSeconds, &runtimeError))
            return Fail(error, runtimeError.empty() ? L"Cannot advance Miao Scene frame-time input." : runtimeError);
    } else if (!runtime.AdvanceTimeline(timeSeconds, &runtimeError)) {
        return Fail(error, runtimeError.empty() ? L"Cannot advance Miao Scene animation timeline." : runtimeError);
    }

    *demand = Evaluate(runtime, timeSeconds, lastRenderedGeneration, animationFps);
    if (error) error->clear();
    return true;
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
    definition.inputs.push_back(InputChannelDefinition{std::wstring(kFrameTimeInput), PropertyType::Float, 0.0});
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
    std::uint64_t renderedGeneration = runtime.State().generation;
    if (!AdvanceAndEvaluate(runtime, 0.0, renderedGeneration, &demand, 60, &error)) return false;
    if (demand.render || demand.contentDirty || demand.continuousAnimation || demand.intervalMs != 0) return false;

    if (!runtime.SetInput(L"input://event/pulse", true, &error)) return false;
    if (!AdvanceAndEvaluate(runtime, 0.0, renderedGeneration, &demand, 60, &error)) return false;
    if (!demand.render || !demand.contentDirty || !demand.continuousAnimation || demand.intervalMs != 17) return false;

    // Simulate the host presenting the trigger frame.
    runtime.MarkPaintReady();
    renderedGeneration = runtime.State().generation;

    if (!AdvanceAndEvaluate(runtime, 0.49, renderedGeneration, &demand, 120, &error)) return false;
    if (!demand.render || !demand.contentDirty || !demand.continuousAnimation || demand.intervalMs != 9) return false;
    const PropertyAddress opacity{L"component://root/transform", L"opacity"};
    const auto* almostDone = runtime.GetProperty(opacity);
    if (!almostDone || !std::holds_alternative<double>(*almostDone) || std::get<double>(*almostDone) >= 1.0) return false;

    // Simulate the host presenting the penultimate frame. A pure pre-draw query
    // at t=0.75 must still request one terminal frame because the last stored
    // InputBus clock (0.49) was inside the Once animation.
    runtime.MarkPaintReady();
    renderedGeneration = runtime.State().generation;
    demand = Evaluate(runtime, 0.75, renderedGeneration, 60);
    if (!demand.render || demand.contentDirty || demand.continuousAnimation || demand.intervalMs != 0) return false;

    if (!AdvanceAndEvaluate(runtime, 0.75, renderedGeneration, &demand, 60, &error)) return false;
    if (!demand.render || !demand.contentDirty || demand.continuousAnimation || demand.intervalMs != 0) return false;
    const auto* finalValue = runtime.GetProperty(opacity);
    if (!finalValue || !std::holds_alternative<double>(*finalValue) || std::get<double>(*finalValue) != 1.0) return false;

    runtime.MarkPaintReady();
    renderedGeneration = runtime.State().generation;
    if (!AdvanceAndEvaluate(runtime, 0.75, renderedGeneration, &demand, 60, &error)) return false;
    if (demand.render || demand.contentDirty || demand.continuousAnimation || demand.intervalMs != 0) return false;

    SceneRuntimeDefinition particleDefinition;
    particleDefinition.scene.id = L"scene://frame-scheduler-particle-self-test";
    particleDefinition.scene.kind = ContentKind::Widget;
    particleDefinition.scene.rootNodeId = L"node://particle-root";
    SceneNodeDefinition particleRoot;
    particleRoot.id = L"node://particle-root";
    particleDefinition.scene.nodes.push_back(std::move(particleRoot));
    particleDefinition.profile = RuntimeProfile::Widget;
    ParticleEmitterDefinition emitter;
    emitter.id = L"particle://frame-scheduler-self-test";
    emitter.maxParticles = 8;
    emitter.spawnRate = 4.0;
    particleDefinition.particleEmitters.push_back(std::move(emitter));

    MiaoSceneRuntime particleScene;
    if (!particleScene.Initialize(std::move(particleDefinition), &error)) return false;
    particleScene.MarkPaintReady();
    const auto particleGeneration = particleScene.State().generation;
    demand = Evaluate(particleScene, 10.0, particleGeneration, 60);
    if (!demand.render || demand.contentDirty || !demand.continuousAnimation || demand.intervalMs != 17) return false;

    if (IntervalForFps(0) != 1000 || IntervalForFps(1000) != 5 || IntervalForFps(30) != 34) return false;
    return true;
}

} // namespace miaodesk::content
