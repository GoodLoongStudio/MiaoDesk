#include "miaodesk/MiaoSceneRuntimeModel.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool HasPrefix(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
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

bool IsNumeric(PropertyType type) noexcept {
    return type == PropertyType::Int || type == PropertyType::Float;
}

bool IsAnimatable(PropertyType type) noexcept {
    return type == PropertyType::Int || type == PropertyType::Float ||
           type == PropertyType::Vec2 || type == PropertyType::Vec3 ||
           type == PropertyType::Vec4 || type == PropertyType::Color;
}

bool AnimationValueFinite(PropertyType type, const PropertyValue& value) noexcept {
    if (!ValueMatchesType(type, value)) return false;
    if (const auto* item = std::get_if<double>(&value)) return std::isfinite(*item);
    if (const auto* item = std::get_if<Vec2>(&value)) return std::isfinite(item->x) && std::isfinite(item->y);
    if (const auto* item = std::get_if<Vec3>(&value))
        return std::isfinite(item->x) && std::isfinite(item->y) && std::isfinite(item->z);
    if (const auto* item = std::get_if<Vec4>(&value))
        return std::isfinite(item->x) && std::isfinite(item->y) && std::isfinite(item->z) && std::isfinite(item->w);
    if (const auto* item = std::get_if<Color4>(&value))
        return std::isfinite(item->r) && std::isfinite(item->g) && std::isfinite(item->b) && std::isfinite(item->a);
    return true;
}

const ShaderDefinition* FindShader(const SceneDefinition& scene, std::wstring_view id) noexcept {
    for (const auto& shader : scene.shaders) if (shader.id == id) return &shader;
    return nullptr;
}

const AssetDefinition* FindAsset(const SceneDefinition& scene, std::wstring_view id) noexcept {
    for (const auto& asset : scene.assets) if (asset.id == id) return &asset;
    return nullptr;
}

bool ValidateNamedValue(
    const NamedValueDefinition& value,
    std::wstring_view prefix,
    std::wstring_view label,
    std::wstring* error) {
    if (!HasPrefix(value.id, prefix))
        return Fail(error, std::wstring(label) + L" id must use the " + std::wstring(prefix) + L" stable-id scheme: " + value.id);
    if (!ValueMatchesType(value.type, value.defaultValue))
        return Fail(error, std::wstring(label) + L" default value does not match its declared type: " + value.id);
    return true;
}

bool ValidatePostProcess(const PostProcessDefinition& effect, std::wstring* error) {
    if (!HasPrefix(effect.id, L"postfx://"))
        return Fail(error, L"Post-process id must use the postfx:// stable-id scheme: " + effect.id);
    if (!std::isfinite(effect.amount) || !std::isfinite(effect.radius) || !std::isfinite(effect.softness))
        return Fail(error, L"Post-process parameters must be finite: " + effect.id);
    if (effect.amount < 0.0 || effect.amount > 4.0)
        return Fail(error, L"Post-process amount must be in [0, 4]: " + effect.id);
    if (effect.radius < 0.0 || effect.radius > 2.0)
        return Fail(error, L"Post-process radius must be in [0, 2]: " + effect.id);
    if (effect.softness < 0.0 || effect.softness > 2.0)
        return Fail(error, L"Post-process softness must be in [0, 2]: " + effect.id);
    return true;
}

