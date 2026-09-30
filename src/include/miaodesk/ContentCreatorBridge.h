#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::creator {

enum class ContentCreatorKind : std::uint32_t {
    None = 0,
    Wallpaper = 1,
    Widget = 2,
};

// Cross-process creator request from MiaoDeskWallpaper.exe to the single
// MiaoDesk.exe owner.
//
// v2 uses a registered message only as a tiny capability/queue handshake. The
// receiver MUST enqueue actual window creation and return immediately; creator
// initialization can read API profiles, transcripts and Skills and therefore
// must never run inside the sender's synchronous IPC timeout.
//
// WM_COPYDATA stays as a compatibility fallback for older running hosts.
inline constexpr ULONG_PTR kContentCreatorCopyDataTag = 0x4D444352u; // "MDCR"
inline constexpr LRESULT kContentCreatorOpenAck = 0x4D444F4Bu; // "MDOK"
inline constexpr LRESULT kContentCreatorOpenVisible = 0x4D445649u; // "MDVI"
inline constexpr LRESULT kContentCreatorOpenReady = 0x4D445244u; // "MDRD"

UINT OpenRequestMessage() noexcept;
UINT OpenStatusMessage() noexcept;

ContentCreatorKind ParseCommandLine(std::wstring_view commandLine) noexcept;
std::wstring InitialPrompt(ContentCreatorKind kind);

bool DecodeCopyData(const COPYDATASTRUCT* data, ContentCreatorKind* kind) noexcept;
bool SendToRunningApp(ContentCreatorKind kind, DWORD timeoutMs = 3000) noexcept;

// 这一作品的**工作区根目录**。宿主持有,不从工具参数取。
//
// 为什么放在这里:PiRuntime 只在 mode == Creator 时导出 MIAODESK_CREATOR_WORKSPACE,
// 而 src/app/main.cpp 的 RunCreatorTool 正是从那个环境变量取工作区。于是"根目录放在
// 哪"一直是未决定的状态,而它未决定,创作 profile 就装不上。
//
// 布局按 AppPaths 的既有惯例:<StateRoot>/CreatorWorkspaces/<kind>/<sessionId>,
// 一次作品一个子目录 —— main.cpp 的 FilesystemCreatorWorkspace 用
// root_.parent_path() 放 revisions/ 与 candidate-ledger.state,所以包目录在里、
// 两份宿主持账在它旁边。
//
// <sessionId> 是 NewCreatorSessionId 生成的一段身份,**不是序号**:
// DeriveCreatorSessionId 取路径的最后一段当会话 ID,CreatorWorkspacePolicy::
// SessionMatches 拿它和工具参数里的 sessionId 比。写成 1/2 的话,同一 kind 的
// 所有作品共用一个会话 ID —— 归属判断正是靠这个字符串区分作品的。
//
// 复用规则由计划自己的验收决定:CCA-10 要求"关闭与重开恢复草稿",所以**重开是同
// 一个工作区**,不是新开一个。当前那段身份记在 <kind>/active 里(内容就是目录名
// 本身),它存在就复用,不存在才分配新身份并写回。用户明确要开第二个作品时,
// 换一段新身份即可,不需要迁移已有目录。
std::wstring ResolveCreatorWorkspaceRoot(ContentCreatorKind kind);
std::wstring StartNewCreatorWorkspace(ContentCreatorKind kind);
bool ActivateCreatorWorkspace(ContentCreatorKind kind, std::wstring_view workspaceRoot);
std::vector<std::wstring> ListCreatorWorkspaces(ContentCreatorKind kind);

// Product-facing entry point used by wallpaper/widget management UI. If the
// main app is already running this reuses its ConversationPanel. Otherwise it
// starts MiaoDesk.exe with a creator-mode command line; the new main instance
// opens the same panel after startup.
bool OpenConversation(ContentCreatorKind kind, HWND owner = nullptr,
                      std::wstring* error = nullptr);

} // namespace miaodesk::creator
