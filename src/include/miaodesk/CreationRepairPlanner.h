#pragma once

// CCA-07:有上限的自动修复(纯逻辑)。
//
// 计划验收原文:「注入一个可修复参数错误可自动修正」「不可修复样本达到上限后
// 停止」「取消后不再启动修复」「不得靠删掉用户要求或忽略 validator 使测试通过」。
//
// 这四条里,「不得靠删掉用户要求」是最容易被绕过的一条,所以这个模块的输入刻意
// 包含**用户的原始需求**:修复指令必须把它原样带给模型,并且明确禁止通过删需求
// 来让校验通过。少了这一条,一个"修好了"的候选可能只是把用户要的东西删掉了 ——
// 而校验器对此一无所知,它只检查结构。
//
// 另外三条各自对应一种真实的失控:
//   * 可修复参数错误 -> 一条带定位信息的修复指令(file + nodePath + 一句话),
//     而不是把整个校验报告倒给模型;
//   * 不可修复 / 达到上限 -> 明确停止,并说明停在哪一条;
//   * 取消后 -> 不启动。取消之后每一轮修复都是在替一个已经不存在的决定花时间。
//
// 它不 import Windows 头,所以这些边界在本机就能真验。
#include <cstdint>
#include <string>
#include <vector>

#include "miaodesk/ContentCandidateLedger.h"

namespace miaodesk::creator {

// 一次修复尝试的记录。用来发现"同一个错误反复出现"—— 那说明修复没有起作用,
// 继续下去只是把同样的失败重复到预算用完,而用户看不到任何进展。
struct RepairAttempt {
    std::uint32_t round{};
    std::string failure;      // ContentValidationFailure 的名字
    std::string file;
    std::string nodePath;
    std::string message;
};

enum class RepairDecision {
    Repair,        // 继续修:instruction 是一次可执行的修复指令
    StopNoIssues,  // 没有可修复的问题
    StopUnrepairable,  // 有问题,但没有一条是可修复的
    StopRepeating,     // 同一条错误已经出现过指定次数,修复没有起作用
    StopBudget,        // 修复轮数用完
    StopWallClock,     // 墙钟上限到了
    StopCancelled,     // 已取消
    StopNoBrief,       // 没有原始需求 —— 无从判断"有没有删掉用户要的东西"
};

const char* ToString(RepairDecision decision) noexcept;

// 修复调度的输入。brief 由宿主从会话记录里取,不从模型那边取。
struct RepairRequest {
    content::ContentValidationResult validation;
    std::vector<RepairAttempt> history;
    std::uint32_t repairRoundsUsed{};
    std::uint32_t repairRoundsMax{2};
    std::uint64_t elapsedMs{};
    std::uint64_t wallClockLimitMs{10ull * 60 * 1000};
    bool cancelRequested{false};
    std::string userBrief;          // 用户的原话需求
    std::string lastCandidateSummary;
};

struct RepairInstruction {
    RepairDecision decision{RepairDecision::StopNoIssues};
    std::string reason;             // 停止时说明停在哪;继续时说明修什么
    // 继续修复时给模型的那一段。它必须带上用户原始需求,并禁止删需求。
    std::string instruction;
    std::vector<content::ContentValidationIssue> targets;   // 这一轮要修的具体问题
};

// 同一个错误出现这么多次就停。默认 2:第一次修、第二次确认它是否偶然,
// 第三次还是同一条就说明这条路不通 —— 那时候该让用户看一眼,而不是继续烧预算。
constexpr std::uint32_t kRepairRepeatLimit = 2;

RepairInstruction PlanRepair(const RepairRequest& request);

} // namespace miaodesk::creator
