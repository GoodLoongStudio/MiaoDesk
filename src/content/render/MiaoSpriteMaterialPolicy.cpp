#include "miaodesk/MiaoSpriteMaterialPolicy.h"

#include <utility>
#include <vector>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool IsSolidColor(const MaterialDefinition& material) {
    return material.model == MaterialModel::Builtin && material.builtinName == L"solidColor";
}

} // namespace

bool ResolveSpriteDrawPath(
    const SpriteMaterialInput& input,
    bool backendHasShaderPath,
    const MaterialDefinition** resolvedMaterial,
    SpriteDrawPath* path,
    std::wstring* error) {
    if (resolvedMaterial) *resolvedMaterial = nullptr;
    if (path) *path = SpriteDrawPath::SolidColor;
    if (error) error->clear();

    const std::wstring& who = input.componentId.empty() ? L"(unnamed SpriteRenderer)" : input.componentId;

    // Which material this sprite draws with. Three cases, and the third one is why the
    // lookup moved in here: D2D used to apply a "first builtin in the scene" fallback
    // that D3D11 did not have, so a sprite with no materialId and no texture drew on one
    // backend and was refused by the other. Owning the lookup is what makes that one rule.
    const MaterialDefinition* material = nullptr;
    if (!input.materialId.empty()) {
        if (input.sceneMaterials) {
            for (const auto& candidate : *input.sceneMaterials)
                if (candidate.id == input.materialId) { material = &candidate; break; }
        }
        // An id naming nothing is an error whether or not a texture is present.
        if (material == nullptr)
            return Fail(error, L"SpriteRenderer material is missing from the package: " + input.materialId +
                       L" (component " + who + L")");
    } else if (input.sceneMaterials != nullptr && input.textureAssetId.empty()) {
        // No materialId *and* no texture: take the scene's first builtin. Deliberately
        // *not* "the first material" — a programmable material must never be picked
        // implicitly, since only one backend can run it.
        for (const auto& candidate : *input.sceneMaterials)
            if (candidate.model == MaterialModel::Builtin) { material = &candidate; break; }
    }
    if (resolvedMaterial) *resolvedMaterial = material;

    const bool hasTexture = !input.textureAssetId.empty();
    const bool programmable = material != nullptr && material->model == MaterialModel::Programmable;

    if (hasTexture) {
        // A programmable material and a sprite texture are two different ways to reach t0,
        // which a single pass cannot honour. Refusing beats choosing: choosing silently
        // drops the thing that was not chosen, and "content validates but draws the wrong
        // image" is the failure mode this project keeps paying for. `solidColor` composes
        // here (its colour multiplies into the tint), any other builtin does not.
        if (programmable)
            return Fail(error,
                L"SpriteRenderer on component " + who +
                L" sets both a programmable material and a texture assetReference. The D3D11 backend "
                L"binds a sprite texture to t0, which the material's own textures also use. "
                L"Keep one: drop materialId and keep texture, or drop texture and keep materialId.");
        if (material != nullptr && !IsSolidColor(*material))
            return Fail(error,
                L"SpriteRenderer material must be builtin solidColor when the sprite also sets a "
                L"texture (component " + who + L"): its colour multiplies the image, while any other "
                L"builtin would be asked for something and silently receive the texture instead.");
        if (path) *path = SpriteDrawPath::SpriteTexture;
        return true;
    }

    if (material == nullptr)
        return Fail(error,
            L"SpriteRenderer resolves no material and sets no texture (component " + who +
            L"): declare materialId, or set texture to an Image asset, so there is something to draw.");

    if (programmable) {
        if (!backendHasShaderPath)
            return Fail(error,
                L"SpriteRenderer on component " + who +
                L" resolves a programmable material, which this backend cannot execute: it has no "
                L"package-authored shader path. Draw this scene with the D3D11 backend, or declare a "
                L"texture or a builtin solidColor material.");
        if (path) *path = SpriteDrawPath::ProgrammableMaterial;
        return true;
    }

    if (!IsSolidColor(*material))
        return Fail(error,
            L"SpriteRenderer material must be builtin solidColor (component " + who +
            L"): the D2D and D3D11 MVP backends only implement that builtin.");

    if (path) *path = SpriteDrawPath::SolidColor;
    return true;
}

