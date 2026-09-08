#include "miaodesk/MiaoSceneRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool ValueMatchesType(PropertyType type, const PropertyValue& value) noexcept {
    switch (type) {
    case PropertyType::Bool: return std::holds_alternative<bool>(value);
    case PropertyType::Int: return std::holds_alternative<std::int64_t>(value);
    case PropertyType::Float: return std::holds_alternative<double>(value);
    case PropertyType::String: return std::holds_alternative<std::wstring>(value);
    case PropertyType::Vec2: return std::holds_alternative<Vec2>(value);
    case PropertyType::Vec3: return std::holds_alternative<Vec3>(value);
    case PropertyType::Vec4: return std::holds_alternative<Vec4>(value);
    case PropertyType::Color: return std::holds_alternative<Color4>(value);
    case PropertyType::AssetReference: return std::holds_alternative<AssetReference>(value);
    }
    return false;
}

bool PropertyValuesEqual(const PropertyValue& left, const PropertyValue& right) noexcept {
    if (left.index() != right.index()) return false;
    if (const auto* a = std::get_if<bool>(&left)) return *a == std::get<bool>(right);
    if (const auto* a = std::get_if<std::int64_t>(&left)) return *a == std::get<std::int64_t>(right);
    if (const auto* a = std::get_if<double>(&left)) return *a == std::get<double>(right);
    if (const auto* a = std::get_if<std::wstring>(&left)) return *a == std::get<std::wstring>(right);
    if (const auto* a = std::get_if<Vec2>(&left)) {
        const auto& b = std::get<Vec2>(right);
        return a->x == b.x && a->y == b.y;
    }
    if (const auto* a = std::get_if<Vec3>(&left)) {
        const auto& b = std::get<Vec3>(right);
        return a->x == b.x && a->y == b.y && a->z == b.z;
    }
    if (const auto* a = std::get_if<Vec4>(&left)) {
        const auto& b = std::get<Vec4>(right);
        return a->x == b.x && a->y == b.y && a->z == b.z && a->w == b.w;
    }
    if (const auto* a = std::get_if<Color4>(&left)) {
        const auto& b = std::get<Color4>(right);
        return a->r == b.r && a->g == b.g && a->b == b.b && a->a == b.a;
    }
    if (const auto* a = std::get_if<AssetReference>(&left)) return a->id == std::get<AssetReference>(right).id;
    return false;
}

double ApplyEasing(AnimationEasing easing, double value) noexcept {
    const double t = std::clamp(value, 0.0, 1.0);
    switch (easing) {
    case AnimationEasing::Linear:
        return t;
    case AnimationEasing::EaseIn:
        return t * t;
    case AnimationEasing::EaseOut: {
        const double inverse = 1.0 - t;
        return 1.0 - inverse * inverse;
    }
    case AnimationEasing::EaseInOut:
        if (t < 0.5) return 2.0 * t * t;
        return 1.0 - ((-2.0 * t + 2.0) * (-2.0 * t + 2.0)) / 2.0;
    }
    return t;
}

double AnimationLocalTime(const AnimationTrackDefinition& animation, double timeSeconds) noexcept {
    const double time = std::max(0.0, timeSeconds);
    const double duration = animation.durationSeconds;
    switch (animation.loopMode) {
    case AnimationLoopMode::Once:
        return std::clamp(time, 0.0, duration);
    case AnimationLoopMode::Loop: {
        const double wrapped = std::fmod(time, duration);
        return wrapped < 0.0 ? wrapped + duration : wrapped;
    }
    case AnimationLoopMode::PingPong: {
        const double period = duration * 2.0;
        double wrapped = std::fmod(time, period);
        if (wrapped < 0.0) wrapped += period;
        return wrapped <= duration ? wrapped : period - wrapped;
    }
    }
    return time;
}

