#pragma once

// CCA-05:结构化候选回执的解析与核验(纯逻辑)。
//
// 这一层要替掉的是 `ContentCreatorDialog` 里的**两条猜测**:它从模型的回复正文里
// 正则扫一个 `.mdwall` / `.mdwidget` 路径,再扫一个"看起来像内容包目录"的候选。
// 猜中的代价不是难看,是**不可判定** —— 用户在正文里提到任何一个路径都会被当成
// 这次生成的产物,于是"它到底做出来了没有"取决于模型怎么说话。
//
// 计划 §4.1 的原话:模型提交的路径、ID、成功声明全部按输入处理,不能成为事实来源。
// 所以这里做两件事:
//   1. 解析回执行(工具真的返回的一行结构化文本),拿到结构化字段;
//   2. **核验**它 —— 与宿主自己的台账对账,确认这个回执对应的是宿主持有的那次封存,
//      而不是某段恰好长得像回执的散文。
//
// 它不碰盘、不 import Windows 头,于是"散文伪装成回执"这一类能在任何机器上被验。
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/ContentCandidateLedger.h"

namespace miaodesk::creator {

// 回执行的前缀。它与正文之间用一个不会自然出现的分隔:模型要在散文里写出
// 这一整行并且每项都对上,比它真的调一次工具更难 —— 而这正是要点。
inline constexpr const char* kReceiptLinePrefix = "[receipt]";

// 一次候选提交的结构化结论。字段与 ContentCandidateReceipt 对应,
// 但它是**从文本解析出来的**,所以必须被核验之后才能当事实用。
struct ParsedCandidateReceipt {
    bool present{false};          // 有没有找到回执行
    bool wellFormed{false};       // 字段齐不齐
    bool accepted{false};
    std::uint32_t revision{};
    std::string candidateId;
    std::string digest;
    std::string snapshotPath;
    std::string sessionId;
    std::uint64_t epoch{};
    std::string malformedReason;
};

// 从一段工具结果文本里取出回执行。找不到返回 present=false。
// 一段文本里有多行时取第一行 —— 后面的都不算,宿主只认它自己发出的那一行。
ParsedCandidateReceipt ParseCandidateReceipt(std::string_view text);

// 宿主侧的核验结论。
enum class ReceiptVerdict {
    Trusted,          // 与宿主台账对得上
    NotPresent,       // 这段文本里没有回执
    Malformed,        // 有回执行但字段不齐
    WrongSession,     // 回执属于别的作品
    StaleEpoch,       // 来自已取消或已切换的那一轮
    UnknownCandidate, // 宿主的台账里没有这个 candidateId / digest
    RevisionMismatch, // revision 与宿主记的不一致
    NotAccepted,      // 回执说自己没被接受
};

const char* ToString(ReceiptVerdict verdict) noexcept;

struct ReceiptVerification {
    ReceiptVerdict verdict{ReceiptVerdict::NotPresent};
    std::string reason;
    // Trusted 时,宿主台账里那条回执。上层据此决定能不能把候选显示给用户。
    const content::ContentCandidateReceipt* trusted{nullptr};
};

// 拿宿主自己的台账核验一段文本里的回执。sessionId / epoch 由宿主提供,
// 不从文本里取 —— 文本里那两个字段只用于发现"对不上"。
ReceiptVerification VerifyCandidateReceipt(std::string_view text,
                                           const content::ContentCandidateLedger& ledger,
                                           std::string_view sessionId, std::uint64_t epoch);

// 宿主要写进工具结果的那一行。字段全部来自宿主自己的台账,不是模型给的。
std::string FormatCandidateReceiptLine(const content::ContentCandidateReceipt& receipt,
                                       std::string_view sessionId, std::uint64_t epoch);

} // namespace miaodesk::creator
