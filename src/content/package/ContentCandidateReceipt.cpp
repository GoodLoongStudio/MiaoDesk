#include "miaodesk/ContentCandidateReceipt.h"

#include <algorithm>
#include <set>

namespace miaodesk::creator {
namespace {

// 从 "key=value" 的一行里取一个字段。值到下一个空格为止 ——
// 摘要、candidateId、路径里都不含空格,所以这个分隔是安全的。
std::string Field(std::string_view line, std::string_view key) {
    const std::string needle = std::string(key) + "=";
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t hit = line.find(needle, at);
        if (hit == std::string_view::npos) return {};
        // 必须是词首:前面是空格或行首。不然 "digest=" 会命中 "staledigest="。
        if (hit == 0 || line[hit - 1] == ' ') {
            const std::size_t from = hit + needle.size();
            const std::size_t end = line.find(' ', from);
            return std::string(line.substr(from, end == std::string_view::npos
                                                     ? std::string_view::npos
                                                     : end - from));
        }
        at = hit + 1;
    }
    return {};
}

bool AllDigits(std::string_view text) {
    if (text.empty()) return false;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}

bool LooksLikeDigest(std::string_view digest) {
    if (digest.size() != 64) return false;
    for (const char ch : digest) {
        const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        if (!hex) return false;
    }
    return true;
}

} // namespace

ParsedCandidateReceipt ParseCandidateReceipt(std::string_view text) {
    ParsedCandidateReceipt parsed;

    // 找回执行的起点:它必须顶行(前面只有换行),而不是出现在一句话中间。
    // 模型完全可以在散文里写 "[receipt] accepted=1 ...",所以找到之后还要核验;
    // 但至少它不能藏在半句话里还被我们当成结构化数据。
    std::size_t at = text.find(kReceiptLinePrefix);
    if (at == std::string_view::npos) return parsed;
    if (at != 0 && text[at - 1] != '\n') {
        // 往前找到这一行真正开始的地方。
        const std::size_t lineStart = text.find_last_of('\n', at);
        if (lineStart == std::string_view::npos) return parsed;
    }
    parsed.present = true;

    std::size_t end = text.find('\n', at);
    const std::string_view line =
        text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);

    const std::string acceptedText = Field(line, "accepted");
    const std::string revisionText = Field(line, "revision");
    parsed.candidateId = Field(line, "candidateId");
    parsed.digest = Field(line, "digest");
    parsed.snapshotPath = Field(line, "snapshot");
    parsed.sessionId = Field(line, "sessionId");
    const std::string epochText = Field(line, "epoch");

    // 字段齐不齐是一个独立的判断:缺 candidateId 与缺 digest 是两回事,
    // 而上层要能对着模型说清缺的是哪个。
    std::vector<std::string> missing;
    if (acceptedText.empty()) missing.emplace_back("accepted");
    if (revisionText.empty()) missing.emplace_back("revision");
    if (parsed.candidateId.empty()) missing.emplace_back("candidateId");
    if (parsed.digest.empty()) missing.emplace_back("digest");
    if (parsed.sessionId.empty()) missing.emplace_back("sessionId");
    if (epochText.empty()) missing.emplace_back("epoch");
    if (!missing.empty()) {
        std::string reason = "回执行缺少字段:";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i) reason += ",";
            reason += missing[i];
        }
        parsed.malformedReason = reason;
        return parsed;
    }
    if (acceptedText != "0" && acceptedText != "1") {
        parsed.malformedReason = "回执行的 accepted 只能是 0 或 1。";
        return parsed;
    }
    if (!AllDigits(revisionText) || !AllDigits(epochText)) {
        parsed.malformedReason = "回执行的 revision / epoch 必须是十进制数字。";
        return parsed;
    }
    if (!LooksLikeDigest(parsed.digest)) {
        // 摘要必须是 64 位小写十六进制。一个"看起来像摘要"的字符串
        // 通不过台账核对,而那时候的错误信息会是"对不上"而不是"这不是摘要" ——
        // 后者对排查有用得多。
        parsed.malformedReason = "回执行的 digest 不是 64 位小写十六进制摘要。";
        return parsed;
    }

    parsed.accepted = acceptedText == "1";
    parsed.revision = static_cast<std::uint32_t>(std::strtoul(revisionText.c_str(), nullptr, 10));
    parsed.epoch = std::strtoull(epochText.c_str(), nullptr, 10);
    parsed.wellFormed = true;
    return parsed;
}

