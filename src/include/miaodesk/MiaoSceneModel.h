#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace miaodesk::content {

enum class ContentKind {
    Wallpaper,
    Widget,
};

enum class RuntimeProfile {
    Wallpaper,
    Widget,
};

enum class ComponentKind {
    Transform,
    SpriteRenderer,
    TextRenderer,
    VideoRenderer,
    Material,
    ParticleSystem,
    Animator,
    Script,
    InputBinding,
    Custom,
};

enum class AssetType {
    Image,
    Video,
    Audio,
    Font,
    Shader,
    Script,
    Mesh,
    Binary,
};

enum class ShaderStage {
    Vertex,
    Pixel,
    Compute,
};

enum class PropertyType {
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
    Color,
    AssetReference,
};

struct Vec2 {
    double x{};
    double y{};
};

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct Vec4 {
    double x{};
    double y{};
    double z{};
    double w{};
};

struct Color4 {
    double r{};
    double g{};
    double b{};
    double a{1.0};
};

struct AssetReference {
    std::wstring id;
};

using PropertyValue = std::variant<
    bool,
    std::int64_t,
    double,
    std::wstring,
    Vec2,
    Vec3,
    Vec4,
    Color4,
    AssetReference>;

struct PropertyDefinition {
    std::wstring name;
    PropertyType type{PropertyType::Float};
    PropertyValue defaultValue{0.0};
};

struct SceneComponentDefinition {
    std::wstring id;
    ComponentKind kind{ComponentKind::Custom};
    std::vector<PropertyDefinition> properties;
};

struct SceneNodeDefinition {
    std::wstring id;
    std::wstring name;
    std::wstring parentId;
    bool enabled{true};
    std::vector<SceneComponentDefinition> components;
};

struct AssetDefinition {
    std::wstring id;
    AssetType type{AssetType::Binary};
    std::wstring source;
};

struct ShaderDefinition {
    std::wstring id;
    ShaderStage stage{ShaderStage::Pixel};
    std::wstring assetId;
    std::string entryPoint{"main"};
    bool userAuthored{true};
};

struct SceneDefinition {
    std::uint32_t schemaVersion{1};
    std::wstring id;
    ContentKind kind{ContentKind::Wallpaper};
    std::wstring rootNodeId;
    std::vector<SceneNodeDefinition> nodes;
    std::vector<AssetDefinition> assets;
    std::vector<ShaderDefinition> shaders;
};

class MiaoSceneModel {
public:
    static constexpr std::uint32_t kSchemaVersion = 1;

    static bool Validate(const SceneDefinition& scene, std::wstring* error = nullptr);
    static const SceneNodeDefinition* FindNode(const SceneDefinition& scene, std::wstring_view id) noexcept;
    static const SceneComponentDefinition* FindComponent(const SceneDefinition& scene, std::wstring_view id) noexcept;
    static bool SelfTest();
};

} // namespace miaodesk::content
