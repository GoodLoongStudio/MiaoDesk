#include "miaodesk/CreationRepairPlanner.h"

#include <algorithm>

namespace miaodesk::creator {
namespace {

bool SameIssue(const RepairAttempt& attempt, const content::ContentValidationIssue& issue) {
    // 同一个错误 = 同一个失败种类、同一个文件、同一个字段路径、同一句话。
    // 只按 message 比不够:两句话可能措辞相同而位置不同;只按位置比也不够。
    return attempt.failure == ToString(issue.failure) && attempt.file == issue.file &&
           attempt.nodePath == issue.nodePath && attempt.message == issue.message;
}

std::uint32_t TimesSeen(const std::vector<RepairAttempt>& history,
                        const content::ContentValidationIssue& issue) {
    std::uint32_t count = 0;
    for (const auto& attempt : history) {
        if (SameIssue(attempt, issue)) ++count;
    }
    return count;
}

RepairInstruction Stop(RepairDecision decision, std::string reason) {
    RepairInstruction result;
    result.decision = decision;
    result.reason = std::move(reason);
    return result;
}

} // namespace

const char* ToString(RepairDecision decision) noexcept {
    switch (decision) {
    case RepairDecision::Repair: return "Repair";
    case RepairDecision::StopNoIssues: return "StopNoIssues";
    case RepairDecision::StopUnrepairable: return "StopUnrepairable";
    case RepairDecision::StopRepeating: return "StopRepeating";
    case RepairDecision::StopBudget: return "StopBudget";
    case RepairDecision::StopWallClock: return "StopWallClock";
    case RepairDecision::StopCancelled: return "StopCancelled";
    case RepairDecision::StopNoBrief: return "StopNoBrief";
    }
    return "Unknown";
}

RepairInstruction PlanRepair(const RepairRequest& request) {
    // 1. 取消优先于一切。取消之后启动一轮修复,是在替一个已经不存在的决定花时间 ——
    //    而且它会把"已取消"这个状态又拖回生成中,用户看到的是取消没生效。
    if (request.cancelRequested) {
        return Stop(RepairDecision::StopCancelled, "这一轮已被取消,不再启动修复。");
    }
    // 2. 墙钟。它比轮数更根本:轮数是经验值,墙钟是用户已经等掉的时间。
    if (request.wallClockLimitMs != 0 && request.elapsedMs >= request.wallClockLimitMs) {
        return Stop(RepairDecision::StopWallClock, "已达本次创作的时间上限,停止自动修复。");
    }
    // 3. 轮数预算。
    if (request.repairRoundsMax != 0 && request.repairRoundsUsed >= request.repairRoundsMax) {
        return Stop(RepairDecision::StopBudget, "自动修复轮数已用完,停下来让用户看一眼当前候选。");
    }
    // 4. 没有原始需求就无法判断"有没有删掉用户要的东西"。缺它就别自动修:
    //    一个通过删需求"修好"的候选,比一个明确失败的候选更难发现。
    if (request.userBrief.empty()) {
        return Stop(RepairDecision::StopNoBrief,
                    "没有用户的原始需求,不启动自动修复 —— "
                    "否则无法判断候选是不是靠删需求通过的。");
    }
    // 5. 没有可修复的问题。
    if (request.validation.ok || request.validation.issues.empty()) {
        return Stop(RepairDecision::StopNoIssues, "校验通过,不需要修复。");
    }

    std::vector<content::ContentValidationIssue> targets;
    for (const auto& issue : request.validation.issues) {
        if (issue.repairable) targets.push_back(issue);
    }
    if (targets.empty()) {
        // 有问题但没有一条可修。把它们全列出来 —— 用户需要知道卡在哪,
        // 而不是看到一句"修复失败"。
        std::string reason = "这些不是自动改得动的问题:";
        for (const auto& issue : request.validation.issues) {
            reason += " [" + issue.file + (issue.nodePath.empty() ? "" : " " + issue.nodePath) +
                      "] " + issue.message;
        }
        return Stop(RepairDecision::StopUnrepairable, reason);
    }

    // 6. 同一个错误反复出现。说明这条路不通,继续只是把同样的失败重复到预算用完。
    for (const auto& issue : targets) {
        if (TimesSeen(request.history, issue) >= kRepairRepeatLimit) {
            return Stop(RepairDecision::StopRepeating,
                        std::string("同一个错误已经出现过 ") +
                            std::to_string(TimesSeen(request.history, issue)) + " 次,修复没有起作用:[" +
                            issue.file + (issue.nodePath.empty() ? "" : " " + issue.nodePath) + "] " +
                            issue.message);
        }
    }

    // 7. 组织一次修复指令。
    RepairInstruction result;
    result.decision = RepairDecision::Repair;
    result.targets = targets;

    std::string instruction;
    instruction += "内容包没有通过宿主校验,请只做下面这些局部修改。\n";
    instruction += "要修的问题:\n";
    for (const auto& issue : targets) {
        instruction += "- ";
        if (!issue.file.empty()) instruction += issue.file;
        if (!issue.nodePath.empty()) instruction += " 的 " + issue.nodePath;
        instruction += ": ";
        instruction += issue.message;
        instruction += "\n";
    }
    // 计划验收的第四条,写进指令本身:不得靠删掉用户要求或忽略校验通过。
    instruction += "\n用户的原始需求(必须逐条保留,不能为了通过校验删掉其中任何一条):\n";
    instruction += request.userBrief;
    instruction += "\n\n规则:\n";
    instruction += "- 只改上面列出的问题,不要重写整个包。\n";
    instruction += "- 不要删除用户需求里的任何一项,也不要用忽略校验的方式让它通过 —— "
                  "宿主会重新校验,删掉需求的版本仍然不会被认为满足需求。\n";
    instruction += "- 改完不要声称已修复,由宿主重新校验后告诉你结论。\n";
    if (!request.lastCandidateSummary.empty()) {
        instruction += "\n上一版的情况:\n";
        instruction += request.lastCandidateSummary;
        instruction += "\n";
    }

    result.instruction = instruction;
    result.reason = "发现 " + std::to_string(targets.size()) + " 个可修复的问题。";
    return result;
}

} // namespace miaodesk::creator
