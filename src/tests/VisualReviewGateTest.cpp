// CCA-09:视觉评审的门。
//
// 计划验收里两条是**编排不出来、只能靠门去挡**的:
//   * "图像传输失败不能记为'已看图'";
//   * "同包硬校验失败不能被高视觉分覆盖"。
//
// 这份测试把这两条各配一个只违反它的输入,并确认结论不是"看起来还行",而是
// **不能说成看过**:
//
//   链路没通     → NotReviewed      分数再高也不算,且 sawTheImage == false
//   帧不对        → NotReviewed      拿上一版的截图评这一版
//   硬校验没过    → RefusedHardFailure 视觉分一分都不顶用
//
// 第三条要特别注意它和第一条同时出现时的样子:那时的结论不能只说前者而把
// "没看图"咽回去 —— 排障时最需要的正是两件事同时摆在眼前。
//
// "没提供视觉能力的 Provider"走的是同一条 NotReviewed:如实标记,不给分。
#include "miaodesk/VisualReviewGate.h"

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

const char* kDigest = "d-1";

VisualReviewSubmission Saw(std::vector<VisualIssue> issues = {}) {
    VisualReviewSubmission submission;
    submission.candidateDigest = kDigest;
    submission.frameDigest = kDigest;
    submission.imageDelivered = true;
    submission.hardValidationOk = true;
    submission.issues = std::move(issues);
    return submission;
}

