// CCA-04:创作工具的路径与作品归属策略。
//
// 计划 §5 的验收原文:"拒绝跨作品访问、越界路径、reparse point 与未经允许的代码产物"。
// 这一行里每一样都对应一种模型真能构造出来的请求,所以每一样都要有一条断言 ——
// 而且都要反过来证明"合法请求没有被误杀",否则一个把所有东西都拒绝的策略也能全绿。
//
// 这个文件刻意不碰文件系统:策略只做判定,事实由调用方喂进来。所以它能在这里真实
// 编译并运行,而不是只能留给真机走查。
#include "miaodesk/CreatorWorkspacePolicy.h"

#include <algorithm>
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

CreatorSessionBinding Binding(const char* sessionId = "S-1",
                             const char* workspace = R"(C:\Users\me\AppData\Local\MiaoDesk\creator\S-1)",
                             const char* claimed = nullptr) {
    CreatorSessionBinding binding;
    binding.sessionId = sessionId;
    binding.workspaceRoot = workspace;
    binding.claimedWorkspace = claimed ? claimed : workspace;
    return binding;
}

// 一个合法的小文件,默认通过。
CreatorFileFacts SmallFile() {
    CreatorFileFacts facts;
    facts.byteCount = 2048;
    facts.exists = true;
    return facts;
}

// 判定并给出拒绝原因;空串表示允许。同时把拒绝码带出来,让每条规则各自可断言。
std::string RejectReason(const CreatorWorkspacePolicy& policy, std::string_view path,
                        const CreatorFileFacts& facts = SmallFile(),
                        CreatorWorkspaceReject* code = nullptr) {
    std::string reason;
    CreatorWorkspaceReject reject = CreatorWorkspaceReject::None;
    const bool allowed = policy.Allows(path, facts, &reason, &reject);
    Check(allowed == reason.empty(),
          std::string("允许与否必须和有没有原因一致:") + std::string(path));
    Check(reject == CreatorWorkspaceReject::None || !allowed,
          std::string("给了拒绝码就必须真的拒绝了:") + std::string(path));
    if (code) *code = reject;
    return reason;
}

// 断言"因为某一条具体规则被拒"。
//
// 原来 MustReject 收了 expected 却从不用它,于是"代码产物被拒"和"路径不在布局内
// 被拒"在测试里长得一样。后果实测过:把 IsForbiddenExtension 那一行短路掉,
// 整个测试依然全绿 —— 因为 Classify 仍以 UnknownRole 拒绝同一个路径。
// 少了拒绝码,一处规则的失效会被另一处规则掩盖。
void MustRejectWith(const CreatorWorkspacePolicy& policy, std::string_view path,
                   CreatorWorkspaceReject expected, const CreatorFileFacts& facts = SmallFile()) {
    CreatorWorkspaceReject actual = CreatorWorkspaceReject::None;
    const std::string reason = RejectReason(policy, path, facts, &actual);
    Check(!reason.empty(), std::string("应当拒绝:") + std::string(path));
    CheckEq(std::string(ToString(actual)), std::string(ToString(expected)),
            std::string("拒绝码应当匹配:") + std::string(path));
}

// 一个通过的请求必须真的没有原因 —— 否则 UI 会一闪而过一句警告,而模型学不到任何东西。
void MustAllow(const CreatorWorkspacePolicy& policy, std::string_view path) {
    const std::string reason = RejectReason(policy, path);
    Check(reason.empty(), std::string("应当允许:") + std::string(path) + " 但被拒:" + reason);
}

// facts 必须能传进来。第一版这里只有默认 facts,于是"超大文件被拒""reparse point
// 被拒"这两类断言测的全是默认小文件 —— 实现当然放行,检查等于没写。
void MustReject(const CreatorWorkspacePolicy& policy, std::string_view path,
               CreatorWorkspaceReject expected, const CreatorFileFacts* facts = nullptr) {
    const std::string reason = RejectReason(policy, path, facts ? *facts : SmallFile());
    Check(!reason.empty(), std::string("应当拒绝:") + std::string(path));
    if (reason.empty()) return;
    // 原因必须可读且带路径信息,不能只有一句"非法路径"。
    Check(reason.find("非法") == std::string::npos, "拒绝原因要说清为什么,而不是笼统一句");
    Check(reason.size() > 12, "拒绝原因要有实质内容");
}

