// CCA-05:结构化回执的解析与核验。
//
// 这一层要替掉 `ContentCreatorDialog` 里的两条猜测:从模型回复正文里正则扫一个
// .mdwall 路径、再扫一个"看起来像内容包目录"的候选。猜中的代价不是难看,是
// **不可判定** —— 用户在正文里提到任何一个路径都会被当成这次生成的产物,于是
// "它到底做出来了没有"取决于模型怎么说话。
//
// 所以这里最重要的用例不是"能解析",而是**散文不能伪装成回执**:
//   * 模型在正文里写一行 "[receipt] accepted=1 ...",数字还都对;
//   * 摘要对、candidateId 对,但 sessionId / epoch 是上一轮的;
//   * 回执说 accepted,而宿主的台账里根本没有这个摘要。
// 三种都必须被拒,而且拒绝的理由要说清是哪一个 —— 一句"不可信"等于让排查从零开始。
#include "miaodesk/ContentCandidateReceipt.h"
#include "miaodesk/ContentCandidateLedger.h"

#include <cstdio>
#include <string>

namespace miaodesk::creator {
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

void CheckEq(const std::string& actual, const std::string& expected, const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  %s\n        expected: %s\n        actual:   %s\n",
                    what.c_str(), expected.c_str(), actual.c_str());
    }
}

constexpr const char* kSession = "S-1";
constexpr std::uint64_t kEpoch = 7;
const std::string kOtherDigest(64, 'b');

// 台账加它真实封存下来的那一版。摘要不能由这里编 —— 宿主算出来的才是事实,
// 而编一个"看起来像摘要"的字符串正是这个模块要防的那件事。
struct Sealed {
    content::ContentCandidateLedger ledger{kSession};
    std::string line;
    std::string digest;
    std::string candidateId;
};

Sealed LedgerWithOneCandidate() {
    Sealed sealed;
    sealed.ledger = content::ContentCandidateLedger(kSession);
    content::ContentValidationResult validation;
    validation.ok = true;
    std::vector<content::CandidatePart> parts = {
        {content::CandidatePartRole::Manifest, "manifest.json", "{}"},
        {content::CandidatePartRole::Scene, "scene/scene.json", "{}"},
    };
    content::ContentCandidateSubmission submission;
    submission.summary = "第一版";
    const auto receipt = sealed.ledger.Submit(submission, parts, validation);
    sealed.digest = receipt.digest;
    sealed.candidateId = receipt.candidateId;
    sealed.line = FormatCandidateReceiptLine(receipt, kSession, kEpoch);
    return sealed;
}

// 拼一行回执。默认字段全部取宿主真实封存的那一版,需要改哪个就改哪个 ——
// 这样"伪造"的用例构造起来是一处显式的差异,而不是整行重抄。
std::string ReceiptLine(const Sealed& sealed, std::string digest = {}, std::string candidateId = {},
                        std::uint32_t revision = 1, bool accepted = true,
                        std::string session = kSession, std::uint64_t epoch = kEpoch) {
    content::ContentCandidateReceipt receipt;
    receipt.accepted = accepted;
    receipt.revision = revision;
    receipt.candidateId = candidateId.empty() ? sealed.candidateId : candidateId;
    receipt.digest = digest.empty() ? sealed.digest : digest;
    receipt.snapshotPath = "sealed/x";
    return FormatCandidateReceiptLine(receipt, session, epoch);
}

// ---------------------------------------------------------------------------
// 1. 一条真的回执要能解析回来
// ---------------------------------------------------------------------------

void TestARealReceiptRoundTrips() {
    const auto sealed = LedgerWithOneCandidate();
    Check(sealed.line.rfind(kReceiptLinePrefix, 0) == 0, "回执行以固定前缀开头");

    const auto parsed = ParseCandidateReceipt(sealed.line);
    Check(parsed.present, "找得到回执行");
    Check(parsed.wellFormed, "而且字段齐全");
    Check(parsed.accepted, "accepted 解出来了");
    Check(parsed.revision == 1, "revision 解出来了");
    CheckEq(parsed.digest, sealed.digest, "digest 解出来了,而且是宿主算出来的那个");
    CheckEq(parsed.sessionId, kSession, "sessionId 解出来了");
    Check(parsed.epoch == kEpoch, "epoch 解出来了");
    Check(!parsed.candidateId.empty(), "candidateId 解出来了");
}

// ---------------------------------------------------------------------------
// 2. 没有回执就是没有回执
// ---------------------------------------------------------------------------

