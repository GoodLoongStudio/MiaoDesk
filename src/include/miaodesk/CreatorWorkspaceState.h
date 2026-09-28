#pragma once

// CCA-04:一次创作会话落在盘上的状态。
//
// 工具 worker 是**每次调用一个新进程**(扩展 spawn MiaoDesk.exe
// --native-tool-worker),手里没有任何宿主内存。所以"现在是什么阶段、取消没有、
// 当前候选摘要是什么"这些判据不能问宿主,必须落在工作区里:宿主写,worker 读。
//
// 为什么放在工作区而不是一个全局临时目录:这段状态描述的就是这个作品。换个位置,
// "这段状态属于谁"就成了一个需要额外约定的事,而约定一旦对不上,worker 会拿着
// 另一个作品的阶段去放行这一次调用 —— 那正是归属判断要防的事。
//
// 它是纯逻辑:序列化与解析都不 import Windows 头,于是"写出去再读回来是不是同一份"
// 在本机就能真验,而不是只留给 Windows 真机。
#include <cstdint>
#include <string>
#include <string_view>

namespace miaodesk::creator {

struct CreatorWorkspaceState {
    std::string sessionId;
    std::uint64_t epoch{};
    // CreationStage 的整数值(CreationWorkflow.h)。这里存整数而不是枚举,
    // 理由与 CreatorToolRegistry 相同:不该为了一个整数把整个工作流拖进来。
    int stage{0};
    bool cancelRequested{false};
    // 最近一次封存的候选摘要。空表示这一轮还没有成型候选。
    // 它同时是 expectedDigest 的对照物:模型拿着一个过期摘要来写,必须被拒。
    std::string candidateDigest;
    std::uint32_t revision{0};
    std::uint64_t updatedAtMs{};

    // 两份状态描述的是不是同一次创作的同一轮。epoch 不同即是另一轮 ——
    // 取消后重开、或者用户切到别的作品,epoch 都会变。
    bool SameRoundAs(const CreatorWorkspaceState& other) const noexcept {
        return sessionId == other.sessionId && epoch == other.epoch;
    }
    bool HasCandidate() const noexcept { return !candidateDigest.empty(); }
};

// 会话身份允许出现的字符。
//
// 它要同时被三个地方认:当目录名的最后一段(见 AppPaths.h 的说明)、当
// CreatorSessionBinding::sessionId、当工具参数里的 sessionId 强制比对。而
// DeriveCreatorSessionId 只取路径的最后一段 —— 出现任何分隔符都会把身份切断。
//
// 做成共享的一个函数而不是各写一份,是因为**生成与读取必须严丝合缝**:
// 生成方(NewCreatorSessionId)只产这些字符,读取方(active 文件)只收这些字符。
// 两边各写一份的话,哪天一边松了一边紧,产出来的身份读回来会被整段丢掉 ——
// 表现是每次重开都换一个工作区,草稿永远回不来。
bool IsUsableSessionChar(char ch) noexcept;

// 一个新的会话身份。它必须唯一:同一 kind 的两个作品共用一个会话 ID,归属判断
// (CreatorWorkspacePolicy::SessionMatches)就不再能区分它们,而那种撞车看起来
// 像"另一个作品的调用被接受了"。
//
// 单独做成这里的纯函数、而不是在桥里现写,是为了能在 macOS 上真跑:唯一性与可用
// 字符是这里唯一要断定的事,而那两样都不需要一台 Windows 机器。写在桥里,它们就
// 只剩一句"Windows 真机待验"。
std::string NewCreatorSessionId(std::uint64_t nowMs);

// active 文件里的一段文本 → 会话身份。读失败、读到注释或读到空行都返回空串,
// 调用方必须把空串当成"没有这段会话",而不是当成一个合法的会话名。
//
// 它和 IsUsableSessionChar 是同一件事的两头:生成方只产那些字符,这里只收那些
// 字符。所以"NewCreatorSessionId(...) 过一遍还是它自己"是一条能断言的等式,
// 而不是两个文件各写一份、靠注释祈祷它们一致的巧合。
std::string SanitizeCreatorSessionId(const std::string& text);

// 序列化成行式 key=value。顺序固定,便于人工排查;值里的换行与等号会被拒绝,
// 不让解析出现歧义 —— 一份会被两种写法解析成不同结果的状态文件,比没有更糟。
std::string SerializeCreatorWorkspaceState(const CreatorWorkspaceState& state);

// 解析。返回 false 时 out 的内容不被信任(可能是部分填充),调用方必须把它当成
// "没有状态",而不是"状态长这样"。
//
// 未知的键被忽略而不是报错:将来加字段时,旧版本宿主写出的文件仍要能读。
// 但**已知的键出现两次会报错**:两份矛盾的值里没有该信的那一份。
bool ParseCreatorWorkspaceState(std::string_view text, CreatorWorkspaceState* out);

// 状态文件在工作区里的文件名。用点前缀让它不出现在用户的文件列表里 ——
// 它是宿主的记账,不是作品内容,但它必须在工作区内(见文件头)。
constexpr const char* kCreatorWorkspaceStateFileName = ".miaodesk-session.state";

} // namespace miaodesk::creator
