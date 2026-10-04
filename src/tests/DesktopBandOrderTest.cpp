// STAB-02 / P0-05「Explorer restart 恢复」的层级契约回归。
//
// 逮到的问题不是"它会算错",而是**它在本机一行都验不到**:判定住在
// `DesktopShellHost.cpp`(要 `<windows.h>`),而它有两个方向都用户看得见的失败形态 ——
// 恒说"已经有序"则修复永远不跑(Explorer 重启后壁纸盖住图标、组件点不动);
// 恒说"需要修"则每一跳都重发 z-order,DWM 每秒重组整条带子,用户看到壁纸闪。
// 代码注释里那句 "reads as wallpaper flicker" 说的正是后者。
#include "miaodesk/MiaoDesktopBandOrder.h"

#include <cstdio>
#include <string>
#include <vector>

namespace miaodesk {
namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL  %s\n", what.c_str());
    }
}

// 造一个满足契约的 raised 带子:widget(非 layered)→ icon layer → wallpaper(layered+transparent)
std::vector<DesktopBandSurface> RaisedHealthy() {
    std::vector<DesktopBandSurface> v(3);
    v[0] = {DesktopBandRole::Widget, true, false, false, true};
    v[1] = {DesktopBandRole::IconLayer, true, false, false, true};
    v[2] = {DesktopBandRole::Wallpaper, true, true, true, true};
    return v;
}

// legacy 模式:widget 必须 layered
std::vector<DesktopBandSurface> LegacyHealthy() {
    std::vector<DesktopBandSurface> v(2);
    v[0] = {DesktopBandRole::Widget, true, true, true, true};
    v[1] = {DesktopBandRole::Wallpaper, true, true, true, true};
    return v;
}

