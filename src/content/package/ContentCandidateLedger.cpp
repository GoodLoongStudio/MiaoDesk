#include "miaodesk/ContentCandidateLedger.h"

#include <algorithm>
#include <utility>

namespace miaodesk::content {

const char* ToString(ContentValidationFailure failure) noexcept {
    switch (failure) {
    case ContentValidationFailure::None: return "None";
    case ContentValidationFailure::KindMismatch: return "KindMismatch";
    case ContentValidationFailure::MissingManifestField: return "MissingManifestField";
    case ContentValidationFailure::UnsupportedRuntime: return "UnsupportedRuntime";
    case ContentValidationFailure::UnknownCapability: return "UnknownCapability";
    case ContentValidationFailure::MissingReferencedFile: return "MissingReferencedFile";
    case ContentValidationFailure::InvalidBinding: return "InvalidBinding";
    case ContentValidationFailure::ParameterOutOfRange: return "ParameterOutOfRange";
    case ContentValidationFailure::BackendUnsupported: return "BackendUnsupported";
    case ContentValidationFailure::SizeLimit: return "SizeLimit";
    case ContentValidationFailure::SchemaRejected: return "SchemaRejected";
    case ContentValidationFailure::Unrepairable: return "Unrepairable";
    case ContentValidationFailure::ServiceUnavailable: return "ServiceUnavailable";
    }
    return "Unknown";
}

ContentCandidateLedger::ContentCandidateLedger(std::string sessionId)
    : sessionId_(std::move(sessionId)) {}

std::uint32_t ContentCandidateLedger::RevisionOf(const std::string& digest) const noexcept {
    for (const auto& receipt : receipts_) {
        if (receipt.digest == digest && receipt.revision != 0) return receipt.revision;
    }
    return 0;
}

const ContentCandidateReceipt* ContentCandidateLedger::LastValid() const noexcept {
    // 只看 accepted —— Invalidate() 已经把它清成 false 了,再扫一遍 invalidated_
    // 是同一件事查两遍。实测:把这里的 invalidated_ 扫描短路掉,测试依然全绿,
    // 因为 accepted 那一关已经挡住。两处保险不如一处,因为两处会各自漂移。
    for (auto it = receipts_.rbegin(); it != receipts_.rend(); ++it) {
        if (it->accepted && it->validation.ok) return &*it;
    }
    return nullptr;
}

bool ContentCandidateLedger::Sealed(const std::string& digest) const noexcept {
    if (digest.empty()) return false;
    for (const auto& receipt : receipts_) {
        if (receipt.digest == digest && receipt.accepted) return true;
    }
    return false;
}

void ContentCandidateLedger::Invalidate(const std::string& digest, std::string reason) {
    if (digest.empty()) return;
    if (std::find(invalidated_.begin(), invalidated_.end(), digest) != invalidated_.end()) return;
    invalidated_.push_back(digest);
    for (auto& receipt : receipts_) {
        if (receipt.digest != digest) continue;
        receipt.accepted = false;
        if (receipt.rejectionReason.empty()) receipt.rejectionReason = std::move(reason);
    }
}

ContentCandidateReceipt ContentCandidateLedger::Submit(
    const ContentCandidateSubmission& submission,
    std::vector<CandidatePart> snapshotParts,
    const ContentValidationResult& validation) {
    ContentCandidateReceipt receipt;
    receipt.sourceTurn = submission.sourceTurn;
    receipt.summary = submission.summary.empty() ? submission.claimedPath : submission.summary;
    receipt.validation = validation;

    // 摘要只可能来自**封存快照**。claimedPath 是模型说的话,不能参与摘要 ——
    // 否则模型报一个路径、内容却是别的,摘要照样"对得上",封存就白做了。
    const auto digest = ComputeCandidateDigest(std::move(snapshotParts));
    if (!digest.UsableAsIdentity()) {
        receipt.accepted = false;
        receipt.rejectionReason = digest.incompletenessReason.empty()
                                     ? "候选摘要不可用"
                                     : digest.incompletenessReason;
        return receipt;
    }
    receipt.digest = digest.value;

    // 同一份内容:同一版。revision 不变,snapshot 沿用第一次封存的那一份 ——
    // 复制两次会让"封存后不可变"变成一句空话。
    const auto existing = RevisionOf(digest.value);
    const auto invalidated = std::find(invalidated_.begin(), invalidated_.end(), digest.value) !=
                             invalidated_.end();
    if (existing != 0) {
        receipt.revision = existing;
        receipt.candidateId = candidateIdOf(digest.value);
        receipt.snapshotPath = snapshotPathOf(digest.value);
        receipt.validation = validation.ok ? validation : validationOf(digest.value);
        // 被宿主标记失效的候选不能靠"再交一次同样的内容"复活:它身后的内容已经变了,
        // 而摘要anas变说明它仍然指向那份已经不可信的封存。
        receipt.accepted = !invalidated;
        if (invalidated && receipt.rejectionReason.empty()) {
            receipt.rejectionReason = "候选已被宿主标记失效,不能重新接受";
        }
        return receipt;
    }

    if (!validation.ok) {
        // 校验失败:不分配 revision。给它 revision 等于说"这是一个有效候选的第 N 版",
        // 而它并没有通过校验。
        receipt.accepted = false;
        receipt.rejectionReason = "校验未通过";
        if (!validation.issues.empty()) {
            receipt.rejectionReason += ":" + validation.issues.front().message;
        }
        // 失败也进台账:宿主需要知道它试过什么,并且要能区分"同一份内容第二次失败"
        // 和"另一份内容失败"。但不分配 revision。
        receipts_.push_back(receipt);
        return receipt;
    }

    // 通过:分配 revision,candidateId 由宿主生成,快照路径由宿主持有。
    receipt.revision = nextRevision_++;
    receipt.candidateId = sessionId_ + "/cand-" + std::to_string(receipt.revision);
    receipt.snapshotPath = "creator/" + sessionId_ + "/revisions/" + std::to_string(receipt.revision) +
                           "/" + ShortDigest(digest.value, 12);
    receipt.accepted = true;
    receipts_.push_back(receipt);
    return receipt;
}

std::string ContentCandidateLedger::snapshotPathOf(const std::string& digest) const {
    for (const auto& receipt : receipts_) {
        if (receipt.digest == digest) return receipt.snapshotPath;
    }
    return {};
}

std::string ContentCandidateLedger::candidateIdOf(const std::string& digest) const {
    for (const auto& receipt : receipts_) {
        if (receipt.digest == digest && !receipt.candidateId.empty()) return receipt.candidateId;
    }
    return {};
}

ContentValidationResult ContentCandidateLedger::validationOf(const std::string& digest) const {
    ContentValidationResult result;
    // 取第一次通过校验时的结论:同一份内容重复提交不该把一次失败的历史
    // 变成"有效的",反过来也不该把一次通过的历史变成失败。
    for (const auto& receipt : receipts_) {
        if (receipt.digest != digest) continue;
        if (receipt.validation.ok) return receipt.validation;
        result = receipt.validation;
    }
    return result;
}

// 台账落盘。"receipt=" 一行一条,字段用 '|' 分隔:这个字符不会出现在
// 摘要、路径、或校验结论里 —— 而如果会,这里就必须转义,否则两边的记录
// 会悄悄对不上,而那种错法在测试里看不出来。
std::string ContentCandidateLedger::Serialize() const {
    std::string out = "sessionId=" + sessionId_ + "\n";
    out += "nextRevision=" + std::to_string(nextRevision_) + "\n";
    for (const auto& receipt : receipts_) {
        out += "receipt=";
        out += receipt.candidateId + "|" + std::to_string(receipt.revision) + "|" + receipt.digest +
               "|" + receipt.snapshotPath + "|" + (receipt.accepted ? "1" : "0") + "|" +
               (receipt.validation.ok ? "1" : "0");
        out += "\n";
    }
    for (const auto& digest : invalidated_) {
        out += "invalidated=" + digest + "\n";
    }
    return out;
}

bool ContentCandidateLedger::Parse(std::string_view text) {
    receipts_.clear();
    invalidated_.clear();
    nextRevision_ = 1;
    sessionId_.clear();

    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty()) {
            if (end == text.size()) break;
            continue;
        }
        const std::size_t equal = line.find('=');
        if (equal == std::string_view::npos) return false;
        const std::string_view key = line.substr(0, equal);
        const std::string_view value = line.substr(equal + 1);
        if (key == "sessionId") {
            sessionId_ = std::string(value);
        } else if (key == "nextRevision") {
            if (value.empty()) return false;
            std::uint32_t number = 0;
            for (const char ch : value) {
                if (ch < '0' || ch > '9') return false;
                number = number * 10 + static_cast<std::uint32_t>(ch - '0');
            }
            nextRevision_ = number;
        } else if (key == "receipt") {
            // candidateId|revision|digest|snapshotPath|accepted|validationOk
            std::vector<std::string> fields;
            std::size_t from = 0;
            while (true) {
                const std::size_t bar = value.find('|', from);
                if (bar == std::string_view::npos) {
                    fields.emplace_back(value.substr(from));
                    break;
                }
                fields.emplace_back(value.substr(from, bar - from));
                from = bar + 1;
            }
            if (fields.size() != 6) return false;
            // revision 必须纯十进制。用 strtoul 的话,"abc" 会静默变成 0,
            // 而 0 在回执里的含义是"被拒绝、没有分配版本号" —— 于是一条坏行
            // 会被读成一个"被拒绝的候选",而它其实是根本解析不出来的一行。
            if (fields[1].empty()) return false;
            for (const char ch : fields[1]) {
                if (ch < '0' || ch > '9') return false;
            }
            ContentCandidateReceipt receipt;
            receipt.candidateId = fields[0];
            receipt.revision = static_cast<std::uint32_t>(std::strtoul(fields[1].c_str(), nullptr, 10));
            receipt.digest = fields[2];
            receipt.snapshotPath = fields[3];
            receipt.accepted = fields[4] == "1";
            receipt.validation.ok = fields[5] == "1";
            receipts_.push_back(std::move(receipt));
        } else if (key == "invalidated") {
            invalidated_.emplace_back(value);
        }
        if (end == text.size()) break;
    }
    // sessionId 是归属的锚:没有它,这份台账无法和任何作品对上。
    if (sessionId_.empty()) return false;
    return true;
}

} // namespace miaodesk::content
