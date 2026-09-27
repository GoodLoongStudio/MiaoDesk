#include "miaodesk/VisualReviewGate.h"

#include <string>

namespace miaodesk::creator {

const char* ToString(VisualIssueSeverity severity) noexcept {
    switch (severity) {
    case VisualIssueSeverity::Cosmetic: return "Cosmetic";
    case VisualIssueSeverity::Minor: return "Minor";
    case VisualIssueSeverity::Major: return "Major";
    case VisualIssueSeverity::Blocking: return "Blocking";
    }
    return "Unknown";
}

const char* ToString(VisualGateVerdict verdict) noexcept {
    switch (verdict) {
    case VisualGateVerdict::ReviewedClear: return "ReviewedClear";
    case VisualGateVerdict::ReviewedBlocking: return "ReviewedBlocking";
    case VisualGateVerdict::NotReviewed: return "NotReviewed";
    case VisualGateVerdict::RefusedHardFailure: return "RefusedHardFailure";
    }
    return "Unknown";
}

bool CountsAsLookedAt(VisualGateVerdict verdict) noexcept {
    return verdict == VisualGateVerdict::ReviewedClear ||
           verdict == VisualGateVerdict::ReviewedBlocking;
}

bool CountsAsPassingReview(VisualGateVerdict verdict) noexcept {
    return verdict == VisualGateVerdict::ReviewedClear;
}

bool HasBlockingIssue(const std::vector<VisualIssue>& issues) noexcept {
    for (const auto& issue : issues) {
        if (issue.severity == VisualIssueSeverity::Blocking) return true;
    }
    return false;
}

namespace {

std::string ExplainIssue(const VisualIssue& issue) {
    std::string text = issue.kind.empty() ? "未命名问题" : issue.kind;
    if (!issue.region.empty()) text += "@" + issue.region;
    text += "(" + std::string(ToString(issue.severity)) + ")";
    if (!issue.suggestion.empty()) text += ":" + issue.suggestion;
    return text;
}

} // namespace

VisualGateResult JudgeVisualReview(const VisualReviewSubmission& submission) {
    VisualGateResult result;

    // 1. 同包硬校验没过。它排在"看没看图"之前,因为它是更根本的那一关:
    //    硬校验决定这个包能不能装上,视觉分只决定它好不好看。后者翻不了前者。
    //    但**不能**因此把"没看图"咽回去 —— 那会让这一轮到底发生了什么重新变成
    //    糊涂账,而排障时最需要的正是这两件同时摆在眼前。
    if (!submission.hardValidationOk) {
        result.verdict = VisualGateVerdict::RefusedHardFailure;
        result.passes = false;
        result.sawTheImage = submission.imageDelivered;
        result.note = "同一份候选没有通过硬校验:" +
                      (submission.hardValidationReason.empty() ? std::string("原因见校验报告")
                                                               : submission.hardValidationReason) +
                      "。视觉评分不能覆盖这一条。";
        if (!submission.imageDelivered) {
            result.note += "另外这一轮图像也没有送达(" +
                           (submission.imageFailureReason.empty() ? std::string("原因未记录")
                                                                  : submission.imageFailureReason) +
                           "),所以也说不上看过图。";
        }
        return result;
    }

    // 2. 图像链路没通。无论模型说了什么,这一轮都没有看过图。
    //    "打算送"和"送进去了"不是一回事:评审报告可以在一张没到过模型眼前的
    //    图上生成,而它读起来和真看过一模一样。
    if (!submission.imageDelivered) {
        result.verdict = VisualGateVerdict::NotReviewed;
        result.sawTheImage = false;
        result.passes = false;
        result.note = "这一轮没有看图:图像没有送达(" +
                      (submission.imageFailureReason.empty() ? std::string("原因未记录")
                                                             : submission.imageFailureReason) +
                      ")。不得记为已审。";
        return result;
    }

    // 3. 帧绑的不是这个候选。拿上一版的截图去评这一版,是最隐蔽的一种"已看图"。
    if (submission.frameDigest != submission.candidateDigest) {
        result.verdict = VisualGateVerdict::NotReviewed;
        result.sawTheImage = false;
        result.passes = false;
        result.note = "送出去评审的那一帧不属于当前候选(帧绑 " +
                      (submission.frameDigest.empty() ? std::string("<空>")
                                                      : submission.frameDigest) +
                      ",当前候选 " +
                      (submission.candidateDigest.empty() ? std::string("<空>")
                                                          : submission.candidateDigest) +
                      ")。不能把这一份评审算作当前候选看过。";
        return result;
    }

    // 4. 真的看了图。按问题清单给结论。
    result.sawTheImage = true;
    if (HasBlockingIssue(submission.issues)) {
        result.verdict = VisualGateVerdict::ReviewedBlocking;
        result.passes = false;
        for (const auto& issue : submission.issues) {
            if (issue.severity == VisualIssueSeverity::Blocking) {
                result.blockingIssues.push_back(issue);
            }
        }
        std::string detail;
        for (std::size_t i = 0; i < result.blockingIssues.size(); ++i) {
            if (i != 0) detail += ";";
            detail += ExplainIssue(result.blockingIssues[i]);
        }
        result.note = "看过图,有 " + std::to_string(result.blockingIssues.size()) +
                      " 个阻塞问题:" + detail;
        return result;
    }

    result.verdict = VisualGateVerdict::ReviewedClear;
    result.passes = true;
    result.note = submission.issues.empty()
                      ? "看过图,没有提出阻塞问题。"
                      : "看过图,提出的问题都不阻塞(共 " + std::to_string(submission.issues.size()) +
                            " 条)。";
    return result;
}

} // namespace miaodesk::creator
