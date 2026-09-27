#pragma once

// CCA-09:视觉评审的门。它决定一次评审能不能算"看过图"、能不能把候选放过去。
//
// 计划验收里有两条是**编排不出来、只能靠门去挡**的:
//   * "图像传输失败不能记为'已看图'";
//   * "同包硬校验失败不能被高视觉分覆盖"。
//
// 这两条以前都没有执行点。宿主机把模型的一段评审原样当成 ReviewCompleted 喂进
// 状态机,于是"图没送进去"和"图送进去了、模型说没问题"在账上长得一模一样;
// 而一个漂亮的视觉分也随时可以把一份没通过硬校验的包装成"可以应用"。
//
// 这个门不放任何一个字进"模型说了什么"那一侧:它只收宿主**自己知道**的事实 ——
// 图像链路到底通没通、送出去的帧绑在哪个候选上、同一份候选的硬校验过没过 ——
// 再据此给一个结论。模型给的分数与问题清单**只是输入**,不能推翻这三件事实。
//
// 四个结论:
//   ReviewedClear        真的看了图,没有阻塞问题。
//   ReviewedBlocking     真的看了图,有具体可修复问题。
//   NotReviewed          没看图(链路失败 / 帧不属于这个候选)。如实标记,不给分。
//   RefusedHardFailure   同包硬校验没过。视觉再好也不算通过 —— 硬校验是包能不能
//                        装上,视觉分只是好不好看;后者翻不了前者。
//
// 它不 import Windows 头,于是"把失败记成看过"这一类在本机就能验。
#include <cstdint>
#include <string>
#include <vector>

namespace miaodesk::creator {

enum class VisualIssueSeverity {
    Cosmetic,      // 不影响使用
    Minor,
    Major,
    Blocking,      // 具体可修复的阻塞问题
};

const char* ToString(VisualIssueSeverity severity) noexcept;

// 一条具体问题。kind + region + suggestion 三样都要:只有"不好看"两个字的话,
// 修复轮不知道该改什么,而下一次评审还会报同一条 —— 那会把预算烧在同一个问题上。
struct VisualIssue {
    std::string kind;                    // 例如 "裁切" / "对比度不足" / "文字被遮挡"
    std::string region;                  // 例如 "top-left" / "全图"
    VisualIssueSeverity severity{VisualIssueSeverity::Minor};
    std::string suggestion;              // 具体到能据此改的一句话
};

enum class VisualGateVerdict {
    ReviewedClear,
    ReviewedBlocking,
    NotReviewed,
    RefusedHardFailure,
};

const char* ToString(VisualGateVerdict verdict) noexcept;

// 这个结论算不算"看图了"。NotReviewed 与 RefusedHardFailure 都不算 ——
// 后者尤其不能说成看过:它是被硬校验挡下的,和视觉无关。
bool CountsAsLookedAt(VisualGateVerdict verdict) noexcept;

// 这个结论能不能让候选走到"可以应用"。
bool CountsAsPassingReview(VisualGateVerdict verdict) noexcept;

bool HasBlockingIssue(const std::vector<VisualIssue>& issues) noexcept;

// 一次评审的输入事实。全部由宿主持有,模型说的一个字都不进来。
struct VisualReviewSubmission {
    std::string candidateDigest;        // 这次评审针对的候选
    std::string frameDigest;            // 实际送出去的那一帧绑定的候选摘要
    // 图像链路到底通没通。false 时无论模型说了什么,都只能记 NotReviewed。
    bool imageDelivered{false};
    std::string imageFailureReason;     // 送失败的原因,要能给人看
    // 同一份候选的硬校验结论。false 时视觉再好也不算通过。
    bool hardValidationOk{true};
    std::string hardValidationReason;
    // 模型的评审输出。只是输入。
    std::vector<VisualIssue> issues;
};

struct VisualGateResult {
    VisualGateVerdict verdict{VisualGateVerdict::NotReviewed};
    bool sawTheImage{false};
    bool passes{false};
    // 给人看的一句:为什么是这个结论。硬校验失败与链路失败要同时说清,
    // 不能因为说了前者就把后者咽回去 —— 那会让"到底看没看图"重新变成糊涂账。
    std::string note;
    // 可以直接送进修复轮的问题清单(仅在 ReviewedBlocking 时非空)。
    std::vector<VisualIssue> blockingIssues;
};

VisualGateResult JudgeVisualReview(const VisualReviewSubmission& submission);

} // namespace miaodesk::creator
