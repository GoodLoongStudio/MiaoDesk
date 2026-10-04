// SEARCH-02「排序与去重质量」的去重那一半。
//
// 这个文件钉住 `SearchDedupPolicy` 里去重那一行 `Lower(name + L"|" + target)`
// 的全部性质。这行规则此前内联在 `AppSearch::BuildIndex` 里,而那个文件 include
// `<windows.h>`(索引来自开始菜单与注册表),于是本机一行都跑不到 —— 而 SEARCH-02
// 的验收原话就是"排序与去重质量",排序上一轮已经提成 SearchTextScoring 并在本机
// 有了 22 项断言,去重这半个也提了出来,`BuildIndex` 现在调的就是这里测的函数
// (不是另抄一份 —— 抄一份就等于本机测一行、真机跑另一行)。
//
// 反空洞自检:一个恒返回 true 的 IsFirstSearchEntry 在其余断言上同样全绿,所以先喂
// 一个明知该判重的(键已在 seen 里)和一个明知该放行的(空记录),确认判定真的会动。
#include "miaodesk/SearchDedupPolicy.h"

#include <cstdio>
#include <string>
#include <vector>

namespace miaodesk {
namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL  %s\n", what.c_str());
    }
}

bool VerdictStillMoves() {
    SearchDedupEntry dup;
    dup.name = L"Chrome";
    dup.target = L"chrome.exe";
    std::vector<std::wstring> seen{SearchDedupKey(dup)};
    SearchDedupEntry junk;
    return IsFirstSearchEntry(dup, seen) == false &&      // 已在 seen 里 → 判重
           IsFirstSearchEntry(junk, {}) == false;         // 空记录 → 不放行
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:判重不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:seen 里有的判重、空记录不放行\n");
    ++g_checks;

    // ---- 1. 键的形状:与 AppSearch::BuildIndex 逐字相同 ----
    {
        SearchDedupEntry entry;
        entry.name = L"Chrome";
        entry.target = L"chrome.exe";
        Check(SearchDedupKey(entry) == L"chrome|chrome.exe",
              "键 = Lower(name + L\"|\" + target)");
        Check(SearchDedupKey(entry) == SearchDedupKey(entry), "同一条记录键稳定(可复现)");

        // 大小写:大小写不同的 name/target 是同一个应用 —— Windows 上
        // CHROME.EXE 与 chrome.exe 是同一个文件,不去重就会在结果里出现两个 Chrome。
        SearchDedupEntry upper;
        upper.name = L"CHROME";
        upper.target = L"CHROME.EXE";
        Check(SearchDedupKey(upper) == SearchDedupKey(entry),
              "大小写不同的 name/target 撞同一个键(Windows 上它们是同一个程序)");

        // 分隔符:空 name 也要能拼出唯一键(而不是让所有空 name 挤在一起)。
        SearchDedupEntry emptyName;
        emptyName.target = L"a.exe";
        SearchDedupEntry otherEmptyName;
        otherEmptyName.target = L"b.exe";
        Check(SearchDedupKey(emptyName) != SearchDedupKey(otherEmptyName),
              "name 为空时 target 仍能把两条记录区分开");
    }

    // ---- 2. 主要失败形态一:同一条应用不去重 ----
    {
        std::vector<SearchDedupEntry> index{
            {L"Chrome", L"chrome.exe"},
            {L"Chrome", L"chrome.exe"},   // 开始菜单 + 注册表给两条
            {L"Chrome", L"chrome.exe"},
        };
        const auto deduped = DedupSearchEntries(index);
        Check(deduped.size() == 1, "三条全同的记录去重后只剩一条(用户不该看见三个 Chrome)");
    }

    // ---- 3. 主要失败形态二:两个不同的应用被并成一个 ----
    {
        // name 里带 `|` 的两条不同记录,拼出的键与别的组合相同。
        SearchDedupEntry weird;
        weird.name = L"A|B";
        weird.target = L"C";
        SearchDedupEntry normal;
        normal.name = L"A";
        normal.target = L"B|C";
        Check(SearchDedupKey(weird) == SearchDedupKey(normal),
              "`|` 确实能撞键 —— 这不是假设,ini 注册表项和带管道的目标都会带它");
        const auto deduped = DedupSearchEntries({weird, normal});
        Check(deduped.size() == 1,
              "撞键时保留先出现的那条,另一条被并掉 —— 这是当前规则**已知的代价**,"
              "钉住它以免将来被误以为是 bug 而修成另一套规则");
        Check(deduped.front().name == L"A|B", "保留的是先出现的那条(顺序稳定)");
    }

    // ---- 4. 空记录不参与去重 ----
    {
        SearchDedupEntry junk;
        Check(!IsFirstSearchEntry(junk, {}), "空 name + 空 target 不放行(脏数据)");
        std::vector<SearchDedupEntry> index{{}, {}, {}, {L"Chrome", L"chrome.exe"}, {}};
        const auto deduped = DedupSearchEntries(index);
        Check(deduped.size() == 1, "三条空记录全部被挡掉,只留下真记录");
        Check(deduped.front().name == L"Chrome", "留下的是真记录");
    }

    // ---- 5. 顺序就是基线的一部分 ----
    {
        std::vector<SearchDedupEntry> index{
            {L"Zeta", L"z.exe"},
            {L"Alpha", L"a.exe"},
            {L"Zeta", L"z.exe"},
            {L"Mid", L"m.exe"},
        };
        const auto deduped = DedupSearchEntries(index);
        Check(deduped.size() == 3, "三条不同记录都留下");
        Check(deduped.size() == 3 && deduped[0].name == L"Zeta" && deduped[1].name == L"Alpha" &&
                  deduped[2].name == L"Mid",
              "保留首次出现的顺序(重排会让同分应用每次顺序不同,重回'搜索框自己会换位置')");
    }

    // ---- 6. seen 判重用线性查找,不是哈希 ----
    // 这一条是钉实现选择:索引规模是几百条,线性查找足够且不需要 <unordered_set>
    // (AppSearch.cpp 为此多引了一个头)。换成别的容器不该改变行为。
    {
        SearchDedupEntry entry;
        entry.name = L"Notepad";
        entry.target = L"notepad.exe";
        const std::vector<std::wstring> seen{L"x|y", L"z|w", SearchDedupKey(entry)};
        Check(!IsFirstSearchEntry(entry, seen), "键在 seen 中间也能判重(线性查找确实扫全表)");
        SearchDedupEntry other;
        other.name = L"Calc";
        other.target = L"calc.exe";
        Check(IsFirstSearchEntry(other, seen), "键不在 seen 里就放行");
    }

    // ---- 7. 超长 name 不去重(不信外部输入长度)----
    {
        SearchDedupEntry huge;
        huge.name = std::wstring(kSearchDedupMaxNameLength + 1, L'x');
        huge.target = L"x.exe";
        Check(!IsFirstSearchEntry(huge, {}), "超长 name 不放行(注册表可以写出任意长的值)");
        SearchDedupEntry atLimit;
        atLimit.name = std::wstring(kSearchDedupMaxNameLength, L'x');
        atLimit.target = L"x.exe";
        Check(IsFirstSearchEntry(atLimit, {}), "正好到长度上限的 name 仍放行(上限本身是包含的)");

        // 两条完全相同的超长记录:都不放行,于是都被挡掉,不会并成一条。
        std::vector<SearchDedupEntry> index{huge, huge};
        Check(DedupSearchEntries(index).empty(), "超长记录不去重(也不被并成一条)");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n搜索去重:全部 %d 项通过\n", g_checks);
    return 0;
}