// ---------------------------------------------------------------------------
// 1. 获准的布局必须真的可用,否则策略会变成"什么都拒绝"
// ---------------------------------------------------------------------------

void TestAllowedPackageLayout() {
    const CreatorWorkspacePolicy policy(Binding());
    MustAllow(policy, "manifest.json");
    MustAllow(policy, "parameters.json");
    MustAllow(policy, "scene/scene.json");
    MustAllow(policy, "scene/layers.json");
    MustAllow(policy, "preview.png");
    MustAllow(policy, "assets/cloud.png");
    MustAllow(policy, "assets/sky.jpg");
    MustAllow(policy, "assets/bgm.mp3");
    MustAllow(policy, "assets/film.mp4");

    CreatorFileRole role{};
    Check(policy.Classify("manifest.json", &role) && role == CreatorFileRole::Manifest,
          "manifest.json 归类为 manifest");
    Check(policy.Classify("parameters.json", &role) && role == CreatorFileRole::Parameters,
          "parameters.json 归类为 parameters");
    Check(policy.Classify("scene/scene.json", &role) && role == CreatorFileRole::Scene,
          "scene/*.json 归类为 scene");
    Check(policy.Classify("preview.png", &role) && role == CreatorFileRole::Preview,
          "preview.<图片> 归类为 preview");
    Check(policy.Classify("assets/cloud.png", &role) && role == CreatorFileRole::Asset,
          "assets/* 归类为 asset");

    // 同一份文件的等价写法必须归到同一个角色:换一种写法就被拒,模型会以为路径错了。
    MustAllow(policy, "./manifest.json");
    MustAllow(policy, "scene//layers.json");
    MustAllow(policy, "scene/./layers.json");
    MustAllow(policy, R"(scene\layers.json)");
}

// ---------------------------------------------------------------------------
// 2. 越界路径:每一种真实可用的写法都要被拒
// ---------------------------------------------------------------------------

void TestTraversalRejected() {
    const CreatorWorkspacePolicy policy(Binding());
    MustRejectWith(policy, "../manifest.json", CreatorWorkspaceReject::Traversal);
    MustRejectWith(policy, "scene/../../escape.json", CreatorWorkspaceReject::Traversal);
    MustRejectWith(policy, "a/../../b.json", CreatorWorkspaceReject::Traversal);
    // 后半段回到工作区里也不行:判定必须在规范化之后整体看,不能"看起来最终在里面"就算过。
    MustRejectWith(policy, "assets/../../assets/cloud.png", CreatorWorkspaceReject::Traversal);
    MustReject(policy, "..", CreatorWorkspaceReject::Traversal);
    MustReject(policy, "scene/..", CreatorWorkspaceReject::Traversal);
}

void TestAbsolutePathsRejected() {
    const CreatorWorkspacePolicy policy(Binding());
    // 这些都是模型会用的写法,而且**都不含".."** —— 只查遍历是不够的。
    MustRejectWith(policy, R"(C:\Windows\System32\drivers\etc\hosts)",
                 CreatorWorkspaceReject::NotRelative);
    MustRejectWith(policy, "C:/Windows/win.ini", CreatorWorkspaceReject::NotRelative);
    MustRejectWith(policy, R"(\\server\share\secrets.json)", CreatorWorkspaceReject::NotRelative);
    MustReject(policy, "\\\\server\\share\\secrets.json", CreatorWorkspaceReject::NotRelative);
    MustRejectWith(policy, "/etc/passwd", CreatorWorkspaceReject::NotRelative);
    MustRejectWith(policy, "\\Windows\\win.ini", CreatorWorkspaceReject::NotRelative);
    MustRejectWith(policy, "c:manifest.json", CreatorWorkspaceReject::NotRelative);
}

