#include "miaodesk/CreatorReplyInterpreter.h"

#include "miaodesk/ContentCandidateReceipt.h"

#include <algorithm>

namespace miaodesk::creator {
namespace {

// 正文里一个看起来像包路径的候选:盘符开头、以该 kind 的扩展名结尾。
// 它刻意**不**去看盘 —— 存在性与 manifest 是宿主的事,这里只认出形状。
std::string FindProsePackagePath(std::string_view text, std::uint32_t kind) {
    const std::string_view extension = CreatorPackageExtension(kind);
    std::size_t search = 0;
    while (search < text.size()) {
        const std::size_t hit = text.find(extension, search);
        if (hit == std::string_view::npos) return {};
        const std::size_t end = hit + extension.size();
        // 往前找这一行的起点。
        std::size_t lineStart = text.find_last_of("\r\n", hit);
        lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
        // 再往前找盘符。
        std::size_t start = lineStart;
        for (std::size_t i = hit; i >= lineStart + 2 && i >= 2; --i) {
            const std::size_t drive = i - 2;
            const bool letter = (text[drive] >= 'a' && text[drive] <= 'z') ||
                                (text[drive] >= 'A' && text[drive] <= 'Z');
            if (letter && text[drive + 1] == ':' &&
                (text[drive + 2] == '\\' || text[drive + 2] == '/')) {
                start = drive;
                break;
            }
            if (i == 0) break;
        }
        std::string candidate(text.substr(start, end - start));
        while (!candidate.empty() && (candidate.front() == ' ' || candidate.front() == '\t')) {
            candidate.erase(candidate.begin());
        }
        if (!candidate.empty() && candidate.size() > extension.size()) return candidate;
        search = end;
    }
    return {};
}

} // namespace

std::string_view CreatorPackageExtension(std::uint32_t kind) noexcept {
    return kind == kCreatorKindWidget ? ".mdwidget" : ".mdwall";
}

const char* ToString(CreatorReplySource source) noexcept {
    switch (source) {
    case CreatorReplySource::None: return "None";
    case CreatorReplySource::Receipt: return "Receipt";
    case CreatorReplySource::ProseScan: return "ProseScan";
    }
    return "Unknown";
}

bool ProsePathIsUsable(const ProsePathFacts& facts, std::string* reason) {
    if (!facts.exists) {
        if (reason) *reason = "正文里提到的路径不存在。";
        return false;
    }
    if (!facts.isDirectory) {
        if (reason) *reason = "正文里提到的路径不是目录。";
        return false;
    }
    if (!facts.hasManifest) {
        if (reason) *reason = "正文里提到的目录里没有 manifest.json。";
        return false;
    }
    if (!facts.extensionMatchesKind) {
        if (reason) *reason = "正文里提到的路径扩展名与当前制作类型不符。";
        return false;
    }
    if (!facts.insideWorkspace) {
        if (reason) *reason = "正文里提到的路径不在本作品的工作区内。";
        return false;
    }
    return true;
}

CreatorReplyReading InterpretCreatorReply(std::string_view text, std::uint32_t kind,
                                          const std::string& workspaceRoot,
                                          const content::ContentCandidateLedger& ledger,
                                          std::string_view sessionId, std::uint64_t epoch) {
    CreatorReplyReading reading;

    // 1. 先试结构化凭据。它是唯一能让宿主不猜的那种。
    const auto verification = VerifyCandidateReceipt(text, ledger, sessionId, epoch);
    // 一段**存在但没通过**核验的回执,必须一直带到结论里。
    // 第一版只在"正文也没扫到东西"时才提它,于是一条被拒的回执,只要正文里恰好
    // 还提到了一个路径,就消失得无影无踪 —— 而那正是最该让人知道的事:
    // 有东西被拒绝过,而现在准备采信的是一个更弱的凭据。
    const std::string receiptNote =
        verification.verdict == ReceiptVerdict::NotPresent
            ? std::string{}
            : (std::string("有一段回执但没有通过核验:") +
               std::string(ToString(verification.verdict)) + " " + verification.reason + "。");

    if (verification.verdict == ReceiptVerdict::Trusted && verification.trusted != nullptr) {
        reading.source = CreatorReplySource::Receipt;
        reading.candidateId = verification.trusted->candidateId;
        reading.revision = verification.trusted->revision;
        reading.digest = verification.trusted->digest;
        reading.snapshotPath = verification.trusted->snapshotPath;
        reading.trustworthyWithoutFurtherChecks = true;
        reading.detail = "宿主核验过的结构化回执:revision=" + std::to_string(reading.revision) +
                         " candidateId=" + reading.candidateId;
        // 回执可信时,包就是工作区本身 —— 创作工具写的就是它。这一句让宿主不必
        // 再从正文里扫路径:扫得到的任何路径都不比"宿主持有的工作区"更可信。
        reading.prosePath = workspaceRoot;
        return reading;
    }

    // 2. 退回正文扫描。它仍然可用,但必须说清它退了一步,而且**必须带上**那段
    //    没通过核验的回执 —— 见上面 receiptNote 的说明。
    const auto prose = FindProsePackagePath(text, kind);
    if (!prose.empty()) {
        reading.source = CreatorReplySource::ProseScan;
        reading.prosePath = prose;
        reading.trustworthyWithoutFurtherChecks = false;
        reading.detail = receiptNote + "改而从正文里扫路径(尚未验证):" + prose;
        return reading;
    }

    // 3. 两样都没有。把真实原因说清楚 —— 一句"没有找到内容包"会把排查引向
    //    完全错误的方向。
    reading.detail = receiptNote.empty()
                         ? "这段回复里既没有可信的回执,也没有提到内容包路径。"
                         : receiptNote + "正文里也没有提到内容包路径。";
    return reading;
}

} // namespace miaodesk::creator