bool ValidateAnimation(const SceneRuntimeDefinition& runtime, const AnimationTrackDefinition& animation, std::wstring* error) {
    if (!HasPrefix(animation.id, L"animation://"))
        return Fail(error, L"Animation id must use the animation:// stable-id scheme: " + animation.id);
    const auto* target = MiaoSceneRuntimeModel::FindProperty(runtime.scene, animation.target);
    if (!target) return Fail(error, L"Animation target does not resolve to a component property: " + animation.id);
    if (!IsAnimatable(target->type))
        return Fail(error, L"Animation target type is not interpolatable in v1: " + animation.id);
    if (!std::isfinite(animation.durationSeconds) || animation.durationSeconds <= 0.0 || animation.durationSeconds > 86400.0)
        return Fail(error, L"Animation duration must be in (0, 86400] seconds: " + animation.id);
    if (animation.keyframes.size() < 2 || animation.keyframes.size() > 4096)
        return Fail(error, L"Animation requires between 2 and 4096 keyframes: " + animation.id);

    double previousTime = -1.0;
    for (const auto& keyframe : animation.keyframes) {
        if (!std::isfinite(keyframe.timeSeconds) || keyframe.timeSeconds < 0.0 ||
            keyframe.timeSeconds > animation.durationSeconds)
            return Fail(error, L"Animation keyframe time is outside the track duration: " + animation.id);
        if (keyframe.timeSeconds <= previousTime)
            return Fail(error, L"Animation keyframes must be strictly increasing by time: " + animation.id);
        if (!AnimationValueFinite(target->type, keyframe.value))
            return Fail(error, L"Animation keyframe value does not match its target type or is non-finite: " + animation.id);
        previousTime = keyframe.timeSeconds;
    }

    if (animation.triggerMode == AnimationTriggerMode::Timeline) {
        if (!animation.triggerInputId.empty())
            return Fail(error, L"Timeline animation must not declare triggerInputId: " + animation.id);
    } else {
        if (animation.triggerInputId.empty())
            return Fail(error, L"Input-triggered animation requires triggerInputId: " + animation.id);
        const auto* input = MiaoSceneRuntimeModel::FindInput(runtime, animation.triggerInputId);
        if (!input) return Fail(error, L"Animation trigger input does not exist: " + animation.id);
        if (animation.triggerMode == AnimationTriggerMode::InputRisingEdge && input->type != PropertyType::Bool)
            return Fail(error, L"inputRisingEdge animation trigger requires a bool input: " + animation.id);
    }
    return true;
}

} // namespace