void TestTextWithoutAReceiptIsNotAReceipt() {
    const auto empty = ParseCandidateReceipt("候选已接受。revision=1");
    Check(!empty.present, "没有前缀就没有回执");

    // 这正是一条会被旧的正则扫中的正文:提到了路径、提到了成功。
    const auto prose = ParseCandidateReceipt(
        "我已经把内容包写到 C:\\ws\\S-1\\pkg.mdwall 了,你可以预览。");
    Check(!prose.present, "散文里的路径不算回执");
    const auto verified =
        VerifyCandidateReceipt("我已经写好了。", LedgerWithOneCandidate().ledger, kSession, kEpoch);
    Check(verified.verdict == ReceiptVerdict::NotPresent, "没有回执时核验结论是 NotPresent");
}

// ---------------------------------------------------------------------------
// 3. 散文伪装成回执:三种都必须被拒
// ---------------------------------------------------------------------------

void TestProseCannotImpersonateAReceipt() {
    const auto sealed = LedgerWithOneCandidate();

    // (a) 模型自己写一行,字段格式对,但 sessionId 是别的作品。
    const auto wrongSession = VerifyCandidateReceipt(
        ReceiptLine(sealed, {}, {}, 1, true, "S-2", kEpoch), sealed.ledger, kSession, kEpoch);
    Check(wrongSession.verdict == ReceiptVerdict::WrongSession,
          "会话不符的回执被拒(即使其余字段全对)");
    Check(wrongSession.reason.find("会话") != std::string::npos, "且说明是会话不符");

    // (b) 摘要与 candidateId 都对,但 epoch 是上一轮的。
    const auto staleEpoch = VerifyCandidateReceipt(
        ReceiptLine(sealed, {}, {}, 1, true, kSession, kEpoch - 1), sealed.ledger, kSession, kEpoch);
    Check(staleEpoch.verdict == ReceiptVerdict::StaleEpoch, "上一轮的回执被拒");
    Check(staleEpoch.reason.find("另一轮") != std::string::npos, "且说明它来自另一轮");

    // (c) 摘要根本不在宿主的台账里 —— 模型编了一个。
    const auto unknown = VerifyCandidateReceipt(
        ReceiptLine(sealed, kOtherDigest, {}, 1, true, kSession, kEpoch), sealed.ledger,
        kSession, kEpoch);
    Check(unknown.verdict == ReceiptVerdict::UnknownCandidate, "台账里没有的摘要被拒");
    Check(unknown.reason.find("台账") != std::string::npos, "且说明台账里没有它");

    // (d) revision 与宿主记的不一致。
    const auto wrongRevision = VerifyCandidateReceipt(
        ReceiptLine(sealed), sealed.ledger, kSession, kEpoch);
    Check(wrongRevision.verdict == ReceiptVerdict::Trusted, "字段全对时通过");
    const auto forgedRevision = VerifyCandidateReceipt(
        ReceiptLine(sealed, {}, {}, 99, true, kSession, kEpoch), sealed.ledger, kSession, kEpoch);
    Check(forgedRevision.verdict == ReceiptVerdict::RevisionMismatch,
          "revision 与宿主记的不一致时被拒");

    // (e) candidateId 对但摘要对不上(把两个候选拼在一起)。
    const auto mixed = VerifyCandidateReceipt(
        ReceiptLine(sealed, kOtherDigest, sealed.candidateId, 1, true, kSession, kEpoch),
        sealed.ledger, kSession, kEpoch);
    Check(mixed.verdict == ReceiptVerdict::UnknownCandidate, "摘要不在台账里就被拒");
}

void TestAnUnacceptedReceiptIsReportedAsSuch() {
    const auto sealed = LedgerWithOneCandidate();
    const auto rejected = VerifyCandidateReceipt(
        ReceiptLine(sealed, {}, {}, 1, false, kSession, kEpoch), sealed.ledger, kSession, kEpoch);
    Check(rejected.verdict == ReceiptVerdict::NotAccepted, "没被接受的回执有它自己的结论");
    Check(rejected.reason.find("没有被接受") != std::string::npos, "且说明它没被接受");
}

// ---------------------------------------------------------------------------
// 4. 坏形状的回执:缺哪一项要说清
// ---------------------------------------------------------------------------

