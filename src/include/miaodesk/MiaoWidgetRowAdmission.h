#pragma once

// P0-09「用户数据升级/迁移安全」里"组件布局"那一半的入账判定。
//
// `DesktopWidgetStore::Load` 从 INI 读回每一个组件行，然后一行 `continue` 决定
// 它进不进库：
//
//     if (widget.kind == Unknown || widget.source.empty() || !IsValidPersistedSource(widget)) continue;
//
// 三种完全不同的原因被压进同一个 `continue`，而 `Load` 照常返回 true。调用方只问成败，
// 于是拿着一个悄悄变短的布局继续 —— 这正是 P0-09 的失败形态："升级不丢组件布局"
// 实际变成"丢了几条并报告成功"。
//
// 同一行里其实还藏着**两种**去重结局（撞 singleton 键时，启用状态优先；同状态时
// 先出现者胜），也一起压在这个 `continue` 里。五種結局各有各的下一步，
// 混成一个布尔就分不清该告诉用户什么。
//
// 这里只放入账判定，不碰 INI、不碰文件系统。"这个 source 受不受认"仍由宿主判
// （它要知道内容目录，那是 Windows 的事）—— 与 `MiaoDesktopBandOrder` 同一个分法：
// 归属问"这行是谁"，入账问"该不该收"。
#include <cstddef>
#include <string>

namespace miaodesk::widget_row {

// 一行组件配置读完之后的结局。五种，各有各的用户可见后果。
enum class WidgetRowOutcome {
    Admit,               // 进库
    RejectUnknownKind,   // 本构建不认识这个 Kind —— 版本差异
    RejectMissingSource, // Source 是空的 —— 配置损坏
    RejectUnsafeSource,  // Source 不是受认的形态 —— 拒绝，且这是安全边界
    SupersedeDisabled,   // 撞键，而库里那条是禁用的 → 用新的替换（启用状态优先）
    KeepExisting,        // 撞键，保留库里那条（同状态时先出现者胜）
};

const char* ToString(WidgetRowOutcome outcome) noexcept;

// 判定用得到的事实。调用方喂；这里不看盘。
struct WidgetRowFacts {
    bool kindRecognised{false};   // kind != Unknown
    bool sourcePresent{false};    // !source.empty()
    bool sourceValid{false};      // 宿主验证过 Source 受认
    bool hasSingletonKey{false};  // NativeSingletonKey 非空（Content 组件为空，不参与去重）
    bool singletonClashes{false}; // 已在库里的某一条与它同一个 singleton 键
    bool candidateEnabled{false}; // 这一行自己禁不禁用
    bool existingEnabled{false};  // 撞键时，库里那一条禁不禁用
};

// 这一行读完该怎么办。
WidgetRowOutcome DecideWidgetRowAdmission(const WidgetRowFacts& facts) noexcept;

// 这个结局该不该让布局发生变化（需要重写 INI）。
// 只有"真的收下一条"或"替换掉一条"才算变化 —— 撞键后保留原样虽然也修了文本，
// 但那是另一件事（去重本身要落盘），由调用方自己决定。
bool WidgetRowOutcomeMutatesStore(WidgetRowOutcome outcome) noexcept;

// 给用户/模型看的一句话。技术原文放诊断，不放过长的堆栈。
// 回传 wstring 而不是 const char*:调用方要把它拼进 skippedRows_ 给宿主显示,
// 而那里存的是宽串 —— 与 MiaoLibraryRowFilter::DescribeRowSkip 同一个形状。
std::wstring ExplainWidgetRowOutcome(WidgetRowOutcome outcome);

} // namespace miaodesk::widget_row