bool SpriteMaterialPolicySelfTest() {
    // Built fresh per case, and by value: a shared static here would make the table below
    // depend on evaluation order, which is precisely the kind of coupling this policy
    // exists to remove.
    const auto make = [](std::wstring id, MaterialModel model, std::wstring builtin) {
        MaterialDefinition definition;
        definition.id = std::move(id);
        definition.model = model;
        definition.builtinName = std::move(builtin);
        return definition;
    };

    const auto solid = make(L"material://m", MaterialModel::Builtin, L"solidColor");
    const auto gradient = make(L"material://m", MaterialModel::Builtin, L"gradient");
    const auto programmable = make(L"material://m", MaterialModel::Programmable, L"");
    // A scene whose first material is programmable. Only here to prove the fallback skips
    // it: picking a programmable material implicitly would make a package drawable on one
    // backend and not the other, decided by declaration order.
    const auto progFirst = make(L"material://second", MaterialModel::Programmable, L"");
    const auto solidSecond = make(L"material://second", MaterialModel::Builtin, L"solidColor");

    const std::vector<MaterialDefinition> solidScene{solid};
    const std::vector<MaterialDefinition> mixedScene{progFirst, solidSecond};
    const std::vector<MaterialDefinition> progScene{programmable};
    const std::vector<MaterialDefinition> gradientScene{gradient};

    std::wstring error;
    SpriteDrawPath path{};
    const MaterialDefinition* resolved = nullptr;

    // The shipped MiaoCloud shape: a texture and no material. Both backends must accept it.
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"", &solidScene, L"asset://a/image", L"c"}, false, nullptr, &path, &error))
        return false;
    if (path != SpriteDrawPath::SpriteTexture) return false;
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"", &solidScene, L"asset://a/image", L"c"}, true, nullptr, &path, &error))
        return false;
    if (path != SpriteDrawPath::SpriteTexture) return false;

    // The shipped widget shape: a named solidColor material and no texture.
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &solidScene, L"", L"c"}, false, nullptr, &path, &error))
        return false;
    if (path != SpriteDrawPath::SolidColor) return false;
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &solidScene, L"", L"c"}, true, nullptr, &path, &error))
        return false;
    if (path != SpriteDrawPath::SolidColor) return false;

    // Nothing at all.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"", nullptr, L"", L"c"}, true, nullptr, &path, &error)) return false;

    // An id that names no material.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://gone", &solidScene, L"", L"c"}, true, nullptr, &path, &error))
        return false;
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://gone", &solidScene, L"asset://a/image", L"c"}, true, nullptr, &path, &error))
        return false;

    // The programmable material is where `backendHasShaderPath` is allowed to matter —
    // and it is the only place. The scene has to be `progScene`: naming `material://m`
    // against `solidScene` resolves the *solid* material and is accepted, which says
    // nothing about programmable materials.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &progScene, L"", L"c"}, false, nullptr, &path, &error))
        return false;
    // With only a programmable material in the scene and no materialId, the fallback
    // refuses it rather than picking it.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"", &progScene, L"", L"c"}, true, nullptr, &path, &error))
        return false;
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &progScene, L"", L"c"}, true, nullptr, &path, &error))
        return false;
    if (path != SpriteDrawPath::ProgrammableMaterial) return false;

    // Both ways to reach t0: refused on both backends, so a package cannot load on one
    // and fail on the other.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &progScene, L"asset://a/image", L"c"}, false, nullptr, &path, &error))
        return false;
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &progScene, L"asset://a/image", L"c"}, true, nullptr, &path, &error))
        return false;

    // A non-solidColor builtin, with and without a texture.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &gradientScene, L"", L"c"}, true, nullptr, &path, &error))
        return false;
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &gradientScene, L"asset://a/image", L"c"}, true, nullptr, &path, &error))
        return false;

    // The shared fallback, and the reason it is shared: a scene whose first material is
    // programmable. Both backends must land on the *second* (solidColor) one, via the same
    // lookup, and must report it through `resolvedMaterial`.
    for (bool shaderPath : {false, true}) {
        if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"", &mixedScene, L"", L"c"}, shaderPath, &resolved, &path, &error))
            return false;
        if (path != SpriteDrawPath::SolidColor) return false;
        if (resolved == nullptr || resolved->id != L"material://second") return false;
    }
    // The fallback must not survive a texture either: with a texture the sprite needs no
    // material, and silently colouring it by the first builtin would be a surprise.
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"", &mixedScene, L"asset://a/image", L"c"}, true, &resolved, &path, &error))
        return false;
    if (path != SpriteDrawPath::SpriteTexture) return false;
    if (resolved != nullptr) return false;
    // Same for the solid-first scene, so the rule is about textures and not about ordering.
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"", &solidScene, L"asset://a/image", L"c"}, false, &resolved, &path, &error))
        return false;
    if (resolved != nullptr) return false;

    // The rule that matters: every input both backends accept resolves to the same path.
    const struct { std::wstring id; const std::vector<MaterialDefinition>* scene; std::wstring t; } accepted[] = {
        {L"", &solidScene, L"asset://a/image"},
        {L"", &solidScene, L""},
        {L"material://m", &solidScene, L""},
    };
    for (const auto& c : accepted) {
        SpriteDrawPath d2d{};
        SpriteDrawPath d3d{};
        std::wstring ignored;
        if (!ResolveSpriteDrawPath(SpriteMaterialInput{c.id, c.scene, c.t, L"c"}, false, nullptr, &d2d, &ignored)) return false;
        if (!ResolveSpriteDrawPath(SpriteMaterialInput{c.id, c.scene, c.t, L"c"}, true, nullptr, &d3d, &ignored)) return false;
        if (d2d != d3d) return false;
    }

    return true;
}

} // namespace miaodesk::content
