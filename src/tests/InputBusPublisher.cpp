// The Scene input bus publisher: analysis results land in a scene's InputBus only for
// channels the scene actually declares, and press/click are withheld while a wallpaper
// is click-through. Links against the real MiaoSceneRuntime, so the type gate in
// SetInput is exercised rather than assumed.
#include "miaodesk/MiaoInputBus.h"
#include "miaodesk/MiaoInputBusPublisher.h"
#include "miaodesk/MiaoSceneRuntime.h"

#include <cstdio>
#include <utility>
#include <string>
#include <vector>

using namespace miaodesk;
using namespace miaodesk::content;
namespace ib = miaodesk::content::inputbus;

int failures = 0;
void Check(bool c, const std::string& what) {
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", what.c_str());
    if (!c) ++failures;
}

// 通道类型必须由契约推导:一个声明成 Float 的边沿通道会拒绝 bool 写入,
// 这不是 bug,而是 SetInput 的类型闸在工作。
// 返回 (type, defaultValue):默认值的 variant 必须与声明类型一致,否则 Initialize 拒绝。
std::pair<PropertyType, PropertyValue> ChannelFor(std::wstring_view id) {
    ib::InputChannelShape shape{};
    if (ib::ChannelShape(id, &shape) && shape == ib::InputChannelShape::Float01)
        return {PropertyType::Float, 0.0};
    return {PropertyType::Bool, false};
}

PropertyType TypeFor(std::wstring_view id) { return ChannelFor(id).first; }

