#pragma once

// SEARCH-04「Goz 服务故障自动恢复」里"可诊断"的那一半。
//
// 搜索框有三桶文件搜索状态,而用户只能看见其中一桶的措辞。此前的实现是
// `MergeResults` 里一条三岔 `if/else if/else if`,每次推一条 `ResultKind::Status`
// 进去。它住在 `SearchWindow.cpp`(那个文件要 `<windows.h>`),于是本机一行都跑不到
// —— 而这三句话是用户在文件搜索坏掉时**唯一**能得到的信息。
//
// 为什么值得单独提出来:第三桶的措辞断言了一个输入并没有建立的事实。
// `fileSearchAvailable_` 的真实含义是"goz.exe 客户端二进制装着"
// (`GozSearch::Available()` 就是 `!FindClientBinary().empty()`),**不是**
// "索引已连接"。服务没起来、命名管道等不到时,客户端二进制当然还在,于是用户读到的是
//
//     "文件索引已连接，但本次查询失败"
//
// —— 这句话把一个"服务没起来"说成了"查询出错"。照着它排查的人会去看查询参数、
// 去看索引内容,而真正该做的是确认 gozd 这个 Windows 服务跑没跑。
// 一句错的诊断比没有诊断更坏:它把排查引向相反的方向。
//
// 这里只放判定与文案,不碰管道、不碰窗口、不碰服务。
#include <string>

namespace miaodesk {

// 三类文件搜索状态。顺序就是严重程度:第一条能成立就别看后面的。
enum class FileSearchNotice {
    None,        // 没有要说的(正常路径)
    InFlight,    // 查询还在飞
    NotInstalled,  // 客户端二进制不在
    QueryFailed,  // 客户端在,但这一次没拿到结果
};

// 判定该显示哪一桶。
//
// pending 只在 available 为真时才有意义:客户端都不在,谈不上"正在搜索"。
// 这个先后顺序是不变式的一部分 —— 反了会让没装客户端的机器显示"正在搜索文件…"
// 然后永远等一个不会到来的回包。
FileSearchNotice DecideFileSearchNotice(bool pending, bool available, bool queryFailed) noexcept;

// 这一桶的标题。
const std::wstring& FileSearchNoticeTitle(FileSearchNotice notice) noexcept;

// 这一桶的说明。
//
// 文案受一条不变式约束:**不许断言输入没有建立的事实**。"已连接"只在
// 真的拿到过一次成功回包时才说,而那不是这里的任何一个输入能证明的 ——
// 这里的输入只证明"客户端二进制在"。所以 QueryFailed 说的是这次没拿到结果,
// 以及下一步按 Enter 可以交给妙喵 AI,不提连接状态。
const std::wstring& FileSearchNoticeDetail(FileSearchNotice notice) noexcept;

} // namespace miaodesk
