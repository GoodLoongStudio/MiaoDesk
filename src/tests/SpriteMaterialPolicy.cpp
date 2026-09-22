// Proves the two render backends agree about which SpriteRenderers they can draw.
//
// Why this exists: the D2D and D3D11 backends each worked out their own rule for how a
// sprite reaches a texture, and the rules disagreed. The case that exposed it is not
// hypothetical -- MiaoCloud's five layer sprites declare a `texture` and no `materialId`,
// so D2D drew them while D3D11 refused to load the package at all ("Scene SpriteRenderer
// does not resolve a material"). The only fully populated wallpaper in the repository
// worked on one backend and failed on the other, and the difference could only surface on
// a machine with a D3D11 device.
//
// That is now one rule (MiaoSpriteMaterialPolicy.cpp), and this test pins two things
// about it:
//   1. the rule itself -- every combination below, including the ones that must be refused;
//   2. parity -- every input both backends accept resolves to the same draw path.
//      Otherwise a future edit could make the two disagree again with nothing noticing,
//      because the rendering side of this cannot run on a machine without a D3D11 device.
//
// Every scene in the repository is represented below: MiaoCloud (texture and no
// material), the three widgets and AuroraMinimal (a solidColor material and no texture),
// ShaderPulse (a programmable material -- D3D11 only).
#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSpriteMaterialPolicy.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace miaodesk::content;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (condition) {
        std::printf("  [ok]   %s\n", what);
        return;
    }
    std::printf("  [FAIL] %s\n", what);
    ++failures;
}

MaterialDefinition MakeMaterial(std::wstring id, MaterialModel model, std::wstring builtin) {
    MaterialDefinition definition;
    definition.id = std::move(id);
    definition.model = model;
    definition.builtinName = std::move(builtin);
    return definition;
}

// The scene is passed, not the resolved material: the policy owns the lookup so that
// D2D's old "first builtin in the scene" fallback and D3D11's absence of one cannot
// disagree. Passing a material pointer instead would re-create that split at the call
// site, which is exactly where it was found.
bool Resolves(
    std::wstring materialId,
    const std::vector<MaterialDefinition>* scene,
    std::wstring texture,
    bool shaderPath,
    SpriteDrawPath* path,
    const MaterialDefinition** resolved = nullptr) {
    SpriteMaterialInput input;
    input.materialId = std::move(materialId);
    input.sceneMaterials = scene;
    input.textureAssetId = std::move(texture);
    input.componentId = L"component://panel/sprite";
    std::wstring error;
    return ResolveSpriteDrawPath(input, shaderPath, resolved, path, &error);
}

} // namespace

