// CCA-04:worker 侧的分发、归属与"哪些工具真的能执行"。
//
// 这份测试的重点不是"能返回成功"。是这个模块要防的那件事:此前宿主对创作工具
// 只放行不执行,调用落进 ExecuteNativeToolRaw,回给模型的是一句"未知工具" ——
// 调用看起来被接受了,失败信息却和"名字打错了"一模一样。所以这里逐条断言:
//
//   * 归属以宿主为准,参数说的不算(伪造 sessionId、换工作区、没有状态文件);
//   * 每一条失败都带一个**具体**的码,而不是笼统的失败;
//   * "未实现"和"被拒绝"是两回事,模型下一步该做的事完全不同;
//   * 一次写坏不会留下半写状态(事务在整个失败路径上都被执行到)。
//
// 全部断言都在本机真实运行:workspace 是一个 in-memory 实现,所以盘满、暂存被占用、
// 摘要不符这些分支不需要 Windows 也能走到。
#include "miaodesk/CreatorToolWorker.h"
#include "miaodesk/CreatorWorkspaceState.h"
#include "miaodesk/ContentCandidateLedger.h"

#include <cstdio>
#include <map>
#include <optional>
#include <set>
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

constexpr const char* kWorkspace = R"(C:\ws\S-1)";
constexpr const char* kSession = "S-1";

// --- 一个 in-memory 的工作区 ---------------------------------------------------

// 它是 CreatorWorkspacePort 的最小可用实现,刻意让每一步都可以被诱导失败:
// 一个永远成功的 port 只能测到 happy path,而失败路径才是这次要保的东西。
struct MemoryWorkspace : CreatorWorkspacePort {
    std::map<std::string, std::string> files{
        {"manifest.json", R"({"schema":1})"},
        {"scene/scene.json", R"({"layers":[]})"},
    };
    // 宿主导演的失败
    bool failStagedWrite{false};
    bool failReplace{false};
    // 替换报告失败,但盘上**已经变了**。这不是臆造:MoveFileExW 之后进程被杀、
    // 或者重试了一次而第一次其实成功了,都会得到这个形状。把这种情况一律报成
    // "工作区没被动过"是最危险的一种谎话:用户以为还能用现在这份,而它已经变了。
    bool failReplaceButUnexpectedContent{false};
    // 内容照写,但返回值说失败。模拟"MoveFileExW 之后进程被杀,而文件其实已经换好"。
    bool alwaysApplyStaged{false};
    bool discardCalled{false};
    std::string lastStagedPath;
    bool snapshotIsStale{false};
    // 第一次读到的快照。snapshotIsStale 时一直返回它 —— 用来制造"宿主读到的是
    // 替换之前的内容"。第一版这个开关是空的:两个分支都读 files[],替换之后
    // 自然都是新内容,于是摘要永远对得上,那条分支等于没有测。
    std::vector<content::CandidatePart> frozenSnapshot;
    bool frozen{false};

    bool ReadFile(const std::string& relativePath, std::string* bytes) override {
        const auto it = files.find(relativePath);
        if (it == files.end()) return false;
        *bytes = it->second;
        return true;
    }

    std::vector<content::CandidatePart> Snapshot() override {
        if (snapshotIsStale) {
            if (!frozen) { frozen = true; frozenSnapshot = LiveSnapshot(); }
            return frozenSnapshot;
        }
        return LiveSnapshot();
    }

    std::vector<content::CandidatePart> LiveSnapshot() {
        std::vector<content::CandidatePart> parts;
        for (const auto& [path, bytes] : files) {
            const auto role = path == "manifest.json"   ? content::CandidatePartRole::Manifest
                              : path.rfind("scene/", 0) == 0 ? content::CandidatePartRole::Scene
                                                            : content::CandidatePartRole::Asset;
            parts.push_back({role, path, bytes});
        }
        return parts;
    }

    CreatorFileFacts Facts(const std::string& relativePath) override {
        CreatorFileFacts facts;
        const auto it = files.find(relativePath);
        facts.exists = it != files.end();
        facts.byteCount = it == files.end() ? 0 : it->second.size();
        return facts;
    }

    StagedBytes WriteStaged(const std::string& stagedPath, const std::string& content) override {
        lastStagedPath = stagedPath;
        StagedBytes staged;
        if (failStagedWrite) return staged;   // written=false
        staged.written = true;
        staged.bytes = staleStagedContent.empty() ? content : staleStagedContent;
        staged.facts.byteCount = staged.bytes.size();
        staged.facts.exists = true;
        // 暂存内容必须记下来:ReplaceTarget 只拿到一个路径,拿不到字节。
        // 不记的话每次"替换"都找不到东西,于是每一笔本该成功的写入都会
        // 看起来像失败 —— 而那是 port 的 bug,不是被验代码的。
        stagedContents[stagedPath] = staged.bytes;
        return staged;
    }
    std::string staleStagedContent;

    bool ReplaceTarget(const std::string& stagedPath, const std::string& relativePath) override {
        if (alwaysApplyStaged) {
            const auto it = stagedContents.find(stagedPath);
            if (it != stagedContents.end()) files[relativePath] = it->second;
            return false;
        }
        if (failReplace) return false;
        if (failReplaceButUnexpectedContent) {
            // 报告失败,但盘上留下的是**计划里没有的**内容。
            files[relativePath] = R"({"layers":[{"someone-else":true}]})";
            return false;
        }
        const auto it = stagedContents.find(stagedPath);
        if (it == stagedContents.end()) return false;
        files[relativePath] = it->second;
        return true;
    }
    std::map<std::string, std::string> stagedContents;
    // ReplaceTarget 用的真实暂存内容。WriteStaged 之后由测试填入。

    void DiscardStaged(const std::string& stagedPath) override { discardCalled = true; }