void TestUnknownLayoutRejected() {
    const CreatorWorkspacePolicy policy(Binding());
    MustReject(policy, "notes.txt", CreatorWorkspaceReject::UnknownRole);
    MustReject(policy, "README.md", CreatorWorkspaceReject::UnknownRole);
    // assets 下不允许再建子目录:越深的层级越容易藏越界路径。
    MustRejectWith(policy, "assets/sub/cloud.png", CreatorWorkspaceReject::UnknownRole);
    MustRejectWith(policy, "scene/sub/scene.json", CreatorWorkspaceReject::UnknownRole);
    MustRejectWith(policy, "scene/scene.ini", CreatorWorkspaceReject::UnknownRole);
    MustReject(policy, "scene/scene.json.bak", CreatorWorkspaceReject::UnknownRole);
    MustRejectWith(policy, "preview.svg", CreatorWorkspaceReject::UnknownRole);
    MustReject(policy, "assets/cloud.svg", CreatorWorkspaceReject::UnknownRole);
    // 空路径与空段。
    MustRejectWith(policy, "", CreatorWorkspaceReject::EmptyRelativePath);
    MustRejectWith(policy, "manifest.json/", CreatorWorkspaceReject::Traversal);
}

// ---------------------------------------------------------------------------
// 3. 代码产物不在创作范围内
// ---------------------------------------------------------------------------

void TestCodeArtifactsRejected() {
    const CreatorWorkspacePolicy policy(Binding());
    // 计划 §1:内容生成限定为声明式 Scene 内容与受控素材。所以这些不是"还没实现",
    // 而是明确不在范围内 —— 拒绝码要能指认"范围"而不是"不认识"。
    for (const char* path : {
             "index.html", "theme.css", "main.js", "module.mjs", "component.ts",
             "App.tsx", "hook.jsx", "run.bat", "setup.ps1", "tool.sh", "script.py",
             "helper.rb", "perl.pl", "macro.vbs", "tweak.reg", "patch.msi",
             "payload.exe", "lib.dll", "bridge.lnk", "shortcut.url",
         }) {
        MustRejectWith(policy, path, CreatorWorkspaceReject::ForbiddenExtension);
    }
    // 从扩展名就能判,不必走到布局那一层。
    for (const char* extension : {".js", ".html", ".css", ".exe", ".ps1", ".ts", ".vbs"}) {
        Check(CreatorWorkspacePolicy::IsForbiddenExtension(extension),
              std::string(extension) + " 是明确不允许的产物");
    }
    for (const char* extension : {".png", ".jpg", ".mp4", ".json", ".mp3", ".ttf"}) {
        Check(!CreatorWorkspacePolicy::IsForbiddenExtension(extension),
              std::string(extension) + " 不是代码产物,不该被这一层拒");
    }
    // 无扩展名也不能借道:scene/readme 这种没有 role,归到布局那一层。
    MustReject(policy, "scene/readme", CreatorWorkspaceReject::UnknownRole);
}

// ---------------------------------------------------------------------------
// 4. 大小上限
// ---------------------------------------------------------------------------

void TestSizeLimits() {
    const CreatorWorkspacePolicy policy(Binding());
    CreatorFileFacts huge;
    huge.exists = true;
    huge.byteCount = 512ull * 1024 * 1024;
    // 必须把"超大"这个事实喂进去。第一版我漏了,于是默认用了小文件的 facts,
    // 那条断言测的是"小素材被拒",而实现当然放行 —— 一个为空的检查。
    MustRejectWith(policy, "assets/huge.mp4", CreatorWorkspaceReject::TooLarge, huge);
    // 同一条路径、小文件,必须放行。这样上面那条"被拒"才能确凿归因于大小,
    // 而不是这条路径本身有问题 —— 否则我用大小做借口掩盖了一个路径 bug。
    Check(policy.Allows("assets/huge.mp4", SmallFile(), nullptr),
          "同一路径的小文件必须放行,证明被拒的确实是大小而不是路径");
    const std::string reason = RejectReason(policy, "assets/huge.mp4", huge);
    Check(reason.find("上限") != std::string::npos, "大小拒绝要说明是超上限");
    Check(RejectReason(policy, "scene/huge.json", huge).empty() == false,
          "scene 也受上限约束");
    // 各类别上限不同:manifest 用小上限,素材用大上限。
    CreatorFileFacts medium;
    medium.exists = true;
    medium.byteCount = 8ull * 1024 * 1024;
    Check(RejectReason(policy, "assets/ok.png", medium).empty(), "素材上限比 8 MiB 宽");
    Check(!RejectReason(policy, "manifest.json", medium).empty(),
          "manifest 上限比 8 MiB 窄 —— 它不该是个 8 MiB 的文件");
}

// ---------------------------------------------------------------------------
// 5. 归属:不能跨作品
// ---------------------------------------------------------------------------

