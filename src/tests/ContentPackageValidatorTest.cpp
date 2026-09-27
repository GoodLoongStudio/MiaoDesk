// CCA-05:候选包的结构化校验,以及台账的落盘往返。
//
// 这一份测试的重点是"每一条规则各自可断言"。只有一条"合法包通过"的断言是不够的:
// 把某一条规则短路掉,剩下的规则仍会以别的理由拒绝同一个包,测试全绿 —— 它看起来
// 在验校验器,其实只在验"有个东西拒绝了"。所以这里每条规则都配一个**只**违反它
// 的输入,并要求拒绝原因点明触发了哪一条。
//
// 全部在本机实跑:校验不碰盘,于是这些规则不需要 Windows 也能被验证。
#include "miaodesk/ContentPackageValidator.h"
#include "miaodesk/ContentCandidateLedger.h"

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

// 引号与 JSON 键集中成常量。
// 把 \" 写进字符串字面量里,在这个文件被 shell 传递过几次之后迟早被嚼坏 —— 而嚼坏的
// 表现是"多字符字符常量"这种和本意毫无关系的报错,排查比直接写错更难。集中一处,
// 只在这里冒一次险。
const std::string kQuote(1, '"');
const std::string kIdKey = kQuote + "id" + kQuote + ":";
const std::string kRuntimeKey = kQuote + "runtime" + kQuote + ":";
const std::string kEntryKey = kQuote + "entry" + kQuote + ":";
const std::string kKindKey = kQuote + "kind" + kQuote + ":";
const std::string kSchemaKey = kQuote + "schema" + kQuote + ":";

// 把 manifest 里某个字符串字段的值换掉,别的字节一个都不动。
// 逐字段替换而不是重写整份 manifest:重写很容易一次改到两个字段,于是拒绝原因
// 说不清是哪一个触发的。
std::string ReplaceStringField(std::string manifest, const std::string& key,
                               const std::string& value) {
    const auto at = manifest.find(key);
    if (at == std::string::npos) return manifest;
    const auto open = manifest.find(kQuote, at + key.size());
    if (open == std::string::npos) return manifest;
    const auto close = manifest.find(kQuote, open + kQuote.size());
    if (close == std::string::npos) return manifest;
    return manifest.substr(0, open + kQuote.size()) + value + manifest.substr(close);
}

constexpr const char* kGoodManifest =
    R"({"schema":1,"id":"my.pack","name":"测试包","version":"1.0.0",)"
    R"("kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})";

std::vector<content::CandidatePart> GoodParts() {
    return {
        {content::CandidatePartRole::Manifest, "manifest.json", kGoodManifest},
        {content::CandidatePartRole::Scene, "scene/scene.json", R"({"schema":1,"layers":[]})"},
    };
}

std::string FirstMessage(const content::ContentValidationResult& result) {
    return result.issues.empty() ? std::string{} : result.issues.front().message;
}

std::string FirstFile(const content::ContentValidationResult& result) {
    return result.issues.empty() ? std::string{} : result.issues.front().file;
}

std::string FirstNode(const content::ContentValidationResult& result) {
    return result.issues.empty() ? std::string{} : result.issues.front().nodePath;
}

// ---------------------------------------------------------------------------
// 1. 合法包必须通过
// ---------------------------------------------------------------------------

void TestAValidPackagePasses() {
    const auto result = ValidateCandidatePackage(GoodParts());
    Check(result.ok, "合法包通过校验");
    Check(result.issues.empty(), "且没有任何问题");
    CheckEq(result.targetBackend, "scene", "给出了目标后端");
}

void TestParametersAndPreviewAreOptional() {
    auto parts = GoodParts();
    parts.push_back({content::CandidatePartRole::Parameters, "parameters.json", R"({"speed":1})"});
    parts.push_back({content::CandidatePartRole::Preview, "preview.png", "PNG"});
    const auto result = ValidateCandidatePackage(std::move(parts));
    Check(result.ok, "带 parameters 与 preview 的合法包同样通过");
}

// ---------------------------------------------------------------------------
// 2. 每条规则各自可断言
// ---------------------------------------------------------------------------

