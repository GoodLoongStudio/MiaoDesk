#pragma once

// CCA-04:创作工具的路径与作品归属策略。
//
// 计划 §5 的要求很直接:"仅当前工作区内获准的包文件"、"宿主验证路径、reparse point、
// 候选所有权与扩展加载规则;不能仅靠 prompt 说「不要越界」"。
//
// 这份文件是那套策略的纯逻辑部分。它刻意不 import 任何 Windows 头,也不自己碰文件
// 系统:调用方喂给它"某个会话的工作区根目录是什么"和"这个文件是否存在、是不是
// reparse point",它只回答"这个请求允不允许、为什么"。
//
// 为什么把归属判断做成显式输入而不是让策略自己去查盘:
//   归属的事实来源是宿主自己的会话记录。工具 worker 是另一个进程,拿到的只有 JSON
//   参数 —— 模型说什么都不能成为事实来源。所以工作区根由宿主从自己的状态里取出来
//   传进来,策略只负责按它判定。这样"模型伪造 sessionId 访问另一个作品"在结构上
//   就不可能,而不是靠记得校验。
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::creator {

// 为什么被拒绝。每一条都对应一种真实的越界方式,而不是笼统的"非法路径"。
enum class CreatorWorkspaceReject {
    None,
    EmptySessionId,
    EmptyRelativePath,
    NotRelative,          // 绝对路径、驱动器号、根目录
    Traversal,            // ".." 段
    ForbiddenExtension,   // 代码产物 / 可执行文件
    UnknownRole,          // 不在获准的包文件布局里
    TooLarge,
    OutsideWorkspace,     // 解析后落在工作区之外(symlink/junction/大小写等)
    ReparsePoint,         // Windows:符号链接/联接点
    WrongWorkspace,       // 声称的工作区与宿主记录里的不是同一个
    NoContentPackageYet,  // 还没有 manifest.json,不接受 scene/素材写入
};

const char* ToString(CreatorWorkspaceReject reject) noexcept;

// 一个获准的包文件扮演什么角色。角色决定允许的扩展名与写入顺序。
enum class CreatorFileRole {
    Manifest,      // manifest.json
    Parameters,    // parameters.json
    Scene,         // scene/*.json
    Preview,       // preview.<image ext>
    Asset,         // assets/*
};

const char* CreatorFileRoleName(CreatorFileRole role) noexcept;
bool ParseCreatorFileRole(std::string_view text, CreatorFileRole* role) noexcept;

// 宿主提供的、关于这个文件的事实。策略不自己碰盘,所以这些必须由调用方填。
struct CreatorFileFacts {
    std::uint64_t byteCount{};
    bool exists{false};
    bool isDirectory{false};
    bool isReparsePoint{false};   // Windows:符号链接 / 联接点 / mount point
};

// 会话绑定的输入。workspaceRoot 必须来自宿主的会话记录,不能来自工具参数。
struct CreatorSessionBinding {
    std::string sessionId;
    std::string workspaceRoot;        // 宿主记录里的绝对路径
    std::string claimedWorkspace;     // 工具参数里声称的(只为对账,不作事实来源)
};

struct CreatorWorkspaceLimits {
    std::uint64_t maxManifestBytes{1024 * 1024};
    std::uint64_t maxSceneBytes{16 * 1024 * 1024};
    std::uint64_t maxParameterBytes{1024 * 1024};
    std::uint64_t maxAssetBytes{32 * 1024 * 1024};
    std::uint64_t maxPreviewBytes{32 * 1024 * 1024};
};

class CreatorWorkspacePolicy {
public:
    CreatorWorkspacePolicy(CreatorSessionBinding binding, CreatorWorkspaceLimits limits = {});

    const CreatorSessionBinding& Binding() const noexcept { return binding_; }

    // 这个请求允不允许?reason 非空时说明为什么不允许 —— 拒绝必须带原因,
    // 否则模型只会反复重试同一个越界路径,而用户看不到任何解释。
    //
    // rejectCode 是可选的第二个输出。它存在的理由是:"被拒了"和"因为**哪一条规则**
    // 被拒了"是两件事。计划 §5 要求"返回结构化错误码",而只有布尔值时,
    // "代码产物被拒"和"路径不在布局内被拒"在测试里长得一模一样 —— 于是一处
    // 规则的失效会被另一处规则悄悄掩盖。带上它,每条规则才能各自被断言。
    bool Allows(std::string_view relativePath, const CreatorFileFacts& facts,
                std::string* reason = nullptr,
                CreatorWorkspaceReject* rejectCode = nullptr) const;

    // 路径合法时它是什么角色;不合法返回 false。
    bool Classify(std::string_view relativePath, CreatorFileRole* role) const;

    // 项目内相对路径 -> 工作区内的绝对路径(小写化前)。只在 Allows() 通过后调用。
    // 返回 false 表示连规范化都不安全。
    bool Resolve(std::string_view relativePath, std::string* resolved) const;

    // 一类工具调用里,哪些扩展名根本不该出现。公开是为了测试与日志,
    // 也为了让"代码产物不在创作范围里"这件事是可引用的,而不是藏在正则里。
    static bool IsForbiddenExtension(std::string_view extension) noexcept;
    static bool IsAllowedAssetExtension(std::string_view extension) noexcept;
    static bool IsAllowedImageExtension(std::string_view extension) noexcept;

    // 拒绝原因 -> 给用户/模型看的一句话。技术原文放诊断,不放过长的堆栈。
    static std::string Explain(CreatorWorkspaceReject reject, std::string_view relativePath);

private:
    bool SessionMatches() const noexcept;

    CreatorSessionBinding binding_;
    CreatorWorkspaceLimits limits_;
};

// 归一化:统一分隔符、去掉前导 "./"、拒绝绝对/遍历。返回 false 即结构性非法。
// 这一步不碰盘,所以可以在这里反复测。
bool NormalizeCreatorRelativePath(std::string_view input, std::string* normalized);

// 从工作区路径导出会话 ID:取最后一段目录名。
//
// 所以最后一段不能是序号:它由宿主分配,而**它必须唯一** —— 同一个序号给同一 kind
// 的两件作品用,归属判断(SessionMatches)就再也区分不了它们,表现是"另一个作品的
// 调用被接受了"。生成规则见 CreatorWorkspaceState::NewCreatorSessionId。
//
// 为什么不让调用方再单独传一个 ID:会话 ID 的唯一作用就是把一次创作和它的工作区
// 绑在一起,而工作区路径本身就是宿主分配的那个唯一标识。再另给一个字符串,就会
// 出现两个可以互相矛盾的事实来源 —— 而它们一旦矛盾,"这个调用属于哪个作品"
// 就没有答案了,只有两个都有权声称自己是答案的东西。
//
// 空路径或只有分隔符时返回空串。调用方必须把空串当成"没有会话",而不是当成一个
// 合法的会话名。
std::string DeriveCreatorSessionId(std::string_view workspaceRoot) noexcept;

// 用于把"这个文件到底在不在工作区里"的判断题交给宿主:它知道真实的文件系统。
// 返回 true 表示接受。默认实现只做字符串层级判断,宿主可以再包一层真实检查。
using WorkspaceContainment = std::function<bool(const std::string& resolved)>;

} // namespace miaodesk::creator
