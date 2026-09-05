#pragma once

#include <cstddef>
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

struct SceneRuntimeDefinition {
    SceneDefinition scene;
    RuntimeProfile profile{RuntimeProfile::Wallpaper};
    std::vector<ParameterDefinition> parameters;
    std::vector<InputChannelDefinition> inputs;
    std::vector<MaterialDefinition> materials;
    std::vector<PropertyBindingDefinition> bindings;
};

class MiaoSceneRuntimeModel {
public:
    static bool Validate(const SceneRuntimeDefinition& runtime, std::wstring* error = nullptr);

    static const ParameterDefinition* FindParameter(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const InputChannelDefinition* FindInput(
        const SceneRuntimeDefinition& runtime, std::wstring_view id) noexcept;
    static const MaterialDefinition* FindMaterial(
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
