// SpriteRenderer 的 texture 契约:assetReference 类型 + 校验规则。
//
// 这是 P0-4(壁纸 dogfood)与 B-4(3D)共同卡住的那一块:此前 spriteRenderer 只有
// opacity/tint/cornerRadius/materialId,没有任何指向图片的属性,于是非 solidColor
// 材质被渲染器一律跳过 —— skill 做得出的东西天花板就是"纯色矩形"。
//
// 契约本身(props 里一个 assetReference)不需要新机制,MiaoAssetDatabase 也已经在
// 跟踪组件属性对资产的依赖。真正的新东西是"MISSING asset 引用名"这条强制:
// 少了它,一个拼错的 asset id 会通过校验,然后在渲染期变成一块空白矩形 —— 那读起来
// 像渲染器 bug,不像创作错误。
#include "miaodesk/MiaoSceneModel.h"

#include <cstdio>
#include <string>

using namespace miaodesk::content;

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// 一个最小合法场景:root + 一个带 SpriteRenderer 的节点。
static SceneDefinition Base() {
    SceneDefinition scene;
    scene.id = L"scene://sprite-texture";
    scene.kind = ContentKind::Wallpaper;
    scene.rootNodeId = L"node://root";

    SceneNodeDefinition root;
    root.id = L"node://root";
    root.name = L"Root";
    scene.nodes.push_back(root);

    SceneNodeDefinition sprite;
    sprite.id = L"node://sprite";
    sprite.name = L"Sprite";
    sprite.parentId = L"node://root";
    sprite.components.push_back(SceneComponentDefinition{
        L"component://sprite/renderer", ComponentKind::SpriteRenderer, {}});
    scene.nodes.push_back(sprite);
    return scene;
}

static SceneComponentDefinition& Sprite(SceneDefinition& scene) {
    for (auto& node : scene.nodes) {
        for (auto& component : node.components) {
            if (component.kind == ComponentKind::SpriteRenderer) return component;
        }
    }
    return scene.nodes.front().components.front();
}

static void AddTexture(SceneDefinition& scene, const std::wstring& assetId) {
    SceneComponentDefinition& sprite = Sprite(scene);
    sprite.properties.push_back(
        PropertyDefinition{L"texture", PropertyType::AssetReference, AssetReference{assetId}});
}

int wmain() {
    std::wstring error;

    std::printf("\n1. 没有 texture 属性:合法(纯色路径不受影响)\n");
    {
        const SceneDefinition scene = Base();
        Check(MiaoSceneModel::Validate(scene, &error),
              "无 texture 的 SpriteRenderer 仍然通过校验");
    }

    std::printf("\n2. texture 指向真实图片资产:合法\n");
    {
        SceneDefinition scene = Base();
        AssetDefinition asset;
        asset.id = L"asset://photo";
        asset.type = AssetType::Image;
        asset.source = L"assets/photo.png";
        scene.assets.push_back(asset);
        AddTexture(scene, L"asset://photo");
        Check(MiaoSceneModel::Validate(scene, &error), "指向 Image 资产 -> 通过");
        if (!error.empty()) std::printf("         (error = %ls)\n", error.c_str());
    }

    std::printf("\n3. 空 texture:合法(等于没给,退回纯色)\n");
    {
        SceneDefinition scene = Base();
        AddTexture(scene, L"");
        Check(MiaoSceneModel::Validate(scene, &error), "空引用 -> 通过");
    }

    std::printf("\n4. texture 指向不存在的资产:必须被拒\n");
    {
        SceneDefinition scene = Base();
        AddTexture(scene, L"asset://typo");
        const bool ok = MiaoSceneModel::Validate(scene, &error);
        Check(!ok, "缺失资产 -> 拒绝");
        // 报错必须点名是哪个 asset id,否则创作者无从下手。
        Check(error.find(L"asset://typo") != std::wstring::npos,
              "报错点名字出错的 asset id");
        std::printf("         (error = %ls)\n", error.c_str());
    }

    std::printf("\n5. texture 指向非图片资产:必须被拒\n");
    for (auto type : {AssetType::Video, AssetType::Audio, AssetType::Shader, AssetType::Mesh}) {
        SceneDefinition scene = Base();
        AssetDefinition asset;
        asset.id = L"asset://wrong";
        asset.type = type;
        asset.source = L"assets/wrong.bin";
        scene.assets.push_back(asset);
        AddTexture(scene, L"asset://wrong");
        const bool ok = MiaoSceneModel::Validate(scene, &error);
        Check(!ok, "非 Image 资产 -> 拒绝");
        if (ok) std::printf("         (type %d 竟然通过了)\n", static_cast<int>(type));
    }

    std::printf("\n6. 规则只作用于 SpriteRenderer\n");
    {
        // TextRenderer 上一个同名 texture 属性不该被这条规则管 —— 它有自己的用途。
        SceneDefinition scene = Base();
        AddTexture(scene, L"asset://missing");
        Sprite(scene).kind = ComponentKind::TextRenderer;
        Check(MiaoSceneModel::Validate(scene, &error),
              "同一属性在 TextRenderer 上不受 sprite 规则约束");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
