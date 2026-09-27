#include "miaodesk/ContentReleaseGate.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace miaodesk::creator {
namespace {

// 配置项 → 人话。用一个表而不是十个 if:加一项只改一行,而且顺序稳定(报出来才读得清)。
//
// **刻意不用 static**:第一版把它写成函数内的 static,而它由入参初始化 —— 于是它
// 只按第一次传进来的那份配置构造,之后每次调用都返回第一次的结论。表现是一个
// 配置齐全的臂先被问过之后,**任何**缺字段的臂都读成"记全了"。
// 一个只在第二个入参上犯的错,测试里必须有第二个入参才看得见。
std::vector<std::pair<bool, const char*>> FieldsOf(const EvaluationArmConfig& config) {
    return {
        {!config.model.empty(), "model"},
        {!config.provider.empty(), "provider"},
        {!config.version.empty(), "version"},
        {!config.skillDigest.empty(), "skillDigest"},
        {!config.materialSet.empty(), "materialSet"},
        {!config.targetBackend.empty(), "targetBackend"},
        {!config.tools.empty(), "tools"},
        {config.callBudget != 0, "callBudget"},
        {config.wallClockBudgetMs != 0, "wallClockBudgetMs"},
        {config.costBudget != 0, "costBudget"},
    };
}

} // namespace

std::string EvaluationArmConfig::MissingFields() const {
    std::string missing;
    for (const auto& check : FieldsOf(*this)) {
        if (check.first) continue;
        if (!missing.empty()) missing += ",";
        missing += check.second;
    }
    return missing;
}

std::uint32_t EvaluationArm::CountId(const std::string& sampleId) const {
    std::uint32_t count = 0;
    for (const auto& sample : samples) {
        if (sample.sampleId == sampleId) ++count;
    }
    return count;
}

const char* ToString(EvaluationMode mode) noexcept {
    switch (mode) {
    case EvaluationMode::SameBudgetArchitecture: return "SameBudgetArchitecture";
    case EvaluationMode::QualityCeiling: return "QualityCeiling";
    }
    return "Unknown";
}

const char* ToString(ReleaseVerdict verdict) noexcept {
    switch (verdict) {
    case ReleaseVerdict::Release: return "Release";
    case ReleaseVerdict::RefuseUnrecordedConfig: return "RefuseUnrecordedConfig";
    case ReleaseVerdict::RefuseMixedBudgetArms: return "RefuseMixedBudgetArms";
    case ReleaseVerdict::RefuseBudgetContaminated: return "RefuseBudgetContaminated";
    case ReleaseVerdict::RefuseCriticalFailure: return "RefuseCriticalFailure";
    case ReleaseVerdict::RefuseDeletionOfRequirements: return "RefuseDeletionOfRequirements";
    case ReleaseVerdict::RefuseContaminatedSamples: return "RefuseContaminatedSamples";
    case ReleaseVerdict::RefuseInsufficientSamples: return "RefuseInsufficientSamples";
    case ReleaseVerdict::RefuseOverbroadClaim: return "RefuseOverbroadClaim";
    }
    return "Unknown";
}

namespace {

// §9 的放行线:关键可靠性失败为 0,受支持样本限次成功率不低于基线。
bool SameBudget(const EvaluationArmConfig& left, const EvaluationArmConfig& right) noexcept {
    return left.callBudget == right.callBudget &&
           left.wallClockBudgetMs == right.wallClockBudgetMs &&
           left.costBudget == right.costBudget;
}

// 一题的粗略得分,只用来判"这一题新流程是不是更好"。
// 它刻意不掺权重:掺了权重的比较会变成另一件要辩护的事。
std::uint32_t Score(const SampleOutcome& sample) {
    return (sample.firstTryPass ? 2u : 0u) + (sample.reachableWithinBudget ? 1u : 0u) +
           sample.requirementsPassed;
}

} // namespace

