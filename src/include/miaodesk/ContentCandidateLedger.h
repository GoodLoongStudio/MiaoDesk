#pragma once

// CCA-05:结构化候选提交与 revision 台账(宿主侧)。
//
// 计划原文:"候选提交返回结构化 receipt；宿主通过真正的包校验服务验证、复制封存、
// 生成 digest/revision"、"同路径改内容生成新 revision"、"封存后修改源目录不能改变
// 待应用候选"。
//
// 这个台账把上面三条变成可判定的算术。它刻意不碰文件系统:复制封存由宿主做,
// 台账只记录"封存了什么、摘要是什么、是第几版、校验结论是什么",并据此决定
// 下一次提交是"同一版"还是"新版"。
//
// 为什么必须由宿主分配 revision,而不是沿用模型给的 revision:
//   模型提交的路径、ID、成功声明全部按输入处理,不能成为事实来源(计划 §4.1)。
//   让模型说"这是第 3 版"的话,它可以把旧版说成新版,也可以把两个不同的包说成同一版。

#include <cstdint>
#include <string>
#include <vector>

#include "miaodesk/ContentCandidateDigest.h"

namespace miaodesk::content {

// 校验失败的种类。结构化是为了让修复能定位;一句散文做不到。
enum class ContentValidationFailure {
    None,
    KindMismatch,        // 包类型与当前模式不符
    MissingManifestField,
    UnsupportedRuntime,  // Web/Script 之类不在本轮范围内
    MissingReferencedFile,
    InvalidBinding,
    ParameterOutOfRange,
    BackendUnsupported,
    SizeLimit,
    SchemaRejected,
    Unrepairable,
    ServiceUnavailable,
};

const char* ToString(ContentValidationFailure failure) noexcept;

// 一条定位信息。文件 + 节点/字段路径,两者至少有一个。
struct ContentValidationIssue {
    ContentValidationFailure failure{ContentValidationFailure::None};
    std::string file;         // 包内相对路径,例如 "scene/scene.json"
    std::string nodePath;     // 例如 "layers[0].opacity"
    std::string message;      // 给模型看的一句话,必须能据此改
    bool repairable{false};   // 能否进入有上限自动修复
};

struct ContentValidationResult {
    bool ok{false};
    std::vector<ContentValidationIssue> issues;
    // 通过的校验才会填 targetBackend;空表示没有后端结论。
    std::string targetBackend;

    // 是否值得送进自动修复:有可修复问题,且不是"服务不可用"这类重试也没用的。
    bool Repairable() const noexcept {
        if (ok) return false;
        for (const auto& issue : issues) {
            if (issue.repairable) return true;
        }
        return false;
    }
};

// 宿主签发的结构化回执。模型拿到的就是这个,不是一段自然语言。
struct ContentCandidateReceipt {
    bool accepted{false};
    std::string candidateId;      // 宿主生成
    std::uint32_t revision{0};    // 宿主分配;0 表示被拒绝
    std::string digest;           // 与封存快照绑定
    std::string snapshotPath;     // 宿主持有的只读快照
    std::string summary;
    ContentValidationResult validation;
    std::string rejectionReason;  // 不接受时说明原因
    std::uint64_t sourceTurn{};

    // 同一份内容再次提交时是同一版:revision 不变、accepted 不变。
    bool SameCandidateAs(const ContentCandidateReceipt& other) const noexcept {
        return !digest.empty() && digest == other.digest;
    }
};

// 一次提交的输入。claimedPath / claimedId 都按输入处理,不作事实来源。
struct ContentCandidateSubmission {
    std::string claimedPath;      // 模型说包在哪
    std::string claimedId;        // 模型说它叫什么
    std::string summary;
    std::uint64_t sourceTurn{};
    // 不在这里带 ContentKind:判"包类型与模式是否相符"是校验层的事,它已经结构化在
    // ContentValidationResult 里(ContentValidationFailure::KindMismatch)。再存一份
    // 只会出现两个可以互相矛盾的事实来源,而摘要本身已经说明了里面到底有什么。
};

class ContentCandidateLedger {
public:
    explicit ContentCandidateLedger(std::string sessionId);

    const std::string& SessionId() const noexcept { return sessionId_; }
    const std::vector<ContentCandidateReceipt>& Receipts() const noexcept { return receipts_; }

    // 提交一个候选。parts 是宿主对**封存快照**计算摘要时用的内容清单 ——
    // 摘要必须是快照的,不能是模型给的路径上那份,否则封存就失去了意义。
    //
    // 幂等语义:同样的摘要再次提交返回同一版(新的回执对象但 revision 相同);
    // 摘要不同则分配新 revision,即使 claimedPath 完全一样。
    ContentCandidateReceipt Submit(const ContentCandidateSubmission& submission,
                                  std::vector<CandidatePart> snapshotParts,
                                  const ContentValidationResult& validation);

    // 第几版。0 表示这份摘要还没提交过。
    std::uint32_t RevisionOf(const std::string& digest) const noexcept;

    // 最新一个通过校验的候选。取消/失败后仍然可查,对应"保留上一有效候选"。
    const ContentCandidateReceipt* LastValid() const noexcept;

    // 这个摘要是否已经被封存过。用于"应用前再验证"判断候选是否仍然有效。
    bool Sealed(const std::string& digest) const noexcept;

    // 宿主改动了已封存的候选时调用:标记它不再可信,后续 Apply 必须重验。
    // 台账自己不会篡改已发生的记录,只是拒绝再把它当有效候选。
    void Invalidate(const std::string& digest, std::string reason);

    const std::vector<std::string>& Invalidated() const noexcept { return invalidated_; }

private:
    // 同一份内容重复提交时,沿用第一次封存时记下的路径与校验结论,而不是重新编一个。
    // 各自重新编会让"同一版"这个说法名不副实。
    std::string snapshotPathOf(const std::string& digest) const;
    std::string candidateIdOf(const std::string& digest) const;
    ContentValidationResult validationOf(const std::string& digest) const;

    std::string sessionId_;
    std::vector<ContentCandidateReceipt> receipts_;
    std::vector<std::string> invalidated_;
    std::uint32_t nextRevision_{1};
};

} // namespace miaodesk::content
