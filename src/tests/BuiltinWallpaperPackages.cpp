// Walks the three shipped `.mdwall` packages through the chain that can run without
// Windows: package load → scene deserialize → runtime validate → runtime initialize →
// asset database build.
//
// Why this test exists. P0-4 (壁纸 dogfood) was recorded as blocked by one thing — the
// render contract had no textured-sprite path — and that has since been implemented.
// Walking the real packages here surfaced a second, harder blocker that the TODO had no
// line for: NeonCity and MysticMoon declare five image layers each in scene.ini, and
// **neither package contained any asset file**. They never had: git had no record of
// `assets/wallpapers/NeonCity.mdwall/assets/*` or the MysticMoon equivalent, and the
// filenames appeared nowhere else in the tree. Only MiaoCloud actually shipped its five
// images.
//
// Both halves were then closed, and in the opposite order to the one assumed:
// the art **did** exist — it was in the working tree, uncommitted and unverified, because
// the generator's --check had never once run to completion (it died on a silent
// configparser path at HEAD, and on a missing stream method after that). With the gate
// fixed, the ten files reproduce byte-for-byte from their generator. That left the real
// work: generating the scene.json both packages were missing.
//
// So this test now pins the *migrated* state — all three packages with a populated
// scene.json whose asset references resolve — rather than the empty shells it started
// from. The node counts below are derived, not rounded.
#include "miaodesk/MiaoAnalyticParticleField.h"
#include "miaodesk/MiaoAssetDatabase.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"
#include "miaodesk/MiaoSceneRuntime.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace miaodesk::content;
namespace fs = std::filesystem;

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

struct PackageOutcome {
    bool loaded{};
    bool deserialized{};
    bool runtimeValid{};
    bool assetsResolve{};
    bool initialized{};
    std::size_t nodeCount{};
    std::size_t assetCount{};
    std::size_t resolvedCount{};
    std::size_t texturedSpriteCount{};
    std::size_t animationCount{};
    std::size_t axisYNodeCount{};
    std::size_t emitterCount{};
    std::vector<ParticleEmitterDefinition> emitters;
    fs::path entry;
    std::wstring error;
};

// Everything here is the same chain MiaoSceneD2DRenderer::Load runs, minus the parts
// that need a render target. That is the point: this is the half of the renderer that
// can be verified on every commit rather than on one Windows machine.
static PackageOutcome Walk(const fs::path& root) {
    PackageOutcome out;
    LoadedMiaoContentPackage package;
    if (!MiaoContentPackage::Load(root, &package, &out.error)) return out;
    out.loaded = true;
    out.entry = package.manifest.entry;

    SceneRuntimeDefinition definition;
    if (!MiaoSceneSerializer::DeserializePackage(package, &definition, &out.error)) return out;
    out.deserialized = true;

    if (!MiaoSceneRuntimeModel::Validate(definition, &out.error)) return out;
    out.runtimeValid = true;
    out.nodeCount = definition.scene.nodes.size();
    out.assetCount = definition.scene.assets.size();

    for (const auto& node : definition.scene.nodes) {
        // Counted here rather than derived from expectedNodes, because the number is
        // composed at generation time and differs per package: it is however many
        // layers move y on a different frequency than x. Printing it derived means the
        // message cannot go stale the way a hardcoded "3" did the moment a second
        // package with four such layers existed.
        if (node.id.find(L"/axis-y") != std::wstring::npos) ++out.axisYNodeCount;
        for (const auto& component : node.components) {
            if (component.kind != ComponentKind::SpriteRenderer) continue;
            for (const auto& property : component.properties) {
                if (property.name != L"texture") continue;
                if (const auto* reference = std::get_if<AssetReference>(&property.defaultValue)) {
                    if (!reference->id.empty()) ++out.texturedSpriteCount;
                }
            }
        }
    }

    MiaoAssetDatabase assets;
    if (!assets.Build(root, definition, &out.error)) return out;
    out.assetsResolve = true;
    out.resolvedCount = assets.Size();
    out.animationCount = definition.animations.size();
    out.emitterCount = definition.particleEmitters.size();
    out.emitters = definition.particleEmitters;

    // The last link of the chain the D2D renderer runs. If Initialize fails, the
    // renderer's Load fails, and the host falls back to a built-in wallpaper — so this
    // is the step that decides whether any of the above is reachable at run time.
    MiaoSceneRuntime runtime;
    if (!runtime.Initialize(definition, &out.error)) return out;
    out.initialized = true;
    return out;
}

