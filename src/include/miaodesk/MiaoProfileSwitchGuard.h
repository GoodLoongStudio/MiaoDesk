#pragma once

// AI-05「API profile 热切换语义」里"当前任务不静默换"的那道闸门。
//
// 用户在 AI 面板或创作窗口的 API 下拉里切换 Provider。这道闸门回答的问题是:
// **这一刻允不允许动这个下拉(以及它背后的 profileId)?**
//
// 这段判定原本散在**五处**,四种写法,其中两种结论相反:
//
//   A) `ConversationPanelImpl.inc` 的 `ShowConversationApiProfileMenu`(点菜单):
//          if (busy || (pi && pi->Busy())) { 说一句话; return; }
//      → 有轮次在飞就**不让你切换**,并把原因说出来。
//
//   B) `ConversationPanelImpl.inc` 的 `PopulateConversationApiProfiles`(拉开下拉):
//          if (preserveSelection && pi && pi->Busy() && !busy) return;
//      C) `ContentCreatorDialog.cpp` 的 `PopulateApiProfiles`:与 B 一字不差。
//      → `&& !busy` 是个**豁免**:它让"这个窗口自己的轮次正在跑"成为重建的**理由**,
//        而紧挨着的注释说的是"另一个 AI 窗口可能占着共享的 Pi 轮次,别在它下面
//        重写这个选择器"。
//
//   D) `ConversationPanelImpl.inc` 的 `SelectConversationApiProfile`(真选了一项):
//          if (busy || ...) return;                       ← 静默
//          if (pi && pi->Busy()) { 改回控件; 说一句话; return; }
//      E) `ContentCreatorDialog.cpp` 的 `SelectApiProfile`:与 D 一字不差。
//      → 两支的收梢不一样:CBN_SELCHANGE 是在下拉**已经 visual 改过之后**才到的,
//        于是 Pi 忙那一支把控件改回真正在跑的那个并说明,而这个窗口自己忙那一支
//        **静默返回** —— 留下"下拉显示 B、agent 跑的是 A",界面上一个字都不说。
//
// 三处硬编码的提示文案有两套不同的措辞("当前有 AI 任务…" / "另一个 AI 窗口…"),
// 而 A 与 B/E 管的是同一个共享 Pi 运行时、同一个用户意图("我想换个 API"),
// 菜单那条拦住、下拉那条却放行 —— 而放行那条通向 `L3Agent::ReloadConfig()`:
//
//     Stop(); if (worker_.joinable()) worker_.join();
//     config_ = refreshed;
//     conversation_.clear();          // ← 会话上下文在轮次中途被清空
//
// 也就是说:**用户在自己这一轮跑到一半时拉开下拉,这一轮被打断、上下文被清掉**,
// 而界面上一个字都不提。`&& !busy` 这一句把 A 已经防住的那件事又放回去了。
//
// 这里的判定取这几条路里**安全**的那一种:任一忙位为真就不动。
//
// ⚠ 两处自我更正,都记在这里,因为它们是同一类错:**我按"读起来对"的推理往下写,
// 而没有去证**。
//
// 第一处(判定形状):第一版这里写的是"这个窗口自己的轮次在飞时 piBusy 必然也为真,
// 所以只看 piBusy 就够"。**那句没法证明,而且是错的。**
// `ConversationPanelImpl.inc` 里 `SetBusyVisual(state, true)`(第 984 行)发生在
// `state.pi->AskAsync(...)`(第 993 行)**之前**,于是两者之间有一段 busy 已置真、
// Pi 还不忙的间隙;`AskAsync` 在共享运行时已被别的窗口占走时也不会把 busy 收回去。
// 只看 piBusy,重建就会落进那段间隙里 —— 而那正是 `Stop()` + `conversation_.clear()`
// 最不该出现的时刻。所以 windowBusy 必须单独算一项。
//
// **它确实够得到,不是纸上谈兵:** `ShowConversationApiProfileMenu` 的触发点是
// `WM_LBUTTONDOWN` 里 `PointIn(state->apiProfileRect, point)` 这个**自定义命中区**
// (ConversationPanelImpl.inc:1843),不是那个 combo 控件。busy 时
// `SetBusyVisual` 只禁用 `state.input` 与 `state.apiProfileCombo`,**这个命中区照旧
// 能点**。于是轮次跑到一半时点 API 位置,走的就是 windowBusy 这一条。
// (反过来,两个 combo 路径 `CBN_DROPDOWN` / `CBN_SELCHANGE` 在 busy 时确实发不出来
//  —— 控件禁用了。那两处仍然把 windowBusy 如实传进去:传真实状态而不是替它断定,
// 免得以后 enable/disable 的规矩一改,这里的结论就悄悄错了。)
//
// 第二处(副本数):我先按"两份一字不差的副本"写了整段注释,接完才 grep 出来
// 还有第三处(A,菜单)、再两处(D/E,选择出口)。**每接一处就有一处的新事实冒出来,
// 而注释是先写的。** 所以现在这句"五处"是 grep 过之后的数,不是猜的。
//
// 这里只放判定。它不切换 profile、不碰窗口;调用方照它的结论走。
#include <cstdint>
#include <string>

namespace miaodesk::profile_switch {

// 这一刻该不该保留(不动)这个 API 下拉。
enum class ProfileSelectorAction {
    Rebuild,   // 可以重建:重新读盘上的 profile 列表、重填下拉
    Keep,      // 别动:有轮次在飞
};

// 判定。
//
// preserveSelection  false 表示这是窗口刚打开时的首次填充 —— 那时还没有这个窗口的
//                      轮次,而且用户还没做过任何选择,重建不会打断谁。
//                      true 表示这是运行期刷新(用户拉开下拉、或外部改了配置)。
// piBusy              共享 Pi 运行时有没有轮次在飞。**不分是哪个窗口的** ——
//                      运行时是共享的,轮次也是。
// windowBusy          这个窗口自己有没有轮次在飞。
//
// 两个忙位**取或**:任一为真就不动。理由不是"它们总是一起真"(上面那段说过,证不
// 了),而是动一下的代价 —— `ReloadConfig()` 会 `Stop()`、`join()` worker、
// `conversation_.clear()`。这个窗口自己的轮次哪怕只处在"已置忙、还没派发"的那
// 一小段里,重建也足以让它被打断;共享运行时被别的窗口占着时,重建还会让"这个窗口
// 以为自己选了 X、其实跑的是 Y"。两种情况都不该动,而代价是不对称的:
// 该动没动,用户只是晚一会儿看到新 profile;不该动却动了,一轮对话没了。
//
// Keep 只在 preserveSelection 为真时出现。调用点因此不需要在 Keep 分支里再套一层
// `if (preserveSelection)`,那条会永远为真 —— `ProfileSwitchGuardTest` 把这一点钉成
// 了断言,免得死判断悄悄长回来。
ProfileSelectorAction DecideProfileSelectorAction(bool preserveSelection,
                                                  bool piBusy,
                                                  bool windowBusy) noexcept;

// 这个结论下,界面该说的一句话。空表示不必说 —— 用户没问就报一句是噪音。
//
// 回传 wstring:四个调用方都是 `AddEntry(..., EntryKind::System, ...)` 或直接
// SetWindowTextW,都收不了窄串。与 MiaoGozRecovery::ExplainGozRecovery
// 同一个形状。
std::wstring ExplainProfileSelectorAction(ProfileSelectorAction action);

} // namespace miaodesk::profile_switch