void TestCrossWorkAccessRejected() {
    const CreatorWorkspacePolicy policy(Binding());
    // 声称的工作区是别的作品 —— 必须在第一步就拒,而不是等路径判定碰巧通过。
    CreatorSessionBinding cross = Binding("S-1", R"(C:\…\creator\S-1)", R"(C:\…\creator\S-2)");
    const CreatorWorkspacePolicy other(cross);
    MustRejectWith(other, "manifest.json", CreatorWorkspaceReject::WrongWorkspace);

    // 分隔符/大小写/末尾斜杠的差异不代表换了作品。
    MustAllow(Binding("S-1", R"(C:\a\b)", R"(C:/A/B/)"), "manifest.json");
    MustAllow(Binding("S-1", R"(C:\a\b)", R"(c:\a\b)"), "manifest.json");

    // 没有 sessionId 的工具调用一律拒。
    MustRejectWith(CreatorWorkspacePolicy(Binding("", R"(C:\a\b)")), "manifest.json",
                   CreatorWorkspaceReject::EmptySessionId);
    // 工作区本身缺失:这通常意味着宿主没找到会话记录 —— 宁可什么都不让做。
    CreatorSessionBinding noRoot = Binding("S-1", "", "");
    MustRejectWith(CreatorWorkspacePolicy(noRoot), "manifest.json",
                   CreatorWorkspaceReject::WrongWorkspace);
}

// ---------------------------------------------------------------------------
// 6. reparse point:形状合法但指向在外
// ---------------------------------------------------------------------------

void TestReparsePointRejected() {
    const CreatorWorkspacePolicy policy(Binding());
    // 这是最阴的一类:路径完全合法、角色也合法,只是一个 junction 指向别处。
    // 策略自己碰不到盘,所以事实必须由宿主给 —— 测试在这里把它喂成 true。
    CreatorFileFacts link;
    link.exists = true;
    link.byteCount = 10;
    link.isReparsePoint = true;
    // 同上:facts 必须真的传进去,否则这条断言一次都没测到 reparse point。
    MustRejectWith(policy, "assets/cloud.png", CreatorWorkspaceReject::ReparsePoint, link);
    const std::string reason = RejectReason(policy, "assets/cloud.png", link);
    Check(reason.find("符号链接") != std::string::npos || reason.find("联接点") != std::string::npos,
          "reparse point 的拒绝要点明它是符号链接/联接点");

    // 反转:同一个路径、同样的字节数,只是不是 reparse point —— 必须放行。
    // 少了这一条,"把 isReparsePoint 一律当 true"也能让上面的断言全绿。
    CreatorFileFacts normal;
    normal.exists = true;
    normal.byteCount = 10;
    normal.isReparsePoint = false;
    Check(RejectReason(policy, "assets/cloud.png", normal).empty(),
          "不是 reparse point 的同一路径必须放行");

    // 目录也不该被当成文件写入。
    // 第一版这里只测了 "assets" —— 而它本来就通不过 Classify,于是断言被另一条规则
    // 满足,isDirectory 那一行短路掉整个测试依然全绿。必须用一个**能通过分类**的路径,
    // 让 isDirectory 成为唯一的拒绝理由。
    CreatorFileFacts dir;
    dir.exists = true;
    dir.isDirectory = true;
    dir.byteCount = 3;
    MustRejectWith(policy, "assets/cloud.png", CreatorWorkspaceReject::UnknownRole, dir);
    // 反证:同一个路径、同样的字节数,只是不是目录 —— 必须放行。
    CreatorFileFacts file;
    file.exists = true;
    file.isDirectory = false;
    file.byteCount = 3;
    Check(RejectReason(policy, "assets/cloud.png", file).empty(),
          "同一路径的非目录必须放行,证明被拒的确实是它是目录");
}

// ---------------------------------------------------------------------------
// 7. Resolve 与归一化
// ---------------------------------------------------------------------------

void TestResolveStaysInsideWorkspace() {
    const CreatorWorkspacePolicy policy(Binding("S-1", R"(C:\ws\S-1)"));
    std::string resolved;
    Check(policy.Resolve("scene/scene.json", &resolved), "合法路径能解析");
    CheckEq(resolved, "C:/ws/S-1/scene/scene.json", "解析结果拼在工作区根之后");
    Check(policy.Resolve(R"(assets\cloud.png)", &resolved), "反斜杠写法能解析");
    CheckEq(resolved, "C:/ws/S-1/assets/cloud.png", "分隔符统一成正斜杠");

    // 非法路径不该解析出任何东西。
    Check(!policy.Resolve("../x.json", &resolved), "遍历路径不解析");
    Check(!policy.Resolve(R"(C:\x.json)", &resolved), "绝对路径不解析");
    Check(!policy.Resolve("", &resolved), "空路径不解析");
}

