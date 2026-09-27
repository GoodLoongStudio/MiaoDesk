// CCA-07:有上限的自动修复。
//
// 这一份测试的重点是计划验收的后三条,因为它们各自对应一种真实失控:
//   * 不可修复 / 达到上限 -> 明确停止,并说出停在哪一条;
//   * 取消后 -> 不启动修复;
//   * 不得靠删掉用户需求使测试通过 -> 指令里必须带上用户原始需求,并禁止删它。
//
// 第一条(注入一个可修复参数错误可自动修正)也在这里:断言指令**只**针对那一个
// 问题,而不是把整份校验报告倒给模型 —— 后者会让模型重写整个包,而它只需要改一个字段。
#include "miaodesk/CreationRepairPlanner.h"

#include <cstdio>
#include <string>
#include <vector>

namespace miaodesk::creator {
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

void CheckEq(const std::string& actual, const std::string& expected, const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  %s\n        expected: %s\n        actual:   %s\n",
                    what.c_str(), expected.c_str(), actual.c_str());
    }
}

constexpr const char* kBrief = "要一个蓝色海洋动态壁纸,名字叫深海,带 3 个气泡层。";

content::ContentValidationIssue Issue(content::ContentValidationFailure failure,
                                     const char* file, const char* nodePath, const char* message,
                                     bool repairable) {
    content::ContentValidationIssue issue;
    issue.failure = failure;
    issue.file = file;
    issue.nodePath = nodePath;
    issue.message = message;
    issue.repairable = repairable;
    return issue;
}

// 一个只带一个可修复参数错误的校验结果。
RepairRequest RepairableParameterError(std::vector<RepairAttempt> history = {},
                                       std::uint32_t used = 0, std::uint32_t max = 2) {
    RepairRequest request;
    request.validation.ok = false;
    request.validation.issues.push_back(Issue(content::ContentValidationFailure::ParameterOutOfRange,
                                              "parameters.json", "layers[0].speed",
                                              "speed 必须在 0 到 4 之间,现在是 9。", true));
    request.history = std::move(history);
    request.repairRoundsUsed = used;
    request.repairRoundsMax = max;
    request.userBrief = kBrief;
    return request;
}

// ---------------------------------------------------------------------------
// 1. 一个可修复参数错误 -> 一条针对它的指令
// ---------------------------------------------------------------------------

void TestARepairableParameterErrorBecomesATargetedInstruction() {
    const auto plan = PlanRepair(RepairableParameterError());
    Check(plan.decision == RepairDecision::Repair, "可修复参数错误进入修复");
    Check(plan.targets.size() == 1, "目标只有那一个问题");
    // 指令必须带定位信息:文件名 + 字段路径 + 一句话。少了定位,模型只能在
    // 整个包里猜,而它猜的方向通常是重写一遍。
    Check(plan.instruction.find("parameters.json") != std::string::npos, "指令点明了文件");
    Check(plan.instruction.find("layers[0].speed") != std::string::npos, "指令点明了字段路径");
    Check(plan.instruction.find("0 到 4") != std::string::npos, "指令带上了校验器给的那句话");
    // 不该把整份报告倒给它。
    Check(plan.instruction.find("重写") != std::string::npos, "指令明确说不要重写整个包");
    Check(plan.instruction.find("只改") != std::string::npos, "而且只改列出的问题");
}

// ---------------------------------------------------------------------------
// 2. 不得靠删掉用户需求通过
// ---------------------------------------------------------------------------

void TestTheInstructionCarriesTheUsersBriefVerbatim() {
    const auto plan = PlanRepair(RepairableParameterError());
    // 原话必须在指令里,一字不改。
    Check(plan.instruction.find(kBrief) != std::string::npos, "指令带上用户原话需求");
    // 并且明确禁止删它。
    Check(plan.instruction.find("不能为了通过校验删掉") != std::string::npos,
          "指令明确禁止删需求");
    Check(plan.instruction.find("不要删除用户需求里的任何一项") != std::string::npos,
          "而且再说一遍:任何一项都不删");
    Check(plan.instruction.find("宿主会重新校验") != std::string::npos,
          "并且说明删了也过不了 —— 宿主会重验");

    // 缺 brief 就不自动修:那正是这个模块存在的理由。
    auto noBrief = RepairableParameterError();
    noBrief.userBrief.clear();
    const auto refused = PlanRepair(noBrief);
    Check(refused.decision == RepairDecision::StopNoBrief, "没有原始需求时不启动自动修复");
    Check(refused.reason.find("删需求") != std::string::npos, "且说明为什么不敢修");
    Check(refused.instruction.empty(), "且根本没有产出指令");
}

