#pragma once

#include <string>

namespace miaodesk {

// 宿主把"这次创作属于哪个作品"告诉 Pi 扩展与工具 worker 的两个环境变量。
//
// 它们是宿主与扩展之间的握手,不是配置。所以**只对创作进程导出**:聊天进程拿不到
// 它们,于是即使在最坏情况下聊天侧加载到了创作扩展,工具调用也会因为拿不到绑定而
// 被拒,而不是拿到一个指向桌面目录的工作区。空值在这里是安全的那个方向。
//
// 名字以 MIAODESK_CREATOR_ 开头而不是复用 PI_CODING_AGENT_DIR,是为了让"这两个值
// 只描述创作会话"在进程环境里也看得出来 —— 排查时不需要读代码就知道它们属于谁。
inline constexpr wchar_t kCreatorWorkspaceEnvironment[] = L"MIAODESK_CREATOR_WORKSPACE";
inline constexpr wchar_t kCreatorSessionEnvironment[] = L"MIAODESK_CREATOR_SESSION";

// Which MiaoDesk-owned Pi extension to materialize. Chat and Creator are two
// different files on purpose: they register different tool sets, and a single
// shared path means whichever profile writes last wins for every *future* process.
enum class PiNativeToolsVariant {
    Chat,
    Creator,
};

// Materializes the MiaoDesk-owned Pi extension into the dedicated Pi agent
// directory and exports the current native host path for its isolated workers.
// When extensionPath is non-null, it receives the absolute path of the installed
// extension entrypoint that Pi must load via --extension.
//
// targetDirectory is where the extension file is written. Chat and Creator must
// pass different directories (see PiLaunchProfile); sharing one means the second
// write silently replaces the first, and the next process of the *other* mode
// loads the wrong tool set.
bool EnsurePiNativeToolsExtension(std::wstring* error = nullptr,
                                  std::wstring* extensionPath = nullptr,
                                  PiNativeToolsVariant variant = PiNativeToolsVariant::Chat,
                                  const std::wstring& targetDirectory = {});

} // namespace miaodesk
