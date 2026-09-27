#pragma once

// CCA-04:把工具路由接到真实分发,并明确说出哪些工具现在真的能执行。
//
// 此前宿主对创作工具只"放行"不"执行":worker 的允许表里有它们,于是调用能进到
// RunNativeToolWorkerIfRequested,然后落进 ExecuteNativeToolRaw —— 那条路不认识
// creator_ 前缀,回给模型的是一句"未知工具"。这是最难查的一类缺陷:调用看起来
// 被接受了,失败信息却和"名字打错了"一模一样,而模型会据此反复重试。
//
// 这个文件做三件事:
//   1. 从**宿主自己的**环境与工作区状态构造 CreatorToolContext,不让参数说话;
//   2. 走 CreatorToolRegistry 的路由,得到执行或拒绝;
//   3. 对能执行的工具给出宿主该做的动作,对还不能执行的工具给出一句**明确**的
//      "这个能力在当前构建里不可用",而不是让它看起来像一次拒绝。
//
// 第 3 点是这个文件存在的核心理由。工具做没做完,必须是一句说得出口的话:
// "未实现"被记成"被拒绝"的话,模型会去改一个改不动的东西,而用户看到的是
// 反复失败;反过来,"拒绝了"被记成"未实现"会让一条真实的安全边界看起来像待办。
// 所以可用性是显式列出的,测试逐个工具断言它落在且只落在一类里。
//
// 它不碰盘:读文件、写暂存、替换目标都由 CreatorWorkspacePort 的实现方做,
// 于是整条决策链(包括每一条失败路径)在本机就能真跑。
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/ContentCandidateDigest.h"
#include "miaodesk/CreatorPackageTransaction.h"
#include "miaodesk/CreatorToolRegistry.h"
#include "miaodesk/CreatorWorkspacePolicy.h"
#include "miaodesk/CreatorWorkspaceState.h"
#include "miaodesk/RenderEvidence.h"

namespace miaodesk::creator {

// 宿主对工作区的实际访问能力。worker 是另一个进程,所以这是它唯一的手。
//
// 为什么用抽象而不是把 <filesystem> 传进来:决策逻辑必须在 macOS 上真跑。
// 一个 in-memory 实现能让"写坏一个字节之后,拒绝路径、回退路径、摘要不符路径"
// 全部被执行到 —— 这些恰好是最不该只留给 Windows 真机的分支。
class CreatorWorkspacePort {
public:
    virtual ~CreatorWorkspacePort() = default;

    // 读一个包内文件。返回 false 表示读不到(不存在、是目录、权限不足)。
    virtual bool ReadFile(const std::string& relativePath, std::string* bytes) = 0;

    // 工作区当前的全部内容,用于算摘要。顺序不影响结果(摘要自己会排序)。
    virtual std::vector<content::CandidatePart> Snapshot() = 0;

    // 一个路径当前的事实。路径不存在时返回 exists=false 的事实,不返回 false。
    virtual CreatorFileFacts Facts(const std::string& relativePath) = 0;

    // 事务的物理步骤。宿主据此按顺序执行,并把结果回报给事务。
    //
    // Stage 的返回值是**宿主实际写进去**的字节数,事务拿它和计划对账 ——
    // 不是宿主以为自己写了多少。两者在盘满、被杀进程、暂存位置被占用时会不同。
    struct StagedBytes {
        bool written{false};
        CreatorFileFacts facts;
        std::string bytes;      // 宿主从暂存位置**读回来**的内容
    };
    virtual StagedBytes WriteStaged(const std::string& stagedPath, const std::string& content) = 0;

    // 把暂存文件替换到目标。返回 false 时目标可能已经被换掉也可能没有 ——
    // 调用方必须重新读盘判断,不能假定。
    virtual bool ReplaceTarget(const std::string& stagedPath, const std::string& relativePath) = 0;

    // 删掉暂存文件(回退用)。
    virtual void DiscardStaged(const std::string& stagedPath) = 0;

    // 一个宿主托管的素材来源是否可读,读出来是什么。
    // 只有宿主承认的来源才允许:模型说的任何路径都只是一个字符串。
    virtual bool ReadManagedSource(const std::string& source, std::string* bytes,
                                   std::string* resolvedName) = 0;

    // 把**同一份快照**复制成只读封存。返回 false 表示封存失败 ——
    // 封存失败的候选不能被接受:没有封存快照,"封存后修改源目录不能改变待应用
    // 候选"这条就无处安放,而它正是封存存在的理由。
    //
    // parts 必须由调用方传进来,而不是让实现自己再读一遍盘:摘要和封存的字节
    // 必须来自同一次读取。让实现自己读的话,两次读取之间源目录的任何变动都会
    // 让"摘要对得上内容"变成假话 —— 而那正是封存要保证的那一件事。
    virtual bool SealSnapshot(const std::string& digest,
                              const std::vector<content::CandidatePart>& parts,
                              std::string* snapshotPath) = 0;

