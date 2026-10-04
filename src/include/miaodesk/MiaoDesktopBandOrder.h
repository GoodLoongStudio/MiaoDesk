#pragma once

// STAB-02 / P0-05「Explorer restart 恢复」里"层级还对不对"的那一半。
//
// Explorer 一重启，桌面这条带子上所有窗口的父子关系与 z-order 都会被打乱。
// `DesktopShellHost::RepairRoleOrder` 负责把它修回契约，而它的第一件事是**先判断
// 还要不要修**：
//
//     // The host runs this repair on every sync tick. When the desktop band
//     // already satisfies the contract, re-issuing style/z-order churn makes DWM
//     // recomposite the whole band every second and reads as wallpaper flicker.
//     // Verify first; touch windows only when something violates the contract.
//
// 那个判断是纯逻辑，却住在 `DesktopShellHost.cpp`（要 `<windows.h>`：
// `GetWindowLongPtrW`、`GetWindow`、`SetWindowPos`），于是本机一行都跑不到。
// 而它有两个方向都用户看得见的失败形态：
//
//   · **恒说"已经有序"** —— 修复永远不跑，Explorer 重启后壁纸被图标盖住、
//     组件点不动，而且再也不自己好；
//   · **恒说"需要修"** —— 每一跳都重发一遍 style/z-order，DWM 每秒重组整条带子，
//     用户看到的就是壁纸闪。注释里那句 "reads as wallpaper flicker" 说的就是这个。
//
// 这里只放契约与判定，不碰窗口、不碰 DWM。
#include <cstddef>
#include <vector>

namespace miaodesk {

// 桌面带子上一个已知表面对契约扮演什么角色。顺序即判定优先级：
// 先看是不是组件，再看是不是图标层，剩下的都当壁纸层 —— 与
// `RepairRoleOrder` 里的分支顺序逐字一致。改这个顺序会改变"某个窗口算哪一种"，
// 而那正是归属判断，不是排序偏好。
enum class DesktopBandRole {
    Widget,     // 交互组件
    IconLayer,  // 桌面图标层(SysListView32 / DefView)
    Wallpaper,  // 壁纸层
};

// 判定用得到的事实。调用方喂；这里不看窗口。
struct DesktopBandSurface {
    DesktopBandRole role{DesktopBandRole::Wallpaper};
    bool isChild{false};        // WS_CHILD
    bool isLayered{false};      // WS_EX_LAYERED
    bool isTransparent{false};  // WS_EX_TRANSPARENT
    bool isVisible{false};      // IsWindowVisible
};

// 上一模式：raised desktop（WorkerW 新代）还是 legacy（Progman fallback）。
// 它决定组件的分层契约——raised 下组件是**非** layered 的直接表面，
// legacy 下保留 layered 的 UpdateLayeredWindow 契约。
enum class DesktopBandMode {
    Legacy,
    Raised,
};

// 这条带子现在满足契约吗。
//
// 契约（按 z-order 从上到下走一遍）：
//   1. 每个参与的表面都必须是 WS_CHILD，否则根本无法固定在桌面上；
//   2. 组件層：layered 与否必须与模式相反 —— raised 下必须**不** layered，
//      legacy 下必须 layered。两者都对不上说明有人改过样式；
//   3. 组件必须排在图标层与壁纸层**之前**（raised），或壁纸层之前（legacy）——
//      否则用户点不到组件；
//   4. 图标层必须排在壁纸层之前（raised）—— 否则壁纸盖住桌面图标；
//   5. 壁纸层必须同时 layered 且 transparent —— 否则它要么挡点击，要么不透。
//
// 不可见的壁纸层不参与判定：`RepairRoleOrder` 收集时就把它滤掉了
// （`role != Widget && IsWindowVisible(child) == FALSE` 则跳过）。
// 这不是省略，是一条规则：隐藏中的壁纸层不该让整条带子被判成"需要修"，
// 否则每次隐藏/显示都会触发一轮 z-order 重排。
bool DesktopBandOrderSatisfied(const std::vector<DesktopBandSurface>& surfaces,
                               DesktopBandMode mode) noexcept;

} // namespace miaodesk
