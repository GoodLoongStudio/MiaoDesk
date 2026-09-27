#pragma once

// CCA-11:应用事务的**一次恢复**。
//
// 计划原文:"实现一次恢复"、"恢复不会覆盖用户后来的无关改动"。
//
// 这两句话合起来是一个很难做对的局面:应用已经发出去了,进程在正式 API 回来之前
// 没了。再打开时,宿主持有一笔"已发出、没有结论"的记录,和一份**现在**的桌面状态。
// 恢复必须只做一次,而"现在到底是什么状态"有四种互相不同的真相:
//
//   落地了     —— 目标上挂的就是这一笔的候选。此时**撤销才是错的**:它会把一个
//                 已经生效的东西退回旧值,而用户看到的是"我的壁纸自己变回去了"。
//   没落地     —— 目标上还是账本记下的前态。这一笔确实什么都没发生过,写回前态
//                 (多数情况下是无操作)。
//   被接管     —— 目标上挂着**另一份**候选:用户后来又应用了别的一份。此时
//                 写回前态会把它一并抹掉 —— 这正是"恢复不会覆盖用户后来的
//                 无关改动"要防的那件事。
//   读不出来   —— 宿主读不到目标当前挂的是哪一份。
//
// 最后一种最容易被顺手写成"按没落地处理":那是一次对用户桌面的猜测,而猜错的方向
// 恰好是破坏性的(写回前态会覆盖用户后来装上去的东西)。所以它是一个**独立的结果**,
// 而且它的判据不是"看着像",是"宿主到底读没读到"。
//
// 另外两条贯穿全部四种:
//   一次   —— 已经有结论的一笔不再恢复。第二次恢复会让一个已经结算的结果
//             被再改一次,而恢复本身就是往用户桌面上写。
//   落地了也不自动放行 —— 补结算之前必须重验候选:崩溃这段时间里候选可能已被宿主
//   标记失效,而"一个失效的候选生效在桌面上"比"没生效"更难收拾。
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/CreationWorkflow.h"

namespace miaodesk::creator {

// "这个目标上此前没有候选"的记法。**空串不是合法前态**:它分不清"之前确实没有"
// 和"宿主没记下来",而后者必须让恢复停下来而不是照着一张白纸撤销。
inline constexpr std::string_view kNoAppliedCandidate = "none";

enum class ApplyRecoveryAction {
    NothingToRecover,     // 没有要恢复的东西(或这一笔已经有结论了)
    FinishApply,          // 已经落地了:补一次结算,别撤销一个生效的东西
    RollBack,             // 没落地:写回账本记下的精确前态
    AbandonSuperseded,    // 目标已经被另一份候选接管:这一笔作废,一个字都不写
    GiveUpUnrecognized,   // 读不到、或读出的东西不认识:不许碰
};

const char* ToString(ApplyRecoveryAction action) noexcept;

struct ApplyRecoveryRequest {
    // 账本里那一笔未结清的在途。nullptr、或已 resolved/committed,都表示没有要恢复的。
    const ApplyIdempotencyEntry* entry{nullptr};
    // 宿主到底读没读到目标当前挂着的候选摘要。读不到就什么都不能做 ——
    // 见文件头第四种真相。
    bool observedReadable{false};
    std::string observedCandidateDigest;
};

struct ApplyRecoveryPlan {
    ApplyRecoveryAction action{ApplyRecoveryAction::NothingToRecover};
    std::string reason;
    // 只对 RollBack 有意义:账本记下的那一份**精确**前态,恢复时原样写回。
    std::string restoreState;
    // 只对 FinishApply 有意义:补结算之前必须先重验候选。
    bool revalidateCandidateFirst{false};
};

// 目标上当前生效的候选摘要(最近一笔已提交的应用)。空表示这个目标上还没有候选。
// 只看 resolved && committed:没结论的在途和失败的应用都没有改变桌面,
// 把它们算成"当前生效"会让恢复去撤销一个从未存在的东西。
std::string CurrentCandidateDigestForTarget(const std::vector<ApplyIdempotencyEntry>& ledger,
                                            std::string_view target);

// 这一目标在 operationId 那一笔**之前**的候选摘要。此前没有则返回 kNoAppliedCandidate。
// 它从账本自己推出来,而不是让宿主另传一份"之前是什么" —— 多一个来源就多一处
// 可以互相矛盾的事实。
std::string PreviousCandidateDigestForTarget(const std::vector<ApplyIdempotencyEntry>& ledger,
                                             std::string_view target,
                                             std::string_view operationId);

// 这份候选是不是已经挂在目标上了。
//
// **按摘要比,不按路径比**。同路径换了内容就是新版本;把它当成"已经在桌面上了"
// 会让新版本永远出不来 —— 而用户看到的是"我改了,但桌面上没变"。
bool IsAlreadyAppliedOnTarget(const std::vector<ApplyIdempotencyEntry>& ledger,
                             std::string_view target, std::string_view candidateDigest);

// 把一次未结清的在途应用变成一个可执行的恢复动作,或者一个"停下来并说明原因"。
//
// 这是一个纯函数:它不碰盘、不调桌面 API,只比较账本与宿主持有的那一句现状。
// 真正去读桌面、去写的仍是宿主 —— 但"什么时候不能写"必须在这里就能验,
// 否则它只能等一台 Windows 真机出一次事故。
ApplyRecoveryPlan PlanApplyRecovery(const std::vector<ApplyIdempotencyEntry>& ledger,
                                    const ApplyRecoveryRequest& request);

} // namespace miaodesk::creator
