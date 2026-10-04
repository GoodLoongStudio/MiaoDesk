// SEARCH-05「Search → AI 连续上下文」的回归。
//
// 逮到的缺陷：面板**已经开着**且正忙时，用户在搜索框打的那段话会整段消失 ——
// 既不进输入框也不发出去。用户看到的是 AI 窗口被带到前台、输入框空空如也、
// 什么也没发生。SEARCH-05 的验收原话是"原查询自然成为对话上下文"，而这里它
// 什么都不是。
#include "miaodesk/MiaoSearchHandoff.h"

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

// 反空洞自检。一个恒返回 Drop 的判定会让每段话都消失（用户看得见的那半），
// 一个恒返回 PrefillAndSend 的判定会把第二轮接在一轮还没完的会话后面
// （串话，也是用户看得见的那半）。两个方向各喂一个。
bool VerdictStillMoves() {
    return DecideSearchHandoff(true, false, false) == SearchHandoffAction::PrefillAndSend &&
           DecideSearchHandoff(true, true, false) == SearchHandoffAction::PrefillOnly;
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:交接判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:空闲时发出去、忙时只填进去\n");
    ++g_checks;

    // ---- 1. 主要失败形态:面板忙着,那段话整段消失 ----
    {
        Check(DecideSearchHandoff(true, true, false) == SearchHandoffAction::PrefillOnly,
              "面板正忙时,那段话至少得进输入框(原来它整段消失,界面看着就是没反应)");
        Check(DecideSearchHandoff(true, false, true) == SearchHandoffAction::PrefillOnly,
              "在等用户确认时同样只填进去(此时发出去会接在一轮未完成的会话后面)");
        Check(DecideSearchHandoff(true, true, true) == SearchHandoffAction::PrefillOnly,
              "既忙又在等确认也只填进去");
    }

    // ---- 2. 空闲时照旧自动发 ----
    {
        Check(DecideSearchHandoff(true, false, false) == SearchHandoffAction::PrefillAndSend,
              "面板空闲、没有待确认 → 填进去并当场发");
    }

    // ---- 3. 空词 ----
    {
        Check(DecideSearchHandoff(false, false, false) == SearchHandoffAction::Drop,
              "空词没什么可带的(调用方照旧把面板带到前台并交出焦点)");
        Check(DecideSearchHandoff(false, true, false) == SearchHandoffAction::Drop,
              "空词 + 忙也是 Drop(没有词就没有可丢的东西)");
        Check(DecideSearchHandoff(false, false, true) == SearchHandoffAction::Drop,
              "空词 + 待确认也是 Drop");
    }

    // ---- 4. 不变式:非空的词永不被丢下 ----
    // 这条是整个改动的理由。用户刚打的一段话,"丢了它界面看起来就是点了没反应"。
    // 八种组合逐条过,任何一条非空 input 得到 Drop 都是缺陷。
    {
        for (int busy = 0; busy <= 1; ++busy) {
            for (int pending = 0; pending <= 1; ++pending) {
                const auto action = DecideSearchHandoff(true, busy == 1, pending == 1);
                Check(action != SearchHandoffAction::Drop,
                      "非空的词永不被丢下(busy=" + std::to_string(busy) +
                          ", pending=" + std::to_string(pending) + ")");
                Check(action == SearchHandoffAction::PrefillAndSend ||
                          action == SearchHandoffAction::PrefillOnly,
                      "非空的词不是发出去就是填进去,没有第三种下场");
            }
        }
    }

    // ---- 5. "只填"与"发了"必须真的不同 ----
    // 如果两者被判成同一个,那这段判定就白写了 —— 面板忙时用户会看到第二轮
    // 接在一轮没完的会话后面,而 SEARCH-05 要的"自然"正好被它毁掉。
    {
        Check(DecideSearchHandoff(true, true, false) != DecideSearchHandoff(true, false, false),
              "忙与不忙必须给出不同的动作(否则就是把第二轮接进未完成的会话)");
        Check(DecideSearchHandoff(true, false, true) != DecideSearchHandoff(true, false, false),
              "有待确认与没有也必须给出不同的动作");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n搜索 → AI 交接:全部 %d 项通过\n", g_checks);
    return 0;
}