SceneRuntimeDefinition MakeScene(std::vector<std::wstring_view> inputs) {
    SceneRuntimeDefinition d;
    d.scene.id = L"scene://gate";
    d.scene.kind = ContentKind::Wallpaper;
    d.scene.rootNodeId = L"node://root";
    SceneNodeDefinition root;
    root.id = L"node://root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform", ComponentKind::Transform,
        {PropertyDefinition{L"opacity", PropertyType::Float, 1.0}}});
    d.scene.nodes.push_back(std::move(root));
    d.profile = RuntimeProfile::Wallpaper;
    for (auto id : inputs) { auto c = ChannelFor(id); d.inputs.push_back(InputChannelDefinition{std::wstring(id), c.first, c.second}); }
    return d;
}

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("InputBusPublisher\n");

    std::printf("\n1. 只写 scene 声明过的通道\n");
    {
        MiaoSceneRuntime rt;
        std::wstring err;
        Check(rt.Initialize(MakeScene({ib::kAudioBass, ib::kPointerX}), &err), "scene initializes");
        InputBusPublisher pub(rt);
        ib::AudioSpectrumFrame frame;
        frame.level = 0.9; frame.bands[0] = 0.5; frame.bands[2] = 0.7;
        auto w = pub.PublishAudio(frame);
        Check(w.any && w.ids.size() == 1, "only the declared bass channel is written (got " +
              std::to_string(w.ids.size()) + ")");
        const auto* v = rt.GetInput(ib::kAudioBass);
        Check(v && std::holds_alternative<double>(*v) && std::abs(std::get<double>(*v) - 0.5) < 1e-9,
              "bass value landed in the InputBus");
        Check(rt.GetInput(ib::kAudioLevel) == nullptr, "undeclared level is NOT written");
        Check(rt.GetInput(ib::kAudioMid) == nullptr, "undeclared mid is NOT written");

        ib::PointerNormalizer::Result p;
        p.x = 0.25; p.y = 0.75; p.inside = true;
        auto wp = pub.PublishPointer(p, true);
        Check(rt.GetInput(ib::kPointerX) != nullptr, "declared pointer/x is written");
        Check(rt.GetInput(ib::kPointerY) == nullptr, "undeclared pointer/y is NOT written");
    }

    std::printf("\n2. 节拍是沿,不是电平\n");
    {
        MiaoSceneRuntime rt;
        std::wstring err;
        rt.Initialize(MakeScene({ib::kAudioBeat}), &err);
        InputBusPublisher pub(rt);
        ib::AudioSpectrumFrame f;
        f.beat = true;
        pub.PublishAudio(f);
        const auto* v = rt.GetInput(ib::kAudioBeat);
        Check(v && std::holds_alternative<bool>(*v) && std::get_if<bool>(v) && *std::get_if<bool>(v), "beat true on the onset frame");
        ib::AudioSpectrumFrame g;
        g.beat = false;
        pub.PublishAudio(g);
        v = rt.GetInput(ib::kAudioBeat);
        Check(v && std::holds_alternative<bool>(*v) && std::get_if<bool>(v) && !*std::get_if<bool>(v),
              "beat cleared the next frame so the next onset is a real edge");
    }

    std::printf("\n3. interactive=false 时按压通道被扣留\n");
    {
        MiaoSceneRuntime rt;
        std::wstring err;
        rt.Initialize(MakeScene({ib::kPointerX, ib::kPointerInside, ib::kPointerDown, ib::kPointerClick}), &err);
        InputBusPublisher pub(rt);
        ib::PointerNormalizer::Result p;
        p.x = 0.5; p.y = 0.5; p.inside = true; p.down = true; p.clicked = true;
        auto w = pub.PublishPointer(p, false);
        Check(rt.GetInput(ib::kPointerX) != nullptr, "position still published");
        Check(rt.GetInput(ib::kPointerInside) != nullptr, "inside still published");
        Check([](const PropertyValue* v){ return v == nullptr || !std::get_if<bool>(v) || !*std::get_if<bool>(v); }(rt.GetInput(ib::kPointerDown)),
              "down is not fabricated when click-through is on");
        const auto* c = rt.GetInput(ib::kPointerClick);
        Check(c == nullptr || !std::get_if<bool>(c) || !*std::get_if<bool>(c), "click is not fabricated when click-through is on");
        Check(pub.LastError().empty(), "no error for withheld channels");
    }

    std::printf("\n4. interactive=true 时按压通道照发\n");
    {
        MiaoSceneRuntime rt;
        std::wstring err;
        rt.Initialize(MakeScene({ib::kPointerDown, ib::kPointerClick}), &err);
        InputBusPublisher pub(rt);
        ib::PointerNormalizer::Result p;
        p.inside = true; p.down = true; p.clicked = true;
        pub.PublishPointer(p, true);
        Check(rt.GetInput(ib::kPointerDown) && std::get_if<bool>(rt.GetInput(ib::kPointerDown)) && *std::get_if<bool>(rt.GetInput(ib::kPointerDown)), "down published");
        Check(rt.GetInput(ib::kPointerClick) && std::get_if<bool>(rt.GetInput(ib::kPointerClick)) && *std::get_if<bool>(rt.GetInput(ib::kPointerClick)), "click edge published");
        p.down = false; p.clicked = false;
        pub.PublishPointer(p, true);
        Check([](const PropertyValue* v){ return v == nullptr || !std::get_if<bool>(v) || !*std::get_if<bool>(v); }(rt.GetInput(ib::kPointerDown)), "down released");
        Check([](const PropertyValue* v){ return v == nullptr || !std::get_if<bool>(v) || !*std::get_if<bool>(v); }(rt.GetInput(ib::kPointerClick)), "click edge cleared");
    }

    std::printf("\n5. 通道契约覆盖发布器写的每一个 id\n");
    {
        MiaoSceneRuntime rt;
        std::wstring err;
        SceneRuntimeDefinition d = MakeScene({});
        for (const auto id : {ib::kAudioLevel, ib::kAudioBass, ib::kAudioLowMid, ib::kAudioMid,
                              ib::kAudioHighMid, ib::kAudioTreble, ib::kAudioBeat,
                              ib::kPointerX, ib::kPointerY, ib::kPointerInside,
                              ib::kPointerDown, ib::kPointerClick, ib::kPointerEnter, ib::kPointerLeave})
            { auto c = ChannelFor(id); d.inputs.push_back(InputChannelDefinition{std::wstring(id), c.first, c.second}); }
        rt.Initialize(std::move(d), &err);
        InputBusPublisher pub(rt);
        ib::AudioSpectrumFrame f;
        f.level = 1.0; f.beat = true;
        ib::PointerNormalizer::Result p;
        p.inside = true; p.down = true; p.clicked = true; p.entered = true; p.left = false;
        pub.PublishAudio(f);
        pub.PublishPointer(p, true);
        ib::InputChannelShape shape{};
        int known = 0, total = 0;
        for (const auto& input : rt.Definition()->inputs) {
            ++total;
            if (ib::ChannelShape(input.id, &shape)) ++known;
        }
        Check(known == total, "every channel the publisher can write is in the contract");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
