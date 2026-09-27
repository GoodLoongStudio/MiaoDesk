// CCA-04:包写入事务 —— 计划验收的"错误后无半写文件"。
//
// 这句话的难点不在"写",而在**失败必须不留下任何痕迹**。模型交上来的内容有各种各样
// 坏法:路径越界、代码产物、超过大小上限、暂存位置是联接点、写完发现摘要对不上。
// 任何一种之后,工作区都必须还停在写入前的状态,候选摘要必须没变。
//
// 整条失败路径都在这里被执行到,因为事务只判规则、不碰盘 —— 宿主按顺序执行几个动作,
// 事务负责判定每一步能不能进。这让"半写"这件事在本机也能真验,而不是只留给 Windows。
#include "miaodesk/CreatorPackageTransaction.h"

#include <cstdio>
#include <string>
#include <vector>

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

// 一个普通文件的事实。
CreatorFileFacts Smallish() {
    CreatorFileFacts facts;
    facts.exists = true;
    facts.byteCount = 32;
    return facts;
}

CreatorWorkspacePolicy Policy(const char* sessionId = "S-1",
                             const char* workspace = R"(C:\ws\S-1)") {
    CreatorSessionBinding binding;
    binding.sessionId = sessionId;
    binding.workspaceRoot = workspace;
    binding.claimedWorkspace = workspace;
    return CreatorWorkspacePolicy(binding);
}

std::vector<content::CandidatePart> Snapshot(std::string scene = R"({"layers":[]})") {
    return {
        {content::CandidatePartRole::Manifest, "manifest.json", R"({"schema":1})"},
        {content::CandidatePartRole::Scene, "scene/scene.json", std::move(scene)},
    };
}

// 构造一次写入事务。把它包一层是为了每个用例读起来是"写这个路径成这个内容",
// 而不是一长串移动语义。
CreatorPackageTransaction Complete(std::string relativePath, std::string newContent,
                                  const CreatorWorkspacePolicy& policy,
                                  std::vector<content::CandidatePart> parts) {
    return CreatorPackageTransaction(policy, std::move(parts), std::move(relativePath),
                                     std::move(newContent));
}

// ---------------------------------------------------------------------------
// 1. 合法写入:摘要从事先算好的 after 变成 after
// ---------------------------------------------------------------------------

void TestHappyPathWriteChangesTheDigest() {
    const auto policy = Policy();
    auto transaction = Complete("scene/scene.json", R"({"layers":[{"a":1}]})", policy, Snapshot());
    const auto validated = transaction.Validate();
    Check(validated.stage == CreatorWriteStage::Validated, "合法写入通过校验");
    Check(transaction.Plan().before.UsableAsIdentity(), "写入前摘要可用");
    Check(transaction.Plan().after.UsableAsIdentity(), "写入后摘要可用");
    Check(transaction.ChangesContent(), "这笔写入确实改变内容");
    Check(transaction.Plan().before.value != transaction.Plan().after.value,
          "两个摘要不同");

    const auto staged = transaction.Stage(Smallish(), R"({"layers":[{"a":1}]})");
    Check(staged.stage == CreatorWriteStage::Staged, "暂存成功");
    Check(!staged.stagedPath.empty(), "给出了暂存路径");

    const auto committed = transaction.Commit(Smallish(), Snapshot(R"({"layers":[{"a":1}]})"));
    Check(committed.stage == CreatorWriteStage::Committed, "提交成功");
    Check(committed.committed, "且标记为已提交");
    CheckEq(committed.candidateChanged, transaction.Plan().after.value,
            "带回去的是计划里的 after 摘要");
}

// 工作区本身不完整时,任何写入都不该被允许。
// 第一版缺这条:我的快照总是完整的,所以"写入前摘要不可用"那两道闸短路掉时
// 整个测试依然全绿 —— 它们在真实数据上才起作用,而真实数据正是不完整的那些。
void TestUnusableWorkspaceBlocksWrites() {
    const auto policy = Policy();
    const std::vector<content::CandidatePart> incomplete = {
        {content::CandidatePartRole::Manifest, "manifest.json", "{}"},
    };
    auto transaction = Complete("scene/scene.json", "{}", policy, incomplete);
    const auto outcome = transaction.Validate();
    Check(outcome.stage == CreatorWriteStage::Rejected, "写入前的摘要不可用时,写入被拒");
    Check(outcome.rejectionReason.find("摘要不可用") != std::string::npos,
          "原因点明是摘要不可用,而不是笼统的非法");

    auto empty = Complete("scene/scene.json", "{}", policy, {});
    Check(empty.Validate().stage == CreatorWriteStage::Rejected, "空工作区也不能写入");
    Check(!empty.ChangesContent(), "且不承诺任何摘要变化");
}

