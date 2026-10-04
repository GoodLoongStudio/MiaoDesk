#pragma once

// SEARCH-02「排序与去重质量」的去重那一半。
//
// 索引建好之后,`AppSearch::BuildIndex` 用
//
//     const auto key = Lower(entry.name + L"|" + entry.target);
//
// 去重。这行是纯字符串逻辑,却住在 `AppSearch.cpp` 里 —— 那个文件 include
// `<windows.h>`(索引来自开始菜单、注册表、App Paths),于是**本机一行都跑不到**。
// 而 SEARCH-02 的验收原话就是"排序与去重质量",排序上一轮已经提成
// `SearchTextScoring` 并在本机有了 22 项断言,去重这一半还裸着。
//
// 为什么这半个也值得单独测:用户能直接看见它的两个失败形态。
//
//   · **同一条应用被索引两遍** —— 开始菜单与注册表 App Paths 经常对同一个程序
//     各给一条(name 相同、target 相同),不去重用户会在结果里看到两个"Chrome"。
//   · **两个不同的应用被并成一个** —— key 用 `name|target` 拼,如果 name 里
//     能出现 `|`,两条不同的记录会拼出同一个 key,于是其中一个**消失了**。
//     这在真实索引里不是假想:ini 注册表项、URL、带管道的右键菜单目标都会带 `|`。
//
// 这里只放拼 key 与判重的纯逻辑,不碰盘、不碰注册表。
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk {

// 一个待去重的候选。与 `AppSearch::Entry` 同构,但不引 Windows 头。
struct SearchDedupEntry {
    std::wstring name;
    std::wstring target;
};

// 拼出去重用的键。
//
// 规则与 `AppSearch::BuildIndex` 逐字相同:`Lower(name + L"|" + target)`。
// 抽成函数是为了能单测,也为了让"`|` 会撞键"这件事**看得见**而不是埋在循环里。
std::wstring SearchDedupKey(const SearchDedupEntry& entry) noexcept;

// 判一个候选是不是第一次出现。
//
// seen 里装的是此前 `SearchDedupKey` 返回过的键。空键**不**参与去重 ——
// 空 name + 空 target 的记录本来就是脏数据,把多条脏数据并成一条只会让
// "索引里有几条坏记录"这个答案消失。
bool IsFirstSearchEntry(const SearchDedupEntry& entry,
                        const std::vector<std::wstring>& seenKeys) noexcept;

// 去重:保留首次出现的顺序(顺序本身就是 Top-3 基线的一部分 ——
// 重排会让"同分应用每次顺序不同"重新变成用户可以看见的现象)。
std::vector<SearchDedupEntry> DedupSearchEntries(const std::vector<SearchDedupEntry>& entries);

// 键里允许出现的最大 name 长度。超过就认为这条记录不可信,不去重它 ——
// 一个 64K 的名字会把 `Lower(name + L"|" + target)` 变成一次可观的分配,
// 而索引来自注册表,注册表可以被写成任意长。
inline constexpr std::size_t kSearchDedupMaxNameLength = 4096;

} // namespace miaodesk