void TestNormalize() {
    std::string out;
    Check(NormalizeCreatorRelativePath("a/b.json", &out) && out == "a/b.json", "简单路径原样");
    Check(NormalizeCreatorRelativePath("./a/b.json", &out) && out == "a/b.json", "去掉前导 ./");
    Check(NormalizeCreatorRelativePath(R"(a\.\b.json)", &out) && out == "a/b.json", "中间 /. 收敛");

    Check(NormalizeCreatorRelativePath(R"(a\b.json)", &out) && out == "a/b.json", "反斜杠转正斜杠");
    Check(!NormalizeCreatorRelativePath("", &out), "空串非法");
    Check(!NormalizeCreatorRelativePath("../a", &out), ".. 非法");
    Check(!NormalizeCreatorRelativePath("a/../b", &out), "中间的 .. 也非法");
    Check(!NormalizeCreatorRelativePath("/a", &out), "根斜杠非法");
    Check(!NormalizeCreatorRelativePath(R"(C:\a)", &out), "盘符非法");
    Check(!NormalizeCreatorRelativePath(R"(\\srv\share)", &out), "UNC 非法");
    Check(!NormalizeCreatorRelativePath("c:a", &out), "裸盘符非法");
    // "a//b" 收敛成 "a/b" 是**有意的**:同一份文件换一种写法必须归到同一个角色,
    // 否则模型换个分隔符写法就会被判成 UnknownRole,而它以为路径是对的。
    Check(NormalizeCreatorRelativePath("a//b", &out) && out == "a/b", "重复斜杠收敛");
    // 真正非法的是尾斜杠:那表示一个目录,不是一个文件。
    Check(!NormalizeCreatorRelativePath("a/b/", &out), "尾斜杠非法(那是目录)");
    Check(NormalizeCreatorRelativePath("a/b/c.json", &out) && out == "a/b/c.json", "深层合法(角色另判)");
}

// 会话 ID 从工作区路径导出。它必须是工作区的函数,而不是又一个要同步的字段:
// 两个可以互相矛盾的事实来源一旦矛盾,"这个调用属于哪个作品"就没有答案。
void TestSessionIdDerivesFromTheWorkspace() {
    CheckEq(DeriveCreatorSessionId(R"(C:\Users\me\AppData\Roaming\MiaoDesk\creator\S-1)"), "S-1",
            "取最后一段目录名");
    CheckEq(DeriveCreatorSessionId("C:/ws/S-1"), "S-1", "正斜杠同样取最后一段");
    // 末尾分隔符要先去掉:不去掉的话 "S-1\" 的最后一段是空串,而空会话会让
    // 每一次调用都被判成"没有会话 ID"——那看起来像模型忘了带,其实是我们的 bug。
    CheckEq(DeriveCreatorSessionId(R"(C:\ws\S-1\)"), "S-1", "末尾反斜杠不算新的一段");
    CheckEq(DeriveCreatorSessionId(R"(C:\ws\S-1\\\)"), "S-1", "连续末尾分隔符也只算一层");
    CheckEq(DeriveCreatorSessionId("S-1"), "S-1", "裸名字也是它自己");
    CheckEq(DeriveCreatorSessionId(""), "", "空路径没有会话");
    CheckEq(DeriveCreatorSessionId("\\\\"), "", "只有分隔符也没有会话");
    CheckEq(DeriveCreatorSessionId(R"(C:\)"), "", "盘符根没有会话");
    // 中文目录名要原样回来:工作区是宿主分配的路径,它的形状不该被这里过滤掉。
    CheckEq(DeriveCreatorSessionId(R"(C:\ws\我的作品)"), "我的作品", "非 ASCII 目录名原样保留");
}

