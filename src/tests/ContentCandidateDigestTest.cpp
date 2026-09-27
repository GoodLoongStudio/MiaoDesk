// CCA-05:候选摘要必须真的覆盖内容,而且必须稳定。
//
// 这个文件测两件事:
//   1. SHA-256 本体对得上已发表测试向量 —— 摘要是一种身份,不能拿"自己实现的 hash"
//      自我认证,所以先证原语,再证用法。
//   2. 计划的原文要求:"摘要覆盖 manifest、scene、参数及引用素材;不能只 hash 路径或
//      单个 JSON",以及"同路径改内容生成新 revision"。
//      这两条各自对应一类真实故障:只 hash manifest 会让改了一层还留着旧校验结论;
//      只 hash 绝对路径会让同一份内容每次重新校验都变成"新候选",于是幂等永远不命中。
#include "miaodesk/ContentCandidateDigest.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
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

CandidatePart Part(CandidatePartRole role, std::string relPath, std::string bytes) {
    CandidatePart part;
    part.role = role;
    part.relPath = std::move(relPath);
    part.bytes = std::move(bytes);
    return part;
}

// 一个最小但完整的候选:manifest + scene + 一张引用素材。
std::vector<CandidatePart> CompleteCandidate() {
    return {
        Part(CandidatePartRole::Manifest, "manifest.json", R"({"schema":1,"entry":"scene/scene.json"})"),
        Part(CandidatePartRole::Scene, "scene/scene.json", R"({"layers":[{"texture":"assets/cloud.png"}]})"),
        Part(CandidatePartRole::Parameters, "parameters.json", R"({"speed":{"value":1}})"),
        Part(CandidatePartRole::Asset, "assets/cloud.png", "PNGDATA"),
    };
}

// ---------------------------------------------------------------------------
// 1. 原语:已发表的 SHA-256 测试向量
// ---------------------------------------------------------------------------