bool InterpolateValue(
    const PropertyValue& from,
    const PropertyValue& to,
    double t,
    PropertyValue* output) noexcept {
    if (!output || from.index() != to.index()) return false;
    const double u = std::clamp(t, 0.0, 1.0);
    if (const auto* a = std::get_if<double>(&from)) {
        const auto b = std::get<double>(to);
        *output = *a + (b - *a) * u;
        return true;
    }
    if (const auto* a = std::get_if<std::int64_t>(&from)) {
        const auto b = std::get<std::int64_t>(to);
        const long double value = static_cast<long double>(*a) +
            (static_cast<long double>(b) - static_cast<long double>(*a)) * static_cast<long double>(u);
        *output = static_cast<std::int64_t>(std::llround(value));
        return true;
    }
    if (const auto* a = std::get_if<Vec2>(&from)) {
        const auto& b = std::get<Vec2>(to);
        *output = Vec2{a->x + (b.x - a->x) * u, a->y + (b.y - a->y) * u};
        return true;
    }
    if (const auto* a = std::get_if<Vec3>(&from)) {
        const auto& b = std::get<Vec3>(to);
        *output = Vec3{a->x + (b.x - a->x) * u, a->y + (b.y - a->y) * u, a->z + (b.z - a->z) * u};
        return true;
    }
    if (const auto* a = std::get_if<Vec4>(&from)) {
        const auto& b = std::get<Vec4>(to);
        *output = Vec4{a->x + (b.x - a->x) * u, a->y + (b.y - a->y) * u,
                       a->z + (b.z - a->z) * u, a->w + (b.w - a->w) * u};
        return true;
    }
    if (const auto* a = std::get_if<Color4>(&from)) {
        const auto& b = std::get<Color4>(to);
        *output = Color4{a->r + (b.r - a->r) * u, a->g + (b.g - a->g) * u,
                         a->b + (b.b - a->b) * u, a->a + (b.a - a->a) * u};
        return true;
    }
    return false;
}

} // namespace

bool MiaoSceneRuntime::Initialize(SceneRuntimeDefinition definition, std::wstring* error) {
    Reset();
    std::wstring validationError;
    if (!MiaoSceneRuntimeModel::Validate(definition, &validationError)) {
        state_.error = L"Scene runtime definition is invalid: " + validationError;
        return Fail(error, state_.error);
    }

    definition_ = std::move(definition);
    hasDefinition_ = true;
    if (!properties_.Initialize(definition_.scene, &validationError)) {
        state_.error = validationError;
        hasDefinition_ = false;
        return Fail(error, state_.error);
    }

    for (const auto& parameter : definition_.parameters)
        parameterValues_.emplace(parameter.id, parameter.defaultValue);
    for (const auto& input : definition_.inputs)
        inputValues_.emplace(input.id, input.defaultValue);
    for (const auto& animation : definition_.animations)
        animationPlayback_.emplace(animation.id, AnimationPlaybackState{});

    for (const auto& binding : definition_.bindings) {
        const PropertyValue* source = nullptr;
        if (binding.sourceKind == BindingSourceKind::Parameter) {
            const auto it = parameterValues_.find(binding.sourceId);
            if (it != parameterValues_.end()) source = &it->second;
        } else {
            const auto it = inputValues_.find(binding.sourceId);
            if (it != inputValues_.end()) source = &it->second;
        }
        if (!source || !ApplyBinding(binding, *source, &validationError)) {
            state_.error = validationError.empty() ? L"Cannot apply initial scene binding." : validationError;
            hasDefinition_ = false;
            return Fail(error, state_.error);
        }
    }

    state_.loaded = true;
    state_.paintReady = false;
    state_.error.clear();
    if (error) error->clear();
    return true;
}

bool MiaoSceneRuntime::SetParameter(std::wstring_view id, PropertyValue value, std::wstring* error) {
    if (!hasDefinition_ || !state_.loaded) return Fail(error, L"Scene runtime is not initialized.");
    const auto* definition = MiaoSceneRuntimeModel::FindParameter(definition_, id);
    if (!definition) return Fail(error, L"Unknown scene parameter: " + std::wstring(id));
    if (!ValueMatchesType(definition->type, value)) return Fail(error, L"Scene parameter type mismatch: " + std::wstring(id));
    if (!ApplyBindingsForSource(BindingSourceKind::Parameter, id, value, error)) return false;
    parameterValues_[std::wstring(id)] = std::move(value);
    if (error) error->clear();
    return true;
}