// ---------------------------------------------------------------------------
// 3. 不可修复的样本达到上限后停止
// ---------------------------------------------------------------------------

void TestUnrepairableIssuesStopImmediately() {
    RepairRequest request;
    request.userBrief = kBrief;
    request.validation.ok = false;
    request.validation.issues.push_back(Issue(content::ContentValidationFailure::UnsupportedRuntime,
                                              "manifest.json", "runtime",
                                              "Web 运行时不在本轮范围内。", false));
    const auto plan = PlanRepair(request);
    Check(plan.decision == RepairDecision::StopUnrepairable, "没有可修复问题时直接停");
    // 停的原因要把每一条都列出来 —— 用户需要知道卡在哪,而不是一句"修复失败"。
    Check(plan.reason.find("Web") != std::string::npos, "原因带上了那条具体错误");
    Check(plan.instruction.empty(), "且没有产出指令");
    Check(plan.targets.empty(), "也没有目标");
}

void TestBudgetAndWallClockStopTheRepair() {
    // 轮数用完。
    const auto exhausted = PlanRepair(RepairableParameterError({}, 2, 2));
    Check(exhausted.decision == RepairDecision::StopBudget, "轮数用完就停");
    Check(exhausted.reason.find("轮数") != std::string::npos, "且说明是轮数用完");
    Check(exhausted.instruction.empty(), "没有指令");

    // 还差一轮:继续。
    const auto oneLeft = PlanRepair(RepairableParameterError({}, 1, 2));
    Check(oneLeft.decision == RepairDecision::Repair, "还剩一轮就继续修");

    // 墙钟比轮数更根本:轮数是经验值,墙钟是用户已经等掉的时间。
    auto slow = RepairableParameterError({}, 0, 2);
    slow.wallClockLimitMs = 1000;
    slow.elapsedMs = 1000;
    const auto wallClock = PlanRepair(slow);
    Check(wallClock.decision == RepairDecision::StopWallClock, "墙钟到点就停");
    Check(wallClock.reason.find("时间上限") != std::string::npos, "且说明是时间上限");

    // 墙钟为 0 表示不设限 —— 不能把"没配上限"读成"已经超时"。
    auto unlimited = RepairableParameterError({}, 0, 2);
    unlimited.wallClockLimitMs = 0;
    unlimited.elapsedMs = 999999;
    Check(PlanRepair(unlimited).decision == RepairDecision::Repair, "墙钟为 0 表示不设限");
}

// ---------------------------------------------------------------------------
// 4. 取消后不再启动修复
// ---------------------------------------------------------------------------

void TestCancelledStopsTheRepairBeforeEverything() {
    // 取消优先于一切。哪怕有可修的问题、预算还多、需求也在。
    auto cancelled = RepairableParameterError();
    cancelled.cancelRequested = true;
    const auto plan = PlanRepair(cancelled);
    Check(plan.decision == RepairDecision::StopCancelled, "取消之后不启动修复");
    Check(plan.instruction.empty(), "且没有指令");
    Check(plan.reason.find("取消") != std::string::npos, "且说明是取消了");

    // 连"没有可修复问题"这种本来会提前返回的情形,也必须先看到取消。
    auto cancelledClean = RepairableParameterError();
    cancelledClean.validation.ok = true;
    cancelledClean.validation.issues.clear();
    cancelledClean.cancelRequested = true;
    Check(PlanRepair(cancelledClean).decision == RepairDecision::StopCancelled,
          "取消优先于一切 —— 否则调用方会以为这一轮还在跑");
}

// ---------------------------------------------------------------------------
// 5. 重复相同错误:修复没有起作用
// ---------------------------------------------------------------------------

