#pragma once

// P0-09「旧配置升级不丢 API profile」的读取边界。
//
// `api-profiles.ini` 里每一项都要过这两个 Win32 调用：
//
//     GetPrivateProfileStringW(...)      // 值
//     GetPrivateProfileSectionNamesW(...) // 段名清单
//
// 两者在**缓冲区放不下时不报错**：它们在缓冲区末尾写一个截断的字符串，然后返回
// `nSize - 2`。调用方不看返回值就永远不知道少了东西 —— 而上层拿到的是一段
// **看起来完全正常**的文本。三种用户可见的后果：
//
//   · baseUrl 被截断 → 请求打到另一台主机，而 **Key 也跟着去了**。
//     这不是"少几个字符"，是把凭据发到错误的端点；
//   · 段名清单被截断 → 整个 profile 从列表里消失。用户在下拉里看不到它，
//     而文件里它明明还在；
//   · model / name 被截断 → 模型名不对，请求直接被服务端拒。
//
// 原来这两处的返回值都被丢掉了（`ReadIni` 与 `ProfileSections` 都是）。这里是那条
// 判据的纯逻辑版本，本机可测；Win32 那一侧只负责把 `copied` 实数喂进来。
#include <cstddef>

namespace miaodesk::ini_read {

// 一次读有没有被截断。
//
// 从"要多少位子"算，不背 MSDN 的具体返回措辞：一个 `L` 字符的值需要 `L+1` 个位子
// （L 个字符 + 结尾 null）。所以 `B` 个位子最多容下 `B-1` 个字符 ——
// **`copied >= B-1` 就已经顶到天了**，此时要么正好写满、要么还有更多没写进来。
//
// 这两种情况分不分得清？Win32 在截断时返回 `nSize-2` 而写满时返回 `nSize-1`，
// 看起来能分清。但两个函数（GetPrivateProfileStringW 与
// GetPrivateProfileSectionNamesW）的文档对 `-2` 的措辞并不一致，而**结论只有一边
// 是安全的**：漏报一次截断，baseUrl 就会被截掉一截、请求带着 Key 打到另一台主机。
// 所以这里取保守的一边 —— `copied + 1 >= B` 一律算可疑。
//
// 代价是"正好写满"会误报一次。误报只是一句提醒，漏报是把凭据送到错误的端点。
inline bool ReadTruncated(std::size_t copied, std::size_t bufferChars) noexcept {
    // 没有 bufferChars == 0 的特例:那一档 `copied + 1 >= 0` 对任何 copied 恒为真,
    // 特例是**不可达的冗余分支**。这里曾经写过一条,被变异检测逮到
    // (把 `if (bufferChars == 0) return true;` 整条删掉之后测试依然全绿) ——
    // 与本会话早前删掉的 OverlappingTurns 同一个毛病:一个永远为真的守卫
    // 与没有它长得一模一样,留着只会让人以为这里有过一个需要特殊处理的情形。
    return copied + 1 >= bufferChars;
}

// 给调用方的一句话：读出来的东西少了一截，不要当完整的用。
//
// 之所以要单独一句而不是只返回 bool：截断必须让**用户**知道，不能只让开发者知道。
// 一个被截断的 profile 在下拉里看起来和正常 profile 一模一样 —— 那是这类缺陷
// 最坏的部分。
const char* ExplainTruncatedRead() noexcept;

} // namespace miaodesk::ini_read