bool MiaoSceneRuntime::SetInput(std::wstring_view id, PropertyValue value, std::wstring* error) {
    if (!hasDefinition_ || !state_.loaded) return Fail(error, L"Scene runtime is not initialized.");
    const auto* definition = MiaoSceneRuntimeModel::FindInput(definition_, id);
    if (!definition) return Fail(error, L"Unknown scene input: " + std::wstring(id));
    if (!ValueMatchesType(definition->type, value)) return Fail(error, L"Scene input type mismatch: " + std::wstring(id));

    PropertyValue previousCopy;
    const PropertyValue* previous = nullptr;
    const auto key = std::wstring(id);
    if (const auto it = inputValues_.find(key); it != inputValues_.end()) {
        previousCopy = it->second;
        previous = &previousCopy;
    }

    if (id == L"input://frame/time") {
        const auto* time = std::get_if<double>(&value);
        if (!time) return Fail(error, L"input://frame/time must be a float input.");
        if (!std::isfinite(*time)) return Fail(error, L"input://frame/time must be finite.");
        timelineTime_ = *time;
    }

    if (!ApplyBindingsForSource(BindingSourceKind::Input, id, value, error)) return false;
    if (!TriggerAnimationsForInput(id, previous, value, error)) return false;
    inputValues_[key] = value;

    // Every input event is evaluated against the latest known frame time so an
    // event-triggered track applies its first keyframe immediately. Frame time
    // itself then advances all active tracks normally.
    if (!AdvanceTimeline(timelineTime_, error)) return false;
    if (error) error->clear();
    return true;
}

bool MiaoSceneRuntime::AdvanceTimeline(double timeSeconds, std::wstring* error) {
    if (!hasDefinition_ || !state_.loaded) return Fail(error, L"Scene runtime is not initialized.");
    if (!std::isfinite(timeSeconds)) return Fail(error, L"Animation timeline time must be finite.");
    timelineTime_ = timeSeconds;

    for (const auto& animation : definition_.animations) {
        if (!animation.enabled) continue;
        double localTime = timeSeconds;
        if (animation.triggerMode != AnimationTriggerMode::Timeline) {
            const auto playback = animationPlayback_.find(animation.id);
            if (playback == animationPlayback_.end() || !playback->second.triggered) continue;
            localTime = std::max(0.0, timeSeconds - playback->second.startTime);
        }
        if (!ApplyAnimation(animation, localTime, error)) return false;
    }
    if (error) error->clear();
    return true;
}

const PropertyValue* MiaoSceneRuntime::GetParameter(std::wstring_view id) const noexcept {
    const auto it = parameterValues_.find(std::wstring(id));
    return it == parameterValues_.end() ? nullptr : &it->second;
}

const PropertyValue* MiaoSceneRuntime::GetInput(std::wstring_view id) const noexcept {
    const auto it = inputValues_.find(std::wstring(id));
    return it == inputValues_.end() ? nullptr : &it->second;
}

const PropertyValue* MiaoSceneRuntime::GetProperty(const PropertyAddress& address) const noexcept {
    return properties_.Get(address);
}

bool MiaoSceneRuntime::NeedsContinuousAnimation(double timeSeconds) const noexcept {
    if (!hasDefinition_ || !state_.loaded || !std::isfinite(timeSeconds)) return false;
    for (const auto& animation : definition_.animations) {
        if (!animation.enabled) continue;
        if (animation.triggerMode == AnimationTriggerMode::Timeline) {
            if (animation.loopMode != AnimationLoopMode::Once) return true;
            if (std::max(0.0, timeSeconds) <= animation.durationSeconds) return true;
            continue;
        }

        const auto playback = animationPlayback_.find(animation.id);
        if (playback == animationPlayback_.end() || !playback->second.triggered) continue;
        if (animation.loopMode != AnimationLoopMode::Once) return true;
        const double elapsed = std::max(0.0, timeSeconds - playback->second.startTime);
        if (elapsed <= animation.durationSeconds) return true;
    }
    return false;
}

std::vector<PropertyAddress> MiaoSceneRuntime::ConsumeDirtyProperties() {
    auto result = std::move(dirtyProperties_);
    dirtyProperties_.clear();
    return result;
}

const SceneRuntimeDefinition* MiaoSceneRuntime::Definition() const noexcept {
    return hasDefinition_ ? &definition_ : nullptr;
}

const MiaoSceneRuntimeState& MiaoSceneRuntime::State() const noexcept {
    return state_;
}

void MiaoSceneRuntime::MarkPaintReady(bool value) noexcept {
    state_.paintReady = value && state_.loaded;
}