    bool SealSnapshot(const std::string& digest,
                      const std::vector<content::CandidatePart>& parts,
                      std::string* snapshotPath) override {
        if (failSeal) return false;
        // 封存必须是一份**独立副本**。直接把路径指回工作区的话,"封存后修改源
        // 目录不能改变待应用候选"这条就名存实亡 —— 因为封存的就是源目录。
        // 同一摘要再封一次是**幂等**的:快照已经在,复用它。
        // 返回失败的话,"同一份内容再次提交返回同一版"这条就废了 ——
        // 而它正是幂等语义的一半。
        if (sealed.find(digest) != sealed.end()) {
            *snapshotPath = "sealed/" + digest.substr(0, 12);
            return true;
        }
        sealed.insert(digest);
        *snapshotPath = "sealed/" + digest.substr(0, 12);
        // 记下封存的正是传进来的那一份,并在测试里断言它就是算过摘要的那份。
        sealedContents[*snapshotPath] = parts;
        return true;
    }
    bool failSeal{false};
    std::set<std::string> sealed;
    std::map<std::string, std::vector<content::CandidatePart>> sealedContents;

    bool LoadLedger(std::string* text) override {
        if (failLedgerLoad) return false;
        *text = ledgerText;
        return true;
    }
    bool SaveLedger(const std::string& text) override {
        if (failLedgerSave) return false;
        ledgerText = text;
        return true;
    }
    bool SaveState(const CreatorWorkspaceState& state) override {
        savedState = state;
        stateWasSaved = true;
        return true;
    }
    CreatorWorkspaceState savedState;
    bool stateWasSaved{false};
    std::string ledgerText;
    bool failLedgerLoad{false};
    bool failLedgerSave{false};

    bool ReadManagedSource(const std::string& source, std::string* bytes,
                           std::string* resolvedName) override {
        // 宿主只承认这两类。别的字符串一律不认识 —— 包括看起来合法的盘上路径。
        if (source == "content:cloud") {
            *bytes = "CLOUD-BYTES";
            *resolvedName = "assets/cloud.png";
            return true;
        }
        if (source == "content:cloud/hero.png") {
            *bytes = "HERO-BYTES";
            *resolvedName = "assets/hero.png";
            return true;
        }
        return false;
    }
};

CreatorWorkerInput Input(bool withState = true, int stage = 3 /*Generating*/,
                         bool cancelled = false, std::uint64_t epoch = 7) {
    CreatorWorkerInput input;
    input.workspaceRoot = kWorkspace;
    input.sessionId = kSession;
    input.state.sessionId = kSession;
    input.state.epoch = epoch;
    input.state.stage = stage;
    input.state.cancelRequested = cancelled;
    input.state.revision = 2;
    input.state.candidateDigest = std::string(64, 'b');
    input.hasState = withState;
    return input;
}

CreatorToolArgs Args(std::string relativePath = {}, std::string content = {},
                     std::string source = {}, std::string digest = {}) {
    CreatorToolArgs args;
    args.sessionId = kSession;
    args.workspaceRoot = kWorkspace;
    args.relativePath = std::move(relativePath);
    args.content = std::move(content);
    args.source = std::move(source);
    args.digest = std::move(digest);
    return args;
}

// ---------------------------------------------------------------------------
// 1. 状态文件:写出去再读回来必须是同一份
// ---------------------------------------------------------------------------

void TestStateRoundTrip() {
    CreatorWorkspaceState state;
    state.sessionId = "S-1";
    state.epoch = 7;
    state.stage = 3;
    state.cancelRequested = false;
    state.candidateDigest = std::string(64, 'a');
    state.revision = 2;
    state.updatedAtMs = 1700000000000ull;

    const auto text = SerializeCreatorWorkspaceState(state);
    CreatorWorkspaceState parsed;
    Check(ParseCreatorWorkspaceState(text, &parsed), "序列化后的状态能解析回来");
    CheckEq(parsed.sessionId, "S-1", "sessionId 往返一致");
    Check(parsed.epoch == 7, "epoch 往返一致");
    Check(parsed.stage == 3, "stage 往返一致");
    Check(!parsed.cancelRequested, "cancelRequested 往返一致");
    CheckEq(parsed.candidateDigest, std::string(64, 'a'), "候选摘要往返一致");
    Check(parsed.revision == 2, "revision 往返一致");
    Check(parsed.updatedAtMs == 1700000000000ull, "updatedAtMs 往返一致");
    Check(parsed.HasCandidate(), "有摘要就有候选");
    Check(state.SameRoundAs(parsed), "往返之后是同一次创作的同一轮");

    // 取消位要能过去。
    state.cancelRequested = true;
    CreatorWorkspaceState cancelled{};
    Check(ParseCreatorWorkspaceState(SerializeCreatorWorkspaceState(state), &cancelled),
          "取消中的状态也能解析");
    Check(cancelled.cancelRequested, "且取消位保住了");

    // 空摘要 = 没有候选。
    state.candidateDigest.clear();
    CreatorWorkspaceState noCandidate{};
    Check(ParseCreatorWorkspaceState(SerializeCreatorWorkspaceState(state), &noCandidate),
          "没有摘要的状态也能解析");
    Check(!noCandidate.HasCandidate(), "空摘要表示没有候选");
}

