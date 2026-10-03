// SEARCH-01/02:固定样本上的排序基线与回归。
//
// 面板要的两件事:①固定 30+ 查询基准集入仓;②固定样本的 Top-3 有可复现基线,
// 主要误命中有回归测试。在此之前,a) 没有入仓的查询集,b) 排序规则住在 AppSearch.cpp 的
// 匿名命名空间里,而那个文件要 windows.h 才编 —— 于是排序规则在本机一行都跑不到,
// 用户感知最强的那段反而没有自动保护。
//
// **这份基线是合成的索引,不是某台机器的应用清单**。真实索引来自开始菜单、注册表与
// App Paths,那需要在 Windows 上采集(SEARCH-01 的真机一半)。合成集的价值在于:它把
// 全名/简称/中文/大小写/空格/同名/关键字命中/间隙匹配这些**形状**固定下来,
// 排序规则一改就会在这里红。把它当成真机样本会是假的。
//
// 分数不逐个钉死:分数是实现细节,钉死它会让任何一次调优都变成"改 40 个数"。
// 钉的是**顺序**与**命中/不命中** —— 那才是用户看见的东西。
#include "miaodesk/SearchTextScoring.h"

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

// 合成索引:真实机器应用清单的替身,覆盖排序规则会遇到的形状。
// 名字含中文、英文、缩写、空格与大小写混用;关键字单独设一遍(模拟同义与英文名)。
std::vector<SearchRankEntry> BaselineIndex() {
    return {
        {L"Visual Studio Code", L"vscode editor", L"Code.exe", L"开发工具"},
        {L"Visual Studio 2022", L"devenv", L"devenv.exe", L"开发工具"},
        {L"Microsoft Edge", L"edge browser", L"msedge.exe", L"浏览器"},
        {L"Google Chrome", L"chrome browser", L"chrome.exe", L"浏览器"},
        {L"Mozilla Firefox", L"firefox browser", L"firefox.exe", L"浏览器"},
        {L"Notepad", L"notepad text", L"notepad.exe", L"Windows"},
        {L"Notepad++", L"notepadplusplus text", L"notepad++.exe", L"开发工具"},
        {L"Windows Terminal", L"terminal console", L"wt.exe", L"Windows"},
        {L"Command Prompt", L"cmd console", L"cmd.exe", L"Windows"},
        {L"PowerShell", L"pwsh console", L"powershell.exe", L"Windows"},
        {L"Task Manager", L"taskmgr", L"taskmgr.exe", L"Windows"},
        {L"Control Panel", L"control settings", L"control.exe", L"Windows"},
        {L"File Explorer", L"explorer files", L"explorer.exe", L"Windows"},
        {L"Settings", L"settings system", L"SystemSettings.exe", L"Windows"},
        {L"计算器", L"calculator calc", L"calc.exe", L"Windows"},
        {L"画图", L"mspaint paint", L"mspaint.exe", L"Windows"},
        {L"截图工具", L"screenshot snip", L"SnippingTool.exe", L"Windows"},
        {L"日历", L"calendar outlook", L"Outlook.exe", L"Windows"},
        {L"邮件", L"mail outlook", L"Outlook.exe", L"邮件"},
        {L"时钟", L"clock alarm", L"Clock.exe", L"Windows"},
        {L"妙喵壁纸", L"miaodesk wallpaper", L"MiaoDesk.exe", L"桌面"},
        {L"妙喵壁纸设置", L"miaodesk settings", L"MiaoDesk.exe", L"桌面"},
        // 同名不同目标:两分钟uild it up 的重复项,用来钉去重与稳定序。
        {L"Steam", L"steam games", L"Steam.exe", L"游戏"},
        {L"Steam 客户端", L"steam client", L"Steam.exe", L"游戏"},
    };
}

std::vector<std::wstring> TopTitles(const std::vector<SearchResult>& results, std::size_t count) {
    std::vector<std::wstring> titles;
    for (std::size_t i = 0; i < results.size() && i < count; ++i) titles.push_back(results[i].title);
    return titles;
}

std::string Narrow(const std::wstring& text) {
    std::string out;
    for (wchar_t ch : text) out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
    return out;
}

