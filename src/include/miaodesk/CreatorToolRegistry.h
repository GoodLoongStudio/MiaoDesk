#pragma once

// CCA-04:创作工具的名册、路由与参数解析(纯逻辑)。
//
// 计划 §5 的验收要求里有一条容易被当成"细节":按进程/会话配置验证扩展**实际拿到的**
// 工具清单。它之所以不是细节,是因为 Pi 侧的 --tools 和宿主的 worker 允许表是**两份
// 独立的清单**,而两者一旦不一致,故障是单向静默的:
//
//   * Pi 给了、worker 不认  -> 工具调用失败,模型重试,用户看到"它一直不成功";
//   * worker 认、Pi 没给    -> 能力躺在那里没人能用,但没有任何东西报错。
//
// 这个文件把名册收敛成一处,并让两侧都能引用同一组函数,于是"两份清单一致"变成可断言的
// 事实,而不是两处需要手工同步的字面量。
//
// 它也不碰盘、不 import Windows 头:路由只决定"这个调用能不能进、由谁执行",
// 真正的文件动作在宿主侧。
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::creator {

// 拟新增的八个创作工具(计划 §5)。名字在这里冻结。
enum class CreatorToolName {
    CapabilitiesGet,   // creator_capabilities_get
    SkillGet,          // content_skill_get(已有,复用)
    PackageRead,       // creator_package_read
    PackageUpdate,     // creator_package_update
    AssetImport,       // creator_asset_import
    ImageGenerate,     // creator_image_generate
    CandidateSubmit,   // creator_candidate_submit
    PreviewEvidence,   // creator_preview_evidence
};

const char* ToString(CreatorToolName tool) noexcept;
// 工具在 Pi 侧注册的名字。解析失败返回 false。
bool ParseCreatorTool(std::string_view text, CreatorToolName* tool) noexcept;

// 全部八个名字,顺序固定。worker 允许表与 Pi 的 --tools 都由它派生。
const std::vector<std::string>& CreatorToolNames() noexcept;

// 这两组是给两侧用的判据。它们不是同一份清单的别名,而是**互相校验**的两半:
// 名册是唯一事实来源,worker 与 Pi 各自声明"我用名册",测试再断言它们覆盖一致。
bool IsCreatorTool(std::string_view tool) noexcept;

// 会改变候选内容的工具。它们需要更严的阶段与归属检查:读与查能力放得宽,
// 写入只在制作与修复的那几个阶段允许(与 CreationWorkflow 的策略一致)。
bool IsMutatingCreatorTool(CreatorToolName tool) noexcept;
bool IsMutatingCreatorTool(std::string_view tool) noexcept;

// 工具调用的最小参数。模型提交的一切都按输入处理。
struct CreatorToolArgs {
    std::string sessionId;
    std::string workspaceRoot;    // 模型声称的工作区;只为对账,不作事实来源
    std::string relativePath;     // 包内相对路径
    std::string content;          // PackageUpdate 的新内容
    std::string source;           // AssetImport 的来源
    std::string digest;           // CandidateSubmit 的候选摘要
    std::string backend;          // PreviewEvidence 的目标后端
    std::string summary;          // CandidateSubmit 的一句话说明
};

enum class CreatorToolReject {
    None,
    UnknownTool,         // 不在名册里
    MissingSession,      // 没带 sessionId
    SessionMismatch,     // sessionId 与当前作品不符
    EpochStale,          // 来自已取消或已切换的作品
    MissingWorkspace,    // 工作区对不上
    StageNotAllowed,     // 当前阶段不接受这类工具
    MissingArgument,     // 必填参数缺失
};

const char* ToString(CreatorToolReject reject) noexcept;

// 一次路由的结论。宿主据此决定执行什么,或把哪一句回给模型。
struct CreatorToolRoute {
    bool allowed{false};
    CreatorToolName tool{};
    CreatorToolReject reject{CreatorToolReject::None};
    std::string reason;          // 给人/模型看的一句
    CreatorToolArgs args;
    bool mutating{false};

    // 拒绝时是否值得让模型重试。参数缺失、阶段不对都值得;epoch 过期、会话不符
    // 不值得 —— 重试只会重复同一个错误,而用户看不到任何进展。
    bool Retryable() const noexcept {
        return reject == CreatorToolReject::MissingArgument;
    }
};

// 路由输入。epoch / stage / workspace 由宿主从自己的状态提供,不从参数里取。
struct CreatorToolContext {
    std::string sessionId;
    std::uint64_t epoch{};
    std::string workspaceRoot;   // 宿主记录里的真实工作区
    int stage{0};                // CreationStage 的整数值;避免这里反过来依赖那个头
    bool cancelRequested{false};
};

// 把一次工具调用路由成"执行"或"拒绝"。纯函数:同样的输入永远得到同样的结论,
// 所以它能在任何机器上被测到,包括那些只在真机才会出现的边界。
CreatorToolRoute RouteCreatorTool(std::string_view tool, const CreatorToolArgs& args,
                                 const CreatorToolContext& context);

} // namespace miaodesk::creator
