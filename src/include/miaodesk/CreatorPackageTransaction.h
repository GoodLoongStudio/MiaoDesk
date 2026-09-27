#pragma once

// CCA-04:包写入事务(策略与摘要在先,物理动作由宿主执行)。
//
// 计划验收原文:"错误后无半写文件"。这句话的难点不在"写"——一次 WriteFile 就写完了
// —— 而在于**失败必须不留下任何痕迹**。模型交上来的内容有各种各样坏法:schema 不对、
// 路径越界、超过大小上限、写了一半盘满了。任何一种之后,工作区都必须还停在写入前的
// 状态,而且候选摘要必须没变。否则用户会看到一个"改了但没完全改"的包,而它没有版本号。
//
// 这个事务把那段过程显式建模成阶段机:
//
//     Planned -> Validated -> Staged -> Committed
//        |          |           |
//        v          v           v
//     Rejected   Rejected   RolledBack
//
// 每一步都是宿主要执行的一个动作,事务本身只回答"能不能进入下一步"和"现在摘要是什么"。
// 它刻意不碰盘,所以整个失败路径能在任何机器上被执行到,而不是只留在 Windows 上。
#include <string>
#include <vector>

#include "miaodesk/ContentCandidateDigest.h"
#include "miaodesk/CreatorWorkspacePolicy.h"

namespace miaodesk::creator {

enum class CreatorWriteStage {
    Planned,       // 打算写,还没验
    Validated,     // 通过策略与大小检查,可以进暂存
    Staged,        // 已写进暂存位置,还没替换目标
    Committed,     // 目标已替换,摘要已变
    RolledBack,    // 已恢复到写入前
    Rejected,      // 被规则拒绝,工作区从未被动过
    Unverified,    // 写入已落地,但落盘后的摘要与计划不符 —— 这个候选不可信
};

const char* ToString(CreatorWriteStage stage) noexcept;

// 一次写入的完整计划。before/after 都算好,宿主按顺序执行,事务负责判定。
struct CreatorWritePlan {
    std::string sessionId;
    std::string workspaceRoot;
    std::string relativePath;
    std::string newContent;

    content::CandidateDigest before;   // 写入前工作区的摘要
    content::CandidateDigest after;    // 这笔写入落地后的摘要

    // 目标文件原来在不在。删除(写空内容)与新建要分开处理,回退方式不同。
    bool existed{false};
    std::string previousContent;

    CreatorFileRole role{};
    CreatorFileFacts facts;
};

// 一次事务的结果。宿主据此决定下一步,并决定给模型看什么。
struct CreatorWriteOutcome {
    CreatorWriteStage stage{CreatorWriteStage::Planned};
    bool committed{false};
    std::string rejectionReason;
    std::string candidateChanged;      // committed 时是新的摘要;否则为空
    std::string stagedPath;            // 宿主应该写到哪(暂存)
};

class CreatorPackageTransaction {
public:
    // 用工作区当前内容与这笔写入构造一次事务。parts 是**写入前**的快照内容清单。
    CreatorPackageTransaction(const CreatorWorkspacePolicy& policy,
                              std::vector<content::CandidatePart> beforeParts,
                              std::string relativePath, std::string newContent);

    // 进入 Validated 或 Rejected。这一步只判规则,不动盘。
    CreatorWriteOutcome Validate();

    // 宿主把 newContent 写进 outcome.stagedPath 之后调用,进入 Staged。
    // verifyStagedBy 是宿主对暂存文件算出的事实(大小、是否 reparse point),
    // 事务据此确认暂存内容与计划一致。
    CreatorWriteOutcome Stage(const CreatorFileFacts& stagedFacts,
                             std::string_view stagedBytes);

    // 宿主把暂存替换到目标之后调用,成功进入 Committed。
    //
    // 注意:这一步**失败时副作用已经发生了** —— 目标文件已经被换掉。所以失败不会被
    // 记成 Rejected(那意味着"什么都没动"),而是 Unverified:工作区确实变了,只是变得
    // 和我们计划的不一样。调用方拿到 Unverified 必须把候选标记失效,而不是当没发生过。
    //
    // afterFacts / afterParts 是替换**之后**工作区的事实与内容清单 ——
    // 事务据此确认摘要真的是计划里的 after,而不是别的。
    CreatorWriteOutcome Commit(const CreatorFileFacts& afterFacts,
                              std::vector<content::CandidatePart> afterParts);

    // 任何一步之后宿主想放弃:恢复到写入前。已 Committed 的不能靠这个回退,
    // 必须走候选台账的 Invalidate。
    CreatorWriteOutcome Rollback();

    CreatorWriteStage Stage() const noexcept { return stage_; }
    const CreatorWritePlan& Plan() const noexcept { return plan_; }
    const std::string& RejectionReason() const noexcept { return rejection_; }

    // 这笔写入会不会真的改变内容。same content 不算新修订 —— 模型反复交同一份
    // 内容时,每次都冒出一个新 revision 会让用户以为又在生成。
    bool ChangesContent() const noexcept;

private:
    CreatorWriteOutcome Reject(std::string reason);
    CreatorWriteOutcome Refuse(std::string reason);

    CreatorWritePlan plan_;
    CreatorWorkspacePolicy policy_;
    CreatorWriteStage stage_{CreatorWriteStage::Planned};
    std::string rejection_;
    std::string stagedPath_;
};

} // namespace miaodesk::creator