void TestMissingManifestIsRejected() {
    std::vector<content::CandidatePart> parts = {
        {content::CandidatePartRole::Scene, "scene/scene.json", R"({"schema":1})"},
    };
    // 没有 manifest 时**摘要那一层**先拦住:ComputeCandidateDigest 拒绝给一个
    // 不含 manifest 的快照算摘要。那正是更准确的说法,所以让它先说。
    const auto result = ValidateCandidatePackage(std::move(parts));
    Check(!result.ok, "没有 manifest.json 被拒");
    Check(FirstMessage(result).find("manifest.json") != std::string::npos,
          "且原因里点名了 manifest.json");
}

void TestEachMissingFieldIsNamed() {
    // 逐个删一个字段。这是"每条规则各自可断言"的落点:只测"缺字段被拒"的话,
    // 六个字段里删哪个都一样,校验器少检查哪一个都看不出来。
    const char* fields[] = {"id", "name", "version", "kind", "runtime", "entry"};
    for (const char* field : fields) {
        auto parts = GoodParts();
        const std::string key = kQuote + field + kQuote + ":";
        auto& manifest = parts.front().bytes;
        const auto at = manifest.find(key);
        Check(at != std::string::npos, std::string("准备:") + field + " 能在 manifest 里找到");
        if (at == std::string::npos) continue;
        const auto open = manifest.find(kQuote, at + key.size());
        const auto close = manifest.find(kQuote, open + kQuote.size());
        const std::size_t end = close == std::string::npos ? manifest.size()
                                                          : close + kQuote.size();
        manifest.erase(at, end - at);
        const auto result = ValidateCandidatePackage(std::move(parts));
        Check(!result.ok, std::string("manifest 缺 ") + field + " 被拒");
        CheckEq(FirstNode(result), field, std::string("且原因点明缺的是 ") + field);
        Check(!result.issues.empty() && result.issues.front().repairable,
              std::string(field) + " 这一类是可修复的 —— 补上字段就能过");
    }
}

void TestAnEmptyRequiredFieldCountsAsMissing() {
    // "kind":"" 在不在?在。它是空串。只查键存不存在的话这一个会被放过,
    // 而它到了加载器那里仍然是失败 —— 只不过那次发生在用户眼前。
    for (const char* field : {"id", "kind", "runtime", "entry"}) {
        auto parts = GoodParts();
        parts.front().bytes =
            ReplaceStringField(parts.front().bytes, kQuote + field + kQuote + ":", "");
        const auto result = ValidateCandidatePackage(std::move(parts));
        Check(!result.ok, std::string(field) + " 是空串时被拒(空串等于没有)");
        CheckEq(FirstNode(result), field, std::string("且点明是 ") + field);
    }
}

void TestAValueThatLooksLikeAFieldNameDoesNotCount() {
    // "kind":"id" 里有 "id" 这两个字符,但它不是 id **字段**。
    // 必填检查如果按裸字段名去 find,这里会被判成"id 在",于是放过 ——
    // 而取值那边找的是带引号的键,同一个文件在两处得出不同结论。
    // 这个用例就是钉住那个不一致的:把 HasField 改回裸名搜索,它立刻变红。
    auto parts = GoodParts();
    parts.front().bytes =
        R"({"schema":1,"kind":"id","name":"x","version":"1","runtime":"scene",)"
        R"("entry":"scene/scene.json"})";
    const auto result = ValidateCandidatePackage(std::move(parts));
    Check(!result.ok, "manifest 里没有 id 字段时被拒(即使某个值恰好叫 id)");
    // 扫一遍而不是只看 issues.front():这个 manifest 同时还有个非法的 kind,
    // 而它排在前面。断言"第一条是 id"会把这条用例绑死在问题的报告顺序上 ——
    // 改一下顺序它就红了,而那条红和这里要验证的事无关。
    bool namedId = false;
    for (const auto& issue : result.issues) {
        if (issue.nodePath == "id" && issue.file == "manifest.json") namedId = true;
    }
    Check(namedId, "且有一条点明缺的是 id 字段");
}