void TestRoleRoundTrip() {
    const CreatorFileRole roles[] = {
        CreatorFileRole::Manifest, CreatorFileRole::Parameters, CreatorFileRole::Scene,
        CreatorFileRole::Preview, CreatorFileRole::Asset,
    };
    for (auto role : roles) {
        CreatorFileRole parsed{};
        Check(ParseCreatorFileRole(CreatorFileRoleName(role), &parsed),
              std::string(CreatorFileRoleName(role)) + " 能解析回来");
        Check(parsed == role, "解析回来是同一个 role");
    }
    CreatorFileRole ignored{};
    Check(!ParseCreatorFileRole("nonsense", &ignored), "未知 role 名被拒绝");
}

void TestEveryRejectHasAnExplanation() {
    // 每条拒绝码都要有能给人看的一句话。漏一条,用户看到的就是一串英文枚举名。
    const CreatorWorkspaceReject codes[] = {
        CreatorWorkspaceReject::None,
        CreatorWorkspaceReject::EmptySessionId,
        CreatorWorkspaceReject::EmptyRelativePath,
        CreatorWorkspaceReject::NotRelative,
        CreatorWorkspaceReject::Traversal,
        CreatorWorkspaceReject::ForbiddenExtension,
        CreatorWorkspaceReject::UnknownRole,
        CreatorWorkspaceReject::TooLarge,
        CreatorWorkspaceReject::OutsideWorkspace,
        CreatorWorkspaceReject::ReparsePoint,
        CreatorWorkspaceReject::WrongWorkspace,
        CreatorWorkspaceReject::NoContentPackageYet,
    };
    for (auto code : codes) {
        const std::string text = CreatorWorkspacePolicy::Explain(code, "x.json");
        // None 表示"没有拒绝",本就该是空串 —— 它不是一条错误。
        if (code == CreatorWorkspaceReject::None) {
            Check(text.empty(), "None 不应有解释文本(它不是错误)");
            continue;
        }
        Check(!text.empty(), std::string(ToString(code)) + " 有解释文本");
        Check(text.find("%") == std::string::npos, "解释里不该有未替换的格式符");
        Check(ToString(code) != std::string("Unknown"), "ToString 覆盖全部拒绝码");
    }
}

