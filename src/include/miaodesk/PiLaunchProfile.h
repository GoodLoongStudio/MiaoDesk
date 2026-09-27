#pragma once

// Pi 的启动配置(CCA-03)。
//
// 为什么需要它:PiRuntime 原来把 agent 目录、工作目录、扩展路径、工具 allowlist 和
// system prompt 全部写死在函数体里。于是"普通聊天"和"创作"只能共用同一套值 ——
// 而这两者必须分开:
//
//   * 创作不继承普通聊天的任意 bash/read/edit/write 权限(计划 §5);
//   * 两者的模型/上下文/session/取消必须互不影响;
//   * 各自需要不同的扩展文件:通用工具那套和创作工具那套内容不同。
//
// 尤其第三点是个真 bug 而不是洁癖:EnsurePiNativeToolsExtension 原来写死往
// paths::PiAgentRoot()/extensions/miaodesk-native-tools.ts 写。两份配置共用那一个
// 路径时,后写的那份会覆盖前一份,于是*下一次*启动的进程加载到的是另一模式的工具集 ——
// 表现为"聊天突然不能写文件了",而没有任何一处代码改过聊天的 allowlist。
//
// 这里只放纯数据,不放 Windows 类型:配置从哪来、怎么用是 PiRuntime 的事,
// 而"两份配置是否真的分开了"必须能在任何机器上断言。
#include <string>

namespace miaodesk {

enum class PiLaunchMode {
    // 普通聊天:现有行为,一个字节都不能变。
    Chat,
    // 专用内容创作:自己的目录、自己的 session、自己的 cwd、受约束的工具。
    Creator,
};

struct PiLaunchProfile {
    PiLaunchMode mode{PiLaunchMode::Chat};

    // PI_CODING_AGENT_DIR:models.json / settings.json / auth.json / extensions/。
    std::wstring agentDir;
    // PI_CODING_AGENT_SESSION_DIR:Pi 自己的会话落盘位置。
    std::wstring sessionDir;
    // CreateProcessW 的 lpCurrentDirectory。Pi 的 cwd 决定 file/bash 这类工具
    // 相对解析到哪,所以它本身就是隔离的一部分。
    std::wstring workingDirectory;
    // --extension 指向的扩展文件。Chat 与 Creator 必须是不同文件。
    std::wstring extensionPath;
    // --tools 的取值,同时约束 built-in 与 extension 工具(已由 CCA-01 实测确认)。
    std::wstring toolAllowlist;
    // --append-system-prompt 的取值。
    std::wstring systemPrompt;

    // 用于进程身份:同一份 Provider 配置下,Chat 与 Creator 必须各自持有进程,
    // 不能因为 signature 相同而复用对方的子进程。
    std::wstring signatureSalt;

    // 派生值:同一个 runtime 实例换 profile 时,用它判断是否需要重启进程。
    std::wstring Signature() const;
    // 给人看/写日志用的一行摘要。不得包含凭据或完整 URL。
    std::wstring Describe() const;
};

// 下面两个构造函数把今天的真实取值固定住,这样"聊天行为不变"是编译期可见的事实,
// 而不是靠"我记得没改"。
PiLaunchProfile MakeChatLaunchProfile(std::wstring agentDir);
PiLaunchProfile MakeCreatorLaunchProfile(std::wstring agentDir, std::wstring workspaceRoot,
                                        std::wstring extensionPath);

// 通用 shell / 文件工具。创作会话必须整体拿掉它们,改成受约束的包制作工具。
// 放在这里而不是只写在 PiRuntime.cpp 里,是为了让"两套 allowlist 真的不同"
// 成为可断言的公开事实。
extern const wchar_t kPiChatToolAllowlist[];
extern const wchar_t kPiCreatorToolAllowlist[];

} // namespace miaodesk