void TestKindMustBeWallpaperOrWidget() {
    for (const char* kind : {"poster", "scene", "Wallpaper", "widget-panel"}) {
        auto parts = GoodParts();
        parts.front().bytes = ReplaceStringField(parts.front().bytes, kKindKey, kind);
        const auto result = ValidateCandidatePackage(std::move(parts));
        Check(!result.ok, std::string("kind=") + kind + " 被拒(大小写敏感)");
        CheckEq(FirstFile(result), "manifest.json", "且指出在 manifest.json");
    }
    for (const char* kind : {"wallpaper", "widget"}) {
        auto parts = GoodParts();
        parts.front().bytes = ReplaceStringField(parts.front().bytes, kKindKey, kind);
        Check(ValidateCandidatePackage(std::move(parts)).ok, std::string("kind=") + kind + " 合法");
    }
}

void TestRuntimeOutsideTheEnumIsRejected() {
    // 一个非空但不在枚举里的 runtime。缺这个用例的话,把 runtime 那道判断整条删掉
    // 测试还是绿的 —— 因为空 runtime 由必填字段拦、web 由下面那条拦,只剩
    // "runtime":"json" 这一类从缝里漏过去。
    for (const char* bogus : {"json", "scene-web", "Scene"}) {
        auto parts = GoodParts();
        parts.front().bytes = ReplaceStringField(parts.front().bytes, kRuntimeKey, bogus);
        Check(!ValidateCandidatePackage(std::move(parts)).ok,
              std::string("runtime=") + bogus + " 不在枚举里,被拒");
    }
}

void TestWebRuntimeIsOutOfScopeThisRound() {
    // 计划 §1 明确本轮只做声明式 Scene 内容与受控素材。Web 必须显式拒绝:
    // 放过去的话,后面每一步都会在一个本轮不支持的包上给出"通过"。
    auto parts = GoodParts();
    parts.front().bytes = ReplaceStringField(parts.front().bytes, kRuntimeKey, "web");
    const auto result = ValidateCandidatePackage(std::move(parts));
    Check(!result.ok, "Web 运行时的包被拒");
    Check(!result.issues.empty() && !result.issues.front().repairable,
          "这不是改个参数就能过的问题 —— 整个运行时不在本轮范围内");
    Check(FirstMessage(result).find("本轮") != std::string::npos,
          "原因说明了它为什么不行(不在本轮范围),而不是笼统的不支持");
}

void TestEntryMustExistInThePackage() {
    auto parts = GoodParts();
    parts.front().bytes = ReplaceStringField(parts.front().bytes, kEntryKey, "scene/other.json");
    const auto result = ValidateCandidatePackage(std::move(parts));
    Check(!result.ok, "entry 指向包里不存在的文件时被拒");
    CheckEq(FirstFile(result), "scene/other.json", "且指出缺的是哪个文件");
}

void TestEntryMustPointAtScene() {
    for (const char* entry : {"assets/a.json", "manifest.json", "scene/scene.png", "../x.json"}) {
        auto parts = GoodParts();
        parts.front().bytes = ReplaceStringField(parts.front().bytes, kEntryKey, entry);
        Check(!ValidateCandidatePackage(std::move(parts)).ok,
              std::string("entry=") + entry + " 不是 scene/ 下的 .json,被拒");
    }
}

void TestIdMustHaveTheRightShape() {
    // id 有形状要求:字母数字和 . _ - ,且不能以符号开头。没有这个用例的话,
    // IsAsciiStableId 那道判断被整条删掉测试还是绿的。
    // 注意 "1st" 不在非法列表里:加载器的规则只要求首字符是 isalnum,
    // 而数字满足它。第一版把 "1st" 当成非法的,那是**我**按
    // "标识符不能以数字开头"的常识写的,和加载器的实际规则不一致 ——
    // 校验器比加载器更严的后果是拒绝它本来能加载的包。
    for (const char* id : {"_leading", "has space", "a/b", "id!", "-dash"}) {
        auto parts = GoodParts();
        parts.front().bytes = ReplaceStringField(parts.front().bytes, kIdKey, id);
        Check(!ValidateCandidatePackage(std::move(parts)).ok,
              std::string("id=") + id + " 不符合形状要求,被拒");
    }
    for (const char* id : {"a", "my.pack", "a_b-c", "0abc"}) {
        auto parts = GoodParts();
        parts.front().bytes = ReplaceStringField(parts.front().bytes, kIdKey, id);
        Check(ValidateCandidatePackage(std::move(parts)).ok, std::string("id=") + id + " 合法");
    }
}

