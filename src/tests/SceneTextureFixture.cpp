// Proves the textured-sprite scene *fixture* is well formed.
//
// Why this exists separately from the D2D SelfTest: the SelfTest that consumes this
// scene is Windows-only and needs a render target, so a typo in the fixture JSON would
// surface there as "the draw failed" with nothing pointing at the real cause. The
// fixture's shape is pure logic though — deserialization, validation and the asset
// reference are all platform-independent — so it can be pinned here, on every commit,
// for the cost of one process.
//
// What it actually protects: the scene deliberately declares "materials":[]. If a
// textured sprite still needed a solidColor material to reach the draw path, that scene
// would be invalid, and this test is what says so out loud.
#include "miaodesk/MiaoAssetDatabase.h"
#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include <cstdio>
#include <string>

using namespace miaodesk::content;

// Must stay byte-identical to the `texturedScene` literal in
// MiaoSceneD2DRenderer.cpp's SelfTest. scripts/verify-scene-fixture-parity.sh enforces
// that, because the two copies are otherwise free to drift — and the copy that drifts
// is always the one nobody runs.
constexpr std::string_view kTexturedScene = R"json({
  "schema":1,"id":"scene://selftest-textured","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
  "nodes":[
    {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
    {"id":"node://panel","name":"Panel","parentId":"node://root","enabled":true,"components":[
      {"id":"component://panel/transform","kind":"transform","properties":[
        {"name":"position","type":"vec2","default":[0.0,0.0]},
        {"name":"scale","type":"vec2","default":[1.0,1.0]},
        {"name":"rotation","type":"float","default":0.0},
        {"name":"opacity","type":"float","default":1.0}]},
      {"id":"component://panel/sprite","kind":"spriteRenderer","properties":[
        {"name":"opacity","type":"float","default":1.0},
        {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
        {"name":"cornerRadius","type":"float","default":0.0},
        {"name":"texture","type":"assetReference","default":"asset://panel/texture"}]}
    ]}
  ],
  "assets":[{"id":"asset://panel/texture","type":"image","source":"assets/panel.png"}],
  "shaders":[],"materials":[],
  "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
  "bindings":[],"animations":[]
})json";

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int wmain() {
    std::wstring error;

    std::printf("\n1. 贴图场景 fixture 能被反序列化\n");
    SceneRuntimeDefinition runtime;
    const bool parsed = MiaoSceneSerializer::Deserialize(kTexturedScene, "{\"schema\":1,\"parameters\":[]}", &runtime, &error);
    Check(parsed, "texturedScene 反序列化成功");
    if (!parsed) {
        std::printf("         (error = %ls)\n", error.c_str());
        std::printf("\nSOME CHECKS FAILED (%d failure(s))\n", failures + 1);
        return 1;
    }

    std::printf("\n2. 反序列化结果通过运行时校验\n");
    Check(MiaoSceneRuntimeModel::Validate(runtime, &error), "运行时校验通过");
    if (!error.empty()) std::printf("         (error = %ls)\n", error.c_str());

    std::printf("\n3. 场景模型本身也通过校验(两个校验器都必须接受)\n");
    Check(MiaoSceneModel::Validate(runtime.scene, &error), "MiaoSceneModel::Validate 通过");

    std::printf("\n4. 关键断言:没有 material 也能过\n");
    Check(runtime.materials.empty(), "fixture 的 materials 数组是空的");
    std::printf("         这一条是重点:如果贴图 sprite 还需要 solidColor 材质才能进绘制路径,\n");
    std::printf("         这个场景就是非法的,D2D SelfTest 会画不出来。\n");

    std::printf("\n5. profile 与 kind 匹配\n");
    Check(runtime.scene.kind == ContentKind::Wallpaper, "kind == wallpaper");
    Check(runtime.profile == RuntimeProfile::Wallpaper, "profile == wallpaper");

    std::printf("\n6. texture 属性读回一个 assetReference\n");
    const SceneComponentDefinition* sprite = nullptr;
    for (const auto& node : runtime.scene.nodes) {
        for (const auto& component : node.components) {
            if (component.kind == ComponentKind::SpriteRenderer) sprite = &component;
        }
    }
    Check(sprite != nullptr, "找到了 spriteRenderer 组件");
    if (sprite) {
        const PropertyDefinition* texture = nullptr;
        for (const auto& property : sprite->properties) {
            if (property.name == L"texture") texture = &property;
        }
        Check(texture != nullptr, "sprite 上有 texture 属性");
        if (texture) {
            // An assetReference default is a bare JSON string, not an object. Getting
            // this shape wrong is the single most likely fixture typo, and it would
            // deserialize into nothing rather than error.
            Check(texture->type == PropertyType::AssetReference, "texture 的类型是 assetReference");
            const auto* reference = std::get_if<AssetReference>(&texture->defaultValue);
            Check(reference != nullptr, "默认值的 variant 里存的是 AssetReference");
            if (reference) Check(reference->id == L"asset://panel/texture", "默认值解析出 asset://panel/texture");
        }
    }

    std::printf("\n7. 资产被声明成 image 且指向 assets/panel.png\n");
    Check(runtime.scene.assets.size() == 1, "恰好声明了一个资产");
    if (runtime.scene.assets.size() == 1) {
        Check(runtime.scene.assets.front().id == L"asset://panel/texture", "资产 id 与 texture 引用一致");
        Check(runtime.scene.assets.front().type == AssetType::Image, "资产类型是 Image");
        Check(runtime.scene.assets.front().source == L"assets/panel.png", "资产 source 是 assets/panel.png");
    }

    std::printf("\n8. texture 指向缺失资产时,同一个 fixture 会被拒\n");
    {
        // The difference between "the file is missing" and "the asset id is missing"
        // matters: MiaoAssetDatabase owns the former at load time, MiaoSceneModel owns
        // the latter at validation time. Both have to be checked somewhere; this is the
        // one the author sees before they hit the renderer.
        SceneRuntimeDefinition dangling = runtime;
        for (auto& node : dangling.scene.nodes) {
            for (auto& component : node.components) {
                for (auto& property : component.properties) {
                    if (property.name != L"texture") continue;
                    auto* reference = std::get_if<AssetReference>(&property.defaultValue);
                    if (reference) reference->id = L"asset://panel/typo";
                }
            }
        }
        const bool stillValid = MiaoSceneModel::Validate(dangling.scene, &error);
        Check(!stillValid, "缺失的 asset id 被拒");
        if (stillValid) {
            std::printf("         (通过了 —— 这正是会让渲染期变成空白矩形的那种拼写错误)\n");
        } else {
            Check(error.find(L"asset://panel/typo") != std::wstring::npos, "报错点名了那个拼错的 id");
            std::printf("         (error = %ls)\n", error.c_str());
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
