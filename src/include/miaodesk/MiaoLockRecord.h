#pragma once

// P0-08 后半句:锁持有权记录。
//
// `MiaoLockOwnership` 判"这把锁现在是什么状态",但它要知道"谁持有、多久没心跳" ——
// 而这两件事此前**没有任何地方记录**。命名 mutex 只保证"存在与否",不带身份。
//
// 所以这是让它有身份可判的那一半:owner 把自己的 PID 与心跳写进一份小记录,
// launcher 读它再裁决。纯字符串编解码 + 落盘无关,所以本机就能真跑、真门。
//
// 边界:记录**不是**锁本身。锁仍然是 mutex(它保证跨进程互斥);这份记录只是
// 让人能说出口"持有者是谁、它还活着吗"。记录丢失/损坏时不许把锁判成没有持有者 ——
// 那种情形按不可判处理(见 MiaoLockOwnership 的 Unusable)。
//
// 格式是行式 key=value,与仓库里另几处配置一致(CreatorWorkspaceState、
// WallpaperMonitorAssignments 都是这个形状)。两边都是我们自己的代码,
// 一个轻量格式比一个重解析器更好排查。
#include <cstdint>
#include <string>
#include <string_view>

namespace miaodesk::lock_record {

// 锁持有权记录:owner 的身份与新鲜度。
struct LockOwnership {
    // 持有者进程 ID。0 表示这条记录没有持有者。
    std::uint32_t pid{};
    // 最后一次心跳的 Unix 秒。0 表示没有心跳。
    std::uint64_t heartbeatSeconds{};
};

// 编成文本。pid 为 0 的记录也会编码 —— 调用方要能区分"空记录"与"没有记录",
// 而一个空文件做不到这件事。
std::string Encode(const LockOwnership& ownership);

// 解析。返回 false 时 *out 不被信任(可能已部分填充),调用方必须当成
// "没有可用记录",而不是当成一个空记录 —— 后者会让"锁存在但记录坏了"被误判成
// 没有持有者,于是两个 owner 同时上。
bool Decode(std::string_view text, LockOwnership* out);

// 给 Windows 侧用的宽字符版本(配置是 UTF-16LE 的 INI)。
std::wstring EncodeWide(const LockOwnership& ownership);
bool DecodeWide(std::wstring_view text, LockOwnership* out);

// 自检:一个明知该被抓到的坏文本,确认解析器还动得了。
// 少了这一段,"格式改坏了"这件事在记录恰好有效时永远问不出来。
bool SelfCheck();

} // namespace miaodesk::lock_record
