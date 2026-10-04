#pragma once

// SEARCH-03「异步搜索取消与输入响应」里"快速输入不展示过期结果"那一半。
//
// 用户在搜索框里连敲 `c` → `ca` → `cat`。三次都会起一次文件查询,而后起来的两次
// **不代表**先前的两次已经结束 —— gozd 是另一个进程,回包走命名管道 + WM_COPYDATA,
// 快的那次完全可能后到。不设防的后果用户看得见:`cat` 的结果刚从屏幕上画出来,
// `ca` 的回包抵达,把它**盖掉**。这正是"搜索框自己会换位置"那类投诉的一种。
//
// 防法一直有:`GozSearch::Query` 让每次查询占一个代号(generation),回包只在这个
// 代号还最新时递送。但它住在 `GozSearch.cpp` 里 —— 那个文件 include `<windows.h>`
// (命名管道、`SendMessageTimeoutW`),于是**本机一行都跑不到**,而它是用户输入最快
// 那几毫秒里唯一在保护结果正确性的东西。
//
// 这里只放代号算术,不碰管道、不碰窗口、不碰线程。

#include <atomic>
#include <cstdint>

namespace miaodesk {

// 一次异步查询的代号。0 是"没有"—— 查询从 1 开始占,所以 0 永远不等于任何一次
// 真查询,拿它当"已经占过"来问会得到否,这一点靠 `ShouldDeliverSearchReply` 的
// 零值保护钉住。
using SearchGeneration = std::uint64_t;

inline constexpr SearchGeneration kSearchGenerationUnset = 0;

// 当前代号往前走一步。每次起查询都调一次,所以代号**不复用**。
SearchGeneration NextSearchGeneration(SearchGeneration current) noexcept;

// 占一个代号并返回它。等于 `NextSearchGeneration`,单独一个名字是为了让
// "起一次查询"在调用点读起来像一件事。
SearchGeneration ClaimSearchGeneration(SearchGeneration current) noexcept;

// 这个代号的回包现在还该不该递给界面。
//
// 只有代号**仍然最新**才递。三种情况下不递:
//   · 之后又起了一次查询(用户多敲了一个字符);
//   · 有人显式作废了(`Invalidate`,界面换了查询文本但这次起不来新查询);
//   · 代号是零(从来没有任何查询占过它)。
bool ShouldDeliverSearchReply(SearchGeneration claimed, SearchGeneration current) noexcept;

// 代号追踪器。`GozSearch` 手上那个 `std::atomic_uint64_t generation` 的直接替身:
// 线程语义一模一样(worker 线程读、UI 线程写),所以它是 drop-in;
// 而"该不该递"的判定在外面那个纯函数里,于是本机测得动。
class SearchGenerationTracker {
public:
    SearchGenerationTracker() = default;
    SearchGenerationTracker(const SearchGenerationTracker&) = delete;
    SearchGenerationTracker& operator=(const SearchGenerationTracker&) = delete;

    // 起一次查询:占一个新代号。
    SearchGeneration Claim() noexcept;

    // 这个代号的回包还新吗。
    bool ShouldDeliver(SearchGeneration claimed) const noexcept;

    // 不让新查询发生,也要让在飞的那次作废。
    //
    // 界面每收到一次输入就调它 —— 包括**这次起不来新查询**的情况:输入变成了空、
    // 以 `/` 开头是命令、goz 客户端没装、目标窗口已经没了。这些分支都很自然会
    // `return`,于是一次 setTimeout 的在飞查询的旧回包会在几十毫秒后抵达,
    // 盖在用户已经看到的"命令提示"或空状态上。所以作废必须**无条件**发生在
    // 所有早退之前,这也是 `SearchWindow::OnQueryChanged` 第一行就是 `Shutdown()`
    // 的理由。
    void Invalidate() noexcept;

    SearchGeneration Current() const noexcept;

private:
    std::atomic<SearchGeneration> current_{kSearchGenerationUnset};
};

} // namespace miaodesk
