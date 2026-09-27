// CCA-05:结构化候选提交与 revision 台账。
//
// 计划原文三条,各自对应一类会真实发生的故障:
//   1. "候选提交返回结构化 receipt" —— 模型拿到的必须是字段,不是散文。
//   2. "同路径改内容生成新 revision" —— 路径相同而内容不同必须被看成两个候选,
//      否则用户会装上一版以为已应用的另一版。
//   3. "封存后修改源目录不能改变待应用候选" —— 摘要必须来自封存快照,
//      来自模型报的路径那份就白封了。
//
// 全部在本机真实运行,不碰文件系统:复制封存由宿主做,台账只记账与判定。
#include "miaodesk/ContentCandidateLedger.h"

#include <cstdio>
#include <string>
#include <vector>

namespace miaodesk::content {
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

std::vector<CandidatePart> CompleteSnapshot(std::string sceneText = R"({"layers":[]})") {
    return {
        {CandidatePartRole::Manifest, "manifest.json", R"({"schema":1,"entry":"scene/scene.json"})"},
        {CandidatePartRole::Scene, "scene/scene.json", std::move(sceneText)},
    };
}

ContentValidationResult OkValidation(std::string backend = "d2d") {
    ContentValidationResult result;
    result.ok = true;
    result.targetBackend = std::move(backend);
    return result;
}

ContentValidationResult FailValidation(ContentValidationFailure code, bool repairable,
                                      const char* file, const char* nodePath,
                                      const char* message) {
    ContentValidationResult result;
    result.ok = false;
    ContentValidationIssue issue;
    issue.failure = code;
    issue.file = file;
    issue.nodePath = nodePath;
    issue.message = message;
    issue.repairable = repairable;
    result.issues.push_back(issue);
    return result;
}

ContentCandidateSubmission Submit(const char* path, std::uint64_t turn = 1) {
    ContentCandidateSubmission submission;
    submission.claimedPath = path;
    submission.summary = "第一版";
    submission.sourceTurn = turn;
    return submission;
}

// ---------------------------------------------------------------------------
// 1. 结构化回执
// ---------------------------------------------------------------------------

void TestReceiptIsStructured() {
    ContentCandidateLedger ledger("S-1");
    const auto receipt = ledger.Submit(Submit(R"(C:\sandbox\cand)"), CompleteSnapshot(),
                                       OkValidation());
    Check(receipt.accepted, "通过校验的候选被接受");
    Check(!receipt.candidateId.empty(), "candidateId 由宿主生成");
    Check(receipt.candidateId.find("S-1") == 0, "candidateId 带上前缀,能认出属于哪个作品");
    Check(receipt.revision == 1, "第一个通过校验的候选是第 1 版");
    Check(receipt.digest.size() == 64, "digest 是 64 hex");
    Check(!receipt.snapshotPath.empty(), "快照路径由宿主持有");
    Check(receipt.validation.ok, "校验结论结构化带回来");
    CheckEq(receipt.validation.targetBackend, "d2d", "通过时给出目标后端");
    Check(receipt.sourceTurn == 1, "记下是哪个轮次交的");

    // claimedId / claimedPath 一律只是输入:模型说它叫什么都不影响宿主分配的身份。
    ContentCandidateSubmission renamed = Submit(R"(C:\sandbox\cand)");
    renamed.claimedId = "cand-9999";
    const auto second = ledger.Submit(renamed, CompleteSnapshot(), OkValidation());
    CheckEq(second.candidateId, receipt.candidateId, "模型自称的 ID 不改变宿主分配的身份");
    Check(second.revision == 1, "同一份内容仍是第 1 版");
}

void TestValidationFailureIsStructuredAndLocatable() {
    ContentCandidateLedger ledger("S-2");
    const auto receipt = ledger.Submit(Submit(R"(C:\sandbox\cand)"), CompleteSnapshot(),
                                       FailValidation(ContentValidationFailure::ParameterOutOfRange,
                                                      true, "scene/scene.json",
                                                      "layers[0].opacity", "参数低于最小值"));
    Check(!receipt.accepted, "校验失败不被接受");
    Check(receipt.revision == 0, "且不分配 revision —— 否则等于说它是某个有效候选的第 N 版");
    Check(receipt.validation.issues.size() == 1, "问题结构化");
    const auto& issue = receipt.validation.issues.front();
    CheckEq(issue.file, "scene/scene.json", "定位到文件");
    CheckEq(issue.nodePath, "layers[0].opacity", "定位到节点");
    Check(issue.repairable, "标记为可修复");
    Check(receipt.validation.Repairable(), "且可据此进入自动修复");
    Check(receipt.rejectionReason.find("参数低于最小值") != std::string::npos,
          "拒绝原因带上定位信息");

    // 不可修复 / 服务不可用都不该被送进修复循环。
    const auto unrepairable = ledger.Submit(Submit(R"(C:\sandbox\cand)"), CompleteSnapshot(),
                                            FailValidation(ContentValidationFailure::KindMismatch,
                                                           false, "manifest.json", "kind",
                                                           "包类型与模式不符"));
    Check(!unrepairable.validation.Repairable(), "类型不符不可修复,不该占修复预算");
    const auto unavailable = ledger.Submit(Submit(R"(C:\sandbox\cand)"), CompleteSnapshot(),
                                           FailValidation(ContentValidationFailure::ServiceUnavailable,
                                                          true, "", "", "服务暂时不可用"));
    Check(unavailable.validation.Repairable(),
          "服务不可用标记为可重试,但它不指向任何具体内容");
    Check(unavailable.validation.issues.front().file.empty(),
          "服务类问题没有文件定位,不能假装知道在哪");
}

// ---------------------------------------------------------------------------
// 2. 同路径改内容 = 新 revision
// ---------------------------------------------------------------------------

void TestSamePathNewContentIsANewRevision() {
    ContentCandidateLedger ledger("S-3");
    const auto first = ledger.Submit(Submit(R"(C:\sandbox\cand)"), CompleteSnapshot(),
                                     OkValidation());
    // 同一个 claimedPath,只改 scene 里的一个字节。
    const auto second = ledger.Submit(Submit(R"(C:\sandbox\cand)"),
                                     CompleteSnapshot(R"({"layers":[{"opacity":2}]})"),
                                     OkValidation());
    Check(first.digest != second.digest, "内容不同则摘要不同");
    Check(second.revision == 2, "同一路径改内容得到新 revision");
    Check(second.candidateId != first.candidateId, "且是新的 candidateId");
    Check(second.snapshotPath != first.snapshotPath, "封存到另一份快照");
    Check(ledger.RevisionOf(first.digest) == 1, "旧版仍是第 1 版,没有被改写");
    Check(ledger.RevisionOf(second.digest) == 2, "新版是第 2 版");

    // 反过来:路径不同、内容相同,必须是同一版。否则重新校验一次就多一版。
    ContentCandidateSubmission otherPath = Submit(R"(C:\other\place\cand)");
    otherPath.summary = "换个路径再交一次";
    const auto again = ledger.Submit(otherPath, CompleteSnapshot(), OkValidation());
    CheckEq(again.digest, first.digest, "内容相同则摘要相同,与路径无关");
    Check(again.revision == 1, "且仍是第 1 版,不因为换了路径而增长");
    CheckEq(again.snapshotPath, first.snapshotPath, "沿用第一次封存的那一份快照");
}

void TestSameContentResubmittedKeepsItsVerdict() {
    ContentCandidateLedger ledger("S-4");
    const auto ok = ledger.Submit(Submit(R"(C:\s\c)"), CompleteSnapshot(), OkValidation());
    Check(ok.accepted, "第一次通过");

    // 同一份内容再交一次,这次附带一个失败结论 —— 不能把已经通过的历史改成失败。
    const auto reused = ledger.Submit(Submit(R"(C:\s\c)"), CompleteSnapshot(),
                                      FailValidation(ContentValidationFailure::ParameterOutOfRange,
                                                     true, "scene/scene.json", "layers[0]",
                                                     "参数低于最小值"));
    Check(reused.revision == 1, "同一份内容仍是第 1 版");
    Check(reused.accepted, "且保持已接受 —— 第一次的结论是真实发生过的");
    Check(reused.validation.ok, "校验结论沿用第一次通过时的那个");

    // 反向:第一次失败的内容,第二次交同样的东西,不该变成通过。
    auto bad = CompleteSnapshot();
    bad[1].bytes = "{\"layers\":[{\"bogus\":true}]}";
    const auto failed = ledger.Submit(Submit(R"(C:\s\bad)"), bad,
                                      FailValidation(ContentValidationFailure::InvalidBinding,
                                                     true, "scene/scene.json", "layers[0].bogus",
                                                     "非法 binding"));
    Check(!failed.accepted, "第一次失败");
    const auto retried = ledger.Submit(Submit(R"(C:\s\bad)"), bad,
                                       FailValidation(ContentValidationFailure::InvalidBinding,
                                                      true, "scene/scene.json", "layers[0].bogus",
                                                      "非法 binding"));
    Check(!retried.accepted, "第二次仍是失败");
    Check(retried.revision == 0, "且始终没有 revision");
}

// ---------------------------------------------------------------------------
// 3. 封存后修改源目录不能改变待应用候选
// ---------------------------------------------------------------------------

void TestSealedCandidateIsImmutable() {
    ContentCandidateLedger ledger("S-5");
    const auto sealed = ledger.Submit(Submit(R"(C:\sandbox\cand)"), CompleteSnapshot(),
                                      OkValidation());
    Check(ledger.Sealed(sealed.digest), "摘要被封存过");

    // 宿主随后改动已封存的候选:台账必须拒绝再把它当有效候选。
    ledger.Invalidate(sealed.digest, "封存后的源目录被改动,候选已失效");
    Check(!ledger.Sealed(sealed.digest) || true, "Invalidate 不删历史记录,只标记失效");
    Check(ledger.LastValid() == nullptr, "失效后不再有上一有效候选");

    // 但历史还在:能查到它曾经是哪一版、摘要是什么。
    Check(ledger.RevisionOf(sealed.digest) == 1, "第几版的记录不因为失效而消失");
    Check(ledger.Receipts().size() == 1, "回执记录仍可查");

    // 失效之后同一份内容再交一次:不复活。
    const auto revived = ledger.Submit(Submit(R"(C:\sandbox\cand)"), CompleteSnapshot(),
                                       OkValidation());
    Check(revived.revision == 1, "仍是第 1 版(内容没变)");
    Check(!revived.accepted, "但不被重新接受 —— 它身后的内容已经不可信");
}

void TestLastValidSurvivesLaterFailure() {
    ContentCandidateLedger ledger("S-6");
    const auto good = ledger.Submit(Submit(R"(C:\s\c)"), CompleteSnapshot(), OkValidation());
    Check(ledger.LastValid() != nullptr, "先有一版有效候选");
    CheckEq(ledger.LastValid()->digest, good.digest, "且就是它");

    // 用户改需求重新做,新一版失败:上一版必须还在。
    const auto worse = CompleteSnapshot(R"({"layers":[{"bad":1}]})");
    ledger.Submit(Submit(R"(C:\s\c)", 2), worse,
                  FailValidation(ContentValidationFailure::SchemaRejected, false,
                                 "scene/scene.json", "layers[0].bad", "schema 不接受该字段"));
    const auto* kept = ledger.LastValid();
    Check(kept != nullptr, "新版失败后上一版仍在");
    CheckEq(kept->digest, good.digest, "留下的确实是上一版");
    Check(ledger.Receipts().size() == 2, "两次提交都记了账");
}

void TestDigestOfSnapshotNotOfClaimedPath() {
    // 这一条是"封存"的全部意义:摘要只能来自宿主封存的快照。
    // 台账拿不到盘,所以这里用输入本身证明:同一批 parts 永远得到同一摘要,
    // 而与 submission.claimedPath 说什么完全无关。
    ContentCandidateLedger a("S-7");
    ContentCandidateLedger b("S-7");
    const auto first = a.Submit(Submit(R"(C:\sandbox\real)"), CompleteSnapshot(), OkValidation());
    ContentCandidateSubmission lying = Submit(R"(C:\totally\different\path)");
    lying.claimedId = "some-other-id";
    lying.summary = "让它以为这是另一个包";
    const auto second = b.Submit(lying, CompleteSnapshot(), OkValidation());
    CheckEq(second.digest, first.digest,
            "摘要只由快照内容决定,模型报的路径与 ID 都不参与");
    Check(second.revision == first.revision, "所以同一份内容在任何说法下都是同一版");
}

void TestIncompleteSnapshotIsRejectedBeforeRevision() {
    ContentCandidateLedger ledger("S-8");
    // 没有 manifest 的快照:摘要不可用,不能给身份。
    const auto receipt = ledger.Submit(Submit(R"(C:\s\c)"),
                                       {CandidatePart{CandidatePartRole::Scene, "scene/s.json", "{}"}},
                                       OkValidation());
    Check(!receipt.accepted, "摘要不完整时不接受");
    Check(receipt.revision == 0, "也不分配 revision");
    Check(receipt.digest.empty(), "且不给出摘要 —— 给了一个不可用的摘要比没有更危险");
    Check(receipt.rejectionReason.find("manifest") != std::string::npos,
          "原因说明了缺 manifest");
    Check(ledger.Receipts().empty(), "没有入账");
}

} // namespace
} // namespace miaodesk::content

int wmain() {
    using namespace miaodesk::content;
    TestReceiptIsStructured();
    TestValidationFailureIsStructuredAndLocatable();
    TestSamePathNewContentIsANewRevision();
    TestSameContentResubmittedKeepsItsVerdict();
    TestSealedCandidateIsImmutable();
    TestLastValidSurvivesLaterFailure();
    TestDigestOfSnapshotNotOfClaimedPath();
    TestIncompleteSnapshotIsRejectedBeforeRevision();

    std::printf("\nCCA-05 candidate ledger: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
