// SEARCH-04「Goz 服务故障自动恢复」里"可诊断"那一半的回归。
//
// 三桶状态、三句话。此前是 `MergeResults` 里一条三岔 if/else,住在
// `SearchWindow.cpp`(要 `<windows.h>`),本机一行都跑不到 —— 而这三句话是用户
// 在文件搜索坏掉时**唯一**能拿到的信息。
//
// 这一轮真正逮到的缺陷在第三句的措辞上:它说"文件索引已连接，但本次查询失败",
// 而它键的那个输入 `fileSearchAvailable_` 的真实含义只是"goz.exe 客户端二进制
// 装着"。服务没起来时二进制当然还在,于是这句"已连接"把"服务没起来"说成了
// "查询出错",照着它排查的人会往完全相反的方向找。
#include "miaodesk/MiaoFileSearchNotice.h"

#include <cstdio>
#include <string>

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

// 反空洞自检:一个恒返回 None 的判定会让三句话一句都不出现(用户什么都看不到),
// 一个恒返回 QueryFailed 的判定会让正常路径也报错。先喂一个明知该报的
// (客户端不在)和一个明知不该报的(一切正常),确认判定真的会动。
bool VerdictStillMoves() {
    return DecideFileSearchNotice(false, false, false) == FileSearchNotice::NotInstalled &&
           DecideFileSearchNotice(false, true, false) == FileSearchNotice::None;
}

