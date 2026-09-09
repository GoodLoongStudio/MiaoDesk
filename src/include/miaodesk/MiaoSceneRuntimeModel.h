#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "miaodesk/MiaoSceneModel.h"

namespace miaodesk::content {

enum class MaterialModel {
    Builtin,
    Programmable,
};

enum class BindingSourceKind {
    Parameter,
    Input,
};

enum class PostProcessEffectKind {
    Copy,
    Vignette,
    Noise,
    ColorMatrix,
    BlurHorizontal,
    BlurVertical,
    BloomThreshold,
    BloomCombine,
};

enum class AnimationLoopMode {
    Once,
    Loop,
    PingPong,
};

enum class AnimationEasing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
};

enum class AnimationTriggerMode {
    Timeline,
    InputChange,
    InputRisingEdge,
};

struct PropertyAddress {
    std::wstring componentId;
    std::wstring propertyName;
};

struct NamedValueDefinition {
    std::wstring id;
    PropertyType type{PropertyType::Float};
    PropertyValue defaultValue{0.0};
};

using ParameterDefinition = NamedValueDefinition;
using InputChannelDefinition = NamedValueDefinition;

struct MaterialTextureBinding {
    std::wstring slot;
    AssetReference asset;
};

struct MaterialDefinition {
    std::wstring id;
    MaterialModel model{MaterialModel::Builtin};
    std::wstring builtinName;
    std::wstring vertexShaderId;
    std::wstring pixelShaderId;
    std::vector<PropertyDefinition> properties;
    std::vector<MaterialTextureBinding> textures;
};

struct PropertyBindingDefinition {
    std::wstring id;
    PropertyAddress target;
    BindingSourceKind sourceKind{BindingSourceKind::Parameter};
    std::wstring sourceId;
    double scale{1.0};
    double offset{};
};

// Creative-runtime description of a built-in post process. It deliberately
// does not expose D3D11 resource ids: the renderer compiles this declarative
// list into RenderGraph resources/passes.
struct PostProcessDefinition {
    std::wstring id;
    PostProcessEffectKind effect{PostProcessEffectKind::Copy};
    bool enabled{true};
    double amount{1.0};
    double radius{0.75};
    double softness{0.25};
};

// Timeline animation targets the same stable PropertyAddress used by bindings
// and AI patches. The easing value belongs to the segment that starts at this
// keyframe and ends at the next keyframe.
struct AnimationKeyframeDefinition {
    double timeSeconds{};
    PropertyValue value{0.0};
    AnimationEasing easing{AnimationEasing::Linear};
};

struct AnimationTrackDefinition {
    std::wstring id;
    PropertyAddress target;
    bool enabled{true};
    AnimationLoopMode loopMode{AnimationLoopMode::Loop};
    double durationSeconds{1.0};
    std::vector<AnimationKeyframeDefinition> keyframes;

    // Timeline tracks evaluate against frame time immediately. Input-triggered
    // tracks stay dormant until their declared InputBus channel fires, then use
    // the trigger moment as local time zero. This keeps event animation inside
    // the same property/easing runtime instead of creating a second system.
    AnimationTriggerMode triggerMode{AnimationTriggerMode::Timeline};
    std::wstring triggerInputId;
};

// M4 v1 CPU-authored emitter contract. The definition is renderer-independent:
// simulation can run on CPU first while D3D11 consumes the resulting bounded
// particle state through instanced rendering. Compute remains a later upgrade.
struct ParticleEmitterDefinition {
    std::wstring id;
    bool enabled{true};
    std::uint32_t maxParticles{1024};
    double spawnRate{24.0};
    double lifetimeMinSeconds{1.0};
    double lifetimeMaxSeconds{2.0};
    Vec2 position{};
    Vec2 positionSpread{};
    Vec2 velocity{};
    Vec2 velocitySpread{};
    Vec2 acceleration{};
    double sizeStart{4.0};
    double sizeEnd{1.0};
    Color4 colorStart{1.0, 1.0, 1.0, 1.0};
    Color4 colorEnd{1.0, 1.0, 1.0, 0.0};
    std::wstring materialId;
    std::uint32_t seed{1};
};

struct SceneRuntimeDefinition {
    SceneDefinition scene;
    RuntimeProfile profile{RuntimeProfile::Wallpaper};
    std::vector<ParameterDefinition> parameters;
    std::vector<InputChannelDefinition> inputs;
    std::vector<MaterialDefinition> materials;
    std::vector<PropertyBindingDefinition> bindings;
    std::vector<AnimationTrackDefinition> animations;
    std::vector<PostProcessDefinition> postProcesses;
    std::vector<ParticleEmitterDefinition> particleEmitters;
};

class MiaoSceneRuntimeModel {
public:
    static constexpr std::uint32_t kMaxParticlesPerEmitter = 65536;
    static constexpr std::uint32_t kMaxParticlesPerScene = 131072;

    static bool Validate(const SceneRuntimeDefinition& runtime, std::wstring* error = nullptr);

    static const ParameterDefinition* FindParameter(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const InputChannelDefinition* FindInput(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const MaterialDefinition* FindMaterial(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const AnimationTrackDefinition* FindAnimation(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const PostProcessDefinition* FindPostProcess(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const ParticleEmitterDefinition* FindParticleEmitter(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const PropertyDefinition* FindProperty(
        const SceneDefinition& scene, const PropertyAddress& address) noexcept;

    static bool SelfTest();
};

class MiaoPropertyStore {
public:
    bool Initialize(const SceneDefinition& scene, std::wstring* error = nullptr);
    bool Set(const PropertyAddress& address, PropertyValue value, std::wstring* error = nullptr);
    const PropertyValue* Get(const PropertyAddress& address) const noexcept;
    bool Reset(const PropertyAddress& address, std::wstring* error = nullptr);
    void Clear() noexcept;
    std::size_t Size() const noexcept;

private:
    struct Entry {
        PropertyType type{PropertyType::Float};
        PropertyValue defaultValue{0.0};
        PropertyValue value{0.0};
    };

    static std::wstring Key(const PropertyAddress& address);
    std::unordered_map<std::wstring, Entry> values_;
};

} // namespace miaodesk::content