void MiaoSceneRuntime::Reset() noexcept {
    definition_ = {};
    hasDefinition_ = false;
    properties_.Clear();
    parameterValues_.clear();
    inputValues_.clear();
    animationPlayback_.clear();
    dirtyProperties_.clear();
    timelineTime_ = 0.0;
    state_ = {};
}

bool MiaoSceneRuntime::ApplyBinding(
    const PropertyBindingDefinition& binding,
    const PropertyValue& source,
    std::wstring* error) {
    PropertyValue output = source;
    if (const auto* value = std::get_if<double>(&source)) {
        const double transformed = *value * binding.scale + binding.offset;
        if (!std::isfinite(transformed)) return Fail(error, L"Binding produced a non-finite float: " + binding.id);
        output = transformed;
    } else if (const auto* value = std::get_if<std::int64_t>(&source)) {
        const long double transformed = static_cast<long double>(*value) * static_cast<long double>(binding.scale) +
                                        static_cast<long double>(binding.offset);
        if (!std::isfinite(static_cast<double>(transformed)) ||
            transformed < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
            transformed > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
            return Fail(error, L"Binding produced an out-of-range integer: " + binding.id);
        output = static_cast<std::int64_t>(std::llround(transformed));
    }

    const auto* current = properties_.Get(binding.target);
    if (current && PropertyValuesEqual(*current, output)) {
        if (error) error->clear();
        return true;
    }
    if (!properties_.Set(binding.target, std::move(output), error)) return false;
    MarkDirty(binding.target);
    return true;
}

bool MiaoSceneRuntime::ApplyBindingsForSource(
    BindingSourceKind kind,
    std::wstring_view sourceId,
    const PropertyValue& value,
    std::wstring* error) {
    for (const auto& binding : definition_.bindings) {
        if (binding.sourceKind != kind || binding.sourceId != sourceId) continue;
        if (!ApplyBinding(binding, value, error)) return false;
    }
    if (error) error->clear();
    return true;
}

bool MiaoSceneRuntime::ApplyAnimation(
    const AnimationTrackDefinition& animation,
    double timeSeconds,
    std::wstring* error) {
    if (animation.keyframes.empty()) return Fail(error, L"Animation has no keyframes: " + animation.id);
    const double localTime = AnimationLocalTime(animation, timeSeconds);

    PropertyValue value = animation.keyframes.front().value;
    if (localTime >= animation.keyframes.back().timeSeconds) {
        value = animation.keyframes.back().value;
    } else if (localTime > animation.keyframes.front().timeSeconds) {
        for (std::size_t i = 0; i + 1 < animation.keyframes.size(); ++i) {
            const auto& from = animation.keyframes[i];
            const auto& to = animation.keyframes[i + 1];
            if (localTime > to.timeSeconds) continue;
            const double span = to.timeSeconds - from.timeSeconds;
            if (span <= 0.0) return Fail(error, L"Animation keyframe span is invalid: " + animation.id);
            const double normalized = (localTime - from.timeSeconds) / span;
            const double eased = ApplyEasing(from.easing, normalized);
            if (!InterpolateValue(from.value, to.value, eased, &value))
                return Fail(error, L"Animation interpolation type is unsupported: " + animation.id);
            break;
        }
    }

    const auto* current = properties_.Get(animation.target);
    if (!current) return Fail(error, L"Animation target property is unavailable: " + animation.id);
    if (PropertyValuesEqual(*current, value)) {
        if (error) error->clear();
        return true;
    }
    if (!properties_.Set(animation.target, std::move(value), error)) return false;
    MarkDirty(animation.target);
    return true;
}

bool MiaoSceneRuntime::TriggerAnimationsForInput(
    std::wstring_view inputId,
    const PropertyValue* previous,
    const PropertyValue& current,
    std::wstring* error) {
    for (const auto& animation : definition_.animations) {
        if (!animation.enabled || animation.triggerMode == AnimationTriggerMode::Timeline ||
            animation.triggerInputId != inputId) continue;

        bool shouldTrigger = false;
        if (animation.triggerMode == AnimationTriggerMode::InputChange) {
            shouldTrigger = !previous || !PropertyValuesEqual(*previous, current);
        } else if (animation.triggerMode == AnimationTriggerMode::InputRisingEdge) {
            const auto* next = std::get_if<bool>(&current);
            const auto* before = previous ? std::get_if<bool>(previous) : nullptr;
            shouldTrigger = next && *next && (!before || !*before);
        }
        if (!shouldTrigger) continue;

        auto& playback = animationPlayback_[animation.id];
        playback.triggered = true;
        playback.startTime = timelineTime_;
        if (!ApplyAnimation(animation, 0.0, error)) return false;
    }
    if (error) error->clear();
    return true;
}

void MiaoSceneRuntime::MarkDirty(const PropertyAddress& address) {
    const auto exists = std::find_if(dirtyProperties_.begin(), dirtyProperties_.end(), [&](const PropertyAddress& item) {
        return item.componentId == address.componentId && item.propertyName == address.propertyName;
    });
    if (exists == dirtyProperties_.end()) dirtyProperties_.push_back(address);
    ++state_.generation;
}

bool MiaoSceneRuntime::SelfTest() {
    SceneRuntimeDefinition definition;
    definition.scene.id = L"scene://runtime-instance-self-test";
    definition.scene.kind = ContentKind::Wallpaper;
    definition.scene.rootNodeId = L"node://root";
    SceneNodeDefinition root;
    root.id = L"node://root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform",
        ComponentKind::Transform,
        {
            PropertyDefinition{L"opacity", PropertyType::Float, 1.0},
            PropertyDefinition{L"pulse", PropertyType::Float, 0.0},
        },
    });
    definition.scene.nodes.push_back(std::move(root));
    definition.profile = RuntimeProfile::Wallpaper;
    definition.parameters.push_back(ParameterDefinition{L"param://opacity", PropertyType::Float, 0.75});
    definition.inputs.push_back(InputChannelDefinition{L"input://frame/time", PropertyType::Float, 0.0});
    definition.inputs.push_back(InputChannelDefinition{L"input://event/pulse", PropertyType::Bool, false});
    definition.bindings.push_back(PropertyBindingDefinition{
        L"binding://opacity",
        PropertyAddress{L"component://root/transform", L"opacity"},
        BindingSourceKind::Parameter,
        L"param://opacity",
        1.0,
        0.0,
    });
    definition.animations.push_back(AnimationTrackDefinition{
        L"animation://opacity-pulse",
        PropertyAddress{L"component://root/transform", L"opacity"},
        true,
        AnimationLoopMode::PingPong,
        1.0,
        {
            AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::EaseInOut},
            AnimationKeyframeDefinition{1.0, 0.9, AnimationEasing::Linear},
        },
    });
    AnimationTrackDefinition eventAnimation{
        L"animation://event-pulse",
        PropertyAddress{L"component://root/transform", L"pulse"},
        true,
        AnimationLoopMode::Once,
        1.0,
        {
            AnimationKeyframeDefinition{0.0, 0.0, AnimationEasing::Linear},
            AnimationKeyframeDefinition{1.0, 1.0, AnimationEasing::Linear},
        },
    };
    eventAnimation.triggerMode = AnimationTriggerMode::InputRisingEdge;
    eventAnimation.triggerInputId = L"input://event/pulse";
    definition.animations.push_back(std::move(eventAnimation));

    MiaoSceneRuntime runtime;
    std::wstring error;
    if (!runtime.Initialize(std::move(definition), &error) || !runtime.State().loaded || runtime.State().paintReady) return false;
    const PropertyAddress opacity{L"component://root/transform", L"opacity"};
    const PropertyAddress pulse{L"component://root/transform", L"pulse"};
    const auto* initial = runtime.GetProperty(opacity);
    const auto* initialPulse = runtime.GetProperty(pulse);
    if (!initial || !std::holds_alternative<double>(*initial) || std::get<double>(*initial) != 0.75) return false;
    if (!initialPulse || std::get<double>(*initialPulse) != 0.0) return false;
    if (runtime.ConsumeDirtyProperties().size() != 1) return false;

    if (!runtime.SetInput(L"input://frame/time", 0.5, &error)) return false;
    const auto* animated = runtime.GetProperty(opacity);
    if (!animated || !std::holds_alternative<double>(*animated) || std::abs(std::get<double>(*animated) - 0.55) > 0.000001) return false;
    runtime.ConsumeDirtyProperties();

    if (!runtime.SetInput(L"input://event/pulse", false, &error)) return false;
    if (std::get<double>(*runtime.GetProperty(pulse)) != 0.0) return false;
    if (!runtime.SetInput(L"input://event/pulse", true, &error)) return false;
    if (std::get<double>(*runtime.GetProperty(pulse)) != 0.0) return false;

    if (!runtime.SetInput(L"input://frame/time", 1.0, &error)) return false;
    const auto* halfPulse = runtime.GetProperty(pulse);
    if (!halfPulse || std::abs(std::get<double>(*halfPulse) - 0.5) > 0.000001) return false;

    // A falling edge does not retrigger. The next rising edge does and resets
    // local event time to zero at the current frame time.
    if (!runtime.SetInput(L"input://event/pulse", false, &error)) return false;
    if (!runtime.SetInput(L"input://event/pulse", true, &error)) return false;
    if (std::get<double>(*runtime.GetProperty(pulse)) != 0.0) return false;
    if (!runtime.SetInput(L"input://frame/time", 1.25, &error)) return false;
    const auto* retriggered = runtime.GetProperty(pulse);
    if (!retriggered || std::abs(std::get<double>(*retriggered) - 0.25) > 0.000001) return false;

    if (!runtime.SetParameter(L"param://opacity", 0.42, &error)) return false;
    const auto* changed = runtime.GetProperty(opacity);
    if (!changed || std::get<double>(*changed) != 0.42) return false;
    if (runtime.SetParameter(L"param://opacity", std::wstring(L"wrong"), &error)) return false;

    if (!runtime.AdvanceTimeline(2.5, &error)) return false;
    const auto* completedPulse = runtime.GetProperty(pulse);
    if (!completedPulse || std::abs(std::get<double>(*completedPulse) - 1.0) > 0.000001) return false;
    if (!runtime.NeedsContinuousAnimation(2.5)) return false; // timeline ping-pong remains active

    // Event-only runtime can fully sleep before its first trigger and after a
    // once track completes, which is the scheduler contract M8 will reuse.
    SceneRuntimeDefinition eventOnly;
    eventOnly.scene.id = L"scene://event-only-self-test";
    eventOnly.scene.kind = ContentKind::Widget;
    eventOnly.scene.rootNodeId = L"node://root";
    SceneNodeDefinition eventRoot;
    eventRoot.id = L"node://root";
    eventRoot.components.push_back(SceneComponentDefinition{
        L"component://root/transform", ComponentKind::Transform,
        {PropertyDefinition{L"pulse", PropertyType::Float, 0.0}},
    });
    eventOnly.scene.nodes.push_back(std::move(eventRoot));
    eventOnly.profile = RuntimeProfile::Widget;
    eventOnly.inputs.push_back(InputChannelDefinition{L"input://event/pulse", PropertyType::Bool, false});
    AnimationTrackDefinition onlyTrack{
        L"animation://event-only", PropertyAddress{L"component://root/transform", L"pulse"}, true,
        AnimationLoopMode::Once, 0.5,
        {AnimationKeyframeDefinition{0.0, 0.0, AnimationEasing::Linear},
         AnimationKeyframeDefinition{0.5, 1.0, AnimationEasing::Linear}},
    };
    onlyTrack.triggerMode = AnimationTriggerMode::InputRisingEdge;
    onlyTrack.triggerInputId = L"input://event/pulse";
    eventOnly.animations.push_back(std::move(onlyTrack));
    MiaoSceneRuntime sleepingRuntime;
    if (!sleepingRuntime.Initialize(std::move(eventOnly), &error)) return false;
    if (sleepingRuntime.NeedsContinuousAnimation(0.0)) return false;
    if (!sleepingRuntime.SetInput(L"input://event/pulse", true, &error)) return false;
    if (!sleepingRuntime.NeedsContinuousAnimation(0.25)) return false;
    if (!sleepingRuntime.AdvanceTimeline(0.75, &error)) return false;
    if (sleepingRuntime.NeedsContinuousAnimation(0.75)) return false;

    runtime.MarkPaintReady();
    if (!runtime.State().paintReady) return false;
    runtime.Reset();
    return !runtime.State().loaded && runtime.Definition() == nullptr;
}

} // namespace miaodesk::content