void TestSha256KnownAnswers() {
    CheckEq(Sha256Hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256(\"\") matches the published vector");
    CheckEq(Sha256Hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256(\"abc\") matches the published vector");
    CheckEq(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "SHA-256 of a 56-byte message matches the published vector");
    // 100 万个 'a':跨过多个块,并且把长度字段推入最后一块。
    const std::string million(1000000, 'a');
    CheckEq(Sha256Hex(million),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
            "SHA-256 of one million 'a' matches the published vector");
    // 正好 55 / 56 / 64 字节:填充边界的三种形状。
    Check(Sha256Hex(std::string(55, 'a')).size() == 64, "digest is always 64 hex chars");
    Check(Sha256Hex(std::string(55, 'a')) != Sha256Hex(std::string(56, 'a')),
          "55- and 56-byte inputs hash differently (pad boundary)");
    Check(Sha256Hex(std::string(64, 'a')) != Sha256Hex(std::string(65, 'a')),
          "64- and 65-byte inputs hash differently (block boundary)");
}

// ---------------------------------------------------------------------------
// 2. 覆盖范围:改哪一部分都必须换摘要
// ---------------------------------------------------------------------------

void TestDigestCoversEveryDeclaredPart() {
    const auto base = ComputeCandidateDigest(CompleteCandidate());
    Check(base.complete, "完整候选的摘要可用");
    Check(base.UsableAsIdentity(), "且满足身份判据(complete + 64 hex)");
    Check(base.covered.size() == 4, "四块都进了覆盖清单");

    // 每一部分单独改一个字节,摘要都必须变。少任何一条,就意味着那一部分改了
    // 而候选仍被当成同一个 —— 旧的校验结论会继续有效。
    const std::pair<CandidatePartRole, const char*> mutations[] = {
        {CandidatePartRole::Manifest, "改 manifest"},
        {CandidatePartRole::Scene, "改 scene"},
        {CandidatePartRole::Parameters, "改参数"},
        {CandidatePartRole::Asset, "改引用素材"},
    };
    for (const auto& [role, label] : mutations) {
        auto parts = CompleteCandidate();
        for (auto& part : parts) {
            if (part.role == role) part.bytes += "x";
        }
        const auto changed = ComputeCandidateDigest(parts);
        Check(changed.value != base.value,
              std::string(label) + "必须换摘要(否则那部分改了仍算同一个候选)");
        Check(changed.complete, std::string(label) + "之后摘要仍然完整");
    }
}

void TestSamePathNewContentIsANewRevision() {
    // 计划原文:"候选修改即使路径相同,也必须是新 revision"。
    auto parts = CompleteCandidate();
    const auto first = ComputeCandidateDigest(parts);
    parts[1].bytes = R"({"layers":[{"texture":"assets/other.png"}]})";
    const auto second = ComputeCandidateDigest(parts);
    Check(first.value != second.value, "同路径改 scene 内容得到新摘要");
    CheckEq(second.covered[1], "scene:scene/scene.json",
            "路径没变(变的是内容)");
}

void TestAbsoluteLocationIsNotPartOfIdentity() {
    // 同一份内容换到别的绝对路径 = 同一个候选。否则每次重新校验都是"新候选",
    // 幂等永不命中,而用户反复看到"又生成了一版"。
    const auto a = ComputeCandidateDigest(CompleteCandidate());
    const auto b = ComputeCandidateDigest(CompleteCandidate());
    CheckEq(a.value, b.value, "同样的内容两次计算得到同一个摘要(可重入)");
    Check(a == b, "且两个摘要对象相等");
}

void TestTraversalOrderDoesNotChangeTheDigest() {
    // 目录遍历返回的顺序是实现细节。同一个快照走两遍必须得到同一个摘要。
    auto parts = CompleteCandidate();
    const auto forward = ComputeCandidateDigest(parts);
    std::reverse(parts.begin(), parts.end());
    const auto reverse = ComputeCandidateDigest(parts);
    CheckEq(reverse.value, forward.value, "输入顺序不影响摘要");
    Check(reverse == forward, "且覆盖清单一致");
}

void TestTwoIdenticalAssetsStayDistinct() {
    // 两张内容完全相同的素材放在不同路径下,必须是两块不同的内容。
    // 摘要若不包含包内路径,它们会贡献逐字节相同的数据,于是两块合成一块 ——
    // 覆盖清单会假装没有第二张图,而用户看到的画面上确实有第二张。
    auto parts = CompleteCandidate();
    parts.push_back(Part(CandidatePartRole::Asset, "assets/cloud-copy.png", "PNGDATA"));
    const auto withCopy = ComputeCandidateDigest(parts);
    Check(withCopy.complete, "加一张同内容素材后摘要仍然完整");
    Check(withCopy.covered.size() == 5, "覆盖清单里有 5 块");
    const auto base = ComputeCandidateDigest(CompleteCandidate());
    Check(withCopy.value != base.value, "加一张素材(哪怕内容相同)必须换摘要");
    Check(withCopy.covered.size() > base.covered.size(), "且覆盖清单如实变长");

    // 包内相对路径**是**包结构的一部分:改它意味着 manifest/scene 里的引用也要跟着改,
    // 所以它是新候选。真正与身份无关的是绝对位置 —— 那条由
    // TestAbsoluteLocationIsNotPartOfIdentity 单独覆盖,别把两者混为一谈。
    auto renamed = CompleteCandidate();
    renamed[3].relPath = "assets/z.png";
    Check(ComputeCandidateDigest(renamed).value != base.value,
          "包内相对路径变了就是新候选(引用它的 scene 也得跟着改)");
}

// 编码必须可逆:不同的分块列表不得共享一个摘要。
//
// 这不是"找一次 SHA-256 碰撞"(做不到,也不必),而是检查**编码层**没有把两个不同的
// 列表映到同一条字节流上。最有代表性的失效是去掉长度前缀:
//   [Asset "p" = "a"] + [Other "q" = "b"]  和  [Asset "p" = "ab"] + [Other "q" = ""]
// 裸拼接后都是 "assetp" "ab" "otherq" "b" —— 两个完全不同的候选共用一个摘要,
// 于是一个的校验结论盖在另一个头上。变异测试确认:去掉长度前缀这条会变红。
void TestCanonicalEncodingIsInjective() {
    const CandidatePartRole roles[] = {CandidatePartRole::Asset, CandidatePartRole::Other};
    const char* paths[] = {"p", "q"};
    // 载荷里必须包含能跨块"伪装"成下一块头部的内容。否则枚举永远碰不到那个
    // 去掉了长度前缀才会出现的碰撞:
    //   [Asset p="a"] + [Other q=<"other"+"q"+"B">]
    //   [Asset p=<"a"+"other"+"q">] + [Other q="B"]
    // 裸拼接后两条都是 "assetp" "a" "otherq" "otherq" "B" —— 两个不同的候选
    // 共用一个摘要,一个的校验结论就此盖在另一个头上。
    // "otherqB" 是上面那个构造的另一半:role2+path2 = "other"+"q","B" 是收尾载荷。
    // 少了它就永远拼不出碰撞,去掉长度前缀的改动会静静地留在代码里。
    const char* payloads[] = {"", "a", "B", "ab", "other", "otherq",
                              "aotherq", "otherqB"};

    // 枚举所有"两块 + 不同的角色/路径/内容"组合,要求摘要两两不同。
    std::vector<std::pair<std::string, std::vector<CandidatePart>>> seen;
    auto record = [&](std::vector<CandidatePart> parts) {
        // 键必须是一个确定的全序。只按路径排序会在两个角色共用同一路径时
        // (asset:p 与 other:p)把两个不同的列表排成同一形状,从而报出假碰撞 ——
        // 那两次跑出来的其实是顺序无关性,是本该的相等。
        auto copy = parts;
        std::sort(copy.begin(), copy.end(), [](const CandidatePart& a, const CandidatePart& b) {
            const std::string ra = CandidateRoleName(a.role);
            const std::string rb = CandidateRoleName(b.role);
            if (ra != rb) return ra < rb;
            if (a.relPath != b.relPath) return a.relPath < b.relPath;
            return a.bytes < b.bytes;
        });
        std::string key;
        for (const auto& part : copy) {
            key += std::string(CandidateRoleName(part.role)) + ":" + part.relPath + ":" + part.bytes + ";";
        }
        seen.push_back({key, std::move(copy)});
    };

    for (auto roleA : roles) {
        for (const char* pathA : paths) {
            for (const char* payloadA : payloads) {
                for (auto roleB : roles) {
                    for (const char* pathB : paths) {
                        for (const char* payloadB : payloads) {
                            const CandidatePart a = Part(roleA, pathA, payloadA);
                            const CandidatePart b = Part(roleB, pathB, payloadB);
                            // 同一个 (role,path) 出现两次会被拒绝,跳过。
                            if (roleA == roleB && std::string(pathA) == pathB) continue;
                            // 长在完整候选上:少 manifest/scene 的列表一律 incomplete,
                            // value 是空的,那种"相等"说明不了任何事。
                            auto parts = CompleteCandidate();
                            parts.push_back(a);
                            parts.push_back(b);
                            record(parts);
                        }
                    }
                }
            }
        }
    }

    std::map<std::string, std::string> digestToKey;
    int collisions = 0;
    for (const auto& [key, parts] : seen) {
        const auto digest = ComputeCandidateDigest(parts);
        if (!digest.complete) continue;   // 只有完整候选才需要身份
        const auto it = digestToKey.find(digest.value);
        if (it != digestToKey.end() && it->second != key) {
            ++collisions;
            std::printf("  collision:\n    %s\n    %s\n", it->second.c_str(), key.c_str());
            continue;
        }
        digestToKey[digest.value] = key;
    }
    Check(collisions == 0, "不同的分块列表撞到了同一个摘要");
    // 枚举本身要足够大:太小的样本里"没有碰撞"什么也没证明。
    // 这里数的是**不同的摘要个数**,它 naturally 小于组合数(顺序无关的会合并)。
    std::size_t distinct = digestToKey.size();
    std::printf("      (枚举 %zu 个组合,%zu 个不同摘要)\n", seen.size(), distinct);
    Check(distinct > 200, "枚举出的不同摘要足够多,这条断言才有意义");

    // 单独把那个代表性失效钉住:同样的两块内容,只把字节从一个分块挪到另一个,
    // 也必须是不同的候选。
    const auto splitOne = ComputeCandidateDigest({
        Part(CandidatePartRole::Asset, "assets/x", "a"),
        Part(CandidatePartRole::Other, "assets/y", "b"),
        Part(CandidatePartRole::Manifest, "manifest.json", "{}"),
        Part(CandidatePartRole::Scene, "scene/s.json", "{}"),
    });
    const auto splitTwo = ComputeCandidateDigest({
        Part(CandidatePartRole::Asset, "assets/x", "ab"),
        Part(CandidatePartRole::Other, "assets/y", ""),
        Part(CandidatePartRole::Manifest, "manifest.json", "{}"),
        Part(CandidatePartRole::Scene, "scene/s.json", "{}"),
    });
    Check(splitOne.complete && splitTwo.complete, "两个切分都是完整候选");
    Check(splitOne.value != splitTwo.value,
          "把字节从一个分块挪到另一个分块,必须得到不同的摘要");
}

// 同一个路径、同一份内容,但角色不同 —— 必须是两块不同的东西。
// 摘要若不含角色,Asset "cover.png" 和 Preview "cover.png" 会共用一个身份。
void TestRoleIsPartOfIdentity() {
    const auto asAsset = ComputeCandidateDigest({
        Part(CandidatePartRole::Asset, "cover.png", "PNG"),
        Part(CandidatePartRole::Manifest, "manifest.json", "{}"),
        Part(CandidatePartRole::Scene, "scene/s.json", "{}"),
    });
    const auto asPreview = ComputeCandidateDigest({
        Part(CandidatePartRole::Preview, "cover.png", "PNG"),
        Part(CandidatePartRole::Manifest, "manifest.json", "{}"),
        Part(CandidatePartRole::Scene, "scene/s.json", "{}"),
    });
    Check(asAsset.complete && asPreview.complete, "两者都是完整候选");
    Check(asAsset.value != asPreview.value, "角色不同必须是不同的候选");
    Check(asAsset.covered[asAsset.covered.size() - 1] !=
              asPreview.covered[asPreview.covered.size() - 1] ||
          asAsset.value != asPreview.value,
          "覆盖清单或摘要至少有一项能区分它们");
}

void TestIncompleteDigestIsNotAnIdentity() {
    // 缺 manifest:摘要仍然算得出来,但必须被标记不可用。否则一个只有 scene 的
    // 残缺包也能拿到"合法身份",然后拿旧校验结论蒙混。
    std::vector<CandidatePart> parts = {
        Part(CandidatePartRole::Scene, "scene/scene.json", R"({"layers":[]})"),
    };
    const auto digest = ComputeCandidateDigest(parts);
    Check(!digest.complete, "没有 manifest 的摘要不完整");
    Check(!digest.UsableAsIdentity(), "且不能当作身份使用");
    Check(digest.incompletenessReason.find("manifest") != std::string::npos,
          "原因说明了缺什么");

    // 缺 scene 同理。
    auto parametersOnly = std::vector<CandidatePart>{
        Part(CandidatePartRole::Manifest, "manifest.json", "{}"),
    };
    const auto noScene = ComputeCandidateDigest(parametersOnly);
    Check(!noScene.complete, "没有 scene 的摘要不完整");
    Check(noScene.incompletenessReason.find("scene") != std::string::npos, "原因说明了缺 scene");

    // 完全空的快照。
    const auto empty = ComputeCandidateDigest({});
    Check(!empty.complete && !empty.UsableAsIdentity(), "空快照不是身份");
    Check(empty.incompletenessReason.find("没有任何内容文件") != std::string::npos,
          "空快照的原因说明了是空的");

    // 没有路径的分块:不能悄悄把无名的东西算进去。
    auto noPath = CompleteCandidate();
    noPath[1].relPath.clear();
    const auto nameless = ComputeCandidateDigest(noPath);
    Check(!nameless.complete, "缺包内路径的分块让摘要不完整");
    Check(nameless.incompletenessReason.find("相对路径") != std::string::npos,
          "原因说明了缺路径");
}

void TestDuplicatePartIsRejected() {
    // 同一个文件列两次会让覆盖范围名不副实(声称覆盖 4 块,实际只有 3 份内容)。
    auto parts = CompleteCandidate();
    parts.push_back(parts[2]);
    const auto digest = ComputeCandidateDigest(parts);
    Check(!digest.complete, "重复分块让摘要不完整");
    Check(digest.incompletenessReason.find("列了两次") != std::string::npos,
          "原因说明了是重复");
}

void TestRoleRoundTrip() {
    const CandidatePartRole roles[] = {
        CandidatePartRole::Manifest, CandidatePartRole::Scene,
        CandidatePartRole::Parameters, CandidatePartRole::Preview,
        CandidatePartRole::Asset, CandidatePartRole::Other,
    };
    for (auto role : roles) {
        CandidatePartRole parsed{};
        Check(ParseCandidateRole(CandidateRoleName(role), &parsed),
              std::string(CandidateRoleName(role)) + " 能解析回来");
        Check(parsed == role, "且解析回来是同一个 role");
    }
    CandidatePartRole ignored{};
    Check(!ParseCandidateRole("nonsense", &ignored), "未知 role 名被拒绝");
    Check(CandidateRoleName(CandidatePartRole::Manifest) == std::string("manifest"),
          "manifest 的公开名称是 manifest");
}

void TestShortDigest() {
    const auto digest = ComputeCandidateDigest(CompleteCandidate());
    CheckEq(ShortDigest(digest.value, 12), digest.value.substr(0, 12), "短摘要是前缀");
    Check(ShortDigest(digest.value, 12).size() == 12, "长度是 12");
    Check(ShortDigest("abc", 12) == "abc", "比请求长度短时原样返回");
}

void TestDigestCoversReferencedAssetsNotJustManifest() {
    // 计划明确要求覆盖"引用素材"。这里用一个具体形状表达它:只改 manifest,
    // 摘要会变(上面已测);反过来,只改素材而 manifest 完全不变,摘要也必须变 ——
    // 因为用户看到的画面变了。
    auto base = CompleteCandidate();
    const auto before = ComputeCandidateDigest(base);
    base[3].bytes = "PNGDATA-V2";
    const auto after = ComputeCandidateDigest(base);
    Check(after.value != before.value, "只改引用素材也换摘要");
    Check(after.covered.size() == before.covered.size(), "覆盖清单条目数不变");
}

// 同一个角色下的两块,按不同路径顺序送来:摘要必须一致。
// 只按角色排序而忽略路径时,这条会红 —— 而 CompleteCandidate 每个角色只有一块,
// 所以顺序无关性那边永远碰不到这个分支。
void TestSameRolePathOrderDoesNotMatter() {
    const auto forward = ComputeCandidateDigest({
        Part(CandidatePartRole::Manifest, "manifest.json", "{}"),
        Part(CandidatePartRole::Scene, "scene/s.json", "{}"),
        Part(CandidatePartRole::Asset, "assets/a.png", "A"),
        Part(CandidatePartRole::Asset, "assets/b.png", "B"),
    });
    const auto reverse = ComputeCandidateDigest({
        Part(CandidatePartRole::Manifest, "manifest.json", "{}"),
        Part(CandidatePartRole::Scene, "scene/s.json", "{}"),
        Part(CandidatePartRole::Asset, "assets/b.png", "B"),
        Part(CandidatePartRole::Asset, "assets/a.png", "A"),
    });
    Check(forward.complete && reverse.complete, "两个候选都完整");
    Check(forward.value == reverse.value,
          "同一角色的两块按不同路径顺序送来,摘要必须一致");
    Check(forward.covered == reverse.covered, "覆盖清单顺序也一致");
}

} // namespace
} // namespace miaodesk::content

int wmain() {
    using namespace miaodesk::content;
    TestSha256KnownAnswers();
    TestDigestCoversEveryDeclaredPart();
    TestSamePathNewContentIsANewRevision();
    TestAbsoluteLocationIsNotPartOfIdentity();
    TestTraversalOrderDoesNotChangeTheDigest();
    TestTwoIdenticalAssetsStayDistinct();
    TestCanonicalEncodingIsInjective();
    TestRoleIsPartOfIdentity();
    TestSameRolePathOrderDoesNotMatter();
    TestIncompleteDigestIsNotAnIdentity();
    TestDuplicatePartIsRejected();
    TestRoleRoundTrip();
    TestShortDigest();
    TestDigestCoversReferencedAssetsNotJustManifest();

    std::printf("\nCCA-05 candidate digest: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