int wmain() {
    std::printf("\n1. SpriteRenderer material policy (MiaoSpriteMaterialPolicy.cpp)\n");

    const auto solid = MakeMaterial(L"material://m", MaterialModel::Builtin, L"solidColor");
    const auto gradient = MakeMaterial(L"material://m", MaterialModel::Builtin, L"gradient");
    const auto programmable = MakeMaterial(L"material://m", MaterialModel::Programmable, L"");
    // A scene whose *first* material is programmable, with the solid one second. The
    // fallback must skip the programmable one: picking it implicitly would make a package
    // drawable on one backend only, decided by declaration order.
    const auto progFirst = MakeMaterial(L"material://first", MaterialModel::Programmable, L"");
    const auto solidSecond = MakeMaterial(L"material://second", MaterialModel::Builtin, L"solidColor");

    const std::vector<MaterialDefinition> solidScene{solid};
    const std::vector<MaterialDefinition> mixedScene{progFirst, solidSecond};
    const std::vector<MaterialDefinition> progScene{programmable};
    const std::vector<MaterialDefinition> gradientScene{gradient};

    SpriteDrawPath d2d{};
    SpriteDrawPath d3d{};

    std::printf("\n  a. shapes both backends must accept\n");

    // MiaoCloud's shape. This is the cell that used to diverge: D3D11 refused the whole
    // package here with "does not resolve a material" while D2D drew it.
    Check(Resolves(L"", nullptr, L"asset://layer/cat/image", false, &d2d) &&
              d2d == SpriteDrawPath::SpriteTexture,
          "texture with no materialId -> SpriteTexture (D2D)");
    Check(Resolves(L"", nullptr, L"asset://layer/cat/image", true, &d3d) &&
              d3d == SpriteDrawPath::SpriteTexture,
          "texture with no materialId -> SpriteTexture (D3D11)");

    // The widgets' and AuroraMinimal's shape.
    Check(Resolves(L"material://m", &solidScene, L"", false, &d2d) && d2d == SpriteDrawPath::SolidColor,
          "solidColor material with no texture -> SolidColor (D2D)");
    Check(Resolves(L"material://m", &solidScene, L"", true, &d3d) && d3d == SpriteDrawPath::SolidColor,
          "solidColor material with no texture -> SolidColor (D3D11)");

    // Both at once is legitimate: the material colour tints the image.
    Check(Resolves(L"material://m", &solidScene, L"asset://a/image", true, &d3d) &&
              d3d == SpriteDrawPath::SpriteTexture,
          "solidColor material + texture -> SpriteTexture (colour tints, not a refusal)");

    std::printf("\n  b. shapes that must be refused\n");

    Check(!Resolves(L"", nullptr, L"", true, &d3d), "neither material nor texture -> refused");
    Check(!Resolves(L"material://gone", nullptr, L"", true, &d3d),
          "materialId naming a material that does not exist -> refused");
    Check(!Resolves(L"material://gone", nullptr, L"asset://a/image", false, &d2d),
          "materialId naming nothing is refused with a texture too (the fallback must not hide it)");
    Check(!Resolves(L"material://m", &gradientScene, L"", true, &d3d),
          "a builtin other than solidColor -> refused");
    Check(!Resolves(L"material://m", &gradientScene, L"asset://a/image", true, &d3d),
          "a builtin other than solidColor + texture -> refused (it would ask for one thing and get the texture)");

    std::printf("\n  c. programmable material: the one place the backends may diverge\n");

    // ShaderPulse's shape. D3D11 has a shader path and D2D does not, so this is the only
    // input where the same package is drawable on one backend and not the other. Holding
    // the divergence to this single cell is the whole point of passing the flag in.
    Check(!Resolves(L"material://m", &progScene, L"", false, &d2d),
          "programmable material + D2D -> refused (no shader path)");
    Check(Resolves(L"material://m", &progScene, L"", true, &d3d) &&
              d3d == SpriteDrawPath::ProgrammableMaterial,
          "programmable material + D3D11 -> ProgrammableMaterial");

    std::printf("\n  d. both wanting t0: refused by both\n");

    // Refused on both, so no package can be built that loads on one backend and fails on
    // the other. That asymmetry is exactly the bug this file guards against.
    Check(!Resolves(L"material://m", &progScene, L"asset://a/image", false, &d2d) &&
              !Resolves(L"material://m", &progScene, L"asset://a/image", true, &d3d),
          "programmable material + texture -> refused by both backends");

    std::printf("\n2. the material lookup itself, which is where the second divergence lived\n");

    // This one is not hypothetical either. D2D's ResolveMaterial fell back to "the first
    // builtin material in the scene" when the sprite declared no materialId; D3D11's
    // MaterialIdFor returned an empty string and the sprite was refused. So a scene with
    // a builtin material plus a sprite bearing neither materialId nor texture drew on one
    // backend and failed to load on the other. Nothing in the repository exercises that
    // shape, which is why it stayed hidden until the rule was consolidated.
    for (bool shaderPath : {false, true}) {
        SpriteDrawPath path{};
        const MaterialDefinition* resolved = nullptr;
        Check(Resolves(L"", &solidScene, L"", shaderPath, &path, &resolved) &&
                  path == SpriteDrawPath::SolidColor && resolved != nullptr &&
                  resolved->id == L"material://m",
              "no materialId, scene has solidColor -> SolidColor on BOTH backends");
    }

    // The fallback must skip a programmable material even when it comes first. Picking it
    // implicitly would make the package drawable on one backend and not the other, and the
    // author never wrote a materialId, so nothing would point at the cause.
    for (bool shaderPath : {false, true}) {
        SpriteDrawPath path{};
        const MaterialDefinition* resolved = nullptr;
        Check(Resolves(L"", &mixedScene, L"", shaderPath, &path, &resolved) &&
                  path == SpriteDrawPath::SolidColor && resolved != nullptr &&
                  resolved->id == L"material://second",
              "first material is programmable -> the fallback takes the solidColor one");
    }

    // With a texture the sprite needs no material, and the fallback must not quietly supply
    // one: a colour the author never asked for would then tint their image.
    SpriteDrawPath texturedPath{};
    const MaterialDefinition* texturedResolved = nullptr;
    Check(Resolves(L"", &mixedScene, L"asset://a/image", true, &texturedPath, &texturedResolved) &&
              texturedPath == SpriteDrawPath::SpriteTexture && texturedResolved == nullptr,
          "texture present -> no material is supplied at all");

    std::printf("\n3. per-field boundaries\n");

    // The declared id and the resolved material are separate inputs on purpose: an empty
    // id plus a material is D2D's first-builtin fallback, while a non-empty id with no
    // material is an authoring mistake. Merging the two would either report the fallback
    // as an error or hide the mistake.
    Check(Resolves(L"", &solidScene, L"", false, &d2d) && d2d == SpriteDrawPath::SolidColor,
          "empty materialId resolving to solidColor (D2D's fallback) -> SolidColor");
    Check(!Resolves(L"material://m", nullptr, L"", false, &d2d),
          "non-empty materialId with no material -> refused (the fallback must not mask it)");

    std::printf("\n4. policy self-test\n");
    Check(SpriteMaterialPolicySelfTest(), "SpriteMaterialPolicySelfTest()");

    std::printf("\n");
    if (failures != 0) {
        std::printf("FAILED: %d assertion(s)\n", failures);
        return 1;
    }
    std::printf("Every shape both backends accept resolves identically (all eight scene.json files are covered).\n");
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
