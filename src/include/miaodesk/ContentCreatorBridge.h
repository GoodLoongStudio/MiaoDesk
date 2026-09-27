#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace miaodesk::creator {

enum class ContentCreatorKind : std::uint32_t {
    None = 0,
    Wallpaper = 1,
    Widget = 2,
};

// Cross-process request from MiaoDeskWallpaper.exe to the single MiaoDesk.exe
// conversation owner. The payload is exactly one uint32_t ContentCreatorKind.
inline constexpr ULONG_PTR kContentCreatorCopyDataTag = 0x4D444352u; // "MDCR"

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
// 布局按 AppPaths 的既有惯例:<StateRoot>/CreatorWorkspaces/<kind>/<n>,一次作品一个
// 子目录 —— main.cpp 的 FilesystemCreatorWorkspace 用 root_.parent_path() 放
// revisions/ 与 candidate-ledger.state,所以包目录在里、两份宿主持账在它旁边。
//
// 复用规则由计划自己的验收决定:CCA-10 要求"关闭与重开恢复草稿",所以**重开是同
// 一个工作区**,不是新开一个。目录里已有状态文件就复用它,没有才建第一个。
// 路径上带序号是给"用户明确开第二个作品"留位置 —— 真到那天只加一个动作,
// 不需要迁移已有目录。
std::wstring ResolveCreatorWorkspaceRoot(ContentCreatorKind kind);

// Product-facing entry point used by wallpaper/widget management UI. If the
// main app is already running this reuses its ConversationPanel. Otherwise it
// starts MiaoDesk.exe with a creator-mode command line; the new main instance
// opens the same panel after startup.
bool OpenConversation(ContentCreatorKind kind, HWND owner = nullptr,
                      std::wstring* error = nullptr);

} // namespace miaodesk::creator
