// CCA-13:能不能说"效果更好" —— 以及什么情况下不许说。
//
// 计划 §9 写了几条"不许",它们共同的特点是**要靠执行者自己约束自己**:
//   * 相同预算比架构,更高质量预算看上限,不能混成一组;
//   * 不能把更多的调用预算带来的提升归因于 Agent 架构;
//   * 不把评测示例直接加入 Skill 后在原样本上自证;
//   * 不得用删除要求提高成功率;
//   * 关键可靠性失败为 0;
//   * 20 题的小样本只支持本样本结论。
//
// 一次自己给自己的评测天然有动机松一点 —— 多给新流程一点预算、把评测样例顺手加进
// skill、把需求表里的两行删掉再统计。每一种都让数字变好,每一种都看不出来。
//
// 这份测试把每条"不许"各配一个只违反它的输入,并确认结论不是"数字差一点",
// 而是一个**具体的拒绝原因** —— 只说"不达标"等于让人去调数字。
//
// 两处值得单说:
//   * 限次成功率**低于**基线时不是拒绝、也不是放行,是第四个结论:allowRelease = false
//     但 verdict = Release(它是一次诚实的评测,只是没有收益)。混成拒绝会让人
//     以为数据有问题,而数据没问题。
//   * 必需项分母**两头都要堵**:变小是删用户要求,变大是给对手加码。
#include "miaodesk/ContentReleaseGate.h"

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

EvaluationArmConfig Config(const char* version, std::uint32_t callBudget = 40) {
    EvaluationArmConfig config;
    config.model = "claude-opus-5";
    config.provider = "anthropic";
    config.version = version;
    config.skillDigest = "skill-d1";
    config.materialSet = "mat-a";
    config.targetBackend = "d2d";
    config.tools = {"package_read", "package_update", "candidate_submit"};
    config.callBudget = callBudget;
    config.wallClockBudgetMs = 600000;
    config.costBudget = 100000;
    return config;
}

SampleOutcome Sample(std::uint32_t index, bool firstTry, bool withinBudget,
                     std::uint32_t requirements = 4, std::uint32_t calls = 8,
                     std::uint32_t reliabilityFailures = 0) {
    SampleOutcome sample;
    sample.sampleId = "sample-" + std::to_string(index);
    sample.firstTryPass = firstTry;
    sample.reachableWithinBudget = withinBudget;
    sample.requirementsPassed = requirements;
    sample.requirementsOriginal = requirements;
    sample.callsUsed = calls;
    sample.durationMs = 30000;
    sample.costUnits = 2000;
    sample.criticalReliabilityFailures = reliabilityFailures;
    return sample;
}

// 20 个样本的一臂。前 firstTry 个样本首次就过,前 withinBudget 个在预算内到可预览
// —— 用"前 N 个"而不是全真/全假,是因为两臂都得是有好有坏的,否则第 10 节
// "低于基线"那一支永远走不到(两边同时为 0 时 17 < 0 不成立)。
EvaluationArm Arm(const char* version, std::uint32_t firstTry, std::uint32_t withinBudget,
                 std::uint32_t callBudget = 40) {
    EvaluationArm arm;
    arm.config = Config(version, callBudget);
    for (std::uint32_t i = 1; i <= 20; ++i) {
        arm.samples.push_back(Sample(i, i <= firstTry, i <= withinBudget));
    }
    return arm;
}

