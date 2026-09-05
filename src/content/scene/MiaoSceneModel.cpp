#include "miaodesk/MiaoSceneModel.h"

#include <unordered_map>
#include <unordered_set>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool HasPrefix(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool PropertyTypeMatches(const PropertyDefinition& property) noexcept {
    switch (property.type) {
    case PropertyType::Bool:
        return std::holds_alternative<bool>(property.defaultValue);
    case PropertyType::Int:
        return std::holds_alternative<std::int64_t>(property.defaultValue);
    case PropertyType::Float:
        return std::holds_alternative<double>(property.defaultValue);
    case PropertyType::String:
        return std::holds_alternative<std::wstring>(property.defaultValue);
    case PropertyType::Vec2:
        return std::holds_alternative<Vec2>(property.defaultValue);
    case PropertyType::Vec3:
        return std::holds_alternative<Vec3>(property.defaultValue);
    case PropertyType::Vec4:
        return std::holds_alternative<Vec4>(property.defaultValue);
    case PropertyType::Color:
        return std::holds_alternative<Color4>(property.defaultValue);
    case PropertyType::AssetReference:
        return std::holds_alternative<AssetReference>(property.defaultValue);
    }
    return false;
}

} // namespace

const SceneNodeDefinition* MiaoSceneModel::FindNode(const SceneDefinition& scene, std::wstring_view id) noexcept {
    for (const auto& node : scene.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

const SceneComponentDefinition* MiaoSceneModel::FindComponent(
    const SceneDefinition& scene, std::wstring_view id) noexcept {
    for (const auto& node : scene.nodes) {
        for (const auto& component : node.components) {
            if (component.id == id) return &component;
        }
    }
    return nullptr;
}

bool MiaoSceneModel::Validate(const SceneDefinition& scene, std::wstring* error) {
    if (scene.schemaVersion != kSchemaVersion)
        return Fail(error, L"Unsupported Miao Scene schema version.");
    if (!HasPrefix(scene.id, L"scene://"))
        return Fail(error, L"Scene id must use the scene:// stable-id scheme.");
    if (!HasPrefix(scene.rootNodeId, L"node://"))
        return Fail(error, L"Scene rootNodeId must use the node:// stable-id scheme.");
    if (scene.nodes.empty())
        return Fail(error, L"Scene must contain at least one node.");

    std::unordered_map<std::wstring, const SceneNodeDefinition*> nodes;
    std::unordered_set<std::wstring> componentIds;
    for (const auto& node : scene.nodes) {
        if (!HasPrefix(node.id, L"node://"))
            return Fail(error, L"Every node id must use the node:// stable-id scheme.");
        if (!nodes.emplace(node.id, &node).second)
            return Fail(error, L"Duplicate scene node id: " + node.id);

        for (const auto& component : node.components) {
            if (!HasPrefix(component.id, L"component://"))
                return Fail(error, L"Every component id must use the component:// stable-id scheme.");
            if (!componentIds.emplace(component.id).second)
                return Fail(error, L"Duplicate component id: " + component.id);

            std::unordered_set<std::wstring> propertyNames;
            for (const auto& property : component.properties) {
                if (property.name.empty())
                    return Fail(error, L"Component property names cannot be empty.");
                if (!propertyNames.emplace(property.name).second)
                    return Fail(error, L"Duplicate component property: " + property.name);
                if (!PropertyTypeMatches(property))
                    return Fail(error, L"Component property default value does not match its declared type: " + property.name);
            }
        }
    }

    const auto root = nodes.find(scene.rootNodeId);
    if (root == nodes.end())
        return Fail(error, L"rootNodeId does not resolve to a scene node.");
    if (!root->second->parentId.empty())
        return Fail(error, L"Root scene node must not have a parent.");

    for (const auto& [id, node] : nodes) {
        if (id == scene.rootNodeId) continue;
        if (node->parentId.empty())
            return Fail(error, L"Every non-root node must have a parent: " + id);
        if (node->parentId == id)
            return Fail(error, L"Scene node cannot parent itself: " + id);
        if (!nodes.contains(node->parentId))
            return Fail(error, L"Scene node parent does not exist: " + node->parentId);

        std::unordered_set<std::wstring> ancestry;
        const SceneNodeDefinition* current = node;
        while (!current->parentId.empty()) {
            if (!ancestry.emplace(current->id).second)
                return Fail(error, L"Cycle detected in scene node hierarchy at: " + current->id);
            const auto parent = nodes.find(current->parentId);
            if (parent == nodes.end())
                return Fail(error, L"Scene node parent does not exist: " + current->parentId);
            current = parent->second;
        }
        if (current->id != scene.rootNodeId)
            return Fail(error, L"Scene node hierarchy is not connected to root: " + id);
    }

    std::unordered_map<std::wstring, AssetType> assets;
    for (const auto& asset : scene.assets) {
        if (!HasPrefix(asset.id, L"asset://"))
            return Fail(error, L"Every asset id must use the asset:// stable-id scheme.");
        if (asset.source.empty())
            return Fail(error, L"Asset source cannot be empty: " + asset.id);
        if (!assets.emplace(asset.id, asset.type).second)
            return Fail(error, L"Duplicate asset id: " + asset.id);
    }

    std::unordered_set<std::wstring> shaderIds;
    for (const auto& shader : scene.shaders) {
        if (!HasPrefix(shader.id, L"shader://"))
            return Fail(error, L"Every shader id must use the shader:// stable-id scheme.");
        if (!shaderIds.emplace(shader.id).second)
            return Fail(error, L"Duplicate shader id: " + shader.id);
        if (shader.entryPoint.empty())
            return Fail(error, L"Shader entry point cannot be empty: " + shader.id);
        const auto asset = assets.find(shader.assetId);
        if (asset == assets.end() || asset->second != AssetType::Shader)
            return Fail(error, L"Shader must reference a Shader asset: " + shader.id);
    }

    if (error) error->clear();
    return true;
}

bool MiaoSceneModel::SelfTest() {
    SceneDefinition scene;
    scene.id = L"scene://self-test";
    scene.kind = ContentKind::Wallpaper;
    scene.rootNodeId = L"node://root";

    SceneNodeDefinition root;
    root.id = L"node://root";
    root.name = L"Root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform",
        ComponentKind::Transform,
        {
            PropertyDefinition{L"position", PropertyType::Vec2, Vec2{0.0, 0.0}},
            PropertyDefinition{L"opacity", PropertyType::Float, 1.0},
        },
    });

    SceneNodeDefinition background;
    background.id = L"node://background";
    background.name = L"Background";
    background.parentId = root.id;
    background.components.push_back(SceneComponentDefinition{
        L"component://background/material",
        ComponentKind::Material,
        {
            PropertyDefinition{L"intensity", PropertyType::Float, 0.8},
            PropertyDefinition{L"tint", PropertyType::Color, Color4{0.3, 0.7, 1.0, 1.0}},
        },
    });

    scene.nodes = {root, background};
    scene.assets.push_back(AssetDefinition{
        L"asset://shader/aurora",
        AssetType::Shader,
        L"shaders/aurora.hlsl",
    });
    scene.shaders.push_back(ShaderDefinition{
        L"shader://aurora",
        ShaderStage::Pixel,
        L"asset://shader/aurora",
        "main",
        true,
    });

    std::wstring error;
    if (!Validate(scene, &error)) return false;
    if (!FindNode(scene, L"node://background")) return false;
    if (!FindComponent(scene, L"component://background/material")) return false;

    SceneDefinition invalid = scene;
    invalid.nodes.push_back(background);
    if (Validate(invalid, &error)) return false;

    return true;
}

} // namespace miaodesk::content
