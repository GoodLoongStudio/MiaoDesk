// AI 面板三轮消息的代号比对回归。
//
// 逮到的问题不是"它会算错",而是**它在本机一行都验不到**:那行三方比对住在
// `ConversationPanelImpl.inc` 里,而那个文件要 `<windows.h>`。三处一字不差的副本,
// 任何一处被改动(或者某一处漏改)都没有东西会响。
//
// 两半各有各的事故,所以两边都要单独钉:
//   · 只看 turn  → 取消后立刻重试,旧轮的 delta 追加进新一轮的条目,
//                  用户看到自己没说过的话从模型嘴里出来;
//   · 只看 latest → 面板已切到另一轮,delta 仍落到旧那一轮上,表现为"回复串台"。
#include "miaodesk/MiaoTurnReplyScope.h"

#include <cstdio>
#include <string>

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

// 反空洞自检。一个恒返回 true 的判定会把旧轮的 delta 追加进新一轮的条目(用户看得见),
// 一个恒返回 false 的判定会让模型一个字都不显示(同样看得见)。两个方向各喂一个。
bool VerdictStillMoves() {
    // 同代同最新 → 该采用
    if (!TurnReplyIsCurrent(7, 7, 7)) return false;
    // 产生它的那一轮已经不是当前轮 → 不采用;那一轮也不再是最新 → 同样不采用
    return !TurnReplyIsCurrent(6, 7, 7) && !TurnReplyIsCurrent(6, 6, 7);
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:三轮比对不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:同代同最新才采用,任一边过期就不采用\n");
    ++g_checks;

    // ---- 1. 主要失败形态一:只比 turn,不比 latest ----
    // 场景:用户在轮 7 里取消,面板立刻起轮 8。轮 7 的 worker 还在写 delta。
    // 只比 turn 时,7 != 8(panel 的当前轮),所以这一半恰好挡住了 —— 但把它反过来:
    // 面板.current 仍是 7(取消还没切走)而 gCliGeneration 已经走到 8,只比 turn
    // 的写法会把轮 7 的 delta 继续追加进用户正要放弃的那个条目。
    {
        Check(!TurnReplyIsCurrent(7, 7, 8),
              "当前轮没变但已经起过更新的一轮 → 不采用(只比 turn 的那一半会漏)");
        Check(!TurnReplyIsCurrent(7, 8, 8),
              "那一轮已经不是当前轮 → 不采用(取消后立刻重试时旧轮的 delta 会串进新一轮)");
        Check(TurnReplyIsCurrent(8, 8, 8), "同代同最新才采用");
    }

    // ---- 2. 主要失败形态二:只比 latest,不比 turn ----
    // 场景:轮 7 的 delta 抵达时,面板.current 已经切到轮 8(用户发了新的一句),
    // 而 gCliGeneration 也走到 8。`7 == 8` 为假,所以 latest 那一半挡住了。
    // 但若只比 latest 而 turn 那一侧被写成"总为真",delta 会落进**旧一轮**的条目。
    {
        Check(!TurnReplyIsCurrent(7, 6, 7),
              "那一轮已不是最新 → 不采用(只比 latest 的那一半会漏)");
        Check(!TurnReplyIsCurrent(8, 7, 8),
              "当前轮不是产生它的那一轮 → 不采用(回复串台)");
    }

    // ---- 3. 三方相等是唯一放行的组合 ----
    {
        Check(TurnReplyIsCurrent(1, 1, 1), "轮 1 全等 → 采用");
        Check(TurnReplyIsCurrent(0, 0, 0), "轮 0 全等 → 采用(零代号也是一个合法轮次)");
        Check(TurnReplyIsCurrent(1000000, 1000000, 1000000), "大代号全等 → 采用");
    }

    // ---- 4. 两边必须各自为真,不能互相代替 ----
    // 这不是同一句废话的两半:`turn` 是面板自己的当前轮,`latest` 是进程里最新起过的
    // 那一轮。取消后到"切走当前轮"之间有一段窗口,两者会不一致 —— 而那段窗口正是
    // 用户最快能撞上的时候。
    {
        Check(TurnReplyIsCurrent(5, 5, 5) && !TurnReplyIsCurrent(5, 6, 5) &&
                  !TurnReplyIsCurrent(5, 5, 6),
              "三个输入里任意一个不同就都不采用(没有捷径可走)");
    }

    // ---- 5. 次序无关 ----
    // 判定是对称的:先比 turn 再比 latest 与反过来必须给出同一个答案。
    // 否则"哪一边先比"会变成行为,而三处副本各自写一遍时最容易在这里走偏。
    {
        for (std::uint64_t a = 0; a < 4; ++a) {
            for (std::uint64_t b = 0; b < 4; ++b) {
                for (std::uint64_t c = 0; c < 4; ++c) {
                    const bool direct = TurnReplyIsCurrent(a, b, c);
                    const bool swapped = TurnReplyIsCurrent(a, c, b);
                    if (a == b && b == c) {
                        Check(direct, "全等必须放行(a=b=c=" + std::to_string(a) + ")");
                    } else {
                        Check(!direct, "非全等必须挡住(a=" + std::to_string(a) + ",b=" +
                                           std::to_string(b) + ",c=" + std::to_string(c) + ")");
                    }
                    // 交换 b/c 只在两者相等时结论相同,否则结论必须同为"挡住"。
                    if (direct || swapped) Check(direct == swapped, "放行必须同时放行");
                }
            }
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\nAI 回包代号比对:全部 %d 项通过\n", g_checks);
    return 0;
}