void TestSchemaVersionMustBeRecognised() {
    // schema 缺了不能当成 0:0 恰好是个"看起来像旧版本"的合法值。
    {
        auto parts = GoodParts();
        auto& manifest = parts.front().bytes;
        const std::string needle = kSchemaKey + "1,";
        const auto at = manifest.find(needle);
        Check(at != std::string::npos, "准备:schema:1, 能在 manifest 里找到");
        // 只删这一段本身。多删一个字符就会把后面的 id 也带走,于是这条用例
        // 因为"缺 id"而失败 —— 测试绿了,但绿得不对。
        manifest.erase(at, needle.size());
        Check(!ValidateCandidatePackage(std::move(parts)).ok, "manifest 缺 schema 被拒");
    }
    {
        auto parts = GoodParts();
        auto& manifest = parts.front().bytes;
        const auto at = manifest.find(kSchemaKey + "1");
        Check(at != std::string::npos, "准备:schema:1 能在 manifest 里找到");
        manifest = manifest.substr(0, at) + kSchemaKey + "2" +
                   manifest.substr(at + kSchemaKey.size() + 1);
        const auto result = ValidateCandidatePackage(std::move(parts));
        Check(!result.ok, "schema=2 被拒(加载器只认 1)");
        Check(!result.issues.empty() && !result.issues.front().repairable,
              "而且它不可修复 —— 换版本号不是改个参数的事");
    }
}

void TestSceneAndParametersMustBeJsonObjects() {
    for (const char* body : {"", "   ", "[1,2,3]", "not json"}) {
        auto parts = GoodParts();
        parts.back().bytes = body;
        const auto result = ValidateCandidatePackage(std::move(parts));
        const std::string label = std::string(body).empty() ? std::string("空")
                                                           : std::string(body);
        Check(!result.ok, std::string("scene 内容是 ") + label + " 时被拒");
        Check(!result.issues.empty() && result.issues.front().repairable,
              "而它是可修复的 —— 写对就行");
        // 空文件有它自己的一句话。断言消息而不只断言"被拒",是因为空串这一支与
        // "必须以 { 开头"那一支在非空输入上不重叠 —— 不钉消息的话,把空文件那段
        // 删掉测试依然全绿,而那条分支就等于不存在。
        if (std::string(body).empty()) {
            Check(FirstMessage(result).find("空的") != std::string::npos,
                  "空文件给的是它自己的那句话,不是泛泛的 JSON 提示");
        }
    }
}

void TestCodeArtifactsAreRejected() {
    const char* paths[] = {"scene/helper.js", "assets/style.css", "assets/run.bat",
                           "assets/setup.ps1", "assets/tool.exe"};
    for (const char* path : paths) {
        auto parts = GoodParts();
        parts.push_back({content::CandidatePartRole::Other, path, "// code"});
        const auto result = ValidateCandidatePackage(std::move(parts));
        Check(!result.ok, std::string("包里有 ") + path + " 被拒");
        Check(!result.issues.empty() && !result.issues.back().repairable,
              std::string(path) + " 这一类不可修复 —— 声明式内容包里不放代码");
    }
}

// ---------------------------------------------------------------------------
// 3. 台账:落盘再读回来,revision 不能归零
// ---------------------------------------------------------------------------

