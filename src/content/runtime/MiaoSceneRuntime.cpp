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
    return true;
}

bool MiaoSceneRuntime::SetInput(std::wstring_view id, PropertyValue value, std::wstring* error) {
    if (!hasDefinition_ || !state_.loaded) return Fail(error, L"Scene runtime is not initialized.");
    const auto* definition = MiaoSceneRuntimeModel::FindInput(definition_, id);
    if (!definition) return Fail(error, L"Unknown scene input: " + std::wstring(id));
    if (!ValueMatchesType(definition->type, value)) return Fail(error, L"Scene input type mismatch: " + std::wstring(id));
    if (!ApplyBindingsForSource(BindingSourceKind::Input, id, value, error)) return false;
    inputValues_[std::wstring(id)] = std::move(value);
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
    dirtyProperties_.clear();
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
        {PropertyDefinition{L"opacity", PropertyType::Float, 1.0}},
    });
    definition.scene.nodes.push_back(std::move(root));
    definition.profile = RuntimeProfile::Wallpaper;
    definition.parameters.push_back(ParameterDefinition{L"param://opacity", PropertyType::Float, 0.75});
    definition.inputs.push_back(InputChannelDefinition{L"input://frame/time", PropertyType::Float, 0.0});
    definition.bindings.push_back(PropertyBindingDefinition{
        L"binding://opacity",
        PropertyAddress{L"component://root/transform", L"opacity"},
        BindingSourceKind::Parameter,
        L"param://opacity",
        1.0,
        0.0,
    });

    MiaoSceneRuntime runtime;
    std::wstring error;
    if (!runtime.Initialize(std::move(definition), &error) || !runtime.State().loaded || runtime.State().paintReady) return false;
    const PropertyAddress opacity{L"component://root/transform", L"opacity"};
    const auto* initial = runtime.GetProperty(opacity);
    if (!initial || !std::holds_alternative<double>(*initial) || std::get<double>(*initial) != 0.75) return false;
    if (runtime.ConsumeDirtyProperties().size() != 1) return false;

    if (!runtime.SetParameter(L"param://opacity", 0.42, &error)) return false;
    const auto* changed = runtime.GetProperty(opacity);
    if (!changed || std::get<double>(*changed) != 0.42) return false;
    if (runtime.ConsumeDirtyProperties().size() != 1) return false;
    if (runtime.SetParameter(L"param://opacity", std::wstring(L"wrong"), &error)) return false;

    runtime.MarkPaintReady();
    if (!runtime.State().paintReady) return false;
    runtime.Reset();
    return !runtime.State().loaded && runtime.Definition() == nullptr;
}

} // namespace miaodesk::content