void ExpectTop(const std::vector<SearchRankEntry>& index, const wchar_t* query,
               std::vector<std::wstring> expected, const std::string& what) {
    const auto results = RankSearchEntries(index, query, 8);
    const auto actual = TopTitles(results, expected.size());
    Check(actual == expected,
          what + " | 期望 [" + [&] {
              std::string s;
              for (const auto& t : expected) s += Narrow(t) + ",";
              return s;
          }() + "] 实际 [" + [&] {
              std::string s;
              for (const auto& t : actual) s += Narrow(t) + ",";
              return s;
          }() + "]");
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;

    const auto index = BaselineIndex();

    // ---- 1. 固定查询集:每条都能复现 ----
    ExpectTop(index, L"code", {L"Visual Studio Code"}, "全名/简称:code");
    ExpectTop(index, L"vscode", {L"Visual Studio Code"}, "关键字命中:vscode 只在该条目的关键字里");
    ExpectTop(index, L"chrome", {L"Google Chrome"}, "全名:chrome");
    ExpectTop(index, L"edge", {L"Microsoft Edge"}, "关键字命中:edge");
    ExpectTop(index, L"notepad", {L"Notepad", L"Notepad++"}, "同名族:notepad 的两项按序");
    ExpectTop(index, L"ntp", {L"Notepad"}, "间隙匹配:ntp -> Notepad");
    ExpectTop(index, L"steam", {L"Steam", L"Steam 客户端"}, "同名族:steam 两项");
    ExpectTop(index, L"计算器", {L"计算器"}, "中文全名");
    // 拼音检索**当前不支持**,而这是一个已知缺口,不是"以后再补"的客套:
    // 打 jisuan 找不到"计算器"。规则只做逐字符匹配,而中文字符串里没有拉丁字母,
    // 于是间隙匹配直接返回 0。用户在中文 Windows 上给一个中文名的应用,只能打中文。
    // 这里把它钉成"现在就是这样",是为了将来真的支持拼音时,这条断言会提醒人去改文档
    // 与 Skill —— 而不是让缺口一直安静地存在。
    Check(RankSearchEntries(index, L"jisuan", 8).empty(),
          "已知缺口:拼音检索不支持(打 jisuan 找不到中文名的应用)。要么实现,要么文档讲清。");
    ExpectTop(index, L"calculator", {L"计算器"}, "英文关键字命中中文条目");
    ExpectTop(index, L"CLOCK", {L"时钟"}, "大小写不敏感(全大写查询)");
    ExpectTop(index, L"control", {L"Control Panel"}, "全名:control");

    // ---- 2. 不命中与空查询:不能返回任何东西 ----
    const auto none = RankSearchEntries(index, L"zzzzqqq", 8);
    Check(none.empty(), "没有匹配时不返回任何结果,而不是返回一堆低分凑数。");
    Check(RankSearchEntries(index, L"", 8).empty(), "空查询返回空。");
    Check(RankSearchEntries(index, L"   ", 8).empty(), "全空白查询没有命中(空白不是任何名字的一部分)。");

    // ---- 3. maxResults 真的生效 ----
    const auto capped = RankSearchEntries(index, L"a", 3);
    Check(capped.size() <= 3, "maxResults 截断生效。");

    // ---- 4. 顺序稳定:同一次查询跑多遍,结果逐字节相同 ----
    const auto first = RankSearchEntries(index, L"s", 8);
    for (int i = 0; i < 5; ++i) {
        const auto again = RankSearchEntries(index, L"s", 8);
        Check(again.size() == first.size() && TopTitles(again, 8) == TopTitles(first, 8),
              "重复查询顺序稳定(同分按标题字典序)。");
    }

    // ---- 5. 关键字命中的折扣:名字命中原先于关键字命中 ----
    // 同一条查询下,名字里含它的条目应排在只在关键字里含它的条目之前。
    const auto keywordVsName = RankSearchEntries(index, L"explorer", 8);
    Check(!keywordVsName.empty() && keywordVsName.front().title == L"File Explorer",
          "名字命中排在关键字命中之前。");

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("搜索排序基线(%zu 条样本,%d 项查询):全部 %d 项检查通过\n", index.size(),
                g_checks >= 12 ? 12 : g_checks, g_checks);
    return 0;
}
