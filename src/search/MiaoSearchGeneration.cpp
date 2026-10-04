#include "miaodesk/MiaoSearchGeneration.h"

namespace miaodesk {

SearchGeneration NextSearchGeneration(SearchGeneration current) noexcept {
    // 代号不复用:哪怕绕回同一个 UI 线程串行地起查询,每一次拿到的也是新号码。
    return current + 1;
}

SearchGeneration ClaimSearchGeneration(SearchGeneration current) noexcept {
    return NextSearchGeneration(current);
}

bool ShouldDeliverSearchReply(SearchGeneration claimed, SearchGeneration current) noexcept {
    // 零代号 = 从来没有任何查询占过它。它可能来自一个没走到 Claim 的路径,
    // 此时若 current 恰好也是 0,"相等"会把一次不存在的查询递出去。
    if (claimed == kSearchGenerationUnset) return false;
    return claimed == current;
}

SearchGeneration SearchGenerationTracker::Claim() noexcept {
    return current_.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool SearchGenerationTracker::ShouldDeliver(SearchGeneration claimed) const noexcept {
    return ShouldDeliverSearchReply(claimed, current_.load(std::memory_order_relaxed));
}

void SearchGenerationTracker::Invalidate() noexcept {
    // 只往前走,不占号:它之后没有回包对应这个新号码,而在飞的那个旧号码
    // 从此永远不等于 current。
    current_.fetch_add(1, std::memory_order_relaxed);
}

SearchGeneration SearchGenerationTracker::Current() const noexcept {
    return current_.load(std::memory_order_relaxed);
}

} // namespace miaodesk
