#include "miaodesk/MiaoGpuParameterBlock.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool WriteSlot(const PropertyValue& value, std::array<float, 4>* slot) {
    if (!slot) return false;
    *slot = {0.0f, 0.0f, 0.0f, 0.0f};
    if (const auto* item = std::get_if<bool>(&value)) {
        (*slot)[0] = *item ? 1.0f : 0.0f;
        return true;
    }
    if (const auto* item = std::get_if<std::int64_t>(&value)) {
        (*slot)[0] = static_cast<float>(*item);
        return std::isfinite((*slot)[0]);
    }
    if (const auto* item = std::get_if<double>(&value)) {
        (*slot)[0] = static_cast<float>(*item);
        return std::isfinite((*slot)[0]);
    }
    if (const auto* item = std::get_if<Vec2>(&value)) {
        (*slot)[0] = static_cast<float>(item->x);
        (*slot)[1] = static_cast<float>(item->y);
        return std::isfinite((*slot)[0]) && std::isfinite((*slot)[1]);
    }
    if (const auto* item = std::get_if<Vec3>(&value)) {
        (*slot)[0] = static_cast<float>(item->x);
        (*slot)[1] = static_cast<float>(item->y);
        (*slot)[2] = static_cast<float>(item->z);
        return std::isfinite((*slot)[0]) && std::isfinite((*slot)[1]) && std::isfinite((*slot)[2]);
    }
    if (const auto* item = std::get_if<Vec4>(&value)) {
        (*slot)[0] = static_cast<float>(item->x);
        (*slot)[1] = static_cast<float>(item->y);
        (*slot)[2] = static_cast<float>(item->z);
        (*slot)[3] = static_cast<float>(item->w);
        return std::isfinite((*slot)[0]) && std::isfinite((*slot)[1]) &&
               std::isfinite((*slot)[2]) && std::isfinite((*slot)[3]);
    }
    if (const auto* item = std::get_if<Color4>(&value)) {
        (*slot)[0] = static_cast<float>(item->r);
        (*slot)[1] = static_cast<float>(item->g);
        (*slot)[2] = static_cast<float>(item->b);
        (*slot)[3] = static_cast<float>(item->a);
        return std::isfinite((*slot)[0]) && std::isfinite((*slot)[1]) &&
               std::isfinite((*slot)[2]) && std::isfinite((*slot)[3]);
    }
    // string / assetReference are intentionally not copied to GPU constant data.
    return false;
}

} // namespace

bool MiaoGpuParameterPacker::Pack(
    const SceneRuntimeDefinition& definition,
    const MiaoSceneRuntime& runtime,
    MiaoGpuParameterBlock* block,
    std::wstring* error) {
    if (!block) return Fail(error, L"GPU parameter block output is null.");
    block->slots = {};
    if (definition.parameters.size() > MiaoGpuParameterBlock::kMaxSlots)
        return Fail(error, L"Shader Contract v1 supports at most 16 GPU parameters.");

    for (std::size_t i = 0; i < definition.parameters.size(); ++i) {
        const auto& parameter = definition.parameters[i];
        const auto* value = runtime.GetParameter(parameter.id);
        if (!value) return Fail(error, L"Runtime parameter is missing: " + parameter.id);
        if (parameter.type == PropertyType::String || parameter.type == PropertyType::AssetReference) {
            // Non-GPU parameter types still reserve their ABI slot, allowing later
            // parameters to keep stable positions when a schema mixes UI-only values.
            continue;
        }
        if (!WriteSlot(*value, &block->slots[i]))
            return Fail(error, L"Parameter cannot be packed into Shader Contract v1: " + parameter.id);
    }
    if (error) error->clear();
    return true;
}

bool MiaoGpuParameterPacker::SelfTest() {
    SceneDefinition scene;
    scene.id = L"scene://gpu-parameter-self-test";
    scene.kind = ContentKind::Wallpaper;
    scene.rootNodeId = L"node://root";
    SceneNodeDefinition root;
    root.id = L"node://root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform", ComponentKind::Transform,
        {PropertyDefinition{L"opacity", PropertyType::Float, 1.0}},
    });
    scene.nodes.push_back(std::move(root));

    SceneRuntimeDefinition definition;
    definition.scene = std::move(scene);
    definition.profile = RuntimeProfile::Wallpaper;
    definition.parameters.push_back(ParameterDefinition{L"param://speed", PropertyType::Float, 0.5});
    definition.parameters.push_back(ParameterDefinition{L"param://enabled", PropertyType::Bool, true});
    definition.parameters.push_back(ParameterDefinition{L"param://color", PropertyType::Color, Color4{0.1, 0.2, 0.3, 0.4}});

    MiaoSceneRuntime runtime;
    std::wstring error;
    if (!runtime.Initialize(definition, &error)) return false;
    if (!runtime.SetParameter(L"param://speed", 1.25, &error)) return false;

    MiaoGpuParameterBlock block;
    if (!Pack(definition, runtime, &block, &error)) return false;
    if (block.slots[0][0] != 1.25f) return false;
    if (block.slots[1][0] != 1.0f) return false;
    if (std::abs(block.slots[2][0] - 0.1f) > 0.0001f ||
        std::abs(block.slots[2][3] - 0.4f) > 0.0001f) return false;
    return true;
}

} // namespace miaodesk::content
