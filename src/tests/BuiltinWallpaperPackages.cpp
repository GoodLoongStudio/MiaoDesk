// Walks the three shipped `.mdwall` packages through the chain that can run without
// Windows: package load → scene deserialize → runtime validate → runtime initialize →
// asset database build.
//
// Why this test exists. P0-4 (壁纸 dogfood) was recorded as blocked by one thing — the
// render contract had no textured-sprite path — and that has since been implemented.
// Walking the real packages here surfaced a second, harder blocker that the TODO had no
// line for: NeonCity and MysticMoon declare five image layers each in scene.ini, and
// **neither package contains any asset file**. They never have: git has no record of
// `assets/wallpapers/NeonCity.mdwall/assets/*` or the MysticMoon equivalent, and the
// filenames appear nowhere else in the tree. Only MiaoCloud actually ships its five
// images.
//
// No amount of renderer work closes that. So this test pins both halves as facts rather
// than discoveries: which packages have a populated scene.json, and which ones' asset
// references all resolve to real files on disk.
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
    std::size_t emitterCount{};
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

    SceneRuntimeDefinition definition;
    if (!MiaoSceneSerializer::DeserializePackage(package, &definition, &out.error)) return out;
    out.deserialized = true;

    if (!MiaoSceneRuntimeModel::Validate(definition, &out.error)) return out;
    out.runtimeValid = true;
    out.nodeCount = definition.scene.nodes.size();
    out.assetCount = definition.scene.assets.size();

    for (const auto& node : definition.scene.nodes) {
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
    // keeps the answer independent of the cwd, and it deliberately avoids
    // fs::current_path()/GetCurrentDirectory — verify-path-layout-contract.ps1 rejects
    // any native source that depends on the working directory.
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
        int expectedNodes;      // 0 means "still an empty shell"
        bool expectsAssets;
    };
    const Spec specs[] = {
        // MiaoCloud is the only package that ships its art, so it is the only one that
        // can be migrated to scene.json. Its scene.json is authored with all five
        // layers, each a SpriteRenderer naming a real Image asset.
        {"MiaoCloud", 6, true},
        // These two declare five image layers in scene.ini and own no images at all.
        // Asserting the current state keeps that from being rediscovered as a surprise
        // later — and keeps anyone from "finishing" P0-4 by editing scene.json alone.
        {"NeonCity", 1, false},
        {"MysticMoon", 1, false},
    };

    for (const auto& spec : specs) {
        const fs::path root = wallpapers / (std::string(spec.name) + ".mdwall");
        std::printf("\n%s\n", spec.name);
        const PackageOutcome out = Walk(root);

        Check(out.loaded, "manifest.json 与入口文件通过 MiaoContentPackage::Load");
        if (!out.loaded) {
            std::printf("         (error = %ls)\n", out.error.c_str());
            continue;
        }
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
                  "节点数符合预期(1 root + 5 图层)");
        } else {
            Check(out.nodeCount == 1, "仍是空壳(只有 root)");
        }
        if (spec.expectsAssets) {
            Check(out.assetCount == 5, "scene.json 声明了 5 个 Image 资产");
            Check(out.resolvedCount == 5, "5 个资产全部解析到真实文件");
            Check(out.texturedSpriteCount == 5, "5 个 sprite 都带 texture 资产引用");
            // Recorded gaps, not passes. MiaoCloud's scene.ini drives six analytic
            // animations and three particle emitters; the scene.json carries neither
            // yet. Asserting the count pins the state in both directions — it stops the
            // gap from silently growing, and it stops someone reading 0 as "correct".
            Check(out.animationCount == 0,
                  "[记录在案的缺口] 动画尚未迁移:scene.ini 是解析式正弦,场景动画是线性关键帧轨");
            Check(out.emitterCount == 0,
                  "[记录在案的缺口] 粒子尚未迁移:scene.ini [Particles] 的三个发射器未映射");
        } else {
            Check(out.assetCount == 0, "scene.json 尚未声明任何资产(原因见本文件开头)");
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
