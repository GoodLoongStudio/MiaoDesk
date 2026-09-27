#pragma once

// CCA-13:能不能说"效果更好" —— 以及什么情况下**不许**这么说。
//
// 计划 §9 写了几条不许,它们此前只有文字:
//   * "相同总预算用于比较架构收益;更高质量预算用于观察质量上限,不能混在一组";
//   * "不能把更多的调用预算带来的提升归因于 Agent 架构";
//   * "不把评测示例直接加入 Skill 后在原样本上自证";
//   * "需求符合度:不得用删除要求提高成功率";
//   * "关键可靠性失败为 0";
//   * "20 题的小样本只支持本样本结论,不声称对所有创作普遍最优";
//   * "模型、Provider、版本、工具、Skill、输入素材和目标后端全部记录"。
//
// 这几条共同的特点是:**它们要靠执行者主动约束自己**。而一次自己给自己的评测
// 天然有动机松一点 —— 多给新流程一点预算、把评测用的样例顺手加进 skill、把需求
// 表里的两行删掉再统计成功率。每一种都让数字变好,每一种都看不出来。
//
// 所以这个门只做一件事:把记录与指标读进来,给出一个**能不能对外说**的结论,
// 以及在什么情况下不能。它不产生数字、不访问任何服务 —— 数字是评测跑出来的,
// 而"这组数字能不能支撑那句话"必须是可判定的。
//
// 八个拒绝各自对应上面一条:
//   RefuseUnrecordedConfig      两臂有字段没记录全
//   RefuseMixedBudgetArms       两种预算口径混进一组
//   RefuseBudgetContaminated    靠多花预算拿到的差异
//   RefuseCriticalFailure       关键可靠性失败不是 0
//   RefuseDeletionOfRequirements 靠删需求提高成功率
//   RefuseContaminatedSamples   评测样本与 skill 示例重叠
//   RefuseInsufficientSamples    样本不够
//   RefuseOverbroadClaim        样本比结论小
//
// 它不 import Windows 头,于是"自证"这一类在本机就能验。
#include <cstdint>
#include <string>
#include <vector>

namespace miaodesk::creator {

// 一臂的配置。两臂要能比,这两份都得记全 —— 少一项,"更好"就归不到任何原因上,
// 而报告里会写成"新架构更好"。
struct EvaluationArmConfig {
    std::string model;
    std::string provider;
    std::string version;         // 基线 / 新流程
    std::string skillDigest;     // 用到的 skill 摘要
    std::string materialSet;     // 输入素材集
    std::string targetBackend;   // 目标后端
    std::vector<std::string> tools;
    std::uint32_t callBudget{0};
    std::uint64_t wallClockBudgetMs{0};
    std::uint64_t costBudget{0};

    // 缺哪一项。空表示记全了。
    std::string MissingFields() const;
};

// §9 的两种报告口径。**不同口径的数字不能放进同一组**。
enum class EvaluationMode {
    SameBudgetArchitecture,   // 相同总预算,比的是架构
    QualityCeiling,           // 更高质量预算,看的是上限,不比架构
};

const char* ToString(EvaluationMode mode) noexcept;

struct SampleOutcome {
    std::string sampleId;
    bool firstTryPass{false};              // 首次有效率
    bool reachableWithinBudget{false};     // 限次成功率
    // 需求符合度:必需项通过数 / 原始必需项数。分母变小是删除要求,
    // 而那是提高成功率的作弊路径。
    std::uint32_t requirementsPassed{0};
    std::uint32_t requirementsOriginal{0};
    std::uint32_t callsUsed{0};
    std::uint64_t durationMs{0};
    std::uint64_t costUnits{0};
    // 误应用、重复实例、串会话、未停止修复、越界操作。计划要求它为 0。
    std::uint32_t criticalReliabilityFailures{0};
    // 匿名人工对比:评审者看不到标签,所以这里只记结论。
    std::int32_t blindVerdict{0};          // +1 胜 / 0 平 / -1 负
};

struct EvaluationArm {
    EvaluationArmConfig config;
    std::vector<SampleOutcome> samples;

    std::uint32_t CountId(const std::string& sampleId) const;
};

struct ReleaseRequest {
    EvaluationMode mode{EvaluationMode::SameBudgetArchitecture};
    EvaluationArm baseline;
    EvaluationArm candidate;
    // skill 与文档里的示例集。评测样本与它重名就是自证。
    std::vector<std::string> skillExampleIds;
    // 是不是想说出"对所有创作都更好"。§9 明确不支持小样本这么说。
    bool claimsGeneralConclusion{false};
    // 真要对外说"普遍",得有另一次更大的评测作依据,并在这里记录是哪一次。
    // 空 = 没有。这不是形式:空着就外推,是绝大多数"效果最好"的来源。
    std::string generalConclusionBasisRun;
    // 放行线。§9 最低 10 个壁纸 + 10 个组件。
    std::uint32_t minimumSamples{20};
    std::string claim;    // 想对外说的那句话,拒绝时原样回给它
};

enum class ReleaseVerdict {
    Release,
    RefuseUnrecordedConfig,
    RefuseMixedBudgetArms,
    RefuseBudgetContaminated,
    RefuseCriticalFailure,
    RefuseDeletionOfRequirements,
    RefuseContaminatedSamples,
    RefuseInsufficientSamples,
    RefuseOverbroadClaim,
};

const char* ToString(ReleaseVerdict verdict) noexcept;

struct ReleaseDecision {
    ReleaseVerdict verdict{ReleaseVerdict::RefuseUnrecordedConfig};
    bool allowRelease{false};
    std::string reason;
    // 对比出来的数字。只报,不说 —— 结论由 verdict 给。
    std::uint32_t firstTryBaseline{};
    std::uint32_t firstTryCandidate{};
    std::uint32_t withinBudgetBaseline{};
    std::uint32_t withinBudgetCandidate{};
    std::int32_t blindWins{};
    std::int32_t blindLosses{};
    std::int32_t blindDraws{};
    std::uint64_t candidateExtraCalls{};
    std::string summary;
};

ReleaseDecision JudgeRelease(const ReleaseRequest& request);

} // namespace miaodesk::creator