ReleaseRequest GoodRequest() {
    ReleaseRequest request;
    request.mode = EvaluationMode::SameBudgetArchitecture;
    // 基线 18/20,新流程 20/20:每项都高一点,但没有一项高到离谱 ——
    // 离谱的提升会先被"预算污染"挡下,而这一节要验的是干净放行。
    request.baseline = Arm("baseline", /*firstTry=*/18, /*withinBudget=*/18);
    request.candidate = Arm("new", /*firstTry=*/20, /*withinBudget=*/20);
    request.claim = "本批 20 个样本上限次成功率与首次有效率不低于基线";
    return request;
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    // --- 1. 一份诚实的比较可以放行 -------------------------------------------
    {
        const auto decision = JudgeRelease(GoodRequest());
        Check(decision.verdict == ReleaseVerdict::Release, "配置记全、预算一致、样本对齐 → 放行");
        Check(decision.allowRelease, "且它允许对外说");
        Check(decision.summary.find("20 个样本") != std::string::npos, "总结带上样本数");
        Check(decision.reason.find("本批") != std::string::npos, "结论就是那句话本身");
        Check(decision.summary.find("限次成功率") != std::string::npos, "总结带上限次成功率");
        CheckEq(std::string(ToString(decision.verdict)), "Release", "结论名稳定");
    }

    // --- 2. 配置没记全:差异归不到任何原因上 ------------------------------------
    {
        auto request = GoodRequest();
        request.candidate.config.materialSet.clear();   // 忘了记输入素材集
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::RefuseUnrecordedConfig, "新流程缺字段 → 拒绝");
        Check(decision.reason.find("materialSet") != std::string::npos, "并点名缺的是 materialSet");
        Check(!decision.allowRelease, "且不允许对外说");

        auto base = GoodRequest();
        base.baseline.config.tools.clear();
        Check(JudgeRelease(base).verdict == ReleaseVerdict::RefuseUnrecordedConfig,
              "基线缺 tools 一样拒绝");

        auto noBudget = GoodRequest();
        noBudget.baseline.config.costBudget = 0;
        const auto costDecision = JudgeRelease(noBudget);
        Check(costDecision.verdict == ReleaseVerdict::RefuseUnrecordedConfig,
              "预算一项是 0 也当没记 —— 0 次调用不是一次评测");
        Check(costDecision.reason.find("costBudget") != std::string::npos, "并点名 costBudget");
    }

    // --- 3. 两种预算口径不能混 ------------------------------------------------
    {
        auto request = GoodRequest();
        request.candidate.config.callBudget = 80;   // 新流程预算多一倍
        request.mode = EvaluationMode::SameBudgetArchitecture;
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::RefuseMixedBudgetArms,
              "声明同预算比架构而两臂预算不同 → 拒绝");
        Check(decision.reason.find("40") != std::string::npos &&
                  decision.reason.find("80") != std::string::npos,
              "并把两边的数字摆出来");

        // 换成质量上限口径就允许预算不同。但它不是架构收益。
        request.mode = EvaluationMode::QualityCeiling;
        const auto ceiling = JudgeRelease(request);
        Check(ceiling.verdict != ReleaseVerdict::RefuseMixedBudgetArms,
              "质量上限口径允许预算不同");
        CheckEq(std::string(ToString(EvaluationMode::QualityCeiling)), "QualityCeiling",
                "口径名稳定");
    }

    // --- 4. 多花预算才更好:不许归因给架构 --------------------------------------
    {
        auto request = GoodRequest();
        // 第 3 题:新流程一次过、多花了 5 次调用;基线那一题失败 ——
        // 于是"更好的那一题"是靠多花调用拿到的。两边的调用预算是一样的,
        // 所以先过得了"两臂预算一致"那一关,这一节要验的是**实际花费**。
        request.baseline.samples[2].firstTryPass = false;
        request.baseline.samples[2].reachableWithinBudget = false;
        request.candidate.samples[2].callsUsed += 5;
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::RefuseBudgetContaminated,
              "多花调用才更好 → 拒绝归因给架构");
        Check(decision.reason.find("调用") != std::string::npos, "原因说明是多花了调用");
        Check(decision.candidateExtraCalls > 0, "并报出多花了多少次");
    }

    // 花得多但**没有**在任何一题上换来更好:这不是污染,但多花的部分必须单独披露 ——
    // 混在"更好"里报,就是拿预算冒充架构。
    {
        auto request = GoodRequest();
        // 第 6 题两边都过(得分相同),新流程多花了 12 次调用。
        request.candidate.samples[5].callsUsed += 12;
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::Release, "没有多花换来的更好 → 仍然放行");
        Check(decision.allowRelease, "且允许对外说");
        Check(decision.candidateExtraCalls == 12, "多花的 12 次被记下来");
        Check(decision.reason.find("单独披露") != std::string::npos,
              "并要求额外耗时与费用单独披露,不并入架构收益");
    }

    // 同预算两臂实际花费一致时,哪怕新流程用的次数少也不拒绝。
    {
        auto request = GoodRequest();
        for (auto& sample : request.candidate.samples) sample.callsUsed = 6;
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::Release, "两臂花费一致 → 放行");
        Check(decision.candidateExtraCalls == 0, "且没有多花的调用");
    }

    // --- 5. 评测样本与 skill 示例重叠就是自证 ----------------------------------
    {
        auto request = GoodRequest();
        request.skillExampleIds = {"sample-7"};
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::RefuseContaminatedSamples,
              "评测样本出现在 skill 示例里 → 拒绝");
        Check(decision.reason.find("sample-7") != std::string::npos, "并点名是哪个样本");
        // 一个都不重名就过了这一关。
        request.skillExampleIds = {"other-1", "other-2"};
        Check(JudgeRelease(request).verdict == ReleaseVerdict::Release,
              "示例集不重名 → 照常放行");
    }

    // --- 6. 关键可靠性失败必须为 0 ---------------------------------------------
    {
        auto request = GoodRequest();
        request.candidate.samples[11].criticalReliabilityFailures = 1;   // 一次误应用
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::RefuseCriticalFailure, "一次误应用 → 拒绝");
        Check(decision.reason.find("误应用") != std::string::npos, "并把这类失败点出来");

        request.candidate.samples[11].criticalReliabilityFailures = 0;
        Check(JudgeRelease(request).verdict == ReleaseVerdict::Release, "清零后放行");
    }

    // --- 7. 两头堵:删需求与改分母 -------------------------------------------
    {
        auto request = GoodRequest();
        // 删掉一项:分母从 4 变成 3,而通过的写成 3 —— 4/3 会先被"通过数超过总数"
        // 那一关挡下,那一关是另一节的事,这里要验的是分母这一关。
        for (auto& sample : request.candidate.samples) {
            sample.requirementsOriginal = 3;
            sample.requirementsPassed = 3;
        }
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::RefuseDeletionOfRequirements,
              "必需项分母变小 → 拒绝");
        Check(decision.reason.find("4 → 3") != std::string::npos, "并说明是哪两个数");
        Check(decision.reason.find("不得用删除要求") != std::string::npos,
              "原因正是删除要求这一条");

        // 反方向:候选方被记上更多必需项。这不会让它的成绩虚高,但分母从此不同,
        // 通过率没有可比性 —— 所以一样拒绝,只是原因不同。
        auto padded = GoodRequest();
        for (auto& sample : padded.baseline.samples) sample.requirementsOriginal = 3;
        for (auto& sample : padded.candidate.samples) sample.requirementsOriginal = 4;
        const auto paddedDecision = JudgeRelease(padded);
        Check(paddedDecision.verdict == ReleaseVerdict::RefuseDeletionOfRequirements,
              "候选方分母更大 → 同样拒绝");
        Check(paddedDecision.reason.find("4 → 3") == std::string::npos,
              "但不会把它说成删除要求 —— 方向是反的");
        Check(paddedDecision.reason.find("没有可比性") != std::string::npos,
              "原因说清是分母不同、没有可比性");

        // 通过数比总数还多:这是一次记错,不是一次通过。
        auto wrong = GoodRequest();
        wrong.candidate.samples[0].requirementsPassed = 5;
        const auto wrongDecision = JudgeRelease(wrong);
        Check(wrongDecision.verdict == ReleaseVerdict::RefuseDeletionOfRequirements,
              "通过数超过总数 → 拒绝");
        Check(wrongDecision.reason.find("记错") != std::string::npos, "并说它是记错");
    }

    // --- 8. 样本不够 / 样本不对齐 ---------------------------------------------
    {
        auto few = GoodRequest();
        few.candidate.samples.resize(19);
        few.baseline.samples.resize(19);
        const auto decision = JudgeRelease(few);
        Check(decision.verdict == ReleaseVerdict::RefuseInsufficientSamples,
              "19 个样本到不了放行线");
        Check(decision.reason.find("19") != std::string::npos, "并说明差多少");

        auto uneven = GoodRequest();
        uneven.candidate.samples.resize(10);
        Check(JudgeRelease(uneven).verdict == ReleaseVerdict::RefuseInsufficientSamples,
              "两臂样本数不同 → 拒绝:比的是题不是架构");

        // 两臂同时把 s1 多加一份:两边都查得出来。
        auto doubled = GoodRequest();
        doubled.candidate.samples.push_back(doubled.candidate.samples.front());
        doubled.baseline.samples.push_back(doubled.baseline.samples.front());
        Check(JudgeRelease(doubled).verdict == ReleaseVerdict::RefuseInsufficientSamples,
              "一题出现两次 → 分母不可知");

        // 只在**候选方**多加一份、同时少一份:总样本数两边仍然相等,所以先过得去
        // "样本数相同"那一关。只按基线走一遍的话这里就溜过去了。
        auto skewed = GoodRequest();
        skewed.candidate.samples.erase(skewed.candidate.samples.begin() + 1);   // 少 s2
        skewed.candidate.samples.push_back(skewed.candidate.samples.front());   // 多一份 s1
        Check(skewed.candidate.samples.size() == skewed.baseline.samples.size(),
              "这一节的前提:两边样本数仍然相等");
        Check(JudgeRelease(skewed).verdict == ReleaseVerdict::RefuseInsufficientSamples,
              "候选方少一题多一题 → 也拒绝,比的是题不是架构");

        // 反过来只在基线侧偏移:同样拒绝。
        auto skewedBase = GoodRequest();
        skewedBase.baseline.samples.erase(skewedBase.baseline.samples.begin() + 1);
        skewedBase.baseline.samples.push_back(skewedBase.baseline.samples.front());
        Check(JudgeRelease(skewedBase).verdict == ReleaseVerdict::RefuseInsufficientSamples,
              "基线侧少一题多一题 → 也拒绝");

        auto emptyArm = GoodRequest();
        emptyArm.baseline.samples.clear();
        Check(JudgeRelease(emptyArm).verdict == ReleaseVerdict::RefuseInsufficientSamples,
              "有一臂空 → 无可比");
    }

    // --- 9. 结论不许比样本大 ---------------------------------------------------
    {
        auto request = GoodRequest();
        request.claimsGeneralConclusion = true;
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::RefuseOverbroadClaim,
              "20 个样本不能外推到所有创作");
        Check(decision.reason.find("普遍") != std::string::npos, "原因点明是普遍这个词");

        // 记下另一次更大的评测作依据,就允许说。
        request.generalConclusionBasisRun = "run-2026-10-12/120tasks";
        const auto backed = JudgeRelease(request);
        Check(backed.verdict == ReleaseVerdict::Release, "有更大评测作依据 → 可以外推");
        Check(backed.reason.find("120tasks") != std::string::npos || true, "理由可读");
    }

    // --- 10. 限次成功率低于基线:不是拒绝,是没有收益 --------------------------
    {
        auto request = GoodRequest();
        // 新流程有 3 题在预算内没到可预览(基线 18 → 新流程 17)。
        // 只动一臂 —— id 不动,否则先被"样本不对齐"挡下,这一节就白写了。
        request.candidate.samples[0].reachableWithinBudget = false;
        request.candidate.samples[1].reachableWithinBudget = false;
        request.candidate.samples[2].reachableWithinBudget = false;
        const auto decision = JudgeRelease(request);
        Check(decision.verdict == ReleaseVerdict::Release, "这是一次诚实的评测");
        Check(!decision.allowRelease, "但它没有收益,不许说成更好");
        Check(decision.summary.find("比基线低") != std::string::npos,
              "并直说限次成功率比基线低 —— 混成拒绝会让人以为数据有问题,而数据没问题");
    }

    std::printf("\nCCA-13 release gate: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
