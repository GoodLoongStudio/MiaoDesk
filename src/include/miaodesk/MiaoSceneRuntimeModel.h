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

// How a binding remaps its source before scale/offset are applied.
//
// This is the deliberate alternative to a general script language. A linear
// binding (`* scale + offset`) makes audio and pointer input feel mechanical — a
// spectrum bar cannot sit near zero until the music is loud, and a parallax layer
// cannot overshoot the cursor. Those effects need a nonlinear response, not code.
//
// The set is closed on purpose: every member is a pure function of one float, so a
// binding can never acquire side effects, file access, or unbounded runtime, and the
// AI authoring path stays inside "declarative content only". Adding a member means
// adding a branch to ApplyBindingResponse and a case to its serializer.
enum class BindingResponse {
    Linear,
    Square,
    Cube,
    SquareRoot,
    SmoothStep,
    // Damped overshoot: reads as a spring following the cursor. Peaks above 1 and
    // settles at 1, so callers that need a bounded value should clamp downstream.
    Elastic,
    // Hard switch at the midpoint. Turns a continuous spectrum level into an
    // on/off trigger for a strobe or a threshold-driven bloom.
    Threshold,
    Invert,
};

inline constexpr std::size_t kBindingResponseCount = 8;

// Stable JSON keys for BindingResponse. These live beside the enum so the
// serializer, the authoring skills and the validation error messages cannot drift
// apart on spelling.
const char* BindingResponseKey(BindingResponse response) noexcept;
bool ParseBindingResponse(std::string_view value, BindingResponse* response) noexcept;

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
    // Defaults to Linear, which reproduces the original `scale * value + offset`
    // behaviour exactly, so existing packages are unaffected.
    BindingResponse response{BindingResponse::Linear};
    // Values below this magnitude map to zero. Keeps a resting spectrum level from
    // making a "quiet" wallpaper jitter.
    double deadzone{};
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
// Light and fog are scene-level resources rather than per-node components: a light
// needs a node for placement but is otherwise shared, exactly like a material.
// Both are 3D-only, so a 2D scene that declares either is an authoring error rather
// than a silently ignored light.
enum class LightType {
    Point,
    Spot,
    Tube,
    Directional,
};

// The light ceiling is not arbitrary: Wallpaper Engine documents a 12-light limit
// per scene, and matching it keeps authoring expectations aligned while a scene
// stays inside what a deferred renderer can actually shade.
inline constexpr std::uint32_t kMaxLightsPerScene = 12;

struct LightDefinition {
    std::wstring id;  // light://<name>
    LightType type{LightType::Point};
    // The node whose Transform carries the light's world position and direction.
    std::wstring nodeId;
    Color4 color{1.0, 1.0, 1.0, 1.0};
    double intensity{1.0};
    // Attenuation radius in scene units. 0 means unbounded.
    double range{};
    // Cosine of the spot cone. Both are cosines rather than degrees so the renderer
    // does not have to convert per frame. -1 means "unused" for a non-spot light.
    double spotInnerCos{-1.0};
    double spotOuterCos{-1.0};
};

enum class FogMode {
    // Linear between start and end distances.
    Linear,
    // Density-based exponential falloff.
    Exponential,
};

struct FogDefinition {
    std::wstring id;  // fog://<name>
    FogMode mode{FogMode::Linear};
    Color4 color{0.5, 0.5, 0.5, 1.0};
    // Linear mode: near distance. Exponential mode: density.
    double startOrDensity{};
    // Linear mode: far distance. Ignored by Exponential, where density carries it.
    double end{1.0};
};

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
    // 3D-only. A 2D scene that declares either is rejected by Validate.
    std::vector<LightDefinition> lights;
    std::vector<FogDefinition> fog;
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