ReleaseDecision JudgeRelease(const ReleaseRequest& request) {
    ReleaseDecision decision;

    // --- 1. 配置必须两边都记全 -------------------------------------------------
    // 少一项,"更好"就归不到任何原因上,而报告里会写成"新架构更好"。
    const std::string baselineMissing = request.baseline.config.MissingFields();
    const std::string candidateMissing = request.candidate.config.MissingFields();
    if (!baselineMissing.empty() || !candidateMissing.empty()) {
        decision.verdict = ReleaseVerdict::RefuseUnrecordedConfig;
        decision.reason = "评测配置没有记全";
        if (!baselineMissing.empty()) decision.reason += ";基线缺 " + baselineMissing;
        if (!candidateMissing.empty()) decision.reason += ";新流程缺 " + candidateMissing;
        decision.reason += "。§9 要求模型、Provider、版本、工具、Skill、输入素材和目标后端"
                           "全部记录 —— 少一项,差异就归不到任何原因上。";
        return decision;
    }

    // --- 2. 两种口径不能混 ----------------------------------------------------
    // "相同总预算比较架构收益"与"更高质量预算观察质量上限"是两份报告。把前者写成
    // 后者,多花的那部分预算会被当成架构的功劳。
    if (request.mode == EvaluationMode::SameBudgetArchitecture &&
        !SameBudget(request.baseline.config, request.candidate.config)) {
        decision.verdict = ReleaseVerdict::RefuseMixedBudgetArms;
        decision.reason = "声明按相同总预算比较架构,但两臂预算不同(基线 " +
                          std::to_string(request.baseline.config.callBudget) + " 次调用 / 新流程 " +
                          std::to_string(request.candidate.config.callBudget) +
                          " 次)。§9:相同预算比架构,更高质量预算看上限,不能混成一组。";
        return decision;
    }

    // --- 3. 样本必须够,而且两臂一一对应 ---------------------------------------
    if (request.baseline.samples.empty() || request.candidate.samples.empty()) {
        decision.verdict = ReleaseVerdict::RefuseInsufficientSamples;
        decision.reason = "有一臂一个样本都没有,没有什么可比的。";
        return decision;
    }
    if (request.baseline.samples.size() != request.candidate.samples.size()) {
        decision.verdict = ReleaseVerdict::RefuseInsufficientSamples;
        decision.reason = "两臂样本数不同(基线 " +
                          std::to_string(request.baseline.samples.size()) + " / 新流程 " +
                          std::to_string(request.candidate.samples.size()) +
                          ")。同题同次数才能比;次数不同,比的是题不是架构。";
        return decision;
    }
    // 每一题都要在两臂各出现一次。重复与缺失都算样本接不上 ——
    // 次数不固定,成功率就没有分母。
    //
    // 只按基线走一遍就够,而且这一点是算术而不是猜:上面刚比过两边样本数相等,
    // 于是"基线的每一题都在候选方出现恰好一次"已经蕴含了两边是同一个集合。
    // 反过来再走一遍抓不到任何新情况 —— 我按对称写法试过,并构造了"候选方少一题
    // 多一题(总数仍相等)"这个用例去撞它,它照样被上面这一遍抓住。所以这里不保留
    // 一行抓不到东西的代码:它会让人以为这里有两道保险,而其实只有一道。
    for (const auto& sample : request.baseline.samples) {
        if (request.candidate.CountId(sample.sampleId) != 1 ||
            request.baseline.CountId(sample.sampleId) != 1) {
            decision.verdict = ReleaseVerdict::RefuseInsufficientSamples;
            decision.reason = "样本 " + sample.sampleId +
                              " 在两臂里不是各出现一次。§9 要求每题重复固定次数 —— "
                              "次数不固定,成功率就没有分母。";
            return decision;
        }
    }
    const std::uint32_t sampleCount = static_cast<std::uint32_t>(request.candidate.samples.size());
    if (sampleCount < request.minimumSamples) {
        decision.verdict = ReleaseVerdict::RefuseInsufficientSamples;
        decision.reason = "只有 " + std::to_string(sampleCount) + " 个样本,放行线是 " +
                          std::to_string(request.minimumSamples) +
                          " 个(§9:最低 10 个壁纸任务 + 10 个组件任务)。";
        return decision;
    }

    // --- 4. 评测样本不能与 skill 示例重叠 --------------------------------------
    // 否则是拿教过的东西考自己。这是自证里最不容易被发现的一种:
    // 样本是合法的、跑也跑了,只是它同时出现在 skill 的例子里。
    for (const auto& sample : request.candidate.samples) {
        const bool inExamples = std::find(request.skillExampleIds.begin(),
                                          request.skillExampleIds.end(),
                                          sample.sampleId) != request.skillExampleIds.end();
        if (inExamples) {
            decision.verdict = ReleaseVerdict::RefuseContaminatedSamples;
            decision.reason = "评测样本 " + sample.sampleId +
                              " 同时出现在 skill 示例集里。§9:不把评测示例直接加入 Skill 后"
                              "在原样本上自证。";
            return decision;
        }
    }

    // --- 5. 关键可靠性失败必须为 0 ---------------------------------------------
    for (const auto& sample : request.candidate.samples) {
        if (sample.criticalReliabilityFailures == 0) continue;
        decision.verdict = ReleaseVerdict::RefuseCriticalFailure;
        decision.reason = "样本 " + sample.sampleId + " 上有 " +
                          std::to_string(sample.criticalReliabilityFailures) +
                          " 次关键可靠性失败(误应用 / 重复实例 / 串会话 / 未停止修复 / 越界)。"
                          "初始放行要求它为 0 —— 一次误应用比十次成功更贵。";
        return decision;
    }

    // --- 6. 不得靠改动必需项提高成功率 ---------------------------------------
    // 两头都得堵:分母变小是删掉用户要求,分母变大是给对手加码。
    for (std::size_t i = 0; i < request.baseline.samples.size(); ++i) {
        const auto& baselineSample = request.baseline.samples[i];
        const auto& candidateSample = request.candidate.samples[i];
        if (candidateSample.requirementsPassed > candidateSample.requirementsOriginal) {
            decision.verdict = ReleaseVerdict::RefuseDeletionOfRequirements;
            decision.reason = "样本 " + candidateSample.sampleId + " 通过的必需项比总数还多(" +
                              std::to_string(candidateSample.requirementsPassed) + "/" +
                              std::to_string(candidateSample.requirementsOriginal) +
                              ")。这不是一次通过,这是一次记错。";
            return decision;
        }
        if (candidateSample.requirementsOriginal != baselineSample.requirementsOriginal) {
            decision.verdict = ReleaseVerdict::RefuseDeletionOfRequirements;
            // 两个方向说的是两件不同的事,所以分开写。第一版把"候选方分母更严"那条
            // 写成了"会让本来更好的基线看起来更差" —— 那正好说反了:更严的分母让
            // **候选方**看起来更差。一条解释错了的拒绝比没有拒绝更容易让人改错地方。
            const std::string direction =
                candidateSample.requirementsOriginal < baselineSample.requirementsOriginal
                    ? "§9:不得用删除要求提高成功率。"
                    : "两臂的必需项必须是同一份;分母不同,通过率就没有可比性。";
            decision.reason = "样本 " + candidateSample.sampleId + " 的必需项与基线不同(" +
                              std::to_string(baselineSample.requirementsOriginal) + " → " +
                              std::to_string(candidateSample.requirementsOriginal) + ")。" +
                              direction;
            return decision;
        }
    }

    // --- 7. 汇出数字 ----------------------------------------------------------
    for (const auto& sample : request.baseline.samples) {
        if (sample.firstTryPass) ++decision.firstTryBaseline;
        if (sample.reachableWithinBudget) ++decision.withinBudgetBaseline;
    }
    for (const auto& sample : request.candidate.samples) {
        if (sample.firstTryPass) ++decision.firstTryCandidate;
        if (sample.reachableWithinBudget) ++decision.withinBudgetCandidate;
        if (sample.blindVerdict > 0) ++decision.blindWins;
        else if (sample.blindVerdict < 0) ++decision.blindLosses;
        else ++decision.blindDraws;
    }

    // 限次成功率不得低于基线。低了就不是"效果更好",无论别的数字多好看。
    if (decision.withinBudgetCandidate < decision.withinBudgetBaseline) {
        decision.verdict = ReleaseVerdict::Release;
        decision.allowRelease = false;
        decision.summary = "限次成功率 " + std::to_string(decision.withinBudgetBaseline) +
                           " → " + std::to_string(decision.withinBudgetCandidate) +
                           " —— 比基线低,不构成收益。";
        return decision;
    }

    // --- 8. 预算污染 ----------------------------------------------------------
    // 只看"多花了调用才更好"的那些题。花得多本身不是错(质量上限那一路口径就是要
    // 花得多),错的是把它归因给架构。
    std::uint64_t candidateCalls = 0;
    std::uint64_t baselineCalls = 0;
    bool betterWithMoreCalls = false;
    for (std::size_t i = 0; i < request.baseline.samples.size(); ++i) {
        const auto& baselineSample = request.baseline.samples[i];
        const auto& candidateSample = request.candidate.samples[i];
        candidateCalls += candidateSample.callsUsed;
        baselineCalls += baselineSample.callsUsed;
        if (candidateSample.callsUsed > baselineSample.callsUsed &&
            Score(candidateSample) > Score(baselineSample)) {
            betterWithMoreCalls = true;
        }
    }
    decision.candidateExtraCalls =
        candidateCalls > baselineCalls ? candidateCalls - baselineCalls : 0;
    if (request.mode == EvaluationMode::SameBudgetArchitecture && betterWithMoreCalls) {
        decision.verdict = ReleaseVerdict::RefuseBudgetContaminated;
        decision.reason = "有样本是新流程多花了调用才更好的。§9:不能把更多的调用预算带来的"
                          "提升归因于 Agent 架构。要宣称架构收益,两臂的实际花费也得对齐,"
                          "或者改用更高质量预算那一组口径。";
        return decision;
    }

    // --- 9. 结论不许比样本大 ---------------------------------------------------
    if (request.claimsGeneralConclusion && request.generalConclusionBasisRun.empty()) {
        decision.verdict = ReleaseVerdict::RefuseOverbroadClaim;
        decision.reason = "只有 " + std::to_string(sampleCount) +
                          " 个样本,而结论说的是普遍最优。§9:20 题的小样本只支持"
                          "本样本结论。要往外说,得有一次更大的评测作依据并把它记下来。";
        return decision;
    }

    // --- 10. 放行 ------------------------------------------------------------
    decision.verdict = ReleaseVerdict::Release;
    decision.allowRelease = true;
    decision.summary = std::to_string(sampleCount) + " 个样本;首次有效率 " +
                       std::to_string(decision.firstTryBaseline) + " → " +
                       std::to_string(decision.firstTryCandidate) + ";限次成功率 " +
                       std::to_string(decision.withinBudgetBaseline) + " → " +
                       std::to_string(decision.withinBudgetCandidate) + ";人工对比 胜 " +
                       std::to_string(decision.blindWins) + " / 平 " +
                       std::to_string(decision.blindDraws) + " / 负 " +
                       std::to_string(decision.blindLosses) + ";新流程多花 " +
                       std::to_string(decision.candidateExtraCalls) + " 次调用";
    decision.reason = "数字支撑这句话:" + request.claim;
    if (decision.candidateExtraCalls > 0) {
        // 多花的部分必须单独披露,而不是混在"更好"里。
        decision.reason += "(额外耗时与费用按 §9 单独披露,不并入架构收益)";
    }
    return decision;
}

} // namespace miaodesk::creator