const ParameterDefinition* MiaoSceneRuntimeModel::FindParameter(
    const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept {
    for (const auto& parameter : runtime.parameters) if (parameter.id == id) return &parameter;
    return nullptr;
}

const InputChannelDefinition* MiaoSceneRuntimeModel::FindInput(
    const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept {
    for (const auto& input : runtime.inputs) if (input.id == id) return &input;
    return nullptr;
}

const MaterialDefinition* MiaoSceneRuntimeModel::FindMaterial(
    const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept {
    for (const auto& material : runtime.materials) if (material.id == id) return &material;
    return nullptr;
}

const AnimationTrackDefinition* MiaoSceneRuntimeModel::FindAnimation(
    const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept {
    for (const auto& animation : runtime.animations) if (animation.id == id) return &animation;
    return nullptr;
}

const PostProcessDefinition* MiaoSceneRuntimeModel::FindPostProcess(
    const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept {
    for (const auto& effect : runtime.postProcesses) if (effect.id == id) return &effect;
    return nullptr;
}

const PropertyDefinition* MiaoSceneRuntimeModel::FindProperty(
    const SceneDefinition& scene, const PropertyAddress& address) noexcept {
    const auto* component = MiaoSceneModel::FindComponent(scene, address.componentId);
    if (!component) return nullptr;
    for (const auto& property : component->properties) if (property.name == address.propertyName) return &property;
    return nullptr;
}

bool MiaoSceneRuntimeModel::Validate(const SceneRuntimeDefinition& runtime, std::wstring* error) {
    std::wstring sceneError;
    if (!MiaoSceneModel::Validate(runtime.scene, &sceneError))
        return Fail(error, L"Scene model invalid: " + sceneError);

    if ((runtime.scene.kind == ContentKind::Wallpaper && runtime.profile != RuntimeProfile::Wallpaper) ||
        (runtime.scene.kind == ContentKind::Widget && runtime.profile != RuntimeProfile::Widget))
        return Fail(error, L"RuntimeProfile must match Scene ContentKind.");

    std::unordered_set<std::wstring> parameterIds;
    for (const auto& parameter : runtime.parameters) {
        if (!ValidateNamedValue(parameter, L"param://", L"Parameter", error)) return false;
        if (!parameterIds.emplace(parameter.id).second)
            return Fail(error, L"Duplicate parameter id: " + parameter.id);
    }

    std::unordered_set<std::wstring> inputIds;
    for (const auto& input : runtime.inputs) {
        if (!ValidateNamedValue(input, L"input://", L"Input", error)) return false;
        if (!inputIds.emplace(input.id).second)
            return Fail(error, L"Duplicate input id: " + input.id);
    }

    std::unordered_set<std::wstring> materialIds;
    for (const auto& material : runtime.materials) {
        if (!HasPrefix(material.id, L"material://"))
            return Fail(error, L"Material id must use the material:// stable-id scheme: " + material.id);
        if (!materialIds.emplace(material.id).second)
            return Fail(error, L"Duplicate material id: " + material.id);

        if (material.model == MaterialModel::Builtin) {
            if (material.builtinName.empty()) return Fail(error, L"Builtin material requires builtinName: " + material.id);
            if (!material.vertexShaderId.empty() || !material.pixelShaderId.empty())
                return Fail(error, L"Builtin material must not directly bind custom shader ids: " + material.id);
        } else {
            if (!material.builtinName.empty())
                return Fail(error, L"Programmable material must not declare builtinName: " + material.id);
            if (material.pixelShaderId.empty())
                return Fail(error, L"Programmable material requires a Pixel Shader: " + material.id);
            if (!material.vertexShaderId.empty()) {
                const auto* shader = FindShader(runtime.scene, material.vertexShaderId);
                if (!shader || shader->stage != ShaderStage::Vertex)
                    return Fail(error, L"Material vertexShaderId must resolve to a Vertex Shader: " + material.id);
            }
            const auto* pixel = FindShader(runtime.scene, material.pixelShaderId);
            if (!pixel || pixel->stage != ShaderStage::Pixel)
                return Fail(error, L"Material pixelShaderId must resolve to a Pixel Shader: " + material.id);
        }

        std::unordered_set<std::wstring> propertyNames;
        for (const auto& property : material.properties) {
            if (property.name.empty()) return Fail(error, L"Material property name cannot be empty: " + material.id);
            if (!propertyNames.emplace(property.name).second)
                return Fail(error, L"Duplicate material property: " + material.id + L"/" + property.name);
            if (!ValueMatchesType(property.type, property.defaultValue))
                return Fail(error, L"Material property default value has wrong type: " + material.id + L"/" + property.name);
        }

        std::unordered_set<std::wstring> textureSlots;
        for (const auto& texture : material.textures) {
            if (texture.slot.empty()) return Fail(error, L"Material texture slot cannot be empty: " + material.id);
            if (!textureSlots.emplace(texture.slot).second)
                return Fail(error, L"Duplicate material texture slot: " + material.id + L"/" + texture.slot);
            const auto* asset = FindAsset(runtime.scene, texture.asset.id);
            if (!asset) return Fail(error, L"Material texture must reference an existing asset: " + material.id + L"/" + texture.slot);
            if (asset->type != AssetType::Image && asset->type != AssetType::Video && asset->type != AssetType::Binary)
                return Fail(error, L"Material texture must reference Image, Video, or Binary asset: " + material.id + L"/" + texture.slot);
        }
    }

    std::unordered_set<std::wstring> bindingIds;
    for (const auto& binding : runtime.bindings) {
        if (!HasPrefix(binding.id, L"binding://"))
            return Fail(error, L"Binding id must use the binding:// stable-id scheme: " + binding.id);
        if (!bindingIds.emplace(binding.id).second) return Fail(error, L"Duplicate binding id: " + binding.id);
        if (!std::isfinite(binding.scale) || !std::isfinite(binding.offset))
            return Fail(error, L"Binding scale/offset must be finite: " + binding.id);

        const auto* target = FindProperty(runtime.scene, binding.target);
        if (!target) return Fail(error, L"Binding target does not resolve to a component property: " + binding.id);

        const NamedValueDefinition* source = nullptr;
        if (binding.sourceKind == BindingSourceKind::Parameter) {
            source = FindParameter(runtime, binding.sourceId);
            if (!source) return Fail(error, L"Binding parameter source does not exist: " + binding.id);
        } else {
            source = FindInput(runtime, binding.sourceId);
            if (!source) return Fail(error, L"Binding input source does not exist: " + binding.id);
        }
        if (source->type != target->type)
            return Fail(error, L"Binding source and target types must match in v1: " + binding.id);
        if ((binding.scale != 1.0 || binding.offset != 0.0) && !IsNumeric(source->type))
            return Fail(error, L"Binding scale/offset are only valid for numeric values in v1: " + binding.id);
    }

    std::unordered_set<std::wstring> animationIds;
    for (const auto& animation : runtime.animations) {
        if (!ValidateAnimation(runtime, animation, error)) return false;
        if (!animationIds.emplace(animation.id).second)
            return Fail(error, L"Duplicate animation id: " + animation.id);
    }

    std::unordered_set<std::wstring> postProcessIds;
    for (const auto& effect : runtime.postProcesses) {
        if (!ValidatePostProcess(effect, error)) return false;
        if (!postProcessIds.emplace(effect.id).second)
            return Fail(error, L"Duplicate post-process id: " + effect.id);
    }

    if (error) error->clear();
    return true;
}

std::wstring MiaoPropertyStore::Key(const PropertyAddress& address) {
    std::wstring key = address.componentId;
    key.push_back(L'\x1f');
    key += address.propertyName;
    return key;
}

bool MiaoPropertyStore::Initialize(const SceneDefinition& scene, std::wstring* error) {
    std::wstring sceneError;
    if (!MiaoSceneModel::Validate(scene, &sceneError))
        return Fail(error, L"Cannot initialize property store from invalid scene: " + sceneError);

    values_.clear();
    for (const auto& node : scene.nodes) {
        for (const auto& component : node.components) {
            for (const auto& property : component.properties) {
                const PropertyAddress address{component.id, property.name};
                values_.emplace(Key(address), Entry{property.type, property.defaultValue, property.defaultValue});
            }
        }
    }
    if (error) error->clear();
    return true;
}

bool MiaoPropertyStore::Set(const PropertyAddress& address, PropertyValue value, std::wstring* error) {
    const auto it = values_.find(Key(address));
    if (it == values_.end()) return Fail(error, L"Unknown scene property: " + address.componentId + L"/" + address.propertyName);
    if (!ValueMatchesType(it->second.type, value))
        return Fail(error, L"Scene property value type mismatch: " + address.componentId + L"/" + address.propertyName);
    it->second.value = std::move(value);
    if (error) error->clear();
    return true;
}

const PropertyValue* MiaoPropertyStore::Get(const PropertyAddress& address) const noexcept {
    const auto it = values_.find(Key(address));
    return it == values_.end() ? nullptr : &it->second.value;
}

bool MiaoPropertyStore::Reset(const PropertyAddress& address, std::wstring* error) {
    const auto it = values_.find(Key(address));
    if (it == values_.end()) return Fail(error, L"Unknown scene property: " + address.componentId + L"/" + address.propertyName);
    it->second.value = it->second.defaultValue;
    if (error) error->clear();
    return true;
}

void MiaoPropertyStore::Clear() noexcept { values_.clear(); }
std::size_t MiaoPropertyStore::Size() const noexcept { return values_.size(); }

bool MiaoSceneRuntimeModel::SelfTest() {
    SceneDefinition scene;
    scene.id = L"scene://runtime-self-test";
    scene.kind = ContentKind::Wallpaper;
    scene.rootNodeId = L"node://root";

    SceneNodeDefinition root;
    root.id = L"node://root";
    root.name = L"Root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform", ComponentKind::Transform,
        {PropertyDefinition{L"opacity", PropertyType::Float, 1.0},
         PropertyDefinition{L"pulse", PropertyType::Float, 0.0},
         PropertyDefinition{L"visible", PropertyType::Bool, true}},
    });
    scene.nodes.push_back(root);
    scene.assets.push_back(AssetDefinition{L"asset://shader/self-test", AssetType::Shader, L"shaders/self-test.hlsl"});
    scene.shaders.push_back(ShaderDefinition{L"shader://self-test/pixel", ShaderStage::Pixel,
                                             L"asset://shader/self-test", "main", true});

    SceneRuntimeDefinition runtime;
    runtime.scene = scene;
    runtime.profile = RuntimeProfile::Wallpaper;
    runtime.parameters.push_back(ParameterDefinition{L"param://opacity", PropertyType::Float, 0.75});
    runtime.inputs.push_back(InputChannelDefinition{L"input://audio/bass", PropertyType::Float, 0.0});
    runtime.inputs.push_back(InputChannelDefinition{L"input://event/pulse", PropertyType::Bool, false});
    runtime.materials.push_back(MaterialDefinition{
        L"material://builtin/sprite", MaterialModel::Builtin, L"sprite", L"", L"",
        {PropertyDefinition{L"tint", PropertyType::Color, Color4{1.0, 1.0, 1.0, 1.0}}}, {},
    });
    runtime.materials.push_back(MaterialDefinition{
        L"material://programmable/self-test", MaterialModel::Programmable, L"", L"",
        L"shader://self-test/pixel", {PropertyDefinition{L"strength", PropertyType::Float, 1.0}}, {},
    });
    runtime.bindings.push_back(PropertyBindingDefinition{
        L"binding://opacity", PropertyAddress{L"component://root/transform", L"opacity"},
        BindingSourceKind::Parameter, L"param://opacity", 1.0, 0.0,
    });
    runtime.animations.push_back(AnimationTrackDefinition{
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
        0.5,
        {
            AnimationKeyframeDefinition{0.0, 0.0, AnimationEasing::EaseOut},
            AnimationKeyframeDefinition{0.5, 1.0, AnimationEasing::Linear},
        },
    };
    eventAnimation.triggerMode = AnimationTriggerMode::InputRisingEdge;
    eventAnimation.triggerInputId = L"input://event/pulse";
    runtime.animations.push_back(std::move(eventAnimation));
    runtime.postProcesses.push_back(PostProcessDefinition{
        L"postfx://vignette", PostProcessEffectKind::Vignette, true, 0.8, 0.72, 0.22,
    });

    std::wstring error;
    if (!Validate(runtime, &error)) return false;
    if (!FindParameter(runtime, L"param://opacity")) return false;
    if (!FindInput(runtime, L"input://audio/bass")) return false;
    if (!FindMaterial(runtime, L"material://builtin/sprite")) return false;
    if (!FindAnimation(runtime, L"animation://opacity-pulse") || !FindAnimation(runtime, L"animation://event-pulse")) return false;
    if (!FindPostProcess(runtime, L"postfx://vignette")) return false;

    MiaoPropertyStore store;
    if (!store.Initialize(runtime.scene, &error)) return false;
    if (store.Size() != 3) return false;

    const PropertyAddress opacity{L"component://root/transform", L"opacity"};
    const auto* initial = store.Get(opacity);
    if (!initial || !std::holds_alternative<double>(*initial) || std::get<double>(*initial) != 1.0) return false;
    if (!store.Set(opacity, 0.4, &error)) return false;
    const auto* changed = store.Get(opacity);
    if (!changed || std::get<double>(*changed) != 0.4) return false;
    if (store.Set(opacity, std::wstring(L"wrong"), &error)) return false;
    if (!store.Reset(opacity, &error)) return false;
    const auto* reset = store.Get(opacity);
    if (!reset || std::get<double>(*reset) != 1.0) return false;

    SceneRuntimeDefinition invalid = runtime;
    invalid.bindings.front().sourceId = L"param://missing";
    if (Validate(invalid, &error)) return false;

    invalid = runtime;
    invalid.animations.front().keyframes[1].timeSeconds = 0.0;
    if (Validate(invalid, &error)) return false;

    invalid = runtime;
    invalid.animations.front().target.propertyName = L"visible";
    if (Validate(invalid, &error)) return false;

    invalid = runtime;
    invalid.animations[1].triggerInputId = L"input://audio/bass";
    if (Validate(invalid, &error)) return false;

    invalid = runtime;
    invalid.animations[1].triggerMode = AnimationTriggerMode::Timeline;
    if (Validate(invalid, &error)) return false;

    invalid = runtime;
    invalid.postProcesses.front().id = L"bad-id";
    if (Validate(invalid, &error)) return false;

    return true;
}

} // namespace miaodesk::content
