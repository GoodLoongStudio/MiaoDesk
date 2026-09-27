#pragma once

// CCA-10:关掉创作窗口再打开,要能接着刚才那一份做。
//
// 计划验收两条直接压在这上面:
//   * "关闭与重开恢复草稿";
//   * "需求足够时不再强制重复提问"。
//
// 而画面上 `CreationWorkflow` 只有两个钩子:`SetDraftPersistHook` 与
// `CreatorWorkspaceState`。前者**全仓库只有测试在调**,后者只存 sessionId /
// turnId / epoch / cancelRequested / candidateDigest —— 没有需求正文。于是"重开"
// 要么什么也不恢复(用户重新输入一遍,正好违反第二条),要么恢复一个不完整的草稿:
// 窗口打开着,看起来有内容,而里面那句话不是用户说过的。
//
// 所以这里定义**重开真正需要的那些字段**,以及四种互不相同的恢复结论:
//
//   RestoreAndResume  草稿完整、没取消 —— 可以直接接着做,不必再问用户。
//   RestoreAsDraft    草稿在但用户取消过 —— 只展示,不自动继续。用户取消过又自动
//                     开始,是他取消没生效,而他会看到窗口自己动起来。
//   AskUserAgain      需求不足,必须再问。缺哪一项要说出来 —— 只说"需求不足"
//                     等于让用户猜自己上次漏了什么。
//   StartFresh        没有草稿。
//   RejectAmbiguous   草稿接不上(解析失败、或属于另一个作品会话)。**绝不当成
//                     StartFresh**:那样用户会看到一个全新窗口,而他记得自己
//                     写了一整段需求 —— 他会以为是自己记错了。
//
// 序列化刻意沿用 CreatorWorkspaceState 的行式 key=value,但**带上转义**:需求正文是
// 用户自己写的话,里面出现换行本来就很正常。第一版没转义,于是一段多行需求会把
// 后面的行解析成别的键 —— 表现为"草稿恢复出来一半,而且改不掉"。
//
// 它不 import Windows 头,于是"关掉再打开"这一半在本机就能真验。
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miaodesk/CreationWorkflow.h"

namespace miaodesk::creator {

// 重开要接上的那一份。字段对应 CreationBrief 与上一有效候选 ——
// 少一个就是"重开后要重新问一遍"或"重开后少一句话"。
struct CreationDraft {
    std::string sessionId;
    std::uint64_t epoch{};
    std::uint64_t turnId{};
    std::uint32_t briefRevision{1};
    ContentCreatorKind kind{ContentCreatorKind::None};
    std::string goal;                  // 用户原话
    std::string visualDirection;       // 视觉方向
    std::string aspectOrSize;          // 尺寸 / 比例
    std::string dataAndInteraction;    // 数据与交互
    std::string materialSource;        // 素材来源
    std::vector<std::string> allowedCapabilities;
    std::vector<std::pair<std::string, std::string>> userParameters;
    // 上一有效候选。恢复它的摘要,而不是"猜一个看起来像的路径"。
    std::string candidateDigest;
    std::string candidateSummary;
    std::uint32_t candidateRevision{};
    bool cancelRequested{false};
    std::uint64_t savedAtMs{};

    // 与 CreationBrief::Sufficient 同一个判据:种类 + 目标。两条都不靠猜。
    bool Sufficient() const noexcept;
    bool KindValid() const noexcept;
    // 缺哪一项。给人看,要能直接说"缺的是目标"。
    std::string MissingReason() const;
};

std::string SerializeCreationDraft(const CreationDraft& draft);
// 解析。返回 false 时 *out 不被信任(可能已部分填充),调用方必须把它当成
// "这份草稿接不上",而不是当成一个新草稿。
bool ParseCreationDraft(std::string_view text, CreationDraft* out, std::string* error);

enum class DraftRestoreAction {
    StartFresh,
    RestoreAndResume,
    RestoreAsDraft,
    AskUserAgain,
    RejectAmbiguous,
};

const char* ToString(DraftRestoreAction action) noexcept;

struct DraftRestorePlan {
    DraftRestoreAction action{DraftRestoreAction::StartFresh};
    CreationDraft draft;
    std::string reason;
};

// 把一段草稿文本变成一个可执行的恢复动作。
//
// sessionId 是归属的锚:草稿里那个不属于当前会话的,不算这一份的草稿。
DraftRestorePlan PlanDraftRestore(std::string_view savedDraft, std::string_view sessionId);

} // namespace miaodesk::creator
