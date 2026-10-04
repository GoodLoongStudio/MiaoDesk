// SEARCH-03「快速输入不展示过期结果」的代号算术。
//
// 用户在搜索框里连敲 `c` → `ca` → `cat`。三次各起一次文件查询,后两次**不代表**
// 先前的两次已经结束:gozd 是另一个进程,回包走命名管道。快的那次可能后到。
// 不设防的后果是 `cat` 的结果刚画出来就被 `ca` 的回包盖掉 —— 用户看见的就是
// "搜索框自己会换位置"。
//
// 这段判定此前住在 `GozSearch.cpp`,而那个文件 include `<windows.h>`,本机一行
// 都跑不到。它保护的是用户输入最快那几毫秒里唯一在管结果正确性的东西。
#include "miaodesk/MiaoSearchGeneration.h"

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

// 反空洞自检。一个恒返回 true 的 ShouldDeliverSearchReply 会让过期结果照样显示
// (用户看得见的那半),一个恒返回 false 的会让文件搜索结果永远不出现(同样看得见)。
// 先喂一个明知该递的(刚占的代号)和一个明知不该递的(作废之后的旧代号),
// 确认判定真的会动。
bool VerdictStillMoves() {
    SearchGenerationTracker tracker;
    const SearchGeneration claimed = tracker.Claim();
    if (!tracker.ShouldDeliver(claimed)) return false;      // 刚占的 → 该递
    tracker.Invalidate();
    return !tracker.ShouldDeliver(claimed);                 // 作废后 → 不递
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:递送判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:刚占的代号该递、作废之后不该递\n");
    ++g_checks;

    // ---- 1. 主要失败形态:多敲一个字符,新结果被旧回包盖掉 ----
    {
        SearchGenerationTracker tracker;
        const SearchGeneration first = tracker.Claim();   // 用户敲了 "c"
        Check(tracker.ShouldDeliver(first), "刚占的代号该递");

        const SearchGeneration second = tracker.Claim();  // 又敲了 "a" → "ca"
        Check(second != first, "每次起查询都拿到一个新代号(不复用)");
        Check(!tracker.ShouldDeliver(first),
              "起过第二次之后,第一次的回包不许递(它会把 ca 的结果盖掉)");
        Check(tracker.ShouldDeliver(second), "最新的那次照递");

        const SearchGeneration third = tracker.Claim();   // "cat"
        Check(!tracker.ShouldDeliver(second), "第三次之后,第二次的回包也不许递");
        Check(tracker.ShouldDeliver(third), "第三次的回包该递");
    }

    // ---- 2. 起不来新查询,也得让在飞的那次作废 ----
    // OnQueryChanged 的四个早退分支都很自然:空输入、`/` 命令、goz 没装、
    // 窗口没了。每一次 return 之前都必须作废,否则一次在飞的旧回包会在几十毫秒后
    // 盖在用户已经看到的"命令提示"或空状态上。
    {
        SearchGenerationTracker tracker;
        const SearchGeneration claimed = tracker.Claim();
        tracker.Invalidate();
        Check(!tracker.ShouldDeliver(claimed),
              "Invalidate 之后,在飞的回包不许递(命令/空输入/起不来查询)");

        // 作废之后再起一次查询:旧代号仍然作废,新代号照递。
        const SearchGeneration next = tracker.Claim();
        Check(!tracker.ShouldDeliver(claimed), "作废又起过查询,旧代号仍然不许递");
        Check(tracker.ShouldDeliver(next), "作废之后新起的查询照递");
    }

    // 反复作废:每次按钮/输入都作废,代号单调往前走,没有一次旧回包能混过去。
    {
        SearchGenerationTracker tracker;
        std::vector<SearchGeneration> claimed;
        for (int i = 0; i < 5; ++i) claimed.push_back(tracker.Claim());
        for (int i = 0; i < 3; ++i) tracker.Invalidate();
        for (int i = 0; i < 5; ++i) {
            Check(!tracker.ShouldDeliver(claimed[i]),
                  "五次的旧代号在三次作废之后一条都不许递");
        }
    }

    // ---- 3. 零代号 ----
    // 0 是"没有查询"。它可能来自一条没走到 Claim 的路径,而那一刻 current 恰好
    // 也是 0(刚构造、还没起过任何查询),"相等"会把一次不存在的查询递出去。
    {
        Check(!ShouldDeliverSearchReply(kSearchGenerationUnset, kSearchGenerationUnset),
              "两边都是零代号时不递(没有查询就没有回包)");
        Check(!ShouldDeliverSearchReply(kSearchGenerationUnset, 7), "零代号对任何 current 都不递");
        Check(ShouldDeliverSearchReply(7, 7), "非零且相等才递");
        Check(!ShouldDeliverSearchReply(6, 7), "差一代号就不递");
        Check(!ShouldDeliverSearchReply(8, 7), "未来的代号也不递(猜大一点不算)");
    }

    // ---- 4. 初始状态 ----
    {
        SearchGenerationTracker tracker;
        Check(tracker.Current() == kSearchGenerationUnset, "新追踪器还没有代号");
        Check(!tracker.ShouldDeliver(kSearchGenerationUnset), "新追踪器什么都没得起");
        const SearchGeneration first = tracker.Claim();
        Check(first == kSearchGenerationUnset + 1, "第一次起查询从 1 开始(0 留给没有查询)");
        // Current() 是 ShouldDeliver 的比较基准。它要是恒零,上面每一条"该递"就全废了,
        // 而"不许递"反而一条条都成立 —— 那正是"旧回包永远不来"这种静默故障。
        Check(tracker.Current() == first,
              "Current() 就是刚刚占到的那个代号(它是 ShouldDeliver 的比较基准)");
        tracker.Invalidate();
        Check(tracker.Current() != first, "Invalidate 之后 Current() 变了(旧代号从此不最新)");
    }

    // ---- 5. 追踪器与纯函数是同一套判定 ----
    // 本机测的是纯函数,真机跑的是 `SearchGenerationTracker::ShouldDeliver`。
    // 两者必须是同一个式子,否则就是测了一个替身 —— 而替身通过时与好代码长得一样。
    // 这里逐一对照:Claim 出来的代号,纯函数也说该递;作废之后,两边都说不递。
    {
        SearchGenerationTracker tracker;
        SearchGeneration current = kSearchGenerationUnset;
        for (int i = 1; i <= 4; ++i) {
            const SearchGeneration claimed = ClaimSearchGeneration(current);
            current = claimed;
            Check(ShouldDeliverSearchReply(claimed, current),
                  "刚 Claim 出来的代号,纯函数判定也说该递");
            Check(NextSearchGeneration(claimed) != claimed, "NextSearchGeneration 每次都往前走");
        }
        Check(ClaimSearchGeneration(41) == NextSearchGeneration(41),
              "ClaimSearchGeneration 就是 NextSearchGeneration(别有两套)");

        tracker.Invalidate();
        Check(!ShouldDeliverSearchReply(current, tracker.Current()),
              "Invalidate 之后,纯函数与追踪器都说不递");
    }

    // ---- 6. 单调性:代号永不回头 ----
    // 64 位绕回在现实中不可能,但"回头"会让一次很老的旧回包重新变得"最新"。
    // 这条不防溢出,是防实现里出现 `current - 1` 之类的回退。
    {
        SearchGenerationTracker tracker;
        SearchGeneration previous = tracker.Current();
        for (int i = 0; i < 200; ++i) {
            const SearchGeneration claimed = tracker.Claim();
            Check(claimed > previous, "代号单调递增(一次旧回包不会重新变成最新)");
            Check(tracker.Current() == claimed, "Current() 跟着最新那次走");
            previous = claimed;
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n搜索代号:全部 %d 项通过\n", g_checks);
    return 0;
}