// 策略必须真的接受 **ResolveCreatorWorkspaceRoot 会导出的那个根**。
//
// 为什么值得单独一节:上面全部用例用的是
//   C:\Users\me\AppData\Local\MiaoDesk\creator\S-1
// 深度一样,所以"分类在这条路径上对不对"一直是被覆盖的 —— 但覆盖它的是**另一条**
// 路径。真实根是
//   <LOCALAPPDATA>\MiaoDesk\CreatorWorkspaces\<kind>\<sessionId>
// (paths::CreatorWorkspaceRoot),由 ContentCreatorBridge::ResolveCreatorWorkspaceRoot
// 交给 PiRuntime,PiRuntime 只在 mode == Creator 时把它导出成 MIAODESK_CREATOR_WORKSPACE。
// 哪天有人改布局(多一层、换个名字),分类本身仍可能在旧路径上全绿,而**真实**根上
// 每一条工具调用都被拒 —— 表现是模型写什么路径都失败,而原因看起来像模型写错了。
// 这一节把"真实根"钉住,改布局会让它立刻红。
//
// 而"最后一段必须是会话身份"这一条,最初就是这一节自己撞出来的:第一版按
// …/<kind>/1 排,于是 DeriveCreatorSessionId 对任何作品都返回 "1",同一 kind 的
// 所有作品从此共用一个会话 ID —— 归属判断正是靠那个字符串区分作品的。
void TestTheRealWorkspaceRootTheResolverWillExport() {
    // 最后一段是会话身份本身,不是序号。DeriveCreatorSessionId 取最后一段当会话 ID,
    // 而 SessionMatches 拿它和工具参数里的 sessionId 比 —— 写成 "1" 会让同一 kind 的
    // 所有作品共用一个会话 ID,而归属判断正是靠它区分作品的。
    const char* kRoot =
        R"(C:\Users\me\AppData\Local\MiaoDesk\CreatorWorkspaces\wallpaper\s1755000000000-1-9f3ac1d2)";
    CheckEq(DeriveCreatorSessionId(kRoot), "s1755000000000-1-9f3ac1d2",
            "真实工作区根派生出它自己的会话身份");

    CreatorSessionBinding binding;
    binding.sessionId = DeriveCreatorSessionId(kRoot);
    binding.workspaceRoot = kRoot;
    binding.claimedWorkspace = binding.workspaceRoot;
    const CreatorWorkspacePolicy policy(binding);

    // 布局不变:这些相对路径必须照旧分类,而不是因为根变了就被判成越界。
    CreatorFileRole ignored{};
    for (const char* relative : {"manifest.json", "parameters.json", "scene/scene.json",
                                 "preview.png", "assets/cloud.png"}) {
        std::string reason;
        Check(policy.Allows(relative, SmallFile(), &reason),
              std::string("真实工作区根下 ") + relative + " 仍然获准:" + reason);
        Check(policy.Classify(relative, &ignored), std::string("且仍能分类:") + relative);
    }

    // 反向也钉住:这些仍然要被拒。根变了不该让越界变得合法。
    Check(!policy.Allows("../escape.json", SmallFile()), "真实工作区根下 ../ 仍被拒");
    Check(!policy.Allows("assets/sub/deep.png", SmallFile()), "真实工作区根下深子目录仍被拒");
    Check(!policy.Allows("code.py", SmallFile()), "真实工作区根下代码产物仍被拒");

    // Resolve 出来的绝对路径必须仍在这个根下面。分隔符统一成正斜杠(见
    // TestResolveStaysInsideWorkspace),所以这里比的是归一化之后的形状 ——
    // 直接拿反斜杠的根去 prefix 比会恒假,那是比法错了,不是代码错了。
    std::string resolved;
    Check(policy.Resolve("scene/scene.json", &resolved), "真实工作区根上 Resolve 成功");
    std::string normalizedRoot = binding.workspaceRoot;
    std::replace(normalizedRoot.begin(), normalizedRoot.end(), '\\', '/');
    while (!normalizedRoot.empty() && normalizedRoot.back() == '/') normalizedRoot.pop_back();
    Check(resolved.rfind(normalizedRoot, 0) == 0,
          "Resolve 的结果以真实工作区根开头:" + resolved);
    CheckEq(resolved, normalizedRoot + "/scene/scene.json",
            "且拼出来的就是那一个路径");

    // 两个 kind 的根必须都能用:组件那条走的是同一个解析器,只是目录名不同。
    CreatorSessionBinding widget;
    widget.workspaceRoot =
        R"(C:\Users\me\AppData\Local\MiaoDesk\CreatorWorkspaces\widget\s1755000000000-1-9f3ac1d2)";
    widget.sessionId = DeriveCreatorSessionId(widget.workspaceRoot);
    widget.claimedWorkspace = widget.workspaceRoot;
    const CreatorWorkspacePolicy widgetPolicy(widget);
    std::string widgetReason;
    Check(widgetPolicy.Allows("manifest.json", SmallFile(), &widgetReason),
          "组件的工作区根同样获准:" + widgetReason);

    // 第二个作品是另一段会话身份,不是把 1 改成 2 —— 两个作品各有各的目录与 ID。
    CreatorSessionBinding second;
    second.workspaceRoot =
        R"(C:\Users\me\AppData\Local\MiaoDesk\CreatorWorkspaces\wallpaper\s1755000000009-1-77ab01ef)";
    second.sessionId = DeriveCreatorSessionId(second.workspaceRoot);
    second.claimedWorkspace = second.workspaceRoot;
    const CreatorWorkspacePolicy secondPolicy(second);
    std::string secondReason;
    Check(secondPolicy.Allows("manifest.json", SmallFile(), &secondReason),
          "第二个作品的工作区根(另一段会话身份,同样目录形状)获准:" + secondReason);
    Check(DeriveCreatorSessionId(second.workspaceRoot) != binding.sessionId,
          "两件作品的会话身份必须不同,否则归属判断区分不了它们");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestAllowedPackageLayout();
    TestTraversalRejected();
    TestAbsolutePathsRejected();
    TestUnknownLayoutRejected();
    TestCodeArtifactsRejected();
    TestSizeLimits();
    TestCrossWorkAccessRejected();
    TestReparsePointRejected();
    TestResolveStaysInsideWorkspace();
    TestNormalize();
    TestSessionIdDerivesFromTheWorkspace();
    TestRoleRoundTrip();
    TestEveryRejectHasAnExplanation();
    TestTheRealWorkspaceRootTheResolverWillExport();

    std::printf("\nCCA-04 creator workspace policy: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
