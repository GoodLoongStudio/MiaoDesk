// CCA-05:认出"这一轮做出了什么",并说清凭据。
//
// 这份测试的重点是**凭据分级**:宿主此前只从正文里扫路径,于是"用户在正文里提到
// 一个路径"和"工具真的交回来一个候选"是同一件事。分成 Receipt 与 ProseScan 之后,
// 宿主必须能说清它用的是哪一种,而且只有前者能单独驱动"可以应用"。
//
// 另外几条各自对应一个真实的误判:
//   * 回执存在但没过核验时,不能静默退回扫描 —— 那样一条被拒的回执和一条没写过的
//     回执看起来一模一样,而前者说明有东西被拒绝过;
//   * 回执可信时,包就是工作区本身,不需要再扫路径;
//   * 扫到的路径必须过五道(存在/目录/manifest/扩展名/在工作区内),一道都不能少。
#include "miaodesk/CreatorReplyInterpreter.h"
#include "miaodesk/ContentCandidateLedger.h"
#include "miaodesk/ContentCandidateReceipt.h"

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
constexpr const char* kWorkspace = R"(C:\ws\S-1)";

// 台账加它真实封存下来的那一版。
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

CreatorReplyReading Read(std::string_view text, const content::ContentCandidateLedger& ledger,
                         std::uint32_t kind = kCreatorKindWallpaper) {
    return InterpretCreatorReply(text, kind, kWorkspace, ledger, kSession, kEpoch);
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    // --- 1. 一段可信回执:宿主不必再猜 ---------------------------------------
    {
        const auto sealed = LedgerWithOneCandidate();
        const auto reading = Read(sealed.line, sealed.ledger);
        Check(reading.source == CreatorReplySource::Receipt, "可信回执被认出来");
        Check(reading.trustworthyWithoutFurtherChecks, "且它能单独驱动结论");
        CheckEq(reading.candidateId, sealed.candidateId, "带上宿主持账的 candidateId");
        Check(reading.revision == 1, "带上 revision");
        CheckEq(reading.digest, sealed.digest, "带上摘要");
        // 关键的一条:回执可信时,包就是工作区本身。
        CheckEq(reading.prosePath, kWorkspace,
                "回执可信时包就是工作区 —— 不需要再从正文里扫路径");
    }

    // --- 2. 只有正文扫到的路径:可用,但不能单独驱动结论 -----------------------
    {
        const auto sealed = LedgerWithOneCandidate();
        const auto reading = Read("已经把包写到 C:\\ws\\S-1\\pkg.mdwall 了,可以预览。",
                                  sealed.ledger);
        Check(reading.source == CreatorReplySource::ProseScan, "没有可信回执时退回正文扫描");
        Check(!reading.trustworthyWithoutFurtherChecks,
              "但它不能单独驱动结论 —— 它是正文里的一个字符串");
        CheckEq(reading.prosePath, R"(C:\ws\S-1\pkg.mdwall)", "扫到的路径带回去");
        Check(reading.revision == 0, "而且没有 revision");
        Check(reading.digest.empty(), "也没有摘要");
    }

    // --- 3. 回执存在但没过核验:不能静默退回扫描 --------------------------------
    {
        const auto sealed = LedgerWithOneCandidate();
        // 摘要是别的作品的,而正文里还提到了一个路径。
        std::string forged = sealed.line;
        const auto at = forged.find("digest=");
        forged = forged.substr(0, at) + "digest=" + std::string(64, 'b') +
                 forged.substr(forged.find(' ', at));
        const auto reading = Read(forged + "\n包在 C:\\ws\\S-1\\pkg.mdwall", sealed.ledger);
        // 退回扫描本身没错(宿主还能用),但**必须说清**有一段回执没通过 ——
        // 否则一条被拒的回执和一条没写过的回执看起来一模一样。
        Check(reading.source == CreatorReplySource::ProseScan, "没有可信回执时仍然退回扫描");
        Check(reading.detail.find("没有通过核验") != std::string::npos,
              "且说明有一段回执没通过核验");
        Check(reading.detail.find("UnknownCandidate") != std::string::npos,
              "并带上核验给出的结论");
    }

    // --- 4. 什么都没有 ---------------------------------------------------------
    {
        const auto sealed = LedgerWithOneCandidate();
        const auto reading = Read("我正在看你的需求。", sealed.ledger);
        Check(reading.source == CreatorReplySource::None, "既无回执也无路径时是 None");
        Check(reading.detail.find("没有可信的回执") != std::string::npos, "且说清两样都没有");
    }

    // --- 5. 扫到的路径必须过五道 -------------------------------------------------
    {
        Check(ProsePathIsUsable({true, true, true, true, true}, nullptr), "五道全过才算可用");
        const struct { ProsePathFacts facts; const char* expect; } cases[] = {
            {{false, true, true, true, true}, "不存在"},
            {{true, false, true, true, true}, "不是目录"},
            {{true, true, false, true, true}, "manifest"},
            {{true, true, true, false, true}, "扩展名"},
            {{true, true, true, true, false}, "工作区"},
        };
        for (const auto& c : cases) {
            std::string reason;
            Check(!ProsePathIsUsable(c.facts, &reason), std::string("缺") + c.expect + "时被拒");
            Check(reason.find(c.expect) != std::string::npos,
                  std::string("且原因点明是") + c.expect);
        }
    }

    // --- 6. kind 决定扩展名 ------------------------------------------------------
    {
        const auto sealed = LedgerWithOneCandidate();
        const auto wallpaper =
            Read("包在 C:\\ws\\S-1\\pkg.mdwall", sealed.ledger, kCreatorKindWallpaper);
        const auto widget =
            Read("包在 C:\\ws\\S-1\\pkg.mdwidget", sealed.ledger, kCreatorKindWidget);
        CheckEq(wallpaper.prosePath, R"(C:\ws\S-1\pkg.mdwall)", "壁纸认 .mdwall");
        CheckEq(widget.prosePath, R"(C:\ws\S-1\pkg.mdwidget)", "组件认 .mdwidget");
        // 反过来:提的是另一种 kind 的路径,不该被当成本次的产物。
        const auto wrong = Read("包在 C:\\ws\\S-1\\pkg.mdwidget", sealed.ledger,
                                kCreatorKindWallpaper);
        Check(wrong.source == CreatorReplySource::None, "提的是另一种 kind 的路径,不认");
    }

    // --- 7. 会话/epoch 不符的回执不算 ---------------------------------------------
    {
        auto sealed = LedgerWithOneCandidate();
        const auto wrongSession = InterpretCreatorReply(
            sealed.line, kCreatorKindWallpaper, kWorkspace, sealed.ledger, "S-2", kEpoch);
        Check(wrongSession.source != CreatorReplySource::Receipt, "会话不符的回执不算");
        Check(wrongSession.detail.find("没有通过核验") != std::string::npos, "且说明为什么");

        const auto staleEpoch = InterpretCreatorReply(
            sealed.line, kCreatorKindWallpaper, kWorkspace, sealed.ledger, kSession,
            kEpoch - 1);
        Check(staleEpoch.source != CreatorReplySource::Receipt, "上一轮的回执不算");
    }

    // --- 8. 失效的候选不能靠一行回执回来 --------------------------------------
    {
        auto sealed = LedgerWithOneCandidate();
        sealed.ledger.Invalidate(sealed.digest, "宿主改了它的源目录");
        const auto reading = Read(sealed.line, sealed.ledger);
        Check(reading.source != CreatorReplySource::Receipt, "已失效的候选不算");
        Check(reading.detail.find("NotAccepted") != std::string::npos, "且说明它已失效");
    }

    CheckEq(std::string(ToString(CreatorReplySource::Receipt)), "Receipt", "凭据名稳定");
    CheckEq(std::string(ToString(CreatorReplySource::ProseScan)), "ProseScan", "扫描凭据名稳定");

    std::printf("\nCCA-05 creator reply interpreter: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