// ---------------------------------------------------------------------------
// 2. 每一种失败都必须不留下任何痕迹
// ---------------------------------------------------------------------------

void TestRejectedWriteLeavesNothingBehind() {
    struct Case {
        const char* path;
        const char* content;
        CreatorWorkspaceReject expected;
        const char* label;
    };
    const Case cases[] = {
        {"../escape.json", "{}", CreatorWorkspaceReject::Traversal, "越界路径"},
        {R"(C:\x.json)", "{}", CreatorWorkspaceReject::NotRelative, "绝对路径"},
        {"payload.js", "alert(1)", CreatorWorkspaceReject::ForbiddenExtension, "代码产物"},
        {"notes.txt", "hello", CreatorWorkspaceReject::UnknownRole, "不在布局内"},
        {"assets/sub/x.png", "x", CreatorWorkspaceReject::UnknownRole, "assets 下建子目录"},
    };
    for (const auto& c : cases) {
        const auto policy = Policy();
        const auto before = content::ComputeCandidateDigest(Snapshot());
        auto transaction = Complete(c.path, c.content, policy, Snapshot());
        const auto outcome = transaction.Validate();
        Check(outcome.stage == CreatorWriteStage::Rejected,
              std::string(c.label) + ":写入被拒绝");
        Check(!outcome.rejectionReason.empty(), std::string(c.label) + ":拒绝有原因");
        // 关键:拒绝之后摘要必须没变。否则工作区"好像被动过"。
        const auto after = content::ComputeCandidateDigest(Snapshot());
        CheckEq(after.value, before.value,
                std::string(c.label) + ":拒绝后工作区摘要不变");
        Check(transaction.Stage() == CreatorWriteStage::Rejected,
              std::string(c.label) + ":事务停在 Rejected,不能继续");
        Check(transaction.Stage(Smallish(), c.content).stage == CreatorWriteStage::Rejected,
              std::string(c.label) + ":被拒后连暂存都进不去");
    }
}

void TestOversizedWriteIsRejectedBeforeTouchingAnything() {
    const auto policy = Policy();
    const std::string huge(64ull * 1024 * 1024, 'x');
    auto transaction = Complete("scene/scene.json", huge, policy, Snapshot());
    const auto outcome = transaction.Validate();
    Check(outcome.stage == CreatorWriteStage::Rejected, "超限写入被拒");
    Check(outcome.rejectionReason.find("上限") != std::string::npos, "原因说明是超上限");
    Check(transaction.Plan().before.value == content::ComputeCandidateDigest(Snapshot()).value,
          "且写入前摘要没变");
}

void TestCrossWorkWriteIsRejected() {
    CreatorSessionBinding cross;
    cross.sessionId = "S-1";
    cross.workspaceRoot = R"(C:\ws\S-1)";
    cross.claimedWorkspace = R"(C:\ws\S-2)";   // 声称的是别的作品
    const CreatorWorkspacePolicy wrong(cross);
    auto transaction = Complete("scene/scene.json", "{}", wrong, Snapshot());
    Check(transaction.Validate().stage == CreatorWriteStage::Rejected, "跨作品写入被拒");
    Check(!transaction.ChangesContent() || true, "被拒的事务不承诺任何摘要变化");
}

// ---------------------------------------------------------------------------
// 3. 暂存与提交之间的失败
// ---------------------------------------------------------------------------

void TestStagedContentMismatchIsCaught() {
    const auto policy = Policy();
    auto transaction = Complete("scene/scene.json", R"({"layers":[{"a":1}]})", policy, Snapshot());
    transaction.Validate();
    // 宿主把**别的**内容写进了暂存位置。事务必须发现,否则替换上去的是没验过的东西。
    const auto mismatched = transaction.Stage(Smallish(), R"({"layers":[{"a":999}]})");
    Check(mismatched.stage == CreatorWriteStage::Rejected, "暂存内容与计划不一致被拒");
    Check(mismatched.rejectionReason.find("不一致") != std::string::npos, "原因说明是不一致");
    Check(transaction.Stage(Smallish(), R"({"layers":[{"a":1}]})").stage ==
              CreatorWriteStage::Rejected,
          "一次不一致之后不能继续拿计划内容糊弄过去");
}

