#include "miaodesk/SearchDedupPolicy.h"

#include <algorithm>
#include <cwctype>

namespace miaodesk {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

} // namespace

std::wstring SearchDedupKey(const SearchDedupEntry& entry) noexcept {
    // 与 AppSearch::BuildIndex 逐字相同:`Lower(name + L"|" + target)`。
    return Lower(entry.name + L"|" + entry.target);
}

bool IsFirstSearchEntry(const SearchDedupEntry& entry,
                        const std::vector<std::wstring>& seenKeys) noexcept {
    // 空 name + 空 target:脏数据,不参与去重。
    //
    // 上一版这里没有这个早退,于是"索引里有 3 条空记录"会被拼成同一个空键、
    // 只留下一条。用户看见的是"开始菜单里明明有三个坏快捷方式,索引里只有一个"
    // —— 而那个数字本来是排查问题的线索。
    if (entry.name.empty() && entry.target.empty()) return false;

    // 超长 name 不去重:注册表可以写出任意长的值,而 `Lower(name + "|" + target)`
    // 会为它真分配一块。这里不是优化,是**不信任外部输入的长度**。
    if (entry.name.size() > kSearchDedupMaxNameLength) return false;

    const std::wstring key = SearchDedupKey(entry);
    return std::find(seenKeys.begin(), seenKeys.end(), key) == seenKeys.end();
}

std::vector<SearchDedupEntry> DedupSearchEntries(const std::vector<SearchDedupEntry>& entries) {
    std::vector<SearchDedupEntry> result;
    std::vector<std::wstring> seen;
    seen.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!IsFirstSearchEntry(entry, seen)) continue;
        seen.push_back(SearchDedupKey(entry));
        result.push_back(entry);
    }
    return result;
}

} // namespace miaodesk
