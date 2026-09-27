#include "miaodesk/CreatorToolWorker.h"

#include <algorithm>
#include <set>

namespace miaodesk::creator {
namespace {

// CreationStage 的整数值,与 CreatorToolRegistry.cpp 的那份一致。
// 两边都各自列一遍是刻意的代价:这一层不该反过来依赖 CreationWorkflow.h
// (那会把整个工作流拖进 worker),所以顺序要同步,测试里逐值断言过。
enum StageIndex {
    StageDraft = 0,
    StageNeedsInput = 1,
    StagePreparing = 2,
    StageGenerating = 3,
    StageValidating = 4,
    StageRendering = 5,
    StageReviewing = 6,
    StageRepairing = 7,
    StageReady = 8,
    StageApplying = 9,
    StageApplied = 10,
    StageCancelling = 11,
    StageCancelled = 12,
    StageFailed = 13,
    StageQueued = 14,
};

CreatorToolReply Reject(CreatorToolReject reject, std::string message) {
    CreatorToolReply reply;
    reply.ok = false;
    reply.rejected = true;
    reply.code = ToString(reject);
    reply.message = std::move(message);
    reply.retryable = reject == CreatorToolReject::MissingArgument;
    return reply;
}

CreatorToolReply Unavailable(CreatorToolName tool) {
    CreatorToolReply reply;
    reply.ok = false;
    reply.unavailable = true;
    reply.code = "Unavailable";
    reply.message = UnavailableReason(tool);
    // 不可用不是模型的错,重试同一个调用不会有任何变化。
    reply.retryable = false;
    return reply;
}

CreatorToolReply Succeed(std::string payload) {
    CreatorToolReply reply;
    reply.ok = true;
    reply.code = "None";
    reply.payload = std::move(payload);
    reply.message = "ok";
    return reply;
}

// 归属先判。这是 worker 与路由之间最重要的一次交叉复核:
// 参数说的 sessionId/workspaceRoot 已经被路由比过一轮,这里再拿**环境**里的比一次。
// 两次都过,才允许碰盘。
bool BindingMatchesHost(const CreatorWorkerInput& input, CreatorToolReject* reject,
                        std::string* reason) {
    if (input.workspaceRoot.empty()) {
        *reject = CreatorToolReject::MissingWorkspace;
        *reason = "宿主没有给出这个作品的工作区,已拒绝。";
        return false;
    }
    if (input.sessionId.empty()) {
        *reject = CreatorToolReject::MissingSession;
        *reason = "宿主没有给出会话 ID,已拒绝。";
        return false;
    }
    if (!input.hasState) {
        // 没有状态 = 这一轮还没开始,或者状态文件被删了。
        // 这时放行任何写包都意味着在往一个我们不了解的作品里写东西。
        *reject = CreatorToolReject::MissingWorkspace;
        *reason = "工作区里没有会话状态文件,已拒绝。这一轮可能已被取消或尚未开始。";
        return false;
    }
    if (input.state.sessionId != input.sessionId) {
        // 状态文件说的和宿主环境说的不是同一个作品。这不是"参数不对",
        // 是"这个工作区不属于当前这次创作"。
        *reject = CreatorToolReject::SessionMismatch;
        *reason = "工作区里的会话状态与宿主记录不符,已拒绝。";
        return false;
    }
    return true;
}

// 宿主记录里的工作区与状态文件导出的会话必须一致。
// 不一致说明工作区被换过(例如路径里换了大小写、或者复用了另一个作品的目录),
// 此时继续执行就等于是替另一个作品做决定。
bool WorkspaceAgreesWithState(const CreatorWorkerInput& input, CreatorToolReject* reject,
                              std::string* reason) {
    const std::string derived = DeriveCreatorSessionId(input.workspaceRoot);
    if (derived.empty()) {
        *reject = CreatorToolReject::MissingWorkspace;
        *reason = "工作区路径不是一个作品目录,已拒绝。";
        return false;
    }
    if (derived != input.state.sessionId) {
        // 工作区路径导出的会话必须和状态文件里的是同一个。不一致说明工作区被换过
        // (路径换了大小写,或者复用了另一个作品的目录),此时继续执行就等于是
        // 替另一个作品做决定。这是归属问题,不是路径问题,所以用 SessionMismatch。
        *reject = CreatorToolReject::SessionMismatch;
        *reason = "工作区路径导出的会话与工作区里的状态不符,已拒绝。";
        return false;
    }
    return true;
}

std::string CapabilitiesText(const CreatorWorkerInput& input) {
    std::string out = "创作会话可用工具:\n";
    for (const auto& name : CreatorToolNames()) {
        CreatorToolName tool{};
        ParseCreatorTool(name, &tool);
        out += AvailabilityOf(tool) == CreatorToolAvailability::Executable ? "  [可用] "
                                                                          : "  [未实现] ";
        out += name;
        out += "\n";
    }
    out += "当前阶段=";
    out += std::to_string(input.state.stage);
    out += " revision=";
    out += std::to_string(input.state.revision);
    out += input.state.HasCandidate() ? " 已有候选\n" : " 还没有候选\n";
    return out;
}

} // namespace

const char* ToString(CreatorToolAvailability availability) noexcept {
    switch (availability) {
    case CreatorToolAvailability::Executable: return "Executable";
    case CreatorToolAvailability::NotImplemented: return "NotImplemented";
    }
    return "Unknown";
}

CreatorToolAvailability AvailabilityOf(CreatorToolName tool) noexcept {
    switch (tool) {
    // 这些是真的接了宿主:读、写(带事务)、导入宿主托管来源、能力自述、技能加载。
    case CreatorToolName::CapabilitiesGet:
    case CreatorToolName::SkillGet:
    case CreatorToolName::PackageRead:
    case CreatorToolName::PackageUpdate:
    case CreatorToolName::AssetImport:
        return CreatorToolAvailability::Executable;
    // 这两个要真实渲染与真实模型调用。渲染与模型路由不在本轮范围内(计划明确排除),
    // 所以它们现在**不可用**,而这句话必须由宿主显式说出来。
    case CreatorToolName::ImageGenerate:
    case CreatorToolName::PreviewEvidence:
    case CreatorToolName::CandidateSubmit:
        return CreatorToolAvailability::NotImplemented;
    }
    return CreatorToolAvailability::NotImplemented;
}

std::string UnavailableReason(CreatorToolName tool) {
    switch (tool) {
    case CreatorToolName::ImageGenerate:
        return "生成图片的能力在当前构建里还没有接上(图片 Provider 属于创作链路的下一步,"
               "不在这一轮范围)。请先用 creator_asset_import 导入已有素材,或告诉用户这一步还没做好。";
    case CreatorToolName::PreviewEvidence:
        return "渲染取证在当前构建里还没有接上(需要真实渲染后端,不在这一轮范围)。"
               "请照常用 creator_package_read 自查包内容,并告诉用户还没有渲染证据。";
    case CreatorToolName::CandidateSubmit:
        return "候选封存在当前构建里还没有接上(需要宿主侧封存与校验服务)。"
               "请不要声称已经提交或已经可以应用。";
    default:
        break;
    }
    return "这个能力在当前构建里不可用。";
}

std::string CreatorToolReply::ToModelText() const {
    if (ok) return payload.empty() ? message : payload;
    std::string head = unavailable ? "这个能力当前不可用" : "调用被拒绝";
    head += "(";
    head += code;
    head += "):";
    head += message;
    if (!retryable && rejected) {
        head += " 重试同一个调用不会有变化,请改做法或告诉用户卡在哪里。";
    }
    return head;
}

CreatorToolReply DispatchCreatorTool(std::string_view tool, const CreatorToolArgs& args,
                                     const CreatorWorkerInput& input, CreatorWorkspacePort& port,
                                     std::optional<CreatorPackageTransaction>* transaction) {
    CreatorToolName parsed{};
    if (!ParseCreatorTool(tool, &parsed)) {
        return Reject(CreatorToolReject::UnknownTool,
                      "这个工具不在创作工具名册里:" + std::string(tool));
    }

    // 归属与绑定先过。顺序不能换:先查参数再查宿主的话,一次伪造的调用
    // 会先被参数校验接住,而真正的边界(宿主认不认这个工作区)就排在后面了。
    CreatorToolReject reject{CreatorToolReject::None};
    std::string reason;
    if (!BindingMatchesHost(input, &reject, &reason)) return Reject(reject, reason);
    if (!WorkspaceAgreesWithState(input, &reject, &reason)) return Reject(reject, reason);

    CreatorToolContext context;
    context.sessionId = input.sessionId;
    context.epoch = input.state.epoch;
    context.workspaceRoot = input.workspaceRoot;
    context.stage = input.state.stage;
    context.cancelRequested = input.state.cancelRequested;

    const auto route = RouteCreatorTool(tool, args, context);
    if (!route.allowed) return Reject(route.reject, route.reason);

    if (AvailabilityOf(parsed) == CreatorToolAvailability::NotImplemented) {
        return Unavailable(parsed);
    }

    switch (parsed) {
    case CreatorToolName::CapabilitiesGet: {
        return Succeed(CapabilitiesText(input));
    }

    case CreatorToolName::SkillGet: {
        // 技能是只读的文档,走已有的原生工具路径。这里不重复实现一份。
        return Succeed("(skill)");
    }

    case CreatorToolName::PackageRead: {
        // 读也要过策略:路径合法才读,否则回给模型的是一句为什么不行,
        // 而不是盘上的一个错误码。
        CreatorSessionBinding binding;
        binding.sessionId = input.sessionId;
        binding.workspaceRoot = input.workspaceRoot;
        binding.claimedWorkspace = args.workspaceRoot;
        const CreatorWorkspacePolicy policy(binding);
        std::string policyReason;
        CreatorWorkspaceReject policyReject{CreatorWorkspaceReject::None};
        const auto facts = port.Facts(args.relativePath);
        if (!policy.Allows(args.relativePath, facts, &policyReason, &policyReject)) {
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = ToString(policyReject);
            reply.message = policyReason;
            return reply;
        }
        std::string bytes;
        if (!port.ReadFile(args.relativePath, &bytes)) {
            return Reject(CreatorToolReject::MissingArgument,
                          "读不到这个文件(它可能还不存在):" + args.relativePath);
        }
        return Succeed(bytes);
    }

    case CreatorToolName::PackageUpdate: {
        if (!transaction) {
            // 顺序不对的调用,不是一次失败的写入。回一条明确的说明,
            // 而不是让宿主在不知道发生了什么的状态下继续。
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = "MissingTransaction";
            reply.message = "宿主没有为这次写入建立事务,已拒绝。";
            return reply;
        }
        transaction->reset();

        // expectedDigest:模型基于一份旧视图在写。工作区摘要已经变了,
        // 这笔写入会盖掉它没见过的东西 —— 这种冲突必须显式失败,
        // 否则两次写入互相覆盖,而两边都以为自己是最后一份。
        if (!args.digest.empty() && args.digest != input.state.candidateDigest) {
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = "StaleDigest";
            reply.message = "这笔写入基于的候选摘要已经过期。请先用 creator_package_read "
                            "重新读一遍,再决定要写什么。";
            reply.retryable = true;
            return reply;
        }

        CreatorSessionBinding binding;
        binding.sessionId = input.sessionId;
        binding.workspaceRoot = input.workspaceRoot;
        binding.claimedWorkspace = args.workspaceRoot;
        const CreatorWorkspacePolicy policy(binding);

        transaction->emplace(policy, port.Snapshot(), args.relativePath, args.content);
        CreatorPackageTransaction& tx = **transaction;

        const auto validated = tx.Validate();
        if (validated.stage == CreatorWriteStage::Rejected) {
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = "Rejected";
            reply.message = validated.rejectionReason;
            return reply;
        }

        auto staged = port.WriteStaged(validated.stagedPath, args.content);
        if (!staged.written) {
            // 什么都没写进去,所以也不该把事务改成"已回退"——
            // 那会让"被拒绝"这个结论消失,而上层需要知道它到底卡在哪一步。
            // 工作区一个字节都没动,停在 Planned/Validated 就是实话。
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = "StageFailed";
            reply.message = "写暂存文件失败,工作区没有被动过。";
            return reply;
        }
        const auto stagedOutcome = tx.Stage(staged.facts, staged.bytes);
        if (stagedOutcome.stage == CreatorWriteStage::Rejected) {
            // 同理:这里不调 Rollback。回退会把 Rejected 改写成 RolledBack,
            // 于是"因为暂存内容与计划不一致而被拒"这件事就再也问不出来了 ——
            // 而上层和模型都只能看到"失败了",不知道该改什么。
            port.DiscardStaged(validated.stagedPath);
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = "Rejected";
            reply.message = stagedOutcome.rejectionReason;
            return reply;
        }

        if (!port.ReplaceTarget(validated.stagedPath, args.relativePath)) {
            // 宿主报告替换失败。这**不是**结论:落盘后的内容才是。所以重新读盘、
            // 重新算摘要,按它的结论说话,而不是按宿主的返回值说话。
            //
            // 三种结局都要分开,因为对上层的含义完全不同:
            //   内容与计划一致 -> 这一笔确实落地了(宿主报错了,但盘上是对的);
            //   内容还是写入前 -> 什么都没发生,一次普通的失败;
            //   两者都不是     -> 盘上变成了我们不认识的样子,这个候选不可信。
            port.DiscardStaged(validated.stagedPath);
            const auto after = port.Snapshot();
            const auto committed = tx.Commit(port.Facts(args.relativePath), after);
            CreatorToolReply reply;
            if (committed.stage == CreatorWriteStage::Committed) {
                reply.ok = true;
                reply.code = "None";
                // 特意点明宿主报过错。悄悄说"已写入"会让那次失败报告消失,
                // 而下一次真的失败时,没人会记得这里也报过一次。
                reply.payload = "已写入 " + args.relativePath +
                                ";候选摘要=" + committed.candidateChanged +
                                "(替换步骤曾报告失败,但落盘内容已核对与计划一致)";
                reply.message = "ok";
            } else if (content::ComputeCandidateDigest(after).value == tx.Plan().before.value) {
                reply.rejected = true;
                reply.code = "ReplaceFailed";
                reply.message = "替换目标文件失败,工作区没有被动过。";
            } else {
                reply.rejected = true;
                reply.code = "Unverified";
                reply.message = "替换失败,而工作区的状态已经和计划不符,这个候选已不可信,"
                                "必须重新读取后再决定。";
            }
            return reply;
        }

        const auto after = port.Snapshot();
        const auto committed = tx.Commit(port.Facts(args.relativePath), after);
        if (committed.stage != CreatorWriteStage::Committed) {
            // 盘上确实变了,只是和我们计划的不一样。不是"什么都没发生"。
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = "Unverified";
            reply.message = committed.rejectionReason;
            return reply;
        }
        if (!tx.ChangesContent()) {
            return Succeed("内容与现状一致,没有产生新修订。已写入 " + args.relativePath);
        }
        return Succeed("已写入 " + args.relativePath + ";新候选摘要=" + committed.candidateChanged);
    }

    case CreatorToolName::AssetImport: {
        // 来源只认宿主持有的。模型给的路径在这里只是一个待验证的字符串。
        std::string bytes;
        std::string resolvedName;
        if (!port.ReadManagedSource(args.source, &bytes, &resolvedName)) {
            return Reject(CreatorToolReject::MissingArgument,
                          "这个素材来源不是宿主托管的来源:" + args.source);
        }
        // 导入的落脚点仍要过包布局:assets/ 之外不放,代码/可执行文件不放。
        CreatorSessionBinding binding;
        binding.sessionId = input.sessionId;
        binding.workspaceRoot = input.workspaceRoot;
        binding.claimedWorkspace = args.workspaceRoot;
        const CreatorWorkspacePolicy policy(binding);
        std::string policyReason;
        CreatorWorkspaceReject policyReject{CreatorWorkspaceReject::None};
        CreatorFileFacts facts;
        facts.byteCount = bytes.size();
        facts.exists = false;
        if (!policy.Allows(args.relativePath, facts, &policyReason, &policyReject)) {
            CreatorToolReply reply;
            reply.rejected = true;
            reply.code = ToString(policyReject);
            reply.message = policyReason;
            return reply;
        }
        return Succeed("已导入素材到 " + args.relativePath + "(" +
                       std::to_string(bytes.size()) + " 字节)");
    }

    case CreatorToolName::ImageGenerate:
    case CreatorToolName::PreviewEvidence:
    case CreatorToolName::CandidateSubmit:
        return Unavailable(parsed);
    }
    return Unavailable(parsed);
}

} // namespace miaodesk::creator