void TestARepeatingErrorStopsInsteadOfLooping() {
    // 第一次:没有历史,修。
    const auto first = PlanRepair(RepairableParameterError());
    Check(first.decision == RepairDecision::Repair, "第一次修");

    // 同一条错误已经出现过两次:停。继续下去只是把同样的失败重复到预算用完,
    // 而用户看不到任何进展。
    std::vector<RepairAttempt> history(2);
    for (auto& attempt : history) {
        attempt.failure = ToString(content::ContentValidationFailure::ParameterOutOfRange);
        attempt.file = "parameters.json";
        attempt.nodePath = "layers[0].speed";
        attempt.message = "speed 必须在 0 到 4 之间,现在是 9。";
    }
    const auto repeating = PlanRepair(RepairableParameterError(history, 1, 5));
    Check(repeating.decision == RepairDecision::StopRepeating, "同一个错误出现两次就停");
    Check(repeating.reason.find("修复没有起作用") != std::string::npos, "且说明是修复没起作用");
    Check(repeating.instruction.empty(), "没有指令");

    // 只出现过一次:继续修(第一次修,第二次确认它是否偶然)。
    std::vector<RepairAttempt> once(1, history.front());
    const auto secondTry = PlanRepair(RepairableParameterError(once, 1, 5));
    Check(secondTry.decision == RepairDecision::Repair, "同一个错误只出现过一次就再修一次");

    // 位置不同的同类错误不算重复:那是另一个问题。
    std::vector<RepairAttempt> elsewhere(1, history.front());
    elsewhere.front().nodePath = "layers[1].speed";
    Check(PlanRepair(RepairableParameterError(elsewhere, 1, 5)).decision == RepairDecision::Repair,
          "位置不同的同类错误不算重复");
}

// ---------------------------------------------------------------------------
// 6. 混合情形:可修的修,不可修的说清
// ---------------------------------------------------------------------------

void TestMixedIssuesRepairWhatCanBeRepaired() {
    RepairRequest request;
    request.userBrief = kBrief;
    request.validation.ok = false;
    request.validation.issues.push_back(Issue(content::ContentValidationFailure::ParameterOutOfRange,
                                             "parameters.json", "speed",
                                             "speed 越界。", true));
    request.validation.issues.push_back(Issue(content::ContentValidationFailure::SchemaRejected,
                                              "scene/scene.json", "",
                                              "这个文件必须是 JSON 对象。", true));
    request.validation.issues.push_back(Issue(content::ContentValidationFailure::UnsupportedRuntime,
                                             "manifest.json", "runtime",
                                             "Web 运行时不在本轮范围内。", false));
    const auto plan = PlanRepair(request);
    Check(plan.decision == RepairDecision::Repair, "有可修的问题就继续修");
    Check(plan.targets.size() == 2, "目标只有那两个可修的");
    // 不可修的那条不该被带进指令 —— 带上去等于让模型去改一个改不动的东西。
    Check(plan.instruction.find("Web") == std::string::npos,
          "不可修的那条不进指令(改了也白改)");
    Check(plan.instruction.find("speed 越界") != std::string::npos, "可修的那条在");
    Check(plan.instruction.find("JSON 对象") != std::string::npos, "另一条可修的也在");
}

// ---------------------------------------------------------------------------
// 7. 上一版的情况
// ---------------------------------------------------------------------------

void TestThePreviousCandidateIsCarriedAlong() {
    // 计划原文:"保存需求与上一有效 revision"。修复必须知道上一版长什么样,
    // 否则它会在不知道基线的情况下重写。
    auto request = RepairableParameterError();
    request.lastCandidateSummary = "第 2 版:3 个图层,speed=9,深海主题。";
    const auto plan = PlanRepair(request);
    Check(plan.instruction.find("第 2 版") != std::string::npos, "指令带上上一版的情况");
    Check(plan.instruction.find("speed=9") != std::string::npos, "而且带上了它的具体内容");

    // 没有上一版时不编一段。
    const auto none = PlanRepair(RepairableParameterError());
    Check(none.decision == RepairDecision::Repair, "没有上一版也照样修");
    Check(none.instruction.find("上一版") == std::string::npos, "只是不假装有");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestARepairableParameterErrorBecomesATargetedInstruction();
    TestTheInstructionCarriesTheUsersBriefVerbatim();
    TestUnrepairableIssuesStopImmediately();
    TestBudgetAndWallClockStopTheRepair();
    TestCancelledStopsTheRepairBeforeEverything();
    TestARepeatingErrorStopsInsteadOfLooping();
    TestMixedIssuesRepairWhatCanBeRepaired();
    TestThePreviousCandidateIsCarriedAlong();

    std::printf("\nCCA-07 repair planner: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
