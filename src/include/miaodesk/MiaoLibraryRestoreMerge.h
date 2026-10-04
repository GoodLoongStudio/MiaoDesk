#pragma once

// P0-07「App 重启状态一致性」里"库状态一致恢复"的合并策略。
//
// 壁纸库从盘上读回来之后要走两遍合并:一遍是旧版 Scene 行 → 规范化包 ID
// (`WallpaperLibrary::Load` 第一段),一遍是旧版内置 scene-* 行 → 官方包 ID(第二段)。
// 两遍各自有一份**一字不差**的副本:
//
//     merged.favorite            = merged.favorite || legacy.favorite;
//     merged.importedUnixSeconds = EarliestNonZero(merged.importedUnixSeconds, legacy.importedUnixSeconds);
//     merged.lastUsedUnixSeconds = std::max(merged.lastUsedUnixSeconds, legacy.lastUsedUnixSeconds);
//
// 这个策略本身是对的 —— 它能保证恢复不丢用户状态。问题是它有两份,而且
// **没有一处说明"为什么恰好是这三个字段"**。于是有一天往 `WallpaperLibraryItem`
// 里加一个字段(比如"用户自己起的名字"或"每播放 N 分钟"),两份副本都会安静地不合并它:
// 恢复之后用户发现那一项变回了默认值,而 Load 照常返回 true。这正是 P0-07 的失败形态
// ——"恢复成一个更短的库并报告成功"。
//
// 这里只放合并策略,不碰 INI、不碰文件系统。
#include <string>
#include <string_view>

namespace miaodesk::library_restore {

// 一次合并要用的两个输入里,**属于用户状态**的那部分。
//
// 为什么不是整个 `WallpaperLibraryItem`:ID / Source / Kind / ManagedCopy 由规范化包
// 决定(那叫身份,不叫状态),Title / Thumbnail 同样来自包。把它们塞进"用户状态"会让
// "规范化包该赢"这条规则和"用户改过的东西该留"这条规则混在一起,而它们偶尔会冲突 ——
// 混在一起就分不清该让谁赢了。
struct RestoredUserState {
    bool favorite{};
    unsigned long long importedUnixSeconds{};
    unsigned long long lastUsedUnixSeconds{};
};

// 规范化包那一侧的身份字段合并后是什么。分开一个结构是为了让
// "用户状态单调不减"与"身份由包决定"这两条规则各自可断言。
struct RestoredIdentity {
    std::wstring id;
    std::wstring kindText;
};

// 把两条记录合并成一条。canonical 是规范化包那一份,legacy 是盘上那一份。
//
// 三条不变式:
//   1. **收藏不丢** —— 任一侧收藏过,结果就收藏(or,不是覆盖);
//   2. **导入时间不后退** —— 取更早的那个非零值(0 表示"没记",不是"1970 年");
//   3. **最近使用不后退** —— 取更大值。
//
// 每一条都是单调的:合并结果在该字段上不劣于任何一个输入。反方向就是数据丢失,
// 而且丢得安静 —— Load 照常返回 true。
RestoredUserState MergeRestoredUserState(const RestoredUserState& canonical,
                                         const RestoredUserState& legacy) noexcept;

// 两个非零时间戳里更早的那个。0 表示"没有记录",不当成 1970 年。
unsigned long long EarliestNonZeroSecond(unsigned long long a, unsigned long long b) noexcept;

// 这一栏说明"用户状态"到底有哪些字段。它是上面那个结构体的配对清单 ——
// 加字段时必须同时改这两处,否则自检会红(见 LibraryRestoreMergeTest)。
// 写成字符串而不是反射:C++ 没有反射,而一份手写的清单比没有清单好:
// 它至少让"漏了一个字段"在测试里是可断言的。
constexpr std::string_view kRestoredUserStateFields = "favorite,importedUnixSeconds,lastUsedUnixSeconds";
constexpr std::size_t kRestoredUserStateFieldCount = 3;

} // namespace miaodesk::library_restore
