#pragma once

// SEARCH-04「Goz 服务故障自动恢复」里"可诊断"的另一半。
//
// `MiaoFileSearchNotice` 把三桶状态与三句文案提纯了,但它自己的头注释写着一条
// 限制:"这里的输入只证明'客户端二进制在'"—— 于是第三桶只能说
//
//     "这一次没有拿到文件结果(可能索引服务没起来,也可能这次查询超时)"
//
// 那是一句**诚实的猜测**,不是诊断。用户照它排查,第一步仍然是猜。
//
// 缺的是第四个输入:**恢复动作实际发生了什么**。`GozSearch::EnsurePipeAvailable`
// 一路上看得见每个环节(SCM 打不打得开、服务在不在、状态是什么、StartService
// 成没成、等到没有),但它把每一步都当场丢掉了,只回一个 bool。
//
// 这里放那一步的纯判定。宿主导出观测值,模块回答"该怎么说"。
//
// 分开成独立模块而不是塞进 MiaoFileSearchNotice 的理由:那个模块的输入是
// UI 状态(pending / available / queryFailed),这个的输入是**服务控制管理器
// 的返回值**。混在一起会让"UI 知道什么"和"系统知道什么"共用一套字段,
// 而它们的更新时机完全不同 —— UI 状态每轮查询变一次,服务状态只在恢复时变。
#include <cstdint>
#include <string>

namespace miaodesk::goz_recovery {

// 服务当前状态。取的是 `SERVICE_STATUS_PROCESS::dwCurrentState` 的原始值,
// 但映射单独一个函数 —— 数值写错会让"停着"被读成"正在起",于是不该拉的也拉。
enum class GozServiceState {
    Unknown,
    Stopped,        // SERVICE_STOPPED
    StartPending,   // SERVICE_START_PENDING
    StopPending,    // SERVICE_STOP_PENDING
    Running,        // SERVICE_RUNNING
    ContinuePending,
    PausePending,
    Paused,
};

// 把 Win32 的 dwCurrentState 翻成上面的枚举。
//
// 为什么要一个函数而不是在调用点 switch:调用点在 `EnsurePipeAvailable` 里,
// 而那里同时还有十几个别的 Win32 调用。映射写在调用点,下次有人加一个状态
// 就会漏掉一处,而漏掉的那处会静默落到 default。
GozServiceState DescribeGozServiceState(std::uint32_t rawState) noexcept;
const char* ToString(GozServiceState state) noexcept;

// 一次恢复尝试的结局。
enum class GozRecoveryOutcome {
    NotNeeded,        // 管道本来就在,什么都没做
    ServiceMissing,   // 没有这个服务(gozd 没装,或服务名不对)
    AccessDenied,     // 打不开 SCM 或服务(权限不足)
    StatusUnknown,    // 状态查不到
    StartIssued,      // 服务停着,已发出启动请求
    AlreadyStarting,  // 服务正自己在起,只需等
    StartRefused,     // 拉不起来(被禁用 / 被拒绝)
    Recovered,        // 等到了
    TimedOut,         // 等满了还没就绪
};

const char* ToString(GozRecoveryOutcome outcome) noexcept;

// 判定。参数按 `EnsurePipeAvailable` 的执行顺序:
//   pipeUpAtEntry  进来时管道通不通
//   scmOpened      OpenSCManagerW 成没成
//   serviceOpened  OpenServiceW 成没成
//   state          查到的服务状态(scmOpened && serviceOpened 才有意义)
//   startIssued    StartServiceW 叫没叫出去(只在 Stopped 时才该叫)
//   pipeUpAtExit   等完的时候管道通不通
GozRecoveryOutcome DecideGozRecovery(bool pipeUpAtEntry,
                                     bool scmOpened,
                                     bool serviceOpened,
                                     GozServiceState state,
                                     bool startIssued,
                                     bool pipeUpAtExit) noexcept;

// 这一轮算不算把文件搜索救回来了。
bool GozRecoverySucceeded(GozRecoveryOutcome outcome) noexcept;

// 这一轮该不该让 UI 说点什么。成功与"本来就不需要"都不必说 ——
// 用户没问就报一句"已恢复"是噪音。
bool GozRecoveryNeedsNotice(GozRecoveryOutcome outcome) noexcept;

// 给用户看的一句话。技术原文(错误码)不进这句话,放诊断。
//
// 文案受一条不变式约束:**不许断言观测没有建立的事实**。上一版那句
// "文件索引已连接,但本次查询失败"就是违反者 —— 它键的输入只证明客户端
// 二进制装着。这里每一句都只说这一步实际发生了什么。
//
// 回传 wstring 而不是 const char*:调用方(SM_searchWindow 的提示行)存的是宽串,
// 而那个文件里没有任何 UTF-8→UTF-16 的帮手。与
// `MiaoFileSearchNotice::FileSearchNoticeDetail` 同一个形状。
std::wstring ExplainGozRecovery(GozRecoveryOutcome outcome);

} // namespace miaodesk::goz_recovery
