// D-2:原生待办卡片的行模型。
//
// 这份测试的重点是"没有数据就说没有数据"。旧实现把三条待办写在 painter 里,于是
// 单元测试不可能失败 —— 被测的是一段常量。现在形状是函数,而它的三条硬规则各配一个
// 只违反它的输入:
//   · 快照无效 → valid=false 且不给行,不得用别的东西填满;
//   · 空待办 → 进度是 0 而不是 1(满格会把空态画成"全部完成");
//   · 行数受 maxRows 限制,绝不滚动、绝不裁字。
#include "miaodesk/TodayTaskPresentation.h"

#include <cstdio>
#include <string>

namespace miaodesk::desktop {
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

TodayTaskSnapshot Snap(std::size_t total, std::size_t completed) {
    TodayTaskSnapshot snapshot;
    snapshot.valid = true;
    snapshot.generation = 1;
    snapshot.total = total;
    snapshot.completed = completed;
    snapshot.pending = total - completed;
    for (std::size_t i = 0; i < total; ++i) {
        TodayTaskItem item;
        item.id = L"t" + std::to_wstring(i);
        item.title = L"第 " + std::to_wstring(i) + L" 项";
        item.detail = L"今天";
        item.completed = i < completed;
        snapshot.items.push_back(std::move(item));
    }
    return snapshot;
}

} // namespace
} // namespace miaodesk::desktop

int wmain() {
    using namespace miaodesk::desktop;

    // 1. 真实数据:数量、进度、行都来自快照,一个字段都不编。
    const auto three = BuildTodayTaskCardModel(Snap(3, 1), 4);
    Check(three.valid, "快照可用时卡片可用。");
    Check(three.total == 3 && three.completed == 1 && three.pending == 2, "数量与快照一致。");
    Check(three.rows.size() == 3, "三条真实待办都在。");
    // Snap(3,1) 里第 0 项是已完成的那一条 —— 这里断的是"完成标记来自快照",
    // 旧实现写死的是"第三条完成",所以这个断言在新旧两种实现下结果不同。
    Check(three.rows[0].title == L"第 0 项", "第一行是真实内容。");
    Check(three.rows[0].completed, "第 0 项标为已完成(来自快照)。");
    Check(!three.rows[1].completed, "第 1 项未完成(来自快照)。");
    Check(!three.rows[2].completed, "第 2 项未完成(来自快照)。");
    Check(three.statusText == L"3 项待办", "状态行报真实数量。");
    Check(three.progress > 0.32f && three.progress < 0.34f, "进度按 完成/总数 算(1/3)。");

    // 2. 空待办:进度必须是 0。写成 1 会把空态画成"全部完成"。
    const auto none = BuildTodayTaskCardModel(Snap(0, 0), 4);
    Check(none.valid, "空待办仍是可用状态 —— 它是真的没有,不是读不到。");
    Check(none.rows.empty(), "空待办没有行。");
    Check(none.progress == 0.0f, "空待办进度是 0,不是 1。");
    Check(none.statusText == L"还没有今日待办", "空待办给的是空态说明。");

    // 3. 读不到:valid=false,没有行,并且说出来为什么。
    //    这正是旧实现缺失的那条:它给的是三条不存在的待办。
    TodayTaskSnapshot broken;
    broken.valid = false;
    broken.total = 0;
    broken.completed = 0;
    broken.pending = 0;
    const auto unavailable = BuildTodayTaskCardModel(broken, 4);
    Check(!unavailable.valid, "读不到快照时卡片不可用。");
    Check(unavailable.rows.empty(), "读不到时一行都不给 —— 不得用别的东西填满。");
    Check(unavailable.statusText == L"任务数据暂不可用", "读不到时说明原因。");

    // 4. 行数上限:放不下就放不下,不裁字也不滚动。
    const auto many = BuildTodayTaskCardModel(Snap(9, 0), 4);
    Check(many.rows.size() == 4, "只给画得下的行数。");
    Check(many.total == 9, "总数照实报,不因为只显示 4 行就说只有 4 项。");

    // 5. 与内容组件说同一句话:progressText 的格式必须一致,否则两处显示两个数。
    const auto two = BuildTodayTaskCardModel(Snap(2, 2), 4);
    Check(two.statusText == L"2 项待办", "状态文案与真实数量一致。");

    // ---- 6. 溢出说明必须是真的 ----
    // 缺陷本体:组件顶部的计数说的是真话("8 项待办"),下面只画 4 行,而没有任何地方
    // 告诉用户"还有 4 条"。用户以为组件坏了,或者以为自己只加了 4 条。
    Check(TaskOverflowText(3, kTodayTaskVisibleSlots).empty(), "放得下时没有溢出说明。");
    Check(TaskOverflowText(kTodayTaskVisibleSlots, kTodayTaskVisibleSlots).empty(),
          "刚好放满时也没有溢出说明 —— 不是『还有 0 项』。");
    Check(TaskOverflowText(8, kTodayTaskVisibleSlots) == L"还有 4 项", "8 条待办、4 个槽位:还有 4 项。");
    Check(TaskOverflowText(5, kTodayTaskVisibleSlots) == L"还有 1 项", "多一条时如实说还有 1 条。");
    Check(TaskOverflowText(9, 4) == L"还有 5 项", "槽位数由调用方给,不写死。");
    // 槽位为 0 是退化情形,但说明仍必须是真的:全部待办都放不下。
    Check(TaskOverflowText(4, 0) == L"还有 4 项", "槽位为 0 时说全部放不下,不说空话。");

    // 卡片模型用的行数上限与这里共用同一个常量:两处各写一个 4 早晚对不上。
    const auto many2 = BuildTodayTaskCardModel(Snap(9, 0), kTodayTaskVisibleSlots);
    Check(many2.rows.size() == kTodayTaskVisibleSlots, "卡片按共享槽位数截断。");
    Check(TaskOverflowText(9, kTodayTaskVisibleSlots) == L"还有 5 项",
          "同一份待办在组件与卡片上给出同一个溢出数。");

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("原生待办卡片模型:全部 %d 项检查通过\n", g_checks);
    return 0;
}
