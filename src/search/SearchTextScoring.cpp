#include "miaodesk/SearchTextScoring.h"

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

double ScoreSearchText(std::wstring_view haystackRaw, std::wstring_view needleRaw) noexcept {
    const auto haystack = Lower(std::wstring(haystackRaw));
    const auto needle = Lower(std::wstring(needleRaw));
    if (needle.empty()) return 0.0;
    if (haystack == needle) return 1000.0;
    if (haystack.starts_with(needle))
        return 850.0 - static_cast<double>(haystack.size() - needle.size());
    if (const auto pos = haystack.find(needle); pos != std::wstring::npos)
        return 650.0 - static_cast<double>(pos) * 2.0;

    // 逐字符间隙匹配:打"ntp"要能找到 Notepad。分数随间隙总和下降 ——
    // 间隙越大说明这次匹配越勉强。这一段是它比"有没有匹配"更能解释结果的地方。
    std::size_t h = 0;
    std::size_t gaps = 0;
    for (wchar_t n : needle) {
        const auto found = haystack.find(n, h);
        if (found == std::wstring::npos) return 0.0;
        gaps += found - h;
        h = found + 1;
    }
    return 350.0 - static_cast<double>(gaps);
}

std::vector<SearchResult> RankSearchEntries(const std::vector<SearchRankEntry>& entries,
                                            std::wstring_view query, std::size_t maxResults) {
    std::vector<SearchResult> results;
    if (query.empty()) return results;
    for (const auto& entry : entries) {
        const double score = std::max(ScoreSearchText(entry.name, query),
                                      ScoreSearchText(entry.keywords, query) * 0.85);
        if (score <= 0.0) continue;
        SearchResult result;
        result.kind = ResultKind::App;
        result.title = entry.name;
        result.subtitle = entry.subtitle.empty() ? entry.target : entry.subtitle;
        result.target = entry.target;
        result.score = score;
        results.push_back(std::move(result));
    }
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.title < b.title;
              });
    if (results.size() > maxResults) results.resize(maxResults);
    return results;
}

} // namespace miaodesk