// 文案不变量:**不许断言输入没有建立的事实**。
// 这份文本里出现过的连接性断言,就是这一轮要拆掉的那一颗。
// 具体做法:三句说明合起来,不许出现"已连接"这个词 —— 因为这里的三个输入
// (pending / available / queryFailed)没有一个能证明索引连上了。
// available 只证明客户端二进制装着;那个二进制在、服务没起时,available 仍为真。
bool TextClaimsConnection() {
    const std::wstring all = FileSearchNoticeTitle(FileSearchNotice::InFlight) +
                             FileSearchNoticeDetail(FileSearchNotice::InFlight) +
                             FileSearchNoticeTitle(FileSearchNotice::NotInstalled) +
                             FileSearchNoticeDetail(FileSearchNotice::NotInstalled) +
                             FileSearchNoticeTitle(FileSearchNotice::QueryFailed) +
                             FileSearchNoticeDetail(FileSearchNotice::QueryFailed) +
                             FileSearchNoticeTitle(FileSearchNotice::None) +
                             FileSearchNoticeDetail(FileSearchNotice::None);
    return all.find(L"已连接") != std::wstring::npos;
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:客户端不在时报、一切正常时不报\n");
    ++g_checks;

    if (TextClaimsConnection()) {
        std::printf("\n[FAIL] 文案断言了输入没有建立的事实(出现了\"已连接\")\n");
        return 1;
    }
    std::printf("  [PASS] 文案不变量:没有任何一句说\"已连接\"\n");
    ++g_checks;

    // ---- 1. 主要失败形态:客户端不在,却说"正在搜索" ----
    // 客户端二进制都没装,`Query` 直接返回 false,于是永远不会回包。
    // 此时若 pending 抢先,用户看到的是"正在搜索文件…"然后永远等下去。
    {
        Check(DecideFileSearchNotice(true, false, false) == FileSearchNotice::NotInstalled,
              "客户端不在时,即使 pending 也为真,也报\"未连接\"(否则用户永远等一个不会来的回包)");
        Check(DecideFileSearchNotice(true, false, true) == FileSearchNotice::NotInstalled,
              "客户端不在时,queryFailed 也不该抢戏");
    }

    // ---- 2. 客户端在,查询在飞 ----
    {
        Check(DecideFileSearchNotice(true, true, false) == FileSearchNotice::InFlight,
              "客户端在、查询在飞 → 正在搜索");
        Check(DecideFileSearchNotice(true, true, true) == FileSearchNotice::InFlight,
              "在飞时 queryFailed 还没意义(它是上一轮的遗留标志也不该覆盖在飞)");
    }

    // ---- 3. 客户端在,这一次没拿到结果 ----
    {
        Check(DecideFileSearchNotice(false, true, true) == FileSearchNotice::QueryFailed,
              "客户端在、不在飞、上一次失败了 → 文件查询失败");
    }

    // ---- 4. 一切正常 ----
    {
        Check(DecideFileSearchNotice(false, true, false) == FileSearchNotice::None,
              "客户端在、不在飞、没失败 → 什么都不说");
        Check(DecideFileSearchNotice(false, false, false) == FileSearchNotice::NotInstalled,
              "客户端不在 → 未连接(即使其他两个都是 false)");
    }

    // ---- 5. 四句话各自说什么,逐句钉住 ----
    // 措辞就是这一轮修的东西:用户照它的话去排查,所以每一句都得说对下一步做什么。
    {
        Check(FileSearchNoticeTitle(FileSearchNotice::InFlight) == L"正在搜索文件…",
              "在飞的标题");
        Check(FileSearchNoticeDetail(FileSearchNotice::InFlight).find(L"按 Enter") != std::wstring::npos,
              "在飞的说明给出退路(等不及可以直接交给 AI)");
        Check(FileSearchNoticeDetail(FileSearchNotice::InFlight).find(L"仍在进行") != std::wstring::npos,
              "在飞的说明说清楚查询还没结束");

        Check(FileSearchNoticeTitle(FileSearchNotice::NotInstalled) == L"文件搜索未连接",
              "未安装的标题");
        Check(FileSearchNoticeDetail(FileSearchNotice::NotInstalled).find(L"仍可搜索应用") != std::wstring::npos,
              "未安装的说明告诉用户主功能还在(少一个功能不等于不能用)");

        Check(FileSearchNoticeTitle(FileSearchNotice::QueryFailed) == L"文件查询失败",
              "失败的标题");
        const std::wstring& failed = FileSearchNoticeDetail(FileSearchNotice::QueryFailed);
        Check(failed.find(L"没起来") != std::wstring::npos,
              "失败的说明把\"服务没起来\"列为一个可能原因 —— 这正是原先漏掉的那个");
        Check(failed.find(L"应用搜索") != std::wstring::npos,
              "失败的说明告诉用户应用搜索仍然可用");
        Check(failed.find(L"按 Enter") != std::wstring::npos, "失败的说明给出退路");

        Check(FileSearchNoticeTitle(FileSearchNotice::None).empty(), "None 没有标题");
        Check(FileSearchNoticeDetail(FileSearchNotice::None).empty(), "None 没有说明");
    }

    // ---- 6. None 是唯一空串的那一桶 ----
    // 界面靠"标题为空"判断要不要 push 这一行,所以 None 不能有字。
    {
        Check(FileSearchNoticeTitle(FileSearchNotice::None).empty() &&
                  FileSearchNoticeDetail(FileSearchNotice::None).empty(),
              "None 两串都空(界面靠这个判断要不要显示这一行)");
        Check(!FileSearchNoticeTitle(FileSearchNotice::InFlight).empty() &&
                  !FileSearchNoticeTitle(FileSearchNotice::NotInstalled).empty() &&
                  !FileSearchNoticeTitle(FileSearchNotice::QueryFailed).empty(),
              "另外三桶都有标题");
    }

    // ---- 7. 标题/说明取自同一个 notice,不会串桶 ----
    // 三桶的标题各不相同,否则用户分不清是哪种情况 —— 而"分不清"正是可诊断的反面。
    {
        Check(FileSearchNoticeTitle(FileSearchNotice::InFlight) !=
                  FileSearchNoticeTitle(FileSearchNotice::NotInstalled) &&
              FileSearchNoticeTitle(FileSearchNotice::NotInstalled) !=
                  FileSearchNoticeTitle(FileSearchNotice::QueryFailed) &&
              FileSearchNoticeTitle(FileSearchNotice::InFlight) !=
                  FileSearchNoticeTitle(FileSearchNotice::QueryFailed),
              "三桶标题两两不同(用户分得清是哪种情况)");
        Check(FileSearchNoticeDetail(FileSearchNotice::InFlight) !=
                  FileSearchNoticeDetail(FileSearchNotice::QueryFailed),
              "在飞与失败的说明不同(一个是等,一个是已经失败)");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n文件搜索状态诊断:全部 %d 项通过\n", g_checks);
    return 0;
}
