#pragma once

// CREATE-06「连续修改语义」里"改的是哪个 workspace"那一半。
//
// `ContentCreatorDialog::InspectForGeneratedPackage` 每一轮 delta 和 done 都会跑一遍，
// 从模型的回复正文里扫一个 `.mdwall` / `.mdwidget` 路径，然后直接
// `SetGeneratedPackage(path)` + `generatedPackageIsCurrentRound = true`。
//
// 它**不问这个路径是不是当前 workspace 的**。而 `DialogState` 里压根没有
// workspaceRoot 这个字段 —— 它在 `UseWorkspace` / `ResetSession` / `CreatorConversationPath`
// 里各自临时解析一次，用完就丢。于是对话框 structurally 无法发现自己拿错了作品。
//
// 用户可见的后果（CREATE-06 的验收原话是"修改正确 workspace，不新建错误作品"）：
// 模型回复里提到**另一个** workspace 的路径时（对话历史是从当前 workspace 读回来的，
// 模型完全可能引用旧路径；用户也可能粘一个进去），那个包会成为本轮的结果并可以
// "应用到桌面"。用户在 A 作品上说"再小一点"，落到桌面上的却是 B 作品。
//
// `CreatorReplyInterpreter` 正是为回答"这一轮做出了什么、凭据是什么"而写的
// （Receipt / ProseScan 两种凭据，sessionId 与 epoch 都要对上）—— 但它**没有任何
// 运行时调用方**，只有自己的测试在调。这里先把其中最要紧的那一问提成纯逻辑：
// 这个路径在当前 workspace 里吗。
//
// 只做字符串 containment，不碰盘：路径由调用方给（扫描器已经产出绝对路径），
// 而"目录是否真的存在"是宿主的事。
#include <string>
#include <string_view>

namespace miaodesk::creator_scope {

// 一个候选包路径相对于当前 workspace 的位置。
enum class CreatorPathScope {
    Inside,        // 就在这个 workspace 里
    EmptyPath,     // 路径是空的（扫描器没产出东西）
    EmptyWorkspace,// 当前 workspace 未知 —— 这本身就是缺陷，不能当作"在里面"
    Outside,       // 在别的 workspace（或盘上别处）
    IsWorkspaceItself, // 指向 workspace 目录自己，不是包
};

const char* ToString(CreatorPathScope scope) noexcept;

// 判定。两个入参都应当是绝对路径；分隔符统一按 `\\` 与 `/` 都认
// （Windows 上两者都合法，而 paths::EnsureDirectory 与模型输出都可能用任一种）。
//
// 不做 weakly_canonical：那要碰盘。这里只做前缀判断，而"规范化之后还一样吗"
// 由调用方在真正用它之前决定 —— 把盘上判断塞进纯逻辑会让它只能等真机。
CreatorPathScope ClassifyCreatorPath(std::wstring_view workspaceRoot,
                                     std::wstring_view candidatePath) noexcept;

// 这个结局能不能把包当成本轮的结果。
// 只有 Inside 能。EmptyWorkspace 也不能 —— "不知道在哪"与"在里面"不是一回事，
// 而它恰恰是最该被喊出来的一种：对话框连自己在做哪个作品都不知道。
bool CreatorPathScopeIsUsable(CreatorPathScope scope) noexcept;

// 给用户/模型看的一句话。
const char* ExplainCreatorPathScope(CreatorPathScope scope) noexcept;

} // namespace miaodesk::creator_scope