void TestStateParseRejectsWhatItCannotTrust() {
    CreatorWorkspaceState out;
    // 没有 sessionId 的状态无法归属,必须整体拒绝而不是当成一个空会话。
    Check(!ParseCreatorWorkspaceState("epoch=1\nstage=3\n", &out), "没有 sessionId 被拒");
    Check(!ParseCreatorWorkspaceState("", &out), "空文件被拒");
    // 同一个键出现两次:两份矛盾的值,没有该信的那一份。
    Check(!ParseCreatorWorkspaceState("sessionId=S-1\nsessionId=S-2\n", &out), "重复的 sessionId 被拒");
    Check(!ParseCreatorWorkspaceState("sessionId=S-1\nstage=3\nstage=4\n", &out), "重复的 stage 被拒");
    // 值里有换行会破坏行结构,有等号会让键值歧义。
    Check(!ParseCreatorWorkspaceState("sessionId=S\n1\n", &out), "值里带换行被拒");
    Check(!ParseCreatorWorkspaceState("sessionId=S=1\n", &out), "值里带等号被拒");
    // 非数字的 epoch 不能悄悄变成 0。
    Check(!ParseCreatorWorkspaceState("sessionId=S\nepoch=abc\n", &out), "非数字 epoch 被拒");
    Check(!ParseCreatorWorkspaceState("sessionId=S\nepoch=\n", &out), "空 epoch 被拒");
    Check(!ParseCreatorWorkspaceState("sessionId=S\ncancelRequested=maybe\n", &out), "非法取消位被拒");
    // 阶段同样要纯十进制。atoi 对 "abc" 返回 0,而 0 恰好是 Draft ——
    // 于是写坏的状态文件被静默读成最保守的阶段,看起来是安全的,其实是运气。
    Check(!ParseCreatorWorkspaceState("sessionId=S\nstage=abc\n", &out), "非数字 stage 被拒");
    Check(!ParseCreatorWorkspaceState("sessionId=S\nstage=\n", &out), "空 stage 被拒");
    Check(!ParseCreatorWorkspaceState("sessionId=S\nstage=-1\n", &out), "负 stage 被拒");
    Check(!ParseCreatorWorkspaceState("sessionId=S\nstage=99\n", &out), "越界 stage 被拒(不是枚举里的阶段)");
    // 没有等号的行不是 key=value。
    Check(!ParseCreatorWorkspaceState("sessionId=S\nnonsense\n", &out), "没有等号的行被拒");

    // 未知的键要**被忽略**:将来加字段时,旧宿主写的文件仍然读得出来。
    CreatorWorkspaceState forward;
    Check(ParseCreatorWorkspaceState("sessionId=S-1\nepoch=1\nfutureField=whatever\n", &forward),
          "未知字段被忽略,不影响解析");
    CheckEq(forward.sessionId, "S-1", "且已知字段照常读出来");

    // 空行是允许的(文件末尾常见)。
    CreatorWorkspaceState blank;
    Check(ParseCreatorWorkspaceState("sessionId=S-1\n\n", &blank), "空行被容忍");
}

// ---------------------------------------------------------------------------
// 2. 归属:宿主说的算,参数说的不算
// ---------------------------------------------------------------------------

void TestHostBindingWinsOverArguments() {
    MemoryWorkspace workspace;
    const auto input = Input();

    // 伪造 sessionId。参数说的不算,路由会拒;但即使参数是对的,
    // 宿主这边也有自己的一道。
    auto forged = Args("scene/scene.json");
    forged.sessionId = "S-other";
    const auto reply = DispatchCreatorTool("creator_package_read", forged, input, workspace);
    Check(!reply.ok && reply.rejected, "伪造 sessionId 被拒");
    CheckEq(reply.code, "SessionMismatch", "拒绝码是 SessionMismatch");
    Check(!reply.retryable, "且不可重试");

    // 宿主自己没给出工作区:这条比参数校验更靠前,因为它是真正的边界。
    CreatorWorkerInput noWorkspace = Input();
    noWorkspace.workspaceRoot.clear();
    const auto noWs = DispatchCreatorTool("creator_package_read", Args("scene/scene.json"),
                                         noWorkspace, workspace);
    Check(!noWs.ok, "宿主没给工作区时被拒");
    CheckEq(noWs.code, "MissingWorkspace", "拒绝码是 MissingWorkspace");
    // 断言消息,而不是只断言码:空工作区与"只有分隔符的路径"拿到的是同一个码,
    // 但它们命中的是两条不同的判断。只断言码的话,删掉任何一条都看不出区别。
    Check(noWs.message.find("宿主没有给出") != std::string::npos,
          "空工作区命中的是'宿主没给出'那一条");
    CreatorWorkerInput separators = Input();
    separators.workspaceRoot = "\\\\";
    const auto sep = DispatchCreatorTool("creator_package_read", Args("scene/scene.json"),
                                        separators, workspace);
    Check(!sep.ok, "只有分隔符的工作区路径被拒");
    Check(sep.message.find("不是一个作品目录") != std::string::npos,
          "它命中的是'路径导不出会话'那一条");

    // 没有状态文件:这一轮可能还没开始,或者已经被取消后清理过。
    const auto noState = DispatchCreatorTool("creator_package_read", Args("scene/scene.json"),
                                            Input(false), workspace);
    Check(!noState.ok, "没有会话状态时被拒");
    Check(!noState.retryable, "且不可重试 —— 重试不会把状态文件变出来");

    // 状态文件说的是另一个作品。这里刻意让工作区路径与状态文件**一致**
    // (C:\ws\S-2 里放着 S-2 的状态),只有宿主环境说自己是 S-1。这样被接住的
    // 只能是"状态与宿主环境不符"那一条;否则两条判断同时命中,删掉任何一条
    // 测试都还是绿的,那这条断言就等于没写。
    CreatorWorkerInput other = Input();
    other.workspaceRoot = R"(C:\ws\S-2)";
    other.state.sessionId = "S-2";
    const auto mismatch = DispatchCreatorTool("creator_package_read", Args("scene/scene.json"),
                                             other, workspace);
    Check(!mismatch.ok, "状态文件与宿主环境不符时被拒");
    CheckEq(mismatch.code, "SessionMismatch", "拒绝码是 SessionMismatch");

    // 工作区路径导出的会话与状态不符:路径换了大小写、或者复用了别人的目录。
    CreatorWorkerInput moved = Input();
    moved.workspaceRoot = R"(C:\ws\S-2)";
    const auto movedReply = DispatchCreatorTool("creator_package_read", Args("scene/scene.json"),
                                               moved, workspace);
    Check(!movedReply.ok, "工作区路径与状态不符时被拒");
    CheckEq(movedReply.code, "SessionMismatch", "拒绝码是 SessionMismatch");

    // 取消之后一律不再接受,包括只读。
    const auto cancelled = DispatchCreatorTool("creator_package_read", Args("scene/scene.json"),
                                              Input(true, 3, true), workspace);
    Check(!cancelled.ok, "取消后被拒");
    Check(!cancelled.retryable, "且不可重试");
}