void TestLedgerSurvivesARoundTrip() {
    content::ContentCandidateLedger ledger("S-1");
    content::ContentCandidateSubmission submission;
    submission.summary = "第一版";
    const auto validation = ValidateCandidatePackage(GoodParts());

    const auto first = ledger.Submit(submission, GoodParts(), validation);
    Check(first.accepted, "第一版被接受");
    Check(first.revision == 1, "revision=1");

    // 同一份内容:同一版,而且快照路径沿用第一次那份 —— 复制两份会让
    // "封存后不可变"变成一句空话。
    const auto same = ledger.Submit(submission, GoodParts(), validation);
    Check(same.accepted, "同一份内容再次提交仍被接受");
    Check(same.revision == 1, "而且是同一版");
    CheckEq(same.snapshotPath, first.snapshotPath, "快照路径也沿用第一次那份");

    // 改了内容:新 revision。这是"同路径改内容生成新 revision"。
    auto changed = GoodParts();
    changed.back().bytes = R"({"schema":1,"layers":[{"a":1}]})";
    const auto second = ledger.Submit(submission, changed, validation);
    Check(second.accepted, "改过的内容被接受");
    Check(second.revision == 2, "revision=2");

    // 落盘再读回来。
    const auto text = ledger.Serialize();
    content::ContentCandidateLedger reloaded("S-1");
    Check(reloaded.Parse(text), "序列化后的台账能解析回来");
    Check(reloaded.RevisionOf(first.digest) == 1, "读回来之后第一版还是第一版");
    Check(reloaded.RevisionOf(second.digest) == 2, "第二版还是第二版");
    Check(reloaded.NextRevisionNumber() == 3, "下一个 revision 接着编号");
    Check(reloaded.Sealed(first.digest) && reloaded.Sealed(second.digest), "两个摘要都记得封存过");
    Check(reloaded.LastValid() != nullptr, "最新有效候选可查");

    // 标记失效之后不能靠"再交一次同样的内容"复活。
    reloaded.Invalidate(first.digest, "宿主改了它的源目录");
    const auto revived = reloaded.Submit(submission, GoodParts(), validation);
    Check(!revived.accepted, "被标记失效的候选不能靠重复提交复活");
    Check(!revived.rejectionReason.empty(), "且说明为什么");
    Check(reloaded.Invalidated().size() == 1, "失效记录保留着");
}

void TestLedgerParseRejectsWhatItCannotTrust() {
    content::ContentCandidateLedger ledger("S-1");
    Check(!ledger.Parse("nextRevision=2\n"), "没有 sessionId 的台账被拒");
    Check(!ledger.Parse("sessionId=S-1\nreceipt=只有两段\n"), "字段数不够的 receipt 被拒");
    Check(!ledger.Parse("sessionId=S-1\nreceipt=a|1|b|c|1|1|多出来\n"), "字段过多的 receipt 被拒");
    Check(!ledger.Parse("sessionId=S-1\nreceipt=a|abc|b|c|1|1\n"), "非数字 revision 被拒");
    Check(!ledger.Parse("sessionId=S-1\nreceipt=a||b|c|1|1\n"), "空 revision 被拒");
    Check(!ledger.Parse("sessionId=S-1\nnextRevision=abc\n"), "非数字 nextRevision 被拒");
    Check(!ledger.Parse("sessionId=S-1\n坏行\n"), "没有等号的行被拒");

    // 空台账是合法的(还没提交过任何东西),但它必须真的空。
    content::ContentCandidateLedger fresh("S-9");
    Check(fresh.Parse("sessionId=S-9\nnextRevision=1\n"), "只有 sessionId 的空台账合法");
    Check(fresh.RevisionOf("whatever") == 0, "且没有任何 revision");
    Check(fresh.LastValid() == nullptr, "也没有有效候选");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestAValidPackagePasses();
    TestParametersAndPreviewAreOptional();
    TestMissingManifestIsRejected();
    TestEachMissingFieldIsNamed();
    TestAValueThatLooksLikeAFieldNameDoesNotCount();
    TestAnEmptyRequiredFieldCountsAsMissing();
    TestKindMustBeWallpaperOrWidget();
    TestRuntimeOutsideTheEnumIsRejected();
    TestWebRuntimeIsOutOfScopeThisRound();
    TestEntryMustExistInThePackage();
    TestEntryMustPointAtScene();
    TestIdMustHaveTheRightShape();
    TestSchemaVersionMustBeRecognised();
    TestSceneAndParametersMustBeJsonObjects();
    TestCodeArtifactsAreRejected();
    TestLedgerSurvivesARoundTrip();
    TestLedgerParseRejectsWhatItCannotTrust();

    std::printf("\nCCA-05 package validator + ledger: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
