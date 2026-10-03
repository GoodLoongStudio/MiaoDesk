#pragma once

// 搜索排序的纯逻辑核心。
//
// 为什么把它从 AppSearch.cpp 里提出来:排序规则此前只存在于那个文件的匿名命名空间里,
// 而那个文件 include windows.h 才能编(索引来自开始菜单与注册表)。结果是排序规则
// **在本机一行都跑不到** —— 而它恰好是用户感知最强、也最容易在改注册表枚举时被顺手
// 改坏的那一段。SEARCH-02 要的"固定样本 Top-3 有可复现基线",在规则不可测的情况下
// 只能靠人工比对,那不算基线。
//
// 拆出来之后,索引仍然只能在 Windows 上建(开始菜单、注册表、App Paths),而**排序**在
// 任何机器上都能真跑。这一分工要说清楚,免得有人以为有了它就不需要真机样本。
//
// 它不含 Windows 头,于是这些规则在本机就能真验。
#include <cstddef>
#include <string>
#include <vector>

#include "miaodesk/SearchTypes.h"

namespace miaodesk {

// 一个可排序的候选项。名字、关键字与目标分开:同一条查询可能命中名字也可能命中关键字,
// 而两者的权重不同(关键字命中是次强证据,不是同等证据)。
struct SearchRankEntry {
    std::wstring name;
    std::wstring keywords;
    std::wstring target;
    std::wstring subtitle;
};

// 单个文本字段的匹配分。0 表示不匹配。
//
// 分档是刻意的,因为用户对"我要的那个"的期待就落在这几档上:
//   完全相同 > 前缀 > 中间子串 > 逐字符间隙匹配
// 具体分数是历史值,不要为了"看起来整齐"重排 —— 重排会改变每个用户的 Top-3。
double ScoreSearchText(std::wstring_view haystack, std::wstring_view needle) noexcept;

// 排序。与 AppSearch::Query 用的是同一条规则:取名字分与关键字分(0.85 折扣)的较大者,
// 分数相同按标题字典序 —— 字典序是稳定性的来源,否则同分应用每次顺序都可能不同,
// 而用户看到的是"搜索框自己会换位置"。
std::vector<SearchResult> RankSearchEntries(const std::vector<SearchRankEntry>& entries,
                                            std::wstring_view query, std::size_t maxResults);

} // namespace miaodesk
