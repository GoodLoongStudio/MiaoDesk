#pragma once

// One rule for how a SpriteRenderer reaches a texture, shared by both render backends.
//
// Why this exists: the D2D and D3D11 backends each arrived at their own way of deciding,
// and the decisions disagreed. The concrete case that exposed it: MiaoCloud's five layer
// sprites declare a `texture` and **no** `materialId`. D2D drew them (a textured sprite
// needs no material there). The D3D11 backend resolved the material first and reported
// "Scene SpriteRenderer does not resolve a material" — so the only fully populated
// wallpaper in the repository loaded on one backend and failed on the other, and the
// difference would only surface on a machine running the D3D11 path.
//
// A second, quieter disagreement: a sprite with a programmable material and *no* texture
// is skipped by D2D (which then fails at scene level with "no D2D-renderable component")
// and by D3D11 with a message naming the material. Neither message names the component,
// so neither tells the author what to change.
//
// The two backends differ in exactly one respect — whether they have a shader path at
// all — so that is the only input that varies. Everything else is one rule, stated once
// here and exercised by src/tests/SpriteMaterialPolicy.cpp on every machine, not only
// on the ones that can open a D3D11 device.
//
// Non-goals: tint (animatable, and per-frame), the actual resource binding, and the
// content-model validation. Tint on a textured sprite is refused by the D2D draw path
// and honoured by D3D11's shader, which is a documented divergence: D2D has no way to
// colour a bitmap in one pass, and D3D11 has a shader constant.

#include <string>

#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

// What the sprite will actually be drawn as. Chosen here so both backends draw the same
// thing for the same content, and so a test can assert the choice without a GPU.
enum class SpriteDrawPath {
    // The builtin solidColor material, with no texture sampled.
    SolidColor,
    // The sprite's own `texture` assetReference, sampled at t0.
    SpriteTexture,
    // A programmable material owns t0 and brings its own shader and textures.
    ProgrammableMaterial,
};

struct SpriteMaterialInput {
    std::wstring materialId;
    // The scene's materials in declaration order. Only consulted when `materialId` is
    // empty, to apply one fallback rule both backends share (see the comment on
    // `resolvedMaterial` below). Null when the caller already resolved it.
    const std::vector<MaterialDefinition>* sceneMaterials{};
    // Empty when the sprite declares no texture.
    std::wstring textureAssetId;
    // The component being decided for. Only used to make diagnostics name something.
    std::wstring componentId;
};

// Decides the draw path, or refuses the sprite with a message that names the component.
//
// `backendHasShaderPath` is the one legitimate difference between the backends: D3D11 can
// run a package-authored pixel shader, D2D cannot. It is passed rather than inferred so
// the rule itself stays identical on both sides.
//
// `resolvedMaterial` receives the material to draw with, which is not always the one the
// caller looked up: when the sprite declares no `materialId` at all, both backends apply
// the same fallback of taking the scene's first builtin material. That fallback used to
// live in D2D's ResolveMaterial only, so a sprite with a material-bearing scene but no
// materialId and no texture drew on D2D and was refused by D3D11 — the same
// one-backend-only failure as MiaoCloud. Moving the fallback here is what makes it one
// rule. May be null (a textured sprite needs no material).
bool ResolveSpriteDrawPath(
    const SpriteMaterialInput& input,
    bool backendHasShaderPath,
    const MaterialDefinition** resolvedMaterial,
    SpriteDrawPath* path,
    std::wstring* error);

bool SpriteMaterialPolicySelfTest();

} // namespace miaodesk::content