// 反空洞自检。两个方向各喂一个:一个明知该修(壁纸被改坏了),
// 一个明知不该修(healthy 的带子)。恒 true 会让修复永远不跑,
// 恒 false 会让 DWM 每秒重组整条带子。
bool VerdictStillMoves() {
    auto broken = RaisedHealthy();
    broken[2].isTransparent = false;
    return DesktopBandOrderSatisfied(RaisedHealthy(), DesktopBandMode::Raised) &&
           !DesktopBandOrderSatisfied(broken, DesktopBandMode::Raised);
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:层级判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:健康的带子说有序、改坏的说需要修\n");
    ++g_checks;

    // ---- 1. 两种模式各自 healthy 的带子都说有序 ----
    {
        Check(DesktopBandOrderSatisfied(RaisedHealthy(), DesktopBandMode::Raised),
              "raised 下 widget 非 layered + widget 在前 + 壁纸 layered&transparent → 有序");
        Check(DesktopBandOrderSatisfied(LegacyHealthy(), DesktopBandMode::Legacy),
              "legacy 下 widget layered + 壁纸 layered&transparent → 有序");
        Check(DesktopBandOrderSatisfied({}, DesktopBandMode::Raised),
              "空带子算有序(没什么可违反的;修一个空带子只会空转)");
    }

    // ---- 2. 主要失败形态一:修不好 → 壁纸/组件坏掉且不再自己好 ----
    {
        // 壁纸层不再 transparent:它要么挡点击,要么看起来不对。
        auto s = RaisedHealthy(); s[2].isTransparent = false;
        Check(!DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
              "壁纸层不 transparent → 需要修(否则挡点击或显示不对)");
        // 壁纸层不再 layered:raised 模式下壁纸必须是 layered 表面。
        s = RaisedHealthy(); s[2].isLayered = false;
        Check(!DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
              "壁纸层不 layered → 需要修");
        // 组件排在壁纸之后:用户点不到组件。
        s = RaisedHealthy(); std::swap(s[0], s[2]);
        Check(!DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
              "组件排在壁纸之后 → 需要修(用户点不到组件)");
        // 图标层排在壁纸之后:壁纸盖住桌面图标。
        s = RaisedHealthy(); std::swap(s[1], s[2]);
        Check(!DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
              "图标层排在壁纸之后 → 需要修(壁纸盖住桌面图标)");
        // 下面两条**只有规则 3 能逮到** —— 图标层在壁纸层之前,所以规则 4 是满意的;
        // 壁纸层样式也没坏。把它们单独拎出来,是因为第一版测试在这里有缺口:
        // 变异检测把"不看组件先后顺序"整条删掉之后依然全绿,原因就是每一个乱序用例
        // 都被别的规则顺手挡住了。一个被邻条掩护的规则等于没有规则。
        std::vector<DesktopBandSurface> widgetAfterIcon(3);
        widgetAfterIcon[0] = {DesktopBandRole::IconLayer, true, false, false, true};
        widgetAfterIcon[1] = {DesktopBandRole::Widget, true, false, false, true};
        widgetAfterIcon[2] = {DesktopBandRole::Wallpaper, true, true, true, true};
        Check(!DesktopBandOrderSatisfied(widgetAfterIcon, DesktopBandMode::Raised),
              "raised 下组件排在图标层之后 → 需要修(只有组件先后这一条能逮到)");
        std::vector<DesktopBandSurface> widgetAfterWallpaper(2);
        widgetAfterWallpaper[0] = {DesktopBandRole::Wallpaper, true, true, true, true};
        widgetAfterWallpaper[1] = {DesktopBandRole::Widget, true, true, true, true};
        Check(!DesktopBandOrderSatisfied(widgetAfterWallpaper, DesktopBandMode::Legacy),
              "legacy 下组件排在壁纸层之后 → 需要修(只有组件先后这一条能逮到)");
        // 根本不是子窗口:它压根不在桌面上。
        s = RaisedHealthy(); s[0].isChild = false;
        Check(!DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
              "表面不是 WS_CHILD → 需要修(它压根不在桌面上)");
    }

    // ---- 3. 主要失败形态二:永远要修 → 壁纸闪 ----
    // 这一半反过来:一个 healthy 的带子必须被判"有序",否则每一跳都重发
    // style/z-order,DWM 每秒重组整条带子 —— 注释里的 reads as wallpaper flicker。
    {
        Check(DesktopBandOrderSatisfied(RaisedHealthy(), DesktopBandMode::Raised),
              "healthy 的 raised 带子必须判有序(否则每跳都修,壁纸闪)");
        Check(DesktopBandOrderSatisfied(LegacyHealthy(), DesktopBandMode::Legacy),
              "healthy 的 legacy 带子必须判有序");
        // 反复调用结论必须稳定:幂等。第一次说有序,第二次也必须说有序 ——
        // 否则宿主会陷入"修了还说没修好"的循环。
        for (int i = 0; i < 3; ++i) {
            Check(DesktopBandOrderSatisfied(RaisedHealthy(), DesktopBandMode::Raised),
                  "反复判定同一个 healthy 带子结论稳定(幂等)");
        }
    }

    // ---- 4. 模式决定组件的 layered 契约 ----
    // raised 下组件必须**不** layered,legacy 下必须 layered —— 同一个样式
    // 在两种模式下的结论相反。这不是矛盾,是两代 WorkerW 的契约不同。
    {
        auto s = RaisedHealthy();
        Check(DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
              "raised + widget 非 layered → 有序");
        Check(!DesktopBandOrderSatisfied(s, DesktopBandMode::Legacy),
              "同一带子换到 legacy → 需要修(legacy 下 widget 必须 layered)");

        auto t = LegacyHealthy();
        Check(DesktopBandOrderSatisfied(t, DesktopBandMode::Legacy),
              "legacy + widget layered → 有序");
        Check(!DesktopBandOrderSatisfied(t, DesktopBandMode::Raised),
              "同一带子换到 raised → 需要修(raised 下 widget 必须非 layered)");
    }

    // ---- 5. 不可见的壁纸层不参与判定 ----
    // 收集阶段就滤掉它(`role != Widget && !IsWindowVisible` 则跳过)。
    // 这是规则而非省略:隐藏中的壁纸层不该让整条带子被判"需要修",
    // 否则每次隐藏/显示都触发一轮 z-order 重排。
    {
        auto s = RaisedHealthy();
        s[2].isVisible = false;
        s[2].isTransparent = false;   // 就算它样式是坏的
        Check(DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
              "不可见的壁纸层不参与判定(哪怕它样式是坏的)");
        // 不可见的**组件**仍然参与:它占着 z-order 上该在的位置,排错了照样点不到。
        auto t = RaisedHealthy();
        t[0].isVisible = false;
        t[0].isLayered = true;        // 与 raised 契约相反
        Check(!DesktopBandOrderSatisfied(t, DesktopBandMode::Raised),
              "不可见的组件仍然参与判定(它占着 z-order 上该在的位置)");
    }

    // ---- 6. lumped:干净带子里单独换任何一个事实都要能红 ----
    // 这一条防"只认一种坏法"。五个事实逐个换,每换一个都必须从有序变需要修。
    {
        struct Case { const char* what; void (*break_)(std::vector<DesktopBandSurface>&); };
        // 用 lambda 数组而不是函数指针,省一个 typedef
        std::vector<std::pair<const char*, void (*)(std::vector<DesktopBandSurface>&)>> cases;
        cases.push_back({"widget 的 isChild", [](std::vector<DesktopBandSurface>& v) { v[0].isChild = false; }});
        cases.push_back({"widget 的 isLayered(raised 下必须非 layered)", [](std::vector<DesktopBandSurface>& v) { v[0].isLayered = true; }});
        cases.push_back({"icon layer 的 isChild", [](std::vector<DesktopBandSurface>& v) { v[1].isChild = false; }});
        cases.push_back({"wallpaper 的 isChild", [](std::vector<DesktopBandSurface>& v) { v[2].isChild = false; }});
        cases.push_back({"wallpaper 的 isLayered", [](std::vector<DesktopBandSurface>& v) { v[2].isLayered = false; }});
        cases.push_back({"wallpaper 的 isTransparent", [](std::vector<DesktopBandSurface>& v) { v[2].isTransparent = false; }});
        for (auto& c : cases) {
            auto s = RaisedHealthy();
            c.second(s);
            Check(!DesktopBandOrderSatisfied(s, DesktopBandMode::Raised),
                  std::string("单独改坏 ") + c.first + " 就必须从有序变需要修");
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n桌面带层级契约:全部 %d 项通过\n", g_checks);
    return 0;
}