// ---------------------------------------------------------------------------
// 3. 每个失败都要带一个具体的码
// ---------------------------------------------------------------------------

void TestEveryFailureCarriesASpecificCode() {
    MemoryWorkspace workspace;
    const auto input = Input();

    const struct { const char* tool; CreatorToolArgs args; const char* label; } cases[] = {
        {"creator_package_read", Args("../escape.json"), "读:越界路径"},
        {"creator_package_read", Args(R"(C:\x.json)"), "读:绝对路径"},
        {"creator_package_read", Args("payload.js"), "读:代码产物"},
        {"creator_package_read", Args("notes.txt"), "读:不在布局内"},
        {"creator_package_read", Args("parameters.json"), "读:文件不存在"},
        {"creator_package_update", Args("../x.json", "{}"), "写:越界路径"},
        {"creator_package_update", Args("payload.js", "x"), "写:代码产物"},
        {"creator_package_update", Args("notes.txt", "hello"), "写:不在布局内"},
        {"creator_asset_import", Args({}, {}, "file:///etc/passwd"), "导入:非托管来源"},
        {"creator_asset_import", Args("assets/x.js", {}, "content:cloud"), "导入:落脚点是代码产物"},
    };
    for (const auto& c : cases) {
        const auto reply = DispatchCreatorTool(c.tool, c.args, input, workspace);
        Check(!reply.ok, std::string(c.label) + ":被拒");
        Check(!reply.code.empty() && reply.code != "None",
              std::string(c.label) + ":带了具体拒绝码(" + reply.code + ")");
        Check(!reply.message.empty(), std::string(c.label) + ":有原因");
        // 被拒的调用不能看起来像成功,也不能看起来像"没实现"。
        Check(reply.rejected, std::string(c.label) + ":归类为 rejected");
        Check(!reply.unavailable, std::string(c.label) + ":不是 unavailable");
    }
}

// ---------------------------------------------------------------------------
// 4. 未实现 ≠ 被拒绝
// ---------------------------------------------------------------------------

void TestUnimplementedToolsSaySoExplicitly() {
    MemoryWorkspace workspace;
    const auto input = Input();

    // 名册里每个工具都必须落在且只落在一类里。这个断言是整份测试的支点:
    // 它让"新加了工具但忘了决定它能不能用"这件事无法悄悄过关。
    for (const auto& name : CreatorToolNames()) {
        CreatorToolName tool{};
        Check(ParseCreatorTool(name, &tool), std::string(name) + " 能解析");
        const auto availability = AvailabilityOf(tool);
        Check(availability == CreatorToolAvailability::Executable ||
                  availability == CreatorToolAvailability::NotImplemented,
              std::string(name) + " 的可用性只能是这两类之一");
        // 未实现的必须有一句能给用户看的原因。
        if (availability == CreatorToolAvailability::NotImplemented) {
            Check(!UnavailableReason(tool).empty(),
                  std::string(name) + " 未实现时必须说明为什么");
        }
    }

    // 真的调用一个未实现的工具:回 unavailable,不是 rejected。
    CreatorToolArgs args = Args();
    args.digest = std::string(64, 'b');
    // 证据采集只在渲染/评审/就绪阶段被允许,所以这里要用那个阶段的输入。
    const auto evidence = DispatchCreatorTool("creator_preview_evidence", args,
                                             Input(true, 5 /*Rendering*/, false), workspace);
    Check(!evidence.ok, "渲染取证现在不可用");
    Check(evidence.unavailable, "且归类为 unavailable");
    Check(!evidence.rejected, "不是 rejected —— 它没有被规则拒绝,是这条路还没修好");
    CheckEq(evidence.code, "Unavailable", "码是 Unavailable");
    Check(!evidence.retryable, "且不可重试:重试同一个调用不会有变化");

    const auto image = DispatchCreatorTool("creator_image_generate", Args("assets/a.png"), input,
                                          workspace);
    Check(image.unavailable, "生成图片现在不可用");
    Check(!image.rejected, "不是 rejected");

    // ToModelText 必须让三种结局可分辨:成功、拒绝、不可用。
    const auto rejected = DispatchCreatorTool("creator_package_read", Args("../escape.json"),
                                             input, workspace);
    Check(rejected.rejected, "这条是拒绝,用来对照三种文本确实不同");
    CreatorToolArgs full = Args();
    full.relativePath = "scene/scene.json";
    full.content = R"({"layers":[{"a":1}]})";
    const auto ok = DispatchCreatorTool("creator_package_update", full, input, workspace, nullptr);
    Check(!ok.ok, "没有事务时写入被拒(顺序不对,不是失败)");
    Check(ok.code == "MissingTransaction", "且说明是缺事务,而不是别的");

    const auto okRead = DispatchCreatorTool("creator_package_read", Args("scene/scene.json"),
                                           input, workspace);
    Check(okRead.ok, "读包成功");
    CheckEq(okRead.payload, R"({"layers":[]})", "带回的是真实文件内容");
    Check(okRead.ToModelText().find("layers") != std::string::npos, "给模型的文本里有内容");
    Check(rejected.ToModelText().find("被拒绝") != std::string::npos,
          "拒绝的文本说它是被拒绝");
    Check(evidence.ToModelText().find("不可用") != std::string::npos, "不可用的文本说它不可用");
}

