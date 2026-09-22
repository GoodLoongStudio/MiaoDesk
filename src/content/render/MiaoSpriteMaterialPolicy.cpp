#include "miaodesk/MiaoSpriteMaterialPolicy.h"

#include <utility>

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
    SpriteDrawPath* path,
    std::wstring* error) {
    if (path) *path = SpriteDrawPath::SolidColor;
    if (error) error->clear();

    const std::wstring& who = input.componentId.empty() ? L"(unnamed SpriteRenderer)" : input.componentId;

    // An id naming nothing is an error whether or not a texture is present.
    if (!input.materialId.empty() && input.material == nullptr)
        return Fail(error, L"SpriteRenderer material is missing from the package: " + input.materialId +
                   L" (component " + who + L")");

    const bool hasTexture = !input.textureAssetId.empty();
    const bool programmable = input.material != nullptr && input.material->model == MaterialModel::Programmable;

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
        if (input.material != nullptr && !IsSolidColor(*input.material))
            return Fail(error,
                L"SpriteRenderer material must be builtin solidColor when the sprite also sets a "
                L"texture (component " + who + L"): its colour multiplies the image, while any other "
                L"builtin would be asked for something and silently receive the texture instead.");
        if (path) *path = SpriteDrawPath::SpriteTexture;
        return true;
    }

    if (input.material == nullptr)
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

    if (!IsSolidColor(*input.material))
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
    const auto make = [](MaterialModel model, std::wstring builtin) {
        MaterialDefinition definition;
        definition.id = L"material://m";
        definition.model = model;
        definition.builtinName = std::move(builtin);
        return definition;
    };

    const MaterialDefinition solid = make(MaterialModel::Builtin, L"solidColor");
    const MaterialDefinition gradient = make(MaterialModel::Builtin, L"gradient");
    const MaterialDefinition programmable = make(MaterialModel::Programmable, L"");

    std::wstring error;
    SpriteDrawPath path{};

    // The shipped MiaoCloud shape: a texture and no material. Both backends must accept it.
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"", nullptr, L"asset://a/image", L"c"}, false, &path, &error))
        return false;
    if (path != SpriteDrawPath::SpriteTexture) return false;
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"", nullptr, L"asset://a/image", L"c"}, true, &path, &error))
        return false;
    if (path != SpriteDrawPath::SpriteTexture) return false;

    // The shipped widget shape: a solidColor material and no texture.
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &solid, L"", L"c"}, false, &path, &error))
        return false;
    if (path != SpriteDrawPath::SolidColor) return false;
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &solid, L"", L"c"}, true, &path, &error))
        return false;
    if (path != SpriteDrawPath::SolidColor) return false;

    // Nothing at all.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"", nullptr, L"", L"c"}, true, &path, &error)) return false;

    // An id that names no material.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://gone", nullptr, L"", L"c"}, true, &path, &error))
        return false;
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://gone", nullptr, L"asset://a/image", L"c"}, true, &path, &error))
        return false;

    // The programmable material is where `backendHasShaderPath` is allowed to matter —
    // and it is the only place.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &programmable, L"", L"c"}, false, &path, &error))
        return false;
    if (!ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &programmable, L"", L"c"}, true, &path, &error))
        return false;
    if (path != SpriteDrawPath::ProgrammableMaterial) return false;

    // Both ways to reach t0: refused on both backends, so a package cannot load on one
    // and fail on the other.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &programmable, L"asset://a/image", L"c"}, false, &path, &error))
        return false;
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &programmable, L"asset://a/image", L"c"}, true, &path, &error))
        return false;

    // A non-solidColor builtin, with and without a texture.
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &gradient, L"", L"c"}, true, &path, &error))
        return false;
    if (ResolveSpriteDrawPath(SpriteMaterialInput{L"material://m", &gradient, L"asset://a/image", L"c"}, true, &path, &error))
        return false;

    // The rule that matters: every input both backends accept resolves to the same path.
    const struct { std::wstring id; const MaterialDefinition* m; std::wstring t; } accepted[] = {
        {L"", nullptr, L"asset://a/image"},
        {L"material://m", &solid, L"asset://a/image"},
        {L"material://m", &solid, L""},
    };
    for (const auto& c : accepted) {
        SpriteDrawPath d2d{};
        SpriteDrawPath d3d{};
        std::wstring ignored;
        if (!ResolveSpriteDrawPath(SpriteMaterialInput{c.id, c.m, c.t, L"c"}, false, &d2d, &ignored)) return false;
        if (!ResolveSpriteDrawPath(SpriteMaterialInput{c.id, c.m, c.t, L"c"}, true, &d3d, &ignored)) return false;
        if (d2d != d3d) return false;
    }

    return true;
}

} // namespace miaodesk::content
