#pragma once

// P0-07「库状态一致恢复」:被跳过的库行必须说得出口。
//
// `WallpaperLibrary::Load` 里原本只有一句
//
//     if (!item.id.empty() && item.kind != Unknown) items_.push_back(...);
//
// 也就是"ID 空"或"Kind 不认识"时,那一行**什么都不说**就没了,而 Load 照常返回 true;
// 头文件上也没有"跳过几行"的出口。调用方(`WallpaperService.cpp:210` 等)只问成败,
// 于是拿着一个悄悄变短的库继续。
//
// 这个模块只做判定与"为什么跳过"那句话 —— 它不碰盘、不 import Windows 头,
// 所以本机就能真跑、真门。宿主用它把丢失变成可见:**不改变**"这一行进不进库"
// (那是要动 UI 的决定),只让它不再无声。
#include <string>
#include <string_view>

namespace miaodesk::library_row {

// 一行为什么没进库。
enum class RowSkipReason {
    // 没有跳过。
    None,
    // 记录没有 ID。这是**损坏**,不是版本差异。
    MissingId,
    // 没有 Kind 字段。同样是损坏。
    MissingKind,
    // Kind 写了,但不在本构建认识的字表里 —— 典型是**版本差异**
    // (旧构建写了新字表、或将来构建写了现在的字表)。
    UnknownKind,
};

const wchar_t* RowSkipReasonName(RowSkipReason reason) noexcept;

// 壁纸库认识的 Kind 字表。与 `WallpaperLibrary::ParseKind` 一致(大小写不敏感)。
// 抽成一处:两处各写一份时,改一边不改另一边,"这句话说 Kind 不认识"就会与
// "实际上它认识"对不上。
bool IsKnownLibraryKind(std::wstring_view rawKind) noexcept;

// 判一行该不该跳过,以及为什么。
// rawKind 传 INI 里读出来的原文(未归一化)—— 空串与"写了个不认识的词"要分开,
// 因为一个是损坏、一个是版本差异,对用户是两回事。
RowSkipReason ClassifyLibraryRow(std::wstring_view id, std::wstring_view rawKind) noexcept;

// 给人看的一句话。None 时返回空。
std::wstring DescribeRowSkip(std::wstring_view id, std::wstring_view rawKind,
                             RowSkipReason reason) noexcept;

} // namespace miaodesk::library_row
