#pragma once

// P0-08 后半句:锁持有权记录与"现有的那个还健康吗"。
//
// 这一份是 `MiaoLockOwnership`(裁决)与 `MiaoLockRecord`(编解码)在宿主侧的合流点,
// 存在的理由是**同一个问题有五处启动点,而它们各答一遍**:
//
//     if (NamedMutexExists(name)) return true;
//
// 那个布尔回答不了"它属于谁、那个进程还活着吗"。命名 mutex 只保证存在与否,
// 不带身份 —— 而 `MiaoLockOwnership` 判 Wedged 需要 PID 与心跳,那只能另存一份。
//
// 这个头文件把"记录放哪、谁写它、怎么读它判健康"收成一处。五处启动点共用,
// 就不会出现"壁纸那边判得出卡住、后台 Harness 那边还是一句 return true"这种分裂。
//
// 边界(**这是产品决策,刻意留白**):这里只判与报,**不接管**。接管要
// `TerminateProcess` 一个还活着的进程 —— 那是宿主要决定的事,本模块只提供裁决。
#include <cstdint>
#include <string>

namespace miaodesk::lock_host {

// 一条锁持有权记录的落盘位置。锁名形如 `Local\\MiaoDesk...`,反斜杠不能进文件名,
// 统一换成 '-'(规则固定,路径才可复现)。返回空表示拿不到状态根,调用方必须当
// "判不了"处理,**不许**当成"没有持有者"。
std::wstring OwnerRecordPathFor(std::wstring_view mutexName);

// owner 侧:写一条"我是持有者"。返回是否真的落盘了。
// 只有**真的拿到锁**的那一个才该调它 —— 抢锁失败就写,等于两个 owner 都自称持有者,
// 而记录只有一份。
bool PublishOwnership(std::wstring_view mutexName, std::uint32_t pid, std::uint64_t nowSeconds);

// owner 侧:收掉这条记录。析构里调。
void RetireOwnership(std::wstring_view mutexName);

// launcher 侧:锁已经在,那它算不算"一个好好干活的持有者"。
//
// 返回值刻意是 bool 而不是给人看的一句话:调用点多数没有诊断显示面,
// 提供一个没人读的字符串出口只会变成死代码。真正该显示的那句话,
// 由拿到裁决的宿主自己拼(`MiaoLockOwnership::DescribeLockVerdict`)。
//
// 判不了(记录丢了/坏了/没有)也算**不健康** —— 我们并没有确认有一个能干活的持有者。
bool ExistingOwnerIsHealthy(std::wstring_view mutexName, std::uint64_t nowSeconds,
                            std::uint64_t leaseSeconds);

// 给没有现成时钟的调用方一个默认租约。**显式常量,不是隐式默认值**:
// 配 0 会让裁决变成 Unusable,那是"读到了记录也判不了",比原来的布尔更糊涂。
inline constexpr std::uint64_t kDefaultOwnershipLeaseSeconds = 30;

} // namespace miaodesk::lock_host