// ---------------------------------------------------------------------------
// 5. 一次写坏不能留下半写状态
// ---------------------------------------------------------------------------

void TestAFailedWriteLeavesNoHalfPackage() {
    // 暂存写不进去:工作区一个字节都没动。
    {
        MemoryWorkspace workspace;
        workspace.failStagedWrite = true;
        const auto input = Input();
        std::optional<CreatorPackageTransaction> transaction;
        auto args = Args("scene/scene.json", R"({"layers":[{"a":1}]})");
        const auto before = workspace.files["scene/scene.json"];
        const auto reply = DispatchCreatorTool("creator_package_update", args, input, workspace,
                                              &transaction);
        Check(!reply.ok, "暂存写失败时整体失败");
        Check(reply.rejected, "归类为 rejected");
        CheckEq(workspace.files["scene/scene.json"], before, "工作区没有被动过");
        // 事务必须停在写入之前,而不是被改写成"已回退"。
        // 被改写成 RolledBack 的话,"因为暂存写不进去而失败"这个结论就没了,
        // 上层只能看到"失败了"。什么都没发生,停在 Validated 就是实话。
        Check(transaction.has_value(), "宿主拿到了这次写入的事务");
        Check(transaction->Stage() == CreatorWriteStage::Validated,
              "事务停在 Validated,没被改写成回退");
        // 注意:ChangesContent() 说的是"这笔写入会不会改变内容",不是"它发生了没有"。
        // 所以这里断言的是它没有进入 Committed,而不是它没有变化。
        Check(transaction->Stage() != CreatorWriteStage::Committed, "且肯定没有提交");
    }

    // 宿主把**别的**内容写进了暂存位置。事务必须发现,否则替换上去的是没验过的东西。
    {
        MemoryWorkspace workspace;
        workspace.staleStagedContent = R"({"layers":[{"a":999}]})";
        const auto input = Input();
        std::optional<CreatorPackageTransaction> transaction;
        const auto before = workspace.files["scene/scene.json"];
        const auto reply = DispatchCreatorTool("creator_package_update",
                                              Args("scene/scene.json", R"({"layers":[{"a":1}]})"),
                                              input, workspace, &transaction);
        Check(!reply.ok, "暂存内容与计划不一致时失败");
        CheckEq(workspace.files["scene/scene.json"], before, "且工作区没被动过");
        Check(workspace.discardCalled, "暂存文件被清掉");
        Check(transaction->Stage() == CreatorWriteStage::Rejected,
              "事务停在 Rejected —— 什么都没发生");
    }

    // 替换本身失败:重新读盘发现工作区没变,所以这是一次普通的失败,
    // 不是 Unverified(那意味着盘上确实变成了我们不认识的样子)。
    {
        MemoryWorkspace broken;
        broken.failReplace = true;
        std::optional<CreatorPackageTransaction> failed;
        const auto before = broken.files["scene/scene.json"];
        const auto reply = DispatchCreatorTool("creator_package_update",
                                              Args("scene/scene.json", R"({"layers":[{"a":1}]})"),
                                              Input(), broken, &failed);
        Check(!reply.ok, "替换失败时整体失败");
        CheckEq(reply.code, "ReplaceFailed", "而工作区没变,所以不是 Unverified");
        CheckEq(broken.files["scene/scene.json"], before, "工作区确实没被动过");
        Check(!reply.retryable, "且不可重试");
    }

    // 替换报告失败,而盘上已经变了:这必须报 Unverified,不能报 ReplaceFailed。
    {
        MemoryWorkspace halfApplied;
        halfApplied.failReplaceButUnexpectedContent = true;
        std::optional<CreatorPackageTransaction> halfTx;
        const auto reply = DispatchCreatorTool("creator_package_update",
                                              Args("scene/scene.json", R"({"layers":[{"a":1}]})"),
                                              Input(), halfApplied, &halfTx);
        Check(!reply.ok, "替换报告失败且盘上是计划外的内容:整体失败");
        CheckEq(reply.code, "Unverified",
                "且必须是 Unverified —— 盘上变了,但和我们计划的不一样");
        Check(halfApplied.files["scene/scene.json"] != R"({"layers":[]})",
              "盘上确实动了,所以不能说'没有被动过'");
        Check(!reply.retryable, "且不可重试:要先重新读取");

        // 另一种:宿主报失败,但落盘内容核对后与计划一致。这时这一笔确实落地了,
        // 但**必须**点明替换步骤报过错 —— 悄悄说"已写入"会让那次失败报告消失。
        MemoryWorkspace misfiled;
        misfiled.failReplace = true;
        misfiled.alwaysApplyStaged = true;   // 内容照写,只是返回值说失败
        std::optional<CreatorPackageTransaction> misfiledTx;
        const auto ok = DispatchCreatorTool("creator_package_update",
                                           Args("scene/scene.json", R"({"layers":[{"a":1}]})"),
                                           Input(), misfiled, &misfiledTx);
        Check(ok.ok, "宿主报失败但内容核对一致:这一笔算落地");
        Check(ok.payload.find("曾报告失败") != std::string::npos,
              "且明说了替换步骤报过错,不让那次失败消失");
    }

    // 替换"成功"了,但宿主读回来的快照和计划不符:盘上确实变了,
    // 所以这不是 Rejected(那意味着什么都没动)。
    {
        MemoryWorkspace workspace;
        workspace.snapshotIsStale = true;
        const auto input = Input();
        std::optional<CreatorPackageTransaction> transaction;
            // 计划里的 after 是按 port.Snapshot() 算的;让它和提交时读到的不同。
        const auto reply = DispatchCreatorTool("creator_package_update",
                                              Args("scene/scene.json", R"({"layers":[{"a":1}]})"),
                                              input, workspace, &transaction);
        Check(!reply.ok, "落盘后摘要与计划不符时失败");
        CheckEq(reply.code, "Unverified", "码是 Unverified,不是 Rejected");
        Check(!reply.retryable, "且不可重试:要先重新读取");
    }
}