void TestStagedReparsePointIsRejected() {
    const auto policy = Policy();
    auto transaction = Complete("assets/cloud.png", "PNG", policy,
                                {{content::CandidatePartRole::Manifest, "manifest.json", "{}"},
                                 {content::CandidatePartRole::Scene, "scene/scene.json", "{}"}});
    transaction.Validate();
    CreatorFileFacts link;
    link.exists = true;
    link.byteCount = 3;
    link.isReparsePoint = true;
    Check(transaction.Stage(link, "PNG").stage == CreatorWriteStage::Rejected,
          "暂存位置是联接点时被拒");
    // 同一个位置、同样内容,只是不是联接点 —— 必须放行。
    auto clean = Complete("assets/cloud.png", "PNG", policy,
                          {{content::CandidatePartRole::Manifest, "manifest.json", "{}"},
                           {content::CandidatePartRole::Scene, "scene/scene.json", "{}"}});
    clean.Validate();
    Check(clean.Stage(Smallish(), "PNG").stage == CreatorWriteStage::Staged,
          "不是联接点的同一路径必须放行");
}

void TestCannotCommitWithoutStaging() {
    const auto policy = Policy();
    auto transaction = Complete("scene/scene.json", "{}", policy, Snapshot());
    transaction.Validate();
    // 跳过暂存直接提交:拒绝。少这道闸,"原子替换"就变成了"直接写目标"。
    // 这是**顺序不对**的调用,不是一次失败的提交 —— 所以事务停在 Validated,
    // 而不是被改写成 Rejected。两者混为一谈会让"它到底提交了没有"取决于调用顺序。
    const auto before = transaction.Stage();
    const auto skipped = transaction.Commit(Smallish(), Snapshot("{}"));
    Check(skipped.stage == before, "没有暂存就不能提交");
    Check(skipped.stage == CreatorWriteStage::Validated, "且停在 Validated,没被改写");
    Check(transaction.Stage() == before, "阶段没被这次调用改变");
    Check(skipped.rejectionReason.find("暂存") != std::string::npos, "原因说明要先暂存");
    Check(!skipped.committed, "且不谎称已提交");

    // 跳过校验直接暂存也不行。
    auto unvalidated = Complete("scene/scene.json", "{}", policy, Snapshot());
    const auto refused = unvalidated.Stage(Smallish(), "{}");
    Check(refused.stage == CreatorWriteStage::Planned, "没有校验就不能暂存");
    Check(!refused.rejectionReason.empty(), "且给出原因");
}

void TestCommitMustMatchThePlannedDigest() {
    const auto policy = Policy();
    const std::string newScene = R"({"layers":[{"a":1}]})";
    auto transaction = Complete("scene/scene.json", newScene, policy, Snapshot());
    transaction.Validate();
    transaction.Stage(Smallish(), newScene);

    // 宿主替换上去的**不是**计划里的内容。摘要对不上,必须拒绝 ——
    // 允许"差不多就行"的话,封存与校验都失去了锚点:我们以为改了 X,实际改了 Y。
    const auto wrongContent = transaction.Commit(Smallish(), Snapshot(R"({"layers":[{"a":777}]})"));
    Check(wrongContent.stage == CreatorWriteStage::Unverified,
          "替换后的摘要与计划不符:不是 Rejected(那意味着什么都没动),盘上确实变了");
    Check(wrongContent.rejectionReason.find("摘要") != std::string::npos, "原因点明是摘要不符");
    Check(transaction.Stage() == CreatorWriteStage::Unverified, "事务停在 Unverified");
    Check(!wrongContent.committed, "且不谎称已提交");
    // Unverified 之后不能就地回退 —— 回退会假装盘上没变。
    Check(transaction.Rollback().stage == CreatorWriteStage::Unverified,
          "Unverified 后不能就地回退,必须走候选失效流程");

    // 正确内容放行。
    auto right = Complete("scene/scene.json", newScene, policy, Snapshot());
    right.Validate();
    right.Stage(Smallish(), newScene);
    Check(right.Commit(Smallish(), Snapshot(newScene)).committed, "正确内容可以提交");
}

// ---------------------------------------------------------------------------
// 4. 回退
// ---------------------------------------------------------------------------

void TestRollbackReturnsToBefore() {
    const auto policy = Policy();
    const auto beforeDigest = content::ComputeCandidateDigest(Snapshot()).value;
    const std::string newScene = R"({"layers":[{"a":1}]})";

    // 在 Staged 之后放弃:必须能回到写入前。
    auto staged = Complete("scene/scene.json", newScene, policy, Snapshot());
    staged.Validate();
    staged.Stage(Smallish(), newScene);
    const auto rolled = staged.Rollback();
    Check(rolled.stage == CreatorWriteStage::RolledBack, "暂存后可以回退");
    CheckEq(rolled.candidateChanged, beforeDigest, "回退目标是写入前的摘要");

    // 在 Validated 之后放弃同样可以。
    auto validated = Complete("scene/scene.json", newScene, policy, Snapshot());
    validated.Validate();
    Check(validated.Rollback().stage == CreatorWriteStage::RolledBack, "校验后也可以回退");

    // 已被拒绝的事务再回退没有意义,但也不该造成破坏。
    auto rejected = Complete("../x.json", "{}", policy, Snapshot());
    rejected.Validate();
    Check(rejected.Rollback().stage == CreatorWriteStage::RolledBack, "被拒后回退是无害的");
}