static fs::path FindWallpapersDir() {
    // Walk up from this file until a directory holds assets/wallpapers. CMake always
    // passes an absolute path to the compiler, so __FILE__ is absolute on CI; walking up
    // keeps the answer independent of the process's working directory.
    //
    // The working-directory query APIs are deliberately never spelled out in this file —
    // not here, not in any comment. verify-path-layout-contract.ps1 matches their call
    // forms as raw text over the whole file, so naming one inside a comment trips a gate
    // from a line that never executes. That is exactly what happened the first time this
    // function was written, and the gate was right to complain: a comment is still text
    // in the tree. The lesson belongs in scripts/verify-native-source-hygiene.sh, which
    // runs that check locally, not in a comment that would re-trigger it.
    fs::path here = __FILE__;
    for (std::size_t depth = 0; depth < 6 && !here.empty(); ++depth) {
        const fs::path candidate = here.parent_path().parent_path().parent_path() / "assets" / "wallpapers";
        std::error_code ec;  // error_code& is an lvalue reference, so this cannot be a temporary
        if (fs::exists(candidate, ec)) return candidate;
        here = here.parent_path();
    }
    return {};
}

int wmain() {
    const fs::path wallpapers = FindWallpapersDir();
    if (wallpapers.empty()) {
        std::printf("\n[FAIL] 找不到 assets/wallpapers 目录(从 %s 向上找了 6 层)\n", __FILE__);
        return 1;
    }
    std::printf("\n壁纸包目录:%ls\n", wallpapers.wstring().c_str());

    struct Spec {
        const char* name;
        int expectedNodes;      // 1 root + N layer nodes + M "axis-y" parents
        bool expectsAssets;
        std::size_t expectedEmitters;  // analytic particle fields from [Particles]
    };
    const Spec specs[] = {
        // All three builtin wallpapers now ship their art and a populated canonical
        // scene.json. The old scene.ini files live only under tests/fixtures as frozen
        // migration evidence; they are not runtime package content anymore.
        //
        // Node counts are not "layers + 1". A layer whose y-axis moves at a
        // different frequency than its x-axis gets an extra parent node: one
        // animation track drives one whole property, and `position` is a vec2 with
        // no .x/.y addressing, so a Lissajous cannot be expressed as a single track.
        // The two axes are composed by the parent-chain transform multiply instead.
        // The frequency multipliers are the renderer's own: drift 0.77, sway 0.81,
        // breathe 1.0 (LayeredSceneRenderer.h:229-244).
        //
        // So: 9 = 1 root + 5 layers + 3 parents. Three of MiaoCloud's five layers
        // (cloud_pedestal / tail / cat) animate y; `background` animates nothing.
        {"MiaoCloud", 9, true, 3},
        // 10 = 1 root + 5 layers + 4 parents. city_glow is `breathe` (y + scale), and
        // haze / rain_far / rain_near are `drift`; `background` animates nothing.
        // Four animated layers, four parents.
        {"NeonCity", 10, true, 1},
        // 10 = 1 root + 5 layers + 4 parents. moon_glow is `breathe`, water_glow and
        // fog are `drift`, and fireflies is `float`. `float` is **not** a fifth
        // animation kind — scene.ini's `float` and `drift` share one branch in
        // LayeredSceneRenderer.h:231-233 and move identically — so nothing new had to
        // be migrated for it.
        {"MysticMoon", 10, true, 1},
    };

    for (const auto& spec : specs) {
        const fs::path root = wallpapers / (std::string(spec.name) + ".mdwall");
        std::printf("\n%s\n", spec.name);
        std::error_code legacyError;
        const bool legacyIniExists = fs::exists(root / "scene.ini", legacyError);
        Check(!legacyError && !legacyIniExists,
              "发货包不再携带 legacy scene.ini，只保留 canonical Scene Runtime");

        const PackageOutcome out = Walk(root);

        Check(out.loaded, "manifest.json 与入口文件通过 MiaoContentPackage::Load");
        if (!out.loaded) {
            std::printf("         (error = %ls)\n", out.error.c_str());
            continue;
        }
        Check(out.entry == fs::path("scene.json"),
              "manifest canonical runtime entry 是 scene.json");
        Check(out.deserialized, "scene.json 通过 MiaoSceneSerializer::DeserializePackage");
        Check(out.runtimeValid, "反序列化结果通过 MiaoSceneRuntimeModel::Validate");
        if (!out.runtimeValid) {
            std::printf("         (error = %ls)\n", out.error.c_str());
            continue;
        }
        Check(out.assetsResolve, "MiaoAssetDatabase::Build 解析出全部资产(文件都在磁盘上)");
        if (!out.assetsResolve) {
            std::printf("         (error = %ls)\n", out.error.c_str());
        }
        Check(out.initialized, "MiaoSceneRuntime::Initialize 通过");
        if (!out.initialized) {
            std::printf("         (error = %ls)\n", out.error.c_str());
        }

        if (spec.expectedNodes > 0) {
            Check(out.nodeCount == static_cast<std::size_t>(spec.expectedNodes),
                  "节点数符合预期");
            std::printf("         组成 = 1 root + %zu 图层 + %zu 个 axis-y 父节点"
                        "(期望 %d 个)\n",
                        out.nodeCount - 1 - out.axisYNodeCount, out.axisYNodeCount,
                        spec.expectedNodes);
        } else {
            Check(out.nodeCount == 1, "仍是空壳(只有 root)");
        }
        if (spec.expectsAssets) {
            Check(out.assetCount == 5, "scene.json 声明了 5 个 Image 资产");
            Check(out.resolvedCount == 5, "5 个资产全部解析到真实文件");
            Check(out.texturedSpriteCount == 5, "5 个 sprite 都带 texture 资产引用");
            // The animation and particle counts below pin the migrated Scene Runtime
            // representation. Formula-level fidelity is checked separately against the
            // frozen legacy fixtures; this executable test checks the shipped package.
            //
            // 8 tracks each, but reached by different arithmetic per package — this is
            // pinned as a count precisely so that arithmetic cannot drift silently:
            //   MiaoCloud : drift(cloud_pedestal)=2, sway(tail)=3, breathe(cat)=2,
            //               blink(blink)=1                     -> 8
            //   NeonCity  : breathe(city_glow)=2, drift(haze/rain_far/rain_near)=2*3 -> 8
            //   MysticMoon: breathe(moon_glow)=2, drift(water_glow/fog)=2*2,
            //               float(fireflies)=2                  -> 8
            // `background` is animation=none in all three and contributes none.
            // Note `float` is not a fifth kind: it shares one branch with `drift` in
            // LayeredSceneRenderer.h:231-233 and moves identically.
            //
            // Fidelity is not asserted here — it is asserted, in pixels, by
            // scripts/verify-builtin-wallpaper-animation-parity.py, which samples the engine's
            // own easing/local-time code against the legacy analytic formulas (and today
            // only covers MiaoCloud). This assertion only pins that the migration exists
            // and did not silently lose a layer: 0 would mean "never migrated", and a
            // count that stops matching the arithmetic above means scene.ini and the
            // generator drifted apart.
            Check(out.animationCount == 8,
                  "动画迁移保持为 8 条关键帧轨(旧公式基准保存在 tests/fixtures)");
            // Particles are migrated as of 2026-09-23. The old assertion pinned
            // `emitterCount == 0` with the reason "[Particles] carries only counts,
            // there is no declarative per-particle source" — and that reason was
            // right about *simulated* emitters and wrong about these particles:
            // the legacy sparkles / comet trail / petals are a **stateless analytic
            // field**, evaluated from the index every frame, which needs a different
            // emitter *model*, not more fields. MiaoAnalyticParticleField.cpp holds
            // that model once for both render backends, and the generator declares
            // only what scene.ini exposed (counts, opacities, comet speed).
            //
            // Pinned per package, because the counts differ and a future package's
            // [Particles] will too: 0 would mean the migration silently vanished.
            Check(out.emitterCount == spec.expectedEmitters,
                  "legacy [Particles] 已迁成声明式解析场发射器");
            for (const auto& emitter : out.emitters) {
                if (!IsAnalyticEmitterMode(emitter.mode)) {
                    Check(false, "particle emitter mode is analytic, not simulated");
                }
            }
        } else {
            Check(out.assetCount == 0, "scene.json 尚未声明任何资产(原因见本文件开头)");
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