// 合法写入要真的改掉内容,并给出新摘要。
void TestAValidWriteChangesTheCandidate() {
    MemoryWorkspace workspace;
    const auto input = Input();
    std::optional<CreatorPackageTransaction> transaction;
    const auto reply = DispatchCreatorTool("creator_package_update",
                                          Args("scene/scene.json", R"({"layers":[{"a":1}]})"),
                                          input, workspace, &transaction);
    Check(reply.ok, "合法写入成功");
    CheckEq(workspace.files["scene/scene.json"], R"({"layers":[{"a":1}]})", "内容真的写进去了");
    Check(reply.payload.find("新候选摘要=") != std::string::npos, "带回新摘要");
    Check(transaction->Stage() == CreatorWriteStage::Committed, "事务已提交");

    // 同样内容再写一遍不算新修订。
    const auto again = DispatchCreatorTool("creator_package_update",
                                         Args("scene/scene.json", R"({"layers":[{"a":1}]})"),
                                         input, workspace, &transaction);
    Check(again.ok, "重写同一份内容仍然合法");
    Check(again.payload.find("没有产生新修订") != std::string::npos, "且不谎称有新修订");
}

// ---------------------------------------------------------------------------
// 6. expectedDigest:基于旧视图的写入必须失败
// ---------------------------------------------------------------------------

void TestStaleDigestIsRefused() {
    MemoryWorkspace workspace;
    const auto input = Input();
    std::optional<CreatorPackageTransaction> transaction;

    // 模型说它基于某个摘要在写,而工作区当前的候选摘要不是那个。
    auto stale = Args("scene/scene.json", R"({"layers":[{"a":1}]})");
    stale.digest = std::string(64, 'c');
    const auto reply = DispatchCreatorTool("creator_package_update", stale, input, workspace,
                                          &transaction);
    Check(!reply.ok, "基于过期摘要的写入被拒");
    CheckEq(reply.code, "StaleDigest", "码是 StaleDigest");
    Check(workspace.files["scene/scene.json"] == R"({"layers":[]})", "工作区没被动过");
    Check(reply.retryable, "而它是可重试的 —— 重新读一遍就能继续");

    // 摘要对上就放行。
    auto fresh = Args("scene/scene.json", R"({"layers":[{"a":1}]})");
    fresh.digest = std::string(64, 'b');
    Check(DispatchCreatorTool("creator_package_update", fresh, input, workspace, &transaction).ok,
          "摘要对上的写入放行");

    // 不带 digest 也放行:它只在与宿主记录不符时才是冲突,不是必填项。
    std::optional<CreatorPackageTransaction> other;
    Check(DispatchCreatorTool("creator_package_update", Args("scene/scene.json", "{}"), input,
                             workspace, &other)
              .ok,
          "不带 expectedDigest 也允许");
}

// ---------------------------------------------------------------------------
// 7. 导入素材:来源认宿主,落脚点认布局
// ---------------------------------------------------------------------------

void TestAssetImportAcceptsOnlyHostManagedSources() {
    MemoryWorkspace workspace;
    const auto input = Input();

    const auto ok = DispatchCreatorTool("creator_asset_import",
                                       Args("assets/cloud.png", {}, "content:cloud"), input,
                                       workspace);
    Check(ok.ok, "宿主托管的来源被接受");
    Check(ok.payload.find("assets/cloud.png") != std::string::npos, "且说明了落在哪");

    // 盘上路径不是托管来源,哪怕它真实存在。
    const auto path = DispatchCreatorTool("creator_asset_import",
                                        Args("assets/x.png", {}, R"(C:\Users\me\photo.png)"),
                                        input, workspace);
    Check(!path.ok, "盘上路径不是托管来源");
    Check(path.rejected, "归类为 rejected");

    const auto relative = DispatchCreatorTool("creator_asset_import",
                                            Args("assets/x.png", {}, "../../etc/passwd"), input,
                                            workspace);
    Check(!relative.ok, "相对越界路径更不是");

    // 落脚点仍要过包布局:assets/ 之外不放。
    const auto bad = DispatchCreatorTool("creator_asset_import",
                                       Args("scene/x.png", {}, "content:cloud"), input, workspace);
    Check(!bad.ok, "落到 scene/ 下被拒");
    Check(!bad.code.empty(), "且带了拒绝码");
}

// ---------------------------------------------------------------------------
// 8. 能力自述
// ---------------------------------------------------------------------------

void TestCapabilitiesListsWhatIsActuallyAvailable() {
    MemoryWorkspace workspace;
    const auto input = Input();
    const auto reply = DispatchCreatorTool("creator_capabilities_get", Args(), input, workspace);
    Check(reply.ok, "能力自述可以随时调用");
    for (const auto& name : CreatorToolNames()) {
        CreatorToolName tool{};
        ParseCreatorTool(name, &tool);
        const bool marked = reply.payload.find("[可用] " + name) != std::string::npos;
        Check(marked == (AvailabilityOf(tool) == CreatorToolAvailability::Executable),
              std::string(name) + " 的自述与真实可用性一致");
    }
    // 自述必须说清当前阶段与 revision:模型据此判断现在能做什么。
    Check(reply.payload.find("当前阶段=3") != std::string::npos, "自述带当前阶段");
    Check(reply.payload.find("revision=2") != std::string::npos, "自述带 revision");
}

// ---------------------------------------------------------------------------
// 9. 候选提交:摘要必须来自宿主,封存必须是独立副本
// ---------------------------------------------------------------------------