VisualIssue Blocking(const char* kind, const char* region, const char* suggestion) {
    VisualIssue issue;
    issue.kind = kind;
    issue.region = region;
    issue.severity = VisualIssueSeverity::Blocking;
    issue.suggestion = suggestion;
    return issue;
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    // --- 1. 真的看过、没有阻塞问题 -----------------------------------------
    {
        const auto result = JudgeVisualReview(Saw());
        Check(result.verdict == VisualGateVerdict::ReviewedClear, "看过且无阻塞问题 → Clear");
        Check(result.sawTheImage, "且它算看过");
        Check(result.passes, "且可以进入'可以应用'");
        Check(result.blockingIssues.empty(), "没有阻塞问题要修");
        CheckEq(std::string(ToString(result.verdict)), "ReviewedClear", "结论名稳定");
    }

    // 有提出但都不阻塞的问题:仍然 Clear,但不能因此说"没提问题"。
    {
        VisualIssue cosmetic;
        cosmetic.kind = "右下角略暗";
        cosmetic.region = "bottom-right";
        cosmetic.severity = VisualIssueSeverity::Cosmetic;
        cosmetic.suggestion = "可把该区域提亮 5%";
        const auto result = JudgeVisualReview(Saw({cosmetic}));
        Check(result.verdict == VisualGateVerdict::ReviewedClear, "不阻塞的问题不挡住应用");
        Check(result.note.find("不阻塞") != std::string::npos, "但结论说明提出过问题");
    }

    // --- 2. 看过、有阻塞问题 ------------------------------------------------
    {
        std::vector<VisualIssue> issues = {
            Blocking("标题被裁切", "top", "把标题那层下移 12px"),
            Blocking("对比度不足", "全图", "正文改用 #333 而不是 #999"),
            VisualIssue{"右下角略暗", "bottom-right", VisualIssueSeverity::Cosmetic,
                        "可提亮 5%"},
        };
        const auto result = JudgeVisualReview(Saw(issues));
        Check(result.verdict == VisualGateVerdict::ReviewedBlocking, "有阻塞问题 → Blocking");
        Check(result.sawTheImage, "它确实看过图");
        Check(!result.passes, "但不能进入'可以应用'");
        // 交给修复轮的**只有**阻塞那两条。把不阻塞的一起塞进去,修复轮就会去改一个
        // 本来就允许存在的小瑕疵,而预算烧在它身上。
        Check(result.blockingIssues.size() == 2, "交给修复轮的只有阻塞问题");
        Check(result.note.find("裁切") != std::string::npos, "结论带上具体问题");
        Check(result.note.find("#333") != std::string::npos, "并且带上能据此改的那句话");
        Check(result.note.find("右下角略暗") == std::string::npos, "不阻塞的那条不进结论");
        Check(HasBlockingIssue(issues), "HasBlockingIssue 认得阻塞问题");
        Check(!HasBlockingIssue({VisualIssue{}}), "默认严重度不算阻塞");
    }

    // --- 3. 图像传输失败不能记为"已看图" ------------------------------------
    {
        VisualReviewSubmission submission = Saw();
        submission.imageDelivered = false;
        submission.imageFailureReason = "Provider 多模态端点返回 413";
        // 注意:即使模型这段评审读起来完全正常,这一轮也不算看过。
        submission.issues = {};
        const auto result = JudgeVisualReview(submission);
        Check(result.verdict == VisualGateVerdict::NotReviewed, "图没送进去 → 未审");
        Check(!result.sawTheImage, "且不能算作看过");
        Check(!result.passes, "也不能进入'可以应用'");
        Check(!CountsAsLookedAt(result.verdict), "未审永远不算看过");
        Check(result.note.find("413") != std::string::npos, "结论带上链路失败的原因");
        Check(result.note.find("不得记为已审") != std::string::npos, "并写明不得记为已审");
    }

    // 链路失败**并且**模型还报了几个问题:仍然未审。
    // 那几条问题不会被拿去驱动修复轮 —— 它们来自一张没到过模型眼前的图。
    {
        VisualReviewSubmission submission = Saw({Blocking("裁切", "top", "下移 12px")});
        submission.imageDelivered = false;
        submission.imageFailureReason = "上传超时";
        const auto result = JudgeVisualReview(submission);
        Check(result.verdict == VisualGateVerdict::NotReviewed, "链路下失败时连问题一起不算");
        Check(result.blockingIssues.empty(), "且不把这几条问题交给修复轮");
    }

    // --- 4. 帧不是这个候选 --------------------------------------------------
    {
        VisualReviewSubmission submission = Saw();
        // 上一版的截图:摘要对不上。
        submission.frameDigest = "d-0";
        const auto result = JudgeVisualReview(submission);
        Check(result.verdict == VisualGateVerdict::NotReviewed, "帧不属于当前候选 → 未审");
        Check(!result.sawTheImage, "且不算看过");
        Check(result.note.find("不属于当前候选") != std::string::npos, "结论点明帧对不上");
    }

    // 空摘要同样不行:帧没绑上任何候选,它就不构成这一版的证据。
    {
        VisualReviewSubmission submission = Saw();
        submission.frameDigest.clear();
        const auto result = JudgeVisualReview(submission);
        Check(result.verdict == VisualGateVerdict::NotReviewed, "帧没有绑定候选 → 未审");
        Check(result.note.find("<空>") != std::string::npos, "结论把空的那一侧显式写出来");
    }

    // --- 5. 同包硬校验失败不能被高视觉分覆盖 ---------------------------------
    {
        // 这一份模型给了满分、一个问题都没提 —— 但它没有通过硬校验。
        const std::vector<VisualIssue> none;
        VisualReviewSubmission submission = Saw(none);
        submission.hardValidationOk = false;
        submission.hardValidationReason = "manifest.json 缺 version";
        const auto result = JudgeVisualReview(submission);
        Check(result.verdict == VisualGateVerdict::RefusedHardFailure, "硬校验没过 → 拒绝");
        Check(!result.passes, "视觉满分也顶不上这一条");
        Check(!CountsAsPassingReview(result.verdict), "它不算通过评审");
        Check(!CountsAsLookedAt(result.verdict), "而且不能因为'看了图'就当成看过");
        Check(result.note.find("version") != std::string::npos, "结论带上硬校验的原因");
        Check(result.note.find("不能覆盖") != std::string::npos, "并写明视觉评分不能覆盖它");
    }

    // 硬校验没过**同时**链路也失败:两件都必须说,不能说了一件就把另一件咽回去。
    {
        VisualReviewSubmission submission = Saw();
        submission.hardValidationOk = false;
        submission.hardValidationReason = "引用的素材不在包里";
        submission.imageDelivered = false;
        submission.imageFailureReason = "Provider 不支持视觉输入";
        const auto result = JudgeVisualReview(submission);
        Check(result.verdict == VisualGateVerdict::RefusedHardFailure, "硬校验失败优先");
        Check(!result.sawTheImage, "且如实说明这一轮没有看图");
        Check(result.note.find("不支持视觉输入") != std::string::npos,
              "链路失败的原因没有被咽回去");
        Check(result.note.find("素材") != std::string::npos, "硬校验的原因也在");
    }

    // --- 6. 没有视觉能力的 Provider 走的是同一条 -----------------------------
    {
        // 宿主持有同一个事实:图没送进去(Provider 不提供多模态)。
        VisualReviewSubmission submission = Saw();
        submission.imageDelivered = false;
        submission.imageFailureReason = "当前 Provider 未确证支持视觉输入,本轮不送图";
        const auto result = JudgeVisualReview(submission);
        Check(result.verdict == VisualGateVerdict::NotReviewed, "无视觉能力 → 如实标记未审");
        Check(!result.passes, "且不因为'没意见'就放行");
    }

    // --- 7. 两个判定函数互相不重叠 -------------------------------------------
    {
        Check(CountsAsLookedAt(VisualGateVerdict::ReviewedClear), "Clear 算看过");
        Check(CountsAsLookedAt(VisualGateVerdict::ReviewedBlocking), "Blocking 算看过");
        Check(!CountsAsLookedAt(VisualGateVerdict::NotReviewed), "未审不算看过");
        Check(!CountsAsLookedAt(VisualGateVerdict::RefusedHardFailure), "硬校验拒绝不算看过");
        Check(CountsAsPassingReview(VisualGateVerdict::ReviewedClear), "只有 Clear 算通过");
        Check(!CountsAsPassingReview(VisualGateVerdict::ReviewedBlocking), "Blocking 不算通过");
        Check(!CountsAsPassingReview(VisualGateVerdict::NotReviewed), "未审不算通过");
        Check(!CountsAsPassingReview(VisualGateVerdict::RefusedHardFailure), "硬校验拒绝不算通过");
    }

    std::printf("\nCCA-09 visual review gate: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