const char* ToString(ReceiptVerdict verdict) noexcept {
    switch (verdict) {
    case ReceiptVerdict::Trusted: return "Trusted";
    case ReceiptVerdict::NotPresent: return "NotPresent";
    case ReceiptVerdict::Malformed: return "Malformed";
    case ReceiptVerdict::WrongSession: return "WrongSession";
    case ReceiptVerdict::StaleEpoch: return "StaleEpoch";
    case ReceiptVerdict::UnknownCandidate: return "UnknownCandidate";
    case ReceiptVerdict::RevisionMismatch: return "RevisionMismatch";
    case ReceiptVerdict::NotAccepted: return "NotAccepted";
    }
    return "Unknown";
}

ReceiptVerification VerifyCandidateReceipt(std::string_view text,
                                           const content::ContentCandidateLedger& ledger,
                                           std::string_view sessionId, std::uint64_t epoch) {
    ReceiptVerification verification;
    const auto parsed = ParseCandidateReceipt(text);
    if (!parsed.present) {
        verification.verdict = ReceiptVerdict::NotPresent;
        verification.reason = "这段文本里没有回执行。";
        return verification;
    }
    if (!parsed.wellFormed) {
        verification.verdict = ReceiptVerdict::Malformed;
        verification.reason = parsed.malformedReason;
        return verification;
    }
    // 归属先判。回执说自己属于哪个作品,与宿主持有的那次创作对不上就拒绝 ——
    // 这一条排在所有内容核对之前,因为它回答的是"这行字有没有权利描述这个作品"。
    if (parsed.sessionId != sessionId) {
        verification.verdict = ReceiptVerdict::WrongSession;
        verification.reason = "回执的会话 ID 与当前作品不符。";
        return verification;
    }
    if (parsed.epoch != epoch) {
        verification.verdict = ReceiptVerdict::StaleEpoch;
        verification.reason = "回执来自另一轮:它可能是取消或切换作品之前留下的。";
        return verification;
    }
    if (!parsed.accepted) {
        verification.verdict = ReceiptVerdict::NotAccepted;
        verification.reason = "这个候选没有被接受。";
        return verification;
    }

    // 失效判断必须排在 Sealed() 之前。
    //
    // ContentCandidateLedger::Sealed() 的判据是 receipt.accepted,而 Invalidate()
    // 会把 accepted 置回 false —— 于是"封存过但已被标记失效"和"从来没封存过"
    // 在 Sealed() 看来是同一件事。先查失效,后者才报 UnknownCandidate。
    // 第一版顺序是反的,于是一个被宿主主动失效的候选拿到的是"台账里没有这个摘要",
    // 而它明明在台账里 —— 那条错误信息会把排查引向完全错误的方向。
    const auto invalidated = std::find(ledger.Invalidated().begin(), ledger.Invalidated().end(),
                                       parsed.digest);
    if (invalidated != ledger.Invalidated().end()) {
        verification.verdict = ReceiptVerdict::NotAccepted;
        verification.reason = "这个候选已被宿主标记失效,不能再当成有效候选。";
        return verification;
    }

    // 与宿主的台账对账。摘要与 candidateId 都必须在台账里,且 revision 一致 ——
    // 三项里任何一项对不上,这段文本都不能驱动"有一个可用候选"这个结论。
    const auto revision = ledger.RevisionOf(parsed.digest);
    if (revision == 0 || !ledger.Sealed(parsed.digest)) {
        verification.verdict = ReceiptVerdict::UnknownCandidate;
        verification.reason = "宿主的候选台账里没有这个摘要 —— 它不是这一轮封存出来的。";
        return verification;
    }
    if (revision != parsed.revision) {
        verification.verdict = ReceiptVerdict::RevisionMismatch;
        verification.reason = "回执的 revision 与宿主记的不一致。";
        return verification;
    }
    bool candidateIdMatches = false;
    for (const auto& receipt : ledger.Receipts()) {
        if (receipt.digest == parsed.digest && receipt.candidateId == parsed.candidateId) {
            candidateIdMatches = true;
        }
    }
    if (!candidateIdMatches) {
        verification.verdict = ReceiptVerdict::UnknownCandidate;
        verification.reason = "回执的 candidateId 与宿主记的不是同一个。";
        return verification;
    }
    verification.verdict = ReceiptVerdict::Trusted;
    verification.trusted = ledger.LastValid();
    return verification;
}

std::string FormatCandidateReceiptLine(const content::ContentCandidateReceipt& receipt,
                                       std::string_view sessionId, std::uint64_t epoch) {
    std::string line = kReceiptLinePrefix;
    line += " accepted=";
    line += receipt.accepted ? "1" : "0";
    line += " revision=";
    line += std::to_string(receipt.revision);
    line += " candidateId=";
    line += receipt.candidateId;
    line += " digest=";
    line += receipt.digest;
    line += " snapshot=";
    line += receipt.snapshotPath;
    line += " sessionId=";
    line += std::string(sessionId);
    line += " epoch=";
    line += std::to_string(epoch);
    return line;
}

} // namespace miaodesk::creator
