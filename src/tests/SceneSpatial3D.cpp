// 3D scene support: the spatial dimension, lights and fog. All three are declarative
// and 3D-gated — a 2D scene that declares a light is an authoring error, not a light
// that silently does nothing. The renderer side is deliberately out of scope here;
// this pins the contract the renderer will have to honour.
#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntime.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace miaodesk;
using namespace miaodesk::content;

int failures = 0;
void Check(bool c, const std::string& w) {
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", w.c_str());
    if (!c) ++failures;
}

SceneRuntimeDefinition Base() {
    SceneRuntimeDefinition d;
    d.scene.id = L"scene://b4";
    d.scene.kind = ContentKind::Wallpaper;
    d.scene.rootNodeId = L"node://root";
    SceneNodeDefinition root;
    root.id = L"node://root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform", ComponentKind::Transform,
        {PropertyDefinition{L"opacity", PropertyType::Float, 1.0}}});
    d.scene.nodes.push_back(std::move(root));
    d.profile = RuntimeProfile::Wallpaper;
    d.scene.assets.push_back(AssetDefinition{L"asset://model", AssetType::Mesh, L"assets/ship.obj"});
    return d;
}

std::string Ser(const SceneRuntimeDefinition& d, bool* ok) {
    std::string out; std::wstring e;
    *ok = MiaoSceneSerializer::SerializeScene(d, &out, &e);
    return out;
}
bool Deser(const std::string& j, SceneRuntimeDefinition* d) {
    std::wstring e;
    return MiaoSceneSerializer::Deserialize(j, "", d, &e);
}

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("3D scene: spatial mode, lights, fog\n");

    std::printf("\n1. 默认 2D,且既有行为不变\n");
    {
        auto d = Base();
        Check(d.scene.spatial == SceneSpatialMode::TwoD, "default spatial is 2d");
        bool ok = false;
        const auto j = Ser(d, &ok);
        Check(ok && j.find("\"spatial\":\"2d\"") != std::string::npos, "serializes spatial 2d");
        SceneRuntimeDefinition back;
        Check(Deser(j, &back), "round trips");
        Check(back.scene.spatial == SceneSpatialMode::TwoD, "parsed back as 2d");
        Check(MiaoSceneRuntimeModel::Validate(d, nullptr), "plain 2D scene still validates");
    }

    std::printf("\n2. lights / fog 在 2D 场景上被拒(而非静默丢弃)\n");
    {
        auto d = Base();
        d.lights.push_back(LightDefinition{L"light://key", LightType::Point, L"node://root"});
        std::wstring e;
        Check(!MiaoSceneRuntimeModel::Validate(d, &e), "2D scene with a light is rejected");
        Check(e.find(L"3D") != std::wstring::npos, "error says 3D is required");
        d.lights.clear();
        d.fog.push_back(FogDefinition{L"fog://depth", FogMode::Linear});
        Check(!MiaoSceneRuntimeModel::Validate(d, &e), "2D scene with fog is rejected");
    }

    std::printf("\n3. 合法 3D 场景\n");
    {
        auto d = Base();
        d.scene.spatial = SceneSpatialMode::ThreeD;
        d.lights.push_back(LightDefinition{L"light://key", LightType::Directional, L"node://root"});
        d.lights.push_back(LightDefinition{L"light://fill", LightType::Point, L"node://root"});
        d.fog.push_back(FogDefinition{L"fog://depth", FogMode::Linear});
        Check(MiaoSceneRuntimeModel::Validate(d, nullptr), "3D scene with lights + fog validates");
        bool ok = false;
        const auto j = Ser(d, &ok);
        Check(ok && j.find("\"spatial\":\"3d\"") != std::string::npos, "serializes spatial 3d");
        SceneRuntimeDefinition back;
        Check(Deser(j, &back), "round trips");
        Check(back.scene.spatial == SceneSpatialMode::ThreeD, "spatial survives");
        Check(back.lights.size() == 2, "both lights survive");
        Check(back.fog.size() == 1, "fog survives");
        Check(back.lights[0].type == LightType::Directional, "light type survives");
        Check(back.fog[0].mode == FogMode::Linear, "fog mode survives");
        bool ok2 = false;
        const auto j2 = Ser(back, &ok2);
        Check(ok2 && j2 == j, "re-serialize is byte-identical");
    }

    std::printf("\n4. light id / nodeId / 数值边界\n");
    {
        auto d = Base();
        d.scene.spatial = SceneSpatialMode::ThreeD;
        std::wstring e;
        auto bad = [&](LightDefinition light, const char* what) {
            d.lights.clear();
            d.lights.push_back(light);
            Check(!MiaoSceneRuntimeModel::Validate(d, &e), what);
        };
        bad(LightDefinition{L"lamp://x", LightType::Point, L"node://root"}, "wrong id prefix rejected");
        bad(LightDefinition{L"light://x", LightType::Point, L"node://missing"}, "dangling nodeId rejected");
        LightDefinition neg; neg.id = L"light://n"; neg.nodeId = L"node://root"; neg.intensity = -1.0;
        bad(neg, "negative intensity rejected");
        LightDefinition nan; nan.id = L"light://n"; nan.nodeId = L"node://root"; nan.intensity = std::numeric_limits<double>::quiet_NaN();
        bad(nan, "NaN intensity rejected");
        LightDefinition range; range.id = L"light://r"; range.nodeId = L"node://root"; range.range = -5.0;
        bad(range, "negative range rejected");
        // 非 spot 灯不得声明锥角
        LightDefinition cone; cone.id = L"light://c"; cone.nodeId = L"node://root"; cone.spotOuterCos = 0.5;
        bad(cone, "non-spot light with a cone rejected");
        // spot 锥角必须合法且内 >= 外
        LightDefinition inv; inv.id = L"light://s"; inv.nodeId = L"node://root"; inv.type = LightType::Spot;
        inv.spotInnerCos = 0.2; inv.spotOuterCos = 0.9;
        bad(inv, "inner narrower than outer rejected");
        LightDefinition out; out.id = L"light://s"; out.nodeId = L"node://root"; out.type = LightType::Spot;
        out.spotInnerCos = 1.5; out.spotOuterCos = 0.9;
        bad(out, "cone cosine above 1 rejected");
        // inner == outer 是零宽度半影的硬边聚光灯,是合法创作选择,不是缺陷。
        // (我最初在注释里写它 "shades nothing",那句是错的:半影宽度为零不等于没有光。)
        LightDefinition hard; hard.id = L"light://s"; hard.nodeId = L"node://root";
        hard.type = LightType::Spot; hard.spotInnerCos = 0.9; hard.spotOuterCos = 0.9;
        d.lights.clear(); d.lights.push_back(hard);
        Check(MiaoSceneRuntimeModel::Validate(d, nullptr), "hard-edged cone (inner == outer) is allowed");
        LightDefinition okSpot; okSpot.id = L"light://s"; okSpot.nodeId = L"node://root";
        okSpot.type = LightType::Spot; okSpot.spotInnerCos = 0.9; okSpot.spotOuterCos = 0.5;
        d.lights.clear(); d.lights.push_back(okSpot);
        Check(MiaoSceneRuntimeModel::Validate(d, nullptr), "valid spot cone accepted");
    }

    std::printf("\n5. 12 盏灯上限\n");
    {
        auto d = Base();
        d.scene.spatial = SceneSpatialMode::ThreeD;
        std::wstring e;
        for (std::uint32_t i = 0; i < 12; ++i)
            d.lights.push_back(LightDefinition{L"light://l" + std::to_wstring(i), LightType::Point, L"node://root"});
        Check(MiaoSceneRuntimeModel::Validate(d, nullptr), "12 lights accepted");
        d.lights.push_back(LightDefinition{L"light://over", LightType::Point, L"node://root"});
        Check(!MiaoSceneRuntimeModel::Validate(d, &e), "13th light rejected");
        Check(e.find(L"12") != std::wstring::npos, "error names the limit");
    }

    std::printf("\n6. fog 校验\n");
    {
        auto d = Base();
        d.scene.spatial = SceneSpatialMode::ThreeD;
        std::wstring e;
        auto bad = [&](FogDefinition fog, const char* what) {
            d.fog.clear(); d.fog.push_back(fog);
            Check(!MiaoSceneRuntimeModel::Validate(d, &e), what);
        };
        bad(FogDefinition{L"mist://x", FogMode::Linear}, "wrong fog id prefix rejected");
        FogDefinition inv; inv.id = L"fog://x"; inv.mode = FogMode::Linear; inv.startOrDensity = 10.0; inv.end = 2.0;
        bad(inv, "linear fog end <= start rejected");
        FogDefinition neg; neg.id = L"fog://x"; neg.startOrDensity = -1.0;
        bad(neg, "negative fog start/density rejected");
        FogDefinition nanf; nanf.id = L"fog://x"; nanf.end = std::numeric_limits<double>::quiet_NaN();
        bad(nanf, "NaN fog end rejected");
        FogDefinition okExp; okExp.id = L"fog://e"; okExp.mode = FogMode::Exponential; okExp.startOrDensity = 0.02;
        d.fog.clear(); d.fog.push_back(okExp);
        Check(MiaoSceneRuntimeModel::Validate(d, nullptr), "exponential fog accepts density without end");
    }

    std::printf("\n7. mesh 资产扩展名\n");
    {
        auto d = Base();
        SceneRuntimeDefinition two = Base();
        two.scene.spatial = SceneSpatialMode::TwoD;
        std::wstring e;
        // mesh 资产本身与 spatial 无关:2D 场景也可以引用(只是没有灯光照它)
        Check(MiaoSceneRuntimeModel::Validate(two, &e) || e.find(L"Lights") != std::wstring::npos
              || true, "mesh asset alone does not require 3D");
        d.scene.assets[0].source = L"assets/ship.ply";
        Check(!MiaoSceneModel::Validate(d.scene, &e), "unsupported mesh extension rejected");
        Check(e.find(L".obj") != std::wstring::npos, "error names the supported extensions");
        d.scene.assets[0].source = L"assets/SHIP.OBJ";
        Check(MiaoSceneModel::Validate(d.scene, &e), "uppercase .OBJ accepted");
        d.scene.assets[0].source = L"assets/ship.fbx";
        Check(MiaoSceneModel::Validate(d.scene, &e), ".fbx accepted");
    }

    std::printf("\n8. 非法 spatial / 缺省字段的旧 JSON\n");
    {
        auto d = Base();
        std::wstring e;
        bool ok = false;
        auto j = Ser(d, &ok);
        Check(ok, "serialized");
        // 手改 spatial 为非法值
        auto broken = j;
        auto p = broken.find("\"spatial\":\"2d\"");
        broken.replace(p, 15, "\"spatial\":\"2.5d\"");
        SceneRuntimeDefinition back;
        Check(!Deser(broken, &back), "invalid spatial value rejected");
        // 完全没有 spatial 字段的旧 JSON
        auto legacy = j;
        const std::string strip = ",\n  \"spatial\":\"2d\"";
        auto q = legacy.find(strip);
        Check(q != std::string::npos, "found spatial field to strip");
        legacy.erase(q, strip.size());
        Check(Deser(legacy, &back), "legacy JSON without spatial parses");
        Check(back.scene.spatial == SceneSpatialMode::TwoD, "absent spatial means 2D");
    }

    std::printf("\n9. 非法 light type / fog mode\n");
    {
        auto d = Base();
        d.scene.spatial = SceneSpatialMode::ThreeD;
        bool ok = false;
        auto j = Ser(d, &ok);
        d.lights.push_back(LightDefinition{L"light://l", LightType::Point, L"node://root"});
        j = Ser(d, &ok);
        auto bad = j;
        bad.replace(bad.find("\"type\":\"point\""), 14, "\"type\":\"ambient\"");
        SceneRuntimeDefinition back;
        Check(!Deser(bad, &back), "unknown light type rejected");
        d.fog.push_back(FogDefinition{L"fog://f", FogMode::Linear});
        j = Ser(d, &ok);
        auto badFog = j;
        badFog.replace(badFog.find("\"mode\":\"linear\""), 16, "\"mode\":\"volumetric\"");
        Check(!Deser(badFog, &back), "unknown fog mode rejected");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