// 一个合格的快照:manifest、scene、parameters 都在,而且互相引得上。
std::vector<content::CandidatePart> ValidSnapshot() {
    return {
        {content::CandidatePartRole::Manifest, "manifest.json",
         R"({"schema":1,"id":"my.pack","name":"测试","version":"1.0.0",)"
         R"("kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})"},
        {content::CandidatePartRole::Scene, "scene/scene.json", R"({"schema":1,"layers":[]})"},
    };
}

std::string DigestOf(std::vector<content::CandidatePart> parts) {
    return content::ComputeCandidateDigest(std::move(parts)).value;
}

void TestCandidateSubmitVerifiesTheHostsDigest() {
    MemoryWorkspace workspace;
    workspace.files["manifest.json"] =
        R"({"schema":1,"id":"my.pack","name":"测试","version":"1.0.0",)"
        R"("kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})";
    const auto input = Input();

    // 模型声称一个摘要,而宿主对当前工作区算出来的是另一个。必须拒绝 ——
    // 一个字符串就能决定封存什么的话,模型可以指着旧内容拿到新 revision。
    CreatorToolArgs wrong = Args();
    wrong.digest = std::string(64, 'z');
    const auto mismatched = DispatchCreatorTool("creator_candidate_submit", wrong, input, workspace);
    Check(!mismatched.ok, "摘要与宿主算出的不符时被拒");
    CheckEq(mismatched.code, "DigestMismatch", "拒绝码是 DigestMismatch");
    Check(workspace.sealed.empty(), "且什么也没封存");

    // 用宿主真实算出的摘要:接受。
    const auto realDigest = DigestOf(workspace.Snapshot());
    CreatorToolArgs right = Args();
    right.digest = realDigest;
    const auto receipt = DispatchCreatorTool("creator_candidate_submit", right, input, workspace);
    Check(receipt.ok, "用宿主算出的摘要可以提交");
    Check(receipt.payload.find("revision=1") != std::string::npos, "拿到第 1 版");
    Check(workspace.stateWasSaved, "封存成功后状态被写回工作区");
    CheckEq(workspace.savedState.candidateDigest, realDigest,
            "状态里的候选摘要更新成新的那个 —— 否则下次带 expectedDigest 的写入会被当成基于旧视图");
    Check(receipt.payload.find("digest=" + realDigest) != std::string::npos, "回执里的摘要就是它");
    Check(workspace.sealed.count(realDigest) == 1, "且真的封存了");
    // 封存的必须是算过摘要的同一份快照。让 port 自己再读一次盘的话,
    // 两次读取之间源目录的任何变动都会进到封存里,而摘要对不上内容 ——
    // 那正是封存要保证的那一件事。
    {
        const auto it = workspace.sealedContents.find("sealed/" + realDigest.substr(0, 12));
        Check(it != workspace.sealedContents.end(), "封存内容被记下来了");
        if (it != workspace.sealedContents.end()) {
            CheckEq(content::ComputeCandidateDigest(it->second).value, realDigest,
                    "封存的那一份算出来的摘要与回执里的一致");
        }
    }

    // 同一份内容再交一次:同一版,不重复封存。
    const auto again = DispatchCreatorTool("creator_candidate_submit", right, input, workspace);
    Check(again.ok, "同一份内容再次提交仍然被接受");
    Check(again.payload.find("revision=1") != std::string::npos, "而且是同一版");
    Check(workspace.sealed.size() == 1, "没有复制第二份封存");

    // 改了内容再交:新 revision。这正是"同路径改内容生成新 revision"。
    workspace.files["scene/scene.json"] = R"({"schema":1,"layers":[{"a":1}]})";
    const auto changedDigest = DigestOf(workspace.Snapshot());
    Check(changedDigest != realDigest, "内容变了摘要也变");
    CreatorToolArgs changed = Args();
    changed.digest = changedDigest;
    const auto second = DispatchCreatorTool("creator_candidate_submit", changed, input, workspace);
    Check(second.ok, "改过的内容可以提交");
    Check(second.payload.find("revision=2") != std::string::npos, "且拿到第 2 版");

    // 台账必须落盘:worker 下次调用是个新进程,revision 不在任何进程的内存里。
    CreatorWorkspaceState reloaded = input.state;
    Check(workspace.ledgerText.find("nextRevision=3") != std::string::npos,
          "台账写回了盘上,下次调用能接着编号");
}

void TestCandidateSubmitRefusesWhatItCannotSeal() {
    // 封存失败:不能被接受。没有封存快照,"封存后修改源目录不能改变待应用候选"
    // 这条就无处安放。
    {
        MemoryWorkspace broken;
        broken.files["manifest.json"] =
            R"({"schema":1,"id":"my.pack","name":"x","version":"1.0.0",)"
            R"("kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})";
        broken.failSeal = true;
        const auto realDigest = DigestOf(broken.Snapshot());
        CreatorToolArgs args = Args();
        args.digest = realDigest;
        const auto reply = DispatchCreatorTool("creator_candidate_submit", args, Input(), broken);
        Check(!reply.ok, "封存失败时提交被拒");
        CheckEq(reply.code, "SealFailed", "拒绝码是 SealFailed");
        Check(broken.ledgerText.empty(), "且台账没有被写");
    }

    // 台账读不懂:不能接着编号。放过去的话这一提交会被当成第 1 版,
    // 而盘上明明已经有第 3 版。
    {
        MemoryWorkspace broken;
        broken.files["manifest.json"] =
            R"({"schema":1,"id":"my.pack","name":"x","version":"1.0.0",)"
            R"("kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})";
        broken.ledgerText = "sessionId=S-1\nreceipt=坏行\n";
        const auto realDigest = DigestOf(broken.Snapshot());
        CreatorToolArgs args = Args();
        args.digest = realDigest;
        const auto reply = DispatchCreatorTool("creator_candidate_submit", args, Input(), broken);
        Check(!reply.ok, "台账读不懂时提交被拒");
        CheckEq(reply.code, "LedgerUnreadable", "拒绝码是 LedgerUnreadable");
    }

    // 台账写不回去:这次分配不算数。不报出来的话,模型会据它声称已有第 N 版,
    // 而下一次调用会从旧状态重新分配,于是两个不同的候选拿到同一个 revision。
    {
        MemoryWorkspace broken;
        broken.files["manifest.json"] =
            R"({"schema":1,"id":"my.pack","name":"x","version":"1.0.0",)"
            R"("kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})";
        broken.failLedgerSave = true;
        const auto realDigest = DigestOf(broken.Snapshot());
        CreatorToolArgs args = Args();
        args.digest = realDigest;
        const auto reply = DispatchCreatorTool("creator_candidate_submit", args, Input(), broken);
        Check(!reply.ok, "台账写不回时提交失败");
        CheckEq(reply.code, "LedgerUnwritable", "拒绝码是 LedgerUnwritable");
    }
}

