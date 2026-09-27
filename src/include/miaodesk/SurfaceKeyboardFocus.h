#pragma once

#include <windows.h>

// MiaoDesk 有几个 modeless 顶层窗口(壁纸自动化、应用程序规则)是"关窗即隐藏"的:
// 它们要保存自己的状态给下一次打开,不能 DestroyWindow。隐藏本身没错,但它带着一个
// 键盘问题 —— 被隐藏的窗口往往正是拿着键盘焦点的那一个(刚点过的"关闭"按钮就持有
// 焦点),而 Windows 会把焦点顺手交给 Z 序里的下一个窗口,经常是桌面本身:于是用户的
// 下一次 Tab、空格或打字直接进了别的应用程序,没有任何视觉提示告诉他焦点跑掉了。
//
// 所以这两个窗口都从这里隐藏。两条规则:
//   1. 先记住键盘是不是自己的,隐藏之后再把它交还打开我们的那个界面;
//   2. ESC 与"关闭"按钮走同一条路径,否则两边的行为迟早会漂开。
//
// 只依赖本线程内的 API(GetFocus / SetFocus 都是线程级),这几个窗口都挂在同一条
// 消息泵上,所以在钩子进程里行为一致。

namespace miaodesk::surface_focus {

// 把键盘交还给 target。target 通常是个没有自处理按键的顶层窗口,直接给它焦点只会让
// 键盘停在窗口框架上、Tab 无处可去;所以优先选它第一个可见可用的 Tab 停靠点 —— 那
// 也正是继续按 Tab 本来会到的地方。
inline void RestoreKeyboardFocus(HWND target) {
    if (!IsWindow(target) || !IsWindowVisible(target)) return;
    HWND control = GetWindow(target, GW_CHILD);
    while (control) {
        const LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
        const bool focusable = (style & WS_TABSTOP) && (style & WS_VISIBLE) && IsWindowEnabled(control);
        if (focusable) break;
        control = GetWindow(control, GW_HWNDNEXT);
    }
    SetFocus(control ? control : target);
}

// 隐藏 surface,并在它原本持有键盘焦点时把焦点交还 restoreTo。
inline void HideSurface(HWND surface, HWND restoreTo) {
    const bool heldKeyboard = GetFocus() == surface;
    ShowWindow(surface, SW_HIDE);
    if (heldKeyboard) RestoreKeyboardFocus(restoreTo);
}

} // namespace miaodesk::surface_focus