    // 台账的读与写。它必须落盘:worker 每次调用都是新进程,revision 不存在
    // 任何一个进程的内存里。解析失败时 load 返回 false,调用方必须当成
    // "没有台账"而不是"空台账"。
    virtual bool LoadLedger(std::string* text) = 0;
    virtual bool SaveLedger(const std::string& text) = 0;

    // 把会话状态写回工作区。封存成功之后**必须**调用:否则状态文件里的候选摘要
    // 一直停在旧值,下一次带 expectedDigest 的写入会拿着一个过期的值去比对,
    // 于是每一笔写入都被当成"基于旧视图"而拒绝。
    virtual bool SaveState(const CreatorWorkspaceState& state) = 0;

    // 离屏采集当前候选的若干帧。渲染失败时返回 false,detail 说明原因 ——
    // 宿主**不得**用封面图或上一版的截图顶替:那会让一个坏包看起来渲染得很好,
    // 而用户预览到的不是他做出来的东西。缺素材也算失败,不是"少一帧"。
    virtual bool CollectEvidence(const RenderEvidenceRequirements& requirements,
                                 std::vector<RenderEvidenceSample>* samples,
                                 std::string* detail) = 0;
};

// 一次分发的结论。宿主据此写回 output.txt,并把文本给模型。
struct CreatorToolReply {
    bool ok{false};
    bool rejected{false};        // 被规则拒绝(归属/阶段/路径/参数)
    bool unavailable{false};     // 当前构建没有实现这个能力
    std::string code;            // CreatorToolReject / CreatorWorkspaceReject 的名字,或 "Unavailable"
    std::string message;         // 给模型看的一句话,必须能据此改
    std::string payload;         // 读包:文件内容;能力:清单;封存:receipt 摘要
    bool retryable{false};

    // 一句话拼成给模型的文本。工具结果必须是真实的:失败了就说失败,
    // 并且说明是哪一类失败 —— 三种失败对模型下一步该做什么的要求完全不同。
    std::string ToModelText() const;
};

// 宿主给 worker 的事实。全部来自宿主自己的环境与工作区状态,没有一个来自参数。
struct CreatorWorkerInput {
    std::string workspaceRoot;    // MIAODESK_CREATOR_WORKSPACE
    std::string sessionId;        // MIAODESK_CREATOR_SESSION
    CreatorWorkspaceState state;  // 工作区里的 .miaodesk-session.state
    bool hasState{false};         // false 表示状态文件不存在或读不懂
};

// 分发一次工具调用。纯函数:同样的输入永远得到同样的结论。
//
// transaction 是可选的输出。写包时它收到这次写入建成的事务,因为宿主随后还要
// 用它给台账与上层回报;它不是输入,所以这里用 optional 而不是一个必须提前构造
// 的对象 —— CreatorPackageTransaction 没有默认状态,硬要一个就得先编一个假的。
CreatorToolReply DispatchCreatorTool(std::string_view tool, const CreatorToolArgs& args,
                                     const CreatorWorkerInput& input, CreatorWorkspacePort& port,
                                     std::optional<CreatorPackageTransaction>* transaction = nullptr);

// 这个工具在当前构建里能不能真的执行。可用性是显式列出的,不是"试试看":
// 一个没实现的工具如果悄悄走默认分支,它的失败会长得像一次拒绝。
enum class CreatorToolAvailability {
    Executable,
    NotImplemented,
};

const char* ToString(CreatorToolAvailability availability) noexcept;

// 逐个工具的可用性。新工具加进名册而没进这里,测试会红 —— 那时才被迫回答
// "它现在能不能用",而不是让这个问题一直悬着。
CreatorToolAvailability AvailabilityOf(CreatorToolName tool) noexcept;

// 名册里**现在能用**的那几个名字。给系统提示词与文档用:它们要告诉模型
// "你可以用 X",而那句话只能在 X 真的能用时说。
//
// 它存在的理由是系统提示词此前写着"用 creator_asset_import 导入已有素材、
// 用 creator_image_generate 生成你描述的图" —— 而后者是未实现的(需要图片
// Provider,不在本轮范围)。提示词让模型去调一个调不通的工具,于是它会向用户
// 承诺一件做不到的事,而 `creator_capabilities_get` 里明明写着它"未实现"。
// 两张嘴说不同的话,模型会信提示词那一张。
std::vector<std::string> ExecutableCreatorToolNames();

// 没有实现时给模型的那句话。它必须和"被拒绝"区分开:被拒绝说明调用不合适,
// 未实现说明这条路还没修好,模型该做的是告诉用户,不是改参数重试。
std::string UnavailableReason(CreatorToolName tool);

} // namespace miaodesk::creator