void TestAMalformedReceiptNamesWhatIsMissing() {
    const struct { const char* drop; const char* label; const char* expect; } cases[] = {
        {"accepted=1 ", "缺 accepted", "accepted"},
        {"revision=1 ", "缺 revision", "revision"},
        {"candidateId=S-1/cand-1 ", "缺 candidateId", "candidateId"},
        {"digest=", "缺 digest", "digest"},
        {"sessionId=S-1 ", "缺 sessionId", "sessionId"},
        {"epoch=7 ", "缺 epoch", "epoch"},
    };
    const auto sealed = LedgerWithOneCandidate();
    for (const auto& c : cases) {
        const std::string line = ReceiptLine(sealed);
        const auto trimmed = line.substr(0, line.find(c.drop)) +
                             line.substr(line.find(c.drop) + std::string(c.drop).size());
        const auto parsed = ParseCandidateReceipt(trimmed);
        Check(!parsed.wellFormed, std::string("回执:") + c.label + ":不算完整");
        Check(parsed.malformedReason.find(c.expect) != std::string::npos,
              std::string("回执:") + c.label + ":原因点明了缺 " + c.expect);
        const auto verified = VerifyCandidateReceipt(trimmed, LedgerWithOneCandidate().ledger,
                                                    kSession, kEpoch);
        Check(verified.verdict == ReceiptVerdict::Malformed,
              std::string("回执:") + c.label + ":核验结论是 Malformed");
    }
}

void TestAReceiptWithABadDigestIsMalformedNotMismatched() {
    // 摘要不是 64 位十六进制时,报"这不是摘要",不要说"对不上" ——
    // 前者能一次定位,后者会让人去查台账。
    const auto sealed = LedgerWithOneCandidate();
    const auto shortDigest = ReceiptLine(sealed, "abc");
    const auto parsed = ParseCandidateReceipt(shortDigest);
    Check(!parsed.wellFormed, "短摘要不算完整");
    Check(parsed.malformedReason.find("digest") != std::string::npos, "且点明是 digest 的形状不对");

    const auto upper = ReceiptLine(sealed, std::string(64, 'A'));
    Check(ParseCandidateReceipt(upper).malformedReason.find("摘要") != std::string::npos,
          "大写十六进制也不算(摘要是小写的)");

    const auto nonNumeric = ReceiptLine(sealed);
    const auto withBadRevision = nonNumeric.substr(0, nonNumeric.find("revision=1 ")) +
                                 "revision=x " +
                                 nonNumeric.substr(nonNumeric.find("revision=1 ") + 12);
    Check(!ParseCandidateReceipt(withBadRevision).wellFormed, "非数字 revision 不算完整");
}

void TestAReceiptInTheMiddleOfASentenceIsNotTrusted() {
    // 回执行必须顶行。半句话里出现的 "[receipt]" 是模型在描述它在做什么,
    // 不是工具返回的结构化数据。
    const auto sealed = LedgerWithOneCandidate();
    const std::string embedded = "我已经提交了候选," + sealed.line + ",你可以应用它。";
    const auto parsed = ParseCandidateReceipt(embedded);
    Check(!parsed.present, "半句话里的回执行不算数");
    const auto verified = VerifyCandidateReceipt(embedded, sealed.ledger, kSession, kEpoch);
    Check(verified.verdict == ReceiptVerdict::NotPresent, "核验结论是没有回执");
}

// ---------------------------------------------------------------------------
// 5. 失效的候选不能被一行回执救活
// ---------------------------------------------------------------------------

void TestAnInvalidatedCandidateCannotBeRevivedByAReceipt() {
    auto sealed = LedgerWithOneCandidate();
    sealed.ledger.Invalidate(sealed.digest, "宿主改了它的源目录");
    const auto verified = VerifyCandidateReceipt(sealed.line, sealed.ledger, kSession, kEpoch);
    Check(verified.verdict == ReceiptVerdict::NotAccepted,
          "宿主标记失效的候选,一行回执不能把它救活");
    Check(verified.reason.find("失效") != std::string::npos, "且说明它已失效");
}

// ---------------------------------------------------------------------------
// 6. 多个回执行只认第一个
// ---------------------------------------------------------------------------

void TestOnlyTheFirstReceiptLineCounts() {
    const auto sealed = LedgerWithOneCandidate();
    const auto second = ReceiptLine(sealed, kOtherDigest);
    const auto parsed = ParseCandidateReceipt(sealed.line + "\n" + second);
    Check(parsed.digest == sealed.digest, "取第一行 —— 宿主只认它自己发出的那一行");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestARealReceiptRoundTrips();
    TestTextWithoutAReceiptIsNotAReceipt();
    TestProseCannotImpersonateAReceipt();
    TestAnUnacceptedReceiptIsReportedAsSuch();
    TestAMalformedReceiptNamesWhatIsMissing();
    TestAReceiptWithABadDigestIsMalformedNotMismatched();
    TestAReceiptInTheMiddleOfASentenceIsNotTrusted();
    TestAnInvalidatedCandidateCannotBeRevivedByAReceipt();
    TestOnlyTheFirstReceiptLineCounts();

    std::printf("\nCCA-05 candidate receipt: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