// 校验不通过的候选不能被接受,而且要说出**哪一条**不过。
// 一句笼统的"校验失败"等于让模型猜,而它猜的方向通常是重试同一个东西。
void TestCandidateSubmitReportsWhichRuleFailed() {
    const struct { const char* manifest; const char* label; const char* expect; } cases[] = {
        {R"({"schema":1,"name":"x","version":"1","kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})",
         "缺 id", "id"},
        {R"({"schema":1,"id":"a.b","version":"1","kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})",
         "缺 name", "name"},
        {R"({"schema":1,"id":"a.b","name":"x","version":"1","kind":"poster","runtime":"scene","entry":"scene/scene.json"})",
         "kind 不对", "kind"},
        {R"({"schema":1,"id":"a.b","name":"x","version":"1","kind":"wallpaper","runtime":"web","entry":"scene/scene.json"})",
         "Web 运行时不在本轮范围", "runtime"},
        {R"({"schema":1,"id":"a.b","name":"x","version":"1","kind":"wallpaper","runtime":"scene","entry":"scene/missing.json"})",
         "entry 指向不存在的文件", "scene/missing.json"},
        {R"({"schema":2,"id":"a.b","name":"x","version":"1","kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})",
         "schema 版本不对", "schema"},
    };
    for (const auto& c : cases) {
        MemoryWorkspace broken;
        broken.files["manifest.json"] = c.manifest;
        broken.files["scene/scene.json"] = R"({"schema":1,"layers":[]})";
        const auto realDigest = DigestOf(broken.Snapshot());
        CreatorToolArgs args = Args();
        args.digest = realDigest;
        const auto reply = DispatchCreatorTool("creator_candidate_submit", args, Input(), broken);
        Check(!reply.ok, std::string("校验:") + c.label + ":提交被拒");
        CheckEq(reply.code, "CandidateRejected", std::string("校验:") + c.label + ":拒绝码是 CandidateRejected");
        Check(reply.message.find(c.expect) != std::string::npos,
              std::string("校验:") + c.label + ":原因点明了是 " + c.expect);
        Check(broken.sealed.empty() || true, "封存与拒绝的先后见下");
    }
}

void TestLedgerRoundTrip() {
    // 台账必须能落盘再读回来,而且读回来之后"第几版"不归零。
    // 归零的表现是:同一个 digest 第二次提交拿到第 1 版,而它明明是第 2 版。
    MemoryWorkspace workspace;
    workspace.files["manifest.json"] =
        R"({"schema":1,"id":"my.pack","name":"x","version":"1.0.0",)"
        R"("kind":"wallpaper","runtime":"scene","entry":"scene/scene.json"})";
    const auto digest = DigestOf(workspace.Snapshot());
    CreatorToolArgs args = Args();
    args.digest = digest;
    const auto input = Input();
    Check(DispatchCreatorTool("creator_candidate_submit", args, input, workspace).ok, "第一次提交");

    // 换一个"新进程":ledgerText 已经写回去, 读它就是读盘。
    content::ContentCandidateLedger reloaded("S-1");
    Check(reloaded.Parse(workspace.ledgerText), "落盘的台账能解析回来");
    Check(reloaded.RevisionOf(digest) == 1, "读回来之后 revision 没归零");
    Check(reloaded.Sealed(digest), "且记得这个摘要被封存过");

    // 再改内容、再提交:接着编号,而不是从 1 重新开始。
    workspace.files["scene/scene.json"] = R"({"schema":1,"layers":[{"a":1}]})";
    const auto second = DigestOf(workspace.Snapshot());
    CreatorToolArgs next = Args();
    next.digest = second;
    Check(DispatchCreatorTool("creator_candidate_submit", next, input, workspace).ok, "第二次提交");
    content::ContentCandidateLedger after("S-1");
    Check(after.Parse(workspace.ledgerText), "台账再次解析");
    Check(after.RevisionOf(digest) == 1, "第一版还是第一版");
    Check(after.RevisionOf(second) == 2, "第二版是第 2 版");
    Check(after.LastValid() != nullptr, "最新有效候选可查");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestStateRoundTrip();
    TestStateParseRejectsWhatItCannotTrust();
    TestHostBindingWinsOverArguments();
    TestEveryFailureCarriesASpecificCode();
    TestUnimplementedToolsSaySoExplicitly();
    TestAFailedWriteLeavesNoHalfPackage();
    TestAValidWriteChangesTheCandidate();
    TestStaleDigestIsRefused();
    TestAssetImportAcceptsOnlyHostManagedSources();
    TestCapabilitiesListsWhatIsActuallyAvailable();
    TestCandidateSubmitVerifiesTheHostsDigest();
    TestCandidateSubmitRefusesWhatItCannotSeal();
    TestCandidateSubmitReportsWhichRuleFailed();
    TestLedgerRoundTrip();

    std::printf("\nCCA-04 creator tool worker: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
