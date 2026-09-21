// Binding response end to end: JSON round trip through the real serializer, real
// evaluation through MiaoSceneRuntime and InputBusPublisher, and backward
// compatibility for scene.json written before the response field existed.
#include "miaodesk/MiaoInputBus.h"
#include "miaodesk/MiaoInputBusPublisher.h"
#include "miaodesk/MiaoSceneRuntime.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace miaodesk;
using namespace miaodesk::content;
namespace ib = miaodesk::content::inputbus;

int failures = 0;
void Check(bool c, const std::string& w) {
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", w.c_str());
    if (!c) ++failures;
}

SceneRuntimeDefinition Build() {
    SceneRuntimeDefinition d;
    d.scene.id = L"scene://e2e";
    d.scene.kind = ContentKind::Wallpaper;
    d.scene.rootNodeId = L"node://root";
    SceneNodeDefinition root;
    root.id = L"node://root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform", ComponentKind::Transform,
        {PropertyDefinition{L"opacity", PropertyType::Float, 1.0}}});
    d.scene.nodes.push_back(std::move(root));
    d.profile = RuntimeProfile::Wallpaper;
    d.parameters.push_back(ParameterDefinition{L"param://level", PropertyType::Float, 0.0});
    d.inputs.push_back(InputChannelDefinition{std::wstring(ib::kAudioBass), PropertyType::Float, 0.0});
    d.inputs.push_back(InputChannelDefinition{std::wstring(ib::kAudioBeat), PropertyType::Bool, false});
    // 声明式音频驱动:bass 经 sqrt 抬升后驱动 opacity
    d.bindings.push_back(PropertyBindingDefinition{
        L"binding://bass-opacity",
        PropertyAddress{L"component://root/transform", L"opacity"},
        BindingSourceKind::Input,
        std::wstring(ib::kAudioBass),
        1.0, 0.0, BindingResponse::SquareRoot, 0.05});
    return d;
}

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Binding response end-to-end\n");

    std::printf("\n1. 序列化 -> 反序列化 往返\n");
    SceneRuntimeDefinition original = Build();
    std::string sceneJson;
    std::wstring err;
    Check(MiaoSceneSerializer::SerializeScene(original, &sceneJson, &err), "SerializeScene succeeds");
    Check(sceneJson.find("\"response\":\"sqrt\"") != std::string::npos, "response key emitted");
    Check(sceneJson.find("\"deadzone\":0.05") != std::string::npos, "deadzone emitted");

    SceneRuntimeDefinition parsed;
    Check(MiaoSceneSerializer::Deserialize(sceneJson, "", &parsed, &err), "Deserialize succeeds");
    Check(parsed.bindings.size() == 1, "one binding survives the round trip");
    if (parsed.bindings.size() == 1) {
        Check(parsed.bindings[0].response == BindingResponse::SquareRoot, "response survives");
        Check(std::abs(parsed.bindings[0].deadzone - 0.05) < 1e-9, "deadzone survives");
        Check(parsed.bindings[0].sourceId == ib::kAudioBass, "sourceId survives");
    }

    std::printf("\n2. 再序列化必须逐字节稳定\n");
    std::string again;
    Check(MiaoSceneSerializer::SerializeScene(parsed, &again, &err), "re-serialize succeeds");
    Check(again == sceneJson, "serialize(parse(serialize(x))) == serialize(x)");

    std::printf("\n3. 真实求值:sqrt 抬升 + deadzone 抑制静默\n");
    {
        MiaoSceneRuntime rt;
        Check(rt.Initialize(Build(), &err), "runtime initializes");
        InputBusPublisher pub(rt);
        ib::AudioSpectrumFrame f;
        f.bands[0] = 0.25;   // deadzone 0.05 -> (0.25-0.05)/0.95 = 0.2105 -> sqrt = 0.4588
        pub.PublishAudio(f);
        const auto* v = rt.GetProperty(PropertyAddress{L"component://root/transform", L"opacity"});
        Check(v && std::holds_alternative<double>(*v), "opacity written");
        const double got = v ? std::get<double>(*v) : -1;
        const double expect = std::sqrt((0.25 - 0.05) / 0.95);
        Check(std::abs(got - expect) < 1e-9,
              "sqrt response + deadzone applied (got " + std::to_string(got) +
                  " expect " + std::to_string(expect) + ")");
        Check(std::abs(got - 0.25) > 1e-3, "and it is NOT the raw value (curve actually shapes)");

        // deadzone 之下必须是 0
        ib::AudioSpectrumFrame quiet;
        quiet.bands[0] = 0.03;
        pub.PublishAudio(quiet);
        v = rt.GetProperty(PropertyAddress{L"component://root/transform", L"opacity"});
        Check(v && std::get<double>(*v) == 0.0, "below-deadzone maps to exactly 0");
    }

    std::printf("\n4. Linear 是默认值,旧包行为不变\n");
    {
        SceneRuntimeDefinition d = Build();
        PropertyBindingDefinition& b = d.bindings[0];
        b.response = BindingResponse::Linear;
        b.deadzone = 0.0;
        std::string json;
        Check(MiaoSceneSerializer::SerializeScene(d, &json, &err), "serialize linear binding");
        SceneRuntimeDefinition back;
        Check(MiaoSceneSerializer::Deserialize(json, "", &back, &err), "parse it back");
        Check(back.bindings[0].response == BindingResponse::Linear, "response defaults to linear");
        MiaoSceneRuntime rt;
        rt.Initialize(d, &err);
        InputBusPublisher pub(rt);
        ib::AudioSpectrumFrame f; f.bands[0] = 0.4;
        pub.PublishAudio(f);
        const auto* v = rt.GetProperty(PropertyAddress{L"component://root/transform", L"opacity"});
        Check(v && std::abs(std::get<double>(*v) - 0.4) < 1e-12,
              "linear passes the value through unchanged");
    }

    std::printf("\n5. 缺省 response 字段的旧 JSON 仍能解析\n");
    {
        // 手写一份没有 response / deadzone 的旧格式 binding(扁平结构 + assets 必填)
        const std::string legacy =
            "{\"schema\":1,\"id\":\"scene://legacy\",\"kind\":\"wallpaper\",\"profile\":\"wallpaper\","
            "\"rootNodeId\":\"node://root\","
            "\"nodes\":[{\"id\":\"node://root\",\"components\":[{\"id\":\"component://root/transform\","
            "\"kind\":\"transform\",\"properties\":[{\"name\":\"opacity\",\"type\":\"float\",\"default\":1.0}]}]}],"
            "\"assets\":[],\"shaders\":[],\"materials\":[],"
            "\"inputs\":[{\"id\":\"input://audio/bass\",\"type\":\"float\",\"default\":0.0}],"
            "\"bindings\":[{\"id\":\"binding://b\",\"sourceKind\":\"input\",\"sourceId\":\"input://audio/bass\","
            "\"target\":{\"componentId\":\"component://root/transform\",\"propertyName\":\"opacity\"},"
            "\"scale\":2.0,\"offset\":0.0}],"
            "\"animations\":[],\"particleEmitters\":[],\"postProcesses\":[]}";
        SceneRuntimeDefinition d;
        Check(MiaoSceneSerializer::Deserialize(legacy, "", &d, &err), "legacy JSON without response parses");
        if (d.bindings.size() == 1) {
            Check(d.bindings[0].response == BindingResponse::Linear, "absent response defaults to linear");
            Check(d.bindings[0].deadzone == 0.0, "absent deadzone defaults to 0");
            MiaoSceneRuntime rt;
            rt.Initialize(d, &err);
            InputBusPublisher pub(rt);
            ib::AudioSpectrumFrame f; f.bands[0] = 0.3;
            pub.PublishAudio(f);
            const auto* v = rt.GetProperty(PropertyAddress{L"component://root/transform", L"opacity"});
            Check(v && std::abs(std::get<double>(*v) - 0.6) < 1e-12,
                  "legacy scale*value still applies (0.3*2.0=0.6)");
        }
    }

    std::printf("\n6. 非法 response / deadzone 必须被拒\n");
    {
        auto tryJson = [&](const std::string& bind, const char* what) {
            const std::string j =
                "{\"scene\":{\"id\":\"scene://x\",\"kind\":\"wallpaper\",\"rootNodeId\":\"node://root\","
                "\"nodes\":[{\"id\":\"node://root\",\"components\":[{\"id\":\"component://root/transform\","
                "\"kind\":\"transform\",\"properties\":[{\"name\":\"opacity\",\"type\":\"float\",\"default\":1.0}]}]}]},"
                "\"profile\":\"wallpaper\",\"parameters\":[],"
                "\"inputs\":[{\"id\":\"input://audio/bass\",\"type\":\"float\",\"default\":0.0}],"
                "\"bindings\":[" + bind + "],"
                "\"animations\":[],\"particleEmitters\":[],\"materials\":[],\"postProcesses\":[],\"shaders\":[]}";
            SceneRuntimeDefinition d;
            std::wstring e;
            const bool ok = MiaoSceneSerializer::Deserialize(j, "", &d, &e);
            Check(!ok, what);
        };
        const std::string head =
            "{\"id\":\"binding://b\",\"sourceKind\":\"input\",\"sourceId\":\"input://audio/bass\","
            "\"target\":{\"componentId\":\"component://root/transform\",\"propertyName\":\"opacity\"}";
        tryJson(head + ",\"scale\":1.0,\"offset\":0.0,\"response\":\"wobble\"}", "unknown response rejected");
        tryJson(head + ",\"scale\":1.0,\"offset\":0.0,\"response\":\"Linear\"}", "uppercase response rejected");
        tryJson(head + ",\"scale\":1.0,\"offset\":0.0,\"deadzone\":1.0}", "deadzone 1.0 rejected");
        tryJson(head + ",\"scale\":1.0,\"offset\":0.0,\"deadzone\":-0.1}", "negative deadzone rejected");
        tryJson(head + ",\"scale\":1.0,\"offset\":0.0,\"deadzone\":2.0}", "deadzone 2.0 rejected");

        // 非 float 源上用 response 必须被拒
        tryJson("{\"id\":\"binding://b\",\"sourceKind\":\"input\",\"sourceId\":\"input://beat\","
                "\"target\":{\"componentId\":\"component://root/transform\",\"propertyName\":\"opacity\"},"
                "\"scale\":1.0,\"offset\":0.0,\"response\":\"square\"}",
                "response on a bool source rejected");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