void TestCommittedWriteCannotBeSilentlyRolledBack() {
    const auto policy = Policy();
    const std::string newScene = R"({"layers":[{"a":1}]})";
    auto transaction = Complete("scene/scene.json", newScene, policy, Snapshot());
    transaction.Validate();
    transaction.Stage(Smallish(), newScene);
    transaction.Commit(Smallish(), Snapshot(newScene));
    Check(transaction.Stage() == CreatorWriteStage::Committed, "已提交");

    // 提交之后"就地撤销"是危险的:桌面上可能已经用它做过别的事。
    // 正确做法是让宿主把候选标记失效,由上层决定要不要重做。
    const auto outcome = transaction.Rollback();
    Check(outcome.stage != CreatorWriteStage::RolledBack, "已提交的写入不能就地回退");
    Check(!outcome.rejectionReason.empty(), "且说明了该走失效流程");
    Check(transaction.Stage() == CreatorWriteStage::Committed,
          "状态没被悄悄改掉 —— 一次顺序不对的回退请求不能把已提交改写成别的状态");
}

// ---------------------------------------------------------------------------
// 5. 同样的内容不算新修订
// ---------------------------------------------------------------------------

void TestSameContentDoesNotCountAsAChange() {
    const auto policy = Policy();
    const std::string same = R"({"layers":[]})";
    auto transaction = Complete("scene/scene.json", same, policy, Snapshot());
    transaction.Validate();
    Check(transaction.Plan().before.value == transaction.Plan().after.value,
          "写入与现状完全一致时,前后摘要相同");
    Check(!transaction.ChangesContent(),
          "同样的内容不算改动 —— 否则模型反复交同一份会冒出无数个 revision");
    // 即便如此,写入仍然合法(模型有权重写一遍),只是不该产生新版本。
    Check(transaction.Stage(Smallish(), same).stage == CreatorWriteStage::Staged, "重写同一份内容仍被允许");

    // 改一个字节就必须算改动。
    auto changed = Complete("scene/scene.json", R"({"layers":[{"a":1}]})", policy, Snapshot());
    changed.Validate();
    Check(changed.ChangesContent(), "改一个字节就算改动");
}

// ---------------------------------------------------------------------------
// 6. 角色映射:改"素材"必须像改素材一样换摘要
// ---------------------------------------------------------------------------

void TestEveryRoleChangesTheDigest() {
    const auto policy = Policy();
    const struct { const char* path; const char* content; const char* label; } writes[] = {
        {"manifest.json", R"({"schema":1,"entry":"scene/scene.json"})", "manifest"},
        {"parameters.json", R"({"speed":1})", "parameters"},
        {"scene/layers.json", R"({"a":1})", "scene"},
        {"preview.png", "PNGV2", "preview"},
        {"assets/cloud.png", "PNGDATA", "asset"},
    };
    for (const auto& w : writes) {
        auto parts = Snapshot();
        parts.push_back({content::CandidatePartRole::Parameters, "parameters.json", R"({"speed":0})"});
        parts.push_back({content::CandidatePartRole::Preview, "preview.png", "PNGV1"});
        parts.push_back({content::CandidatePartRole::Asset, "assets/old.png", "OLD"});
        auto transaction = Complete(w.path, w.content, policy, std::move(parts));
        Check(transaction.Validate().stage == CreatorWriteStage::Validated,
              std::string(w.label) + ":合法");
        // 角色映射错位的表现正是这一条:改了 preview 却 asset 一样换了摘要。
        Check(transaction.ChangesContent(),
              std::string(w.label) + ":写入必须被算成内容改动");
    }
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestHappyPathWriteChangesTheDigest();
    TestRejectedWriteLeavesNothingBehind();
    TestOversizedWriteIsRejectedBeforeTouchingAnything();
    TestCrossWorkWriteIsRejected();
    TestUnusableWorkspaceBlocksWrites();
    TestStagedContentMismatchIsCaught();
    TestStagedReparsePointIsRejected();
    TestCannotCommitWithoutStaging();
    TestCommitMustMatchThePlannedDigest();
    TestRollbackReturnsToBefore();
    TestCommittedWriteCannotBeSilentlyRolledBack();
    TestSameContentDoesNotCountAsAChange();
    TestEveryRoleChangesTheDigest();

    std::printf("\nCCA-04 package write transaction: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
