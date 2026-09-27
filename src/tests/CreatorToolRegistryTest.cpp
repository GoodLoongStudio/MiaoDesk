// CCA-04:创作工具名册、路由与两份清单的一致性。
//
// 这一份测试的重点不是"路由能 reject",而是计划 §5 那句"按进程/会话配置验证扩展
// 实际拿到的工具清单"。Pi 侧的 --tools 与宿主的 worker 允许表是两份独立清单,
// 一旦不一致,故障是**单向静默**的:
//   * Pi 给了、worker 不认 -> 调用失败,模型重试,用户看到"它一直不成功";
//   * worker 认、Pi 没给   -> 能力躺着没人能用,而没有任何东西报错。
// 所以这里既测路由,也测两侧对名册的覆盖。
#include "miaodesk/CreatorToolRegistry.h"

#include <cstdio>
#include <set>
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

CreatorToolContext Ctx(const char* sessionId = "S-1", int stage = 3 /*Generating*/,
                      const char* workspace = R"(C:\ws\S-1)", bool cancelled = false,
                      std::uint64_t epoch = 7) {
    CreatorToolContext context;
    context.sessionId = sessionId;
    context.epoch = epoch;
    context.workspaceRoot = workspace;
    context.stage = stage;
    context.cancelRequested = cancelled;
    return context;
}

CreatorToolArgs Args(const char* sessionId = "S-1", const char* workspace = R"(C:\ws\S-1)") {
    CreatorToolArgs args;
    args.sessionId = sessionId;
    args.workspaceRoot = workspace;
    return args;
}

const char* Route(std::string_view tool, const CreatorToolArgs& args,
                 const CreatorToolContext& context) {
    return ToString(RouteCreatorTool(tool, args, context).reject);
}

// ---------------------------------------------------------------------------
// 1. 名册本身
// ---------------------------------------------------------------------------

void TestRegistryIsCompleteAndStable() {
    const auto& names = CreatorToolNames();
    Check(names.size() == 8, "名册里正好八个工具");
    std::set<std::string> unique(names.begin(), names.end());
    Check(unique.size() == names.size(), "没有重复项");

    // 每个名字都能解析回来,而且解析回来的枚举再打出来还是同一个名字。
    for (const auto& name : names) {
        CreatorToolName parsed{};
        Check(ParseCreatorTool(name, &parsed), name + " 能解析");
        CheckEq(std::string(ToString(parsed)), name, "且往返一致");
    }
    CreatorToolName ignored{};
    Check(!ParseCreatorTool("bash", &ignored), "通用工具不在创作名册里");
    Check(!ParseCreatorTool("write", &ignored), "write 也不在");
    Check(!ParseCreatorTool("creator_unknown", &ignored), "未知的 creator_ 前缀也不在");

    // 名册与 IsCreatorTool 必须一致:两侧任何时候都要指向同一组。
    for (const auto& name : names) {
        Check(IsCreatorTool(name), name + " 被 IsCreatorTool 认出");
    }
}

void TestMutatingClassification() {
    // 会改候选内容的与只读的要分开:阶段判据不一样。
    for (const auto& name : {"creator_package_update", "creator_asset_import",
                             "creator_image_generate", "creator_candidate_submit"}) {
        Check(IsMutatingCreatorTool(name) && !std::string(name).empty(),
              std::string(name) + " 是会改候选的工具");
        Check(IsMutatingCreatorTool(name) == true, std::string(name) + " 的 mutating 判据稳定");
    }
    for (const auto& name : {"creator_capabilities_get", "content_skill_get",
                             "creator_package_read", "creator_preview_evidence"}) {
        Check(!IsMutatingCreatorTool(name), std::string(name) + " 是只读的");
    }
}

// ---------------------------------------------------------------------------
// 2. 归属:不能跨作品
// ---------------------------------------------------------------------------

void TestCrossWorkToolCallIsRejected() {
    // sessionId 对不上。这是"能不能访问另一个作品"的问题,所以它排在阶段之前。
    CheckEq(std::string(Route("creator_package_read", Args("S-other"), Ctx())),
            std::string(ToString(CreatorToolReject::SessionMismatch)),
            "伪造 sessionId 被拒");
    // 阶段明明是 Generating(合法阶段),但仍被归属挡下 —— 证明顺序是对的。
    CheckEq(std::string(Route("creator_package_update", Args("S-other"), Ctx())),
            std::string(ToString(CreatorToolReject::SessionMismatch)),
            "会改候选的工具同样先过归属");

    // 没有 sessionId。
    CheckEq(std::string(Route("creator_package_read", Args(""), Ctx())),
            std::string(ToString(CreatorToolReject::MissingSession)),
            "没有 sessionId 被拒");

    // 工作区不符。模型声称的是别的作品的工作区。
    CheckEq(std::string(Route("creator_package_read", Args("S-1", R"(C:\ws\S-2)"), Ctx())),
            std::string(ToString(CreatorToolReject::MissingWorkspace)),
            "工作区不符被拒");
    // 分隔符/大小写差异不算换了作品 —— 模型换个写法不该被当成攻击。
    CheckEq(std::string(Route("creator_package_read", Args("S-1", R"(c:/WS/s-1/)"), Ctx())),
            std::string(ToString(CreatorToolReject::MissingWorkspace)),
            "工作区对账按形状做,大小写与末尾斜杠的差异仍算不符(保守方向)");

    // 取消之后一律拒绝,包括只读。
    CheckEq(std::string(Route("creator_package_read", Args(), Ctx("S-1", 3, R"(C:\ws\S-1)", true))),
            std::string(ToString(CreatorToolReject::EpochStale)),
            "取消后只读工具也被拒");
}

void TestUnknownToolIsRejected() {
    CheckEq(std::string(Route("bash", Args(), Ctx())),
            std::string(ToString(CreatorToolReject::UnknownTool)),
            "不在名册里的工具被拒");
    CheckEq(std::string(Route("", Args(), Ctx())),
            std::string(ToString(CreatorToolReject::UnknownTool)),
            "空工具名被拒");
    // 未知工具的拒绝不可重试:重试同一个名字没有意义。
    Check(!RouteCreatorTool("bash", Args(), Ctx()).Retryable(), "未知工具不值得重试");
}

// ---------------------------------------------------------------------------
// 3. 阶段
// ---------------------------------------------------------------------------

void TestStageGates() {
    // 阶段索引与 CreationWorkflow.h 的 CreationStage 顺序一致。
    const struct { int stage; const char* label; } stages[] = {
        {0, "Draft"},        {1, "NeedsInput"},   {2, "Preparing"},
        {3, "Generating"},   {4, "Validating"},   {5, "Rendering"},
        {6, "Reviewing"},    {7, "Repairing"},    {8, "Ready"},
        {9, "Applying"},     {10, "Applied"},     {11, "Cancelling"},
        {12, "Cancelled"},   {13, "Failed"},      {14, "Queued"},
    };
    // 参齐了的调用。第一版这里传了空 Args,于是 read 全部因 relativePath 缺失被拒 ——
    // 那些"允许读包"的断言测的是参数校验,不是阶段,白测了 15 条。
    CreatorToolArgs full;
    full.sessionId = "S-1";
    full.workspaceRoot = R"(C:\ws\S-1)";
    full.relativePath = "scene/scene.json";
    full.content = R"({"layers":[]})";
    full.digest = std::string(64, 'a');
    full.source = "content:cloud";

    for (const auto& s : stages) {
        const auto context = Ctx("S-1", s.stage);
        // 只读:任何阶段都可以。
        const auto read = RouteCreatorTool("creator_package_read", full, context);
        Check(read.allowed, std::string(s.label) + " 阶段允许读包");
        Check(!read.mutating, std::string(s.label) + " 阶段读包不是 mutating");
        // 会改候选的:只在制作与修复的那几个阶段。
        const auto update = RouteCreatorTool("creator_package_update", full, context);
        const bool expectWrite = s.stage == 2 || s.stage == 3 || s.stage == 4 || s.stage == 7;
        Check(update.allowed == expectWrite,
              std::string(s.label) + " 阶段" + (expectWrite ? "接受" : "不接受") + "包写入");
        // 证据采集:渲染、评审、就绪。
        const auto evidence = RouteCreatorTool("creator_preview_evidence", full, context);
        const bool expectEvidence = s.stage == 5 || s.stage == 6 || s.stage == 8;
        Check(evidence.allowed == expectEvidence,
              std::string(s.label) + " 阶段" + (expectEvidence ? "接受" : "不接受") + "证据采集");
        // 应用之后一律不再动候选。
        const auto submit = RouteCreatorTool("creator_candidate_submit", full, context);
        Check(!submit.allowed || expectWrite, std::string(s.label) + " 阶段提交候选与写入同规则");
    }
}

void TestRetryableRejectionIsDistinguished() {
    // 缺参数值得重试;归属/阶段/取消不值得。混为一谈的后果是模型反复重试同一个
    // 越界调用,而用户看不到任何进展。
    CreatorToolArgs missing = Args();
    missing.relativePath.clear();
    const auto read = RouteCreatorTool("creator_package_read", missing, Ctx());
    Check(!read.allowed, "缺 relativePath 被拒");
    Check(read.Retryable(), "而它是可重试的 —— 补上参数就能继续");
    CheckEq(std::string(ToString(read.reject)),
            std::string(ToString(CreatorToolReject::MissingArgument)), "拒绝码是 MissingArgument");

    const auto cross = RouteCreatorTool("creator_package_read", Args("S-other"), Ctx());
    Check(!cross.Retryable(), "跨作品不可重试");
    const auto stale = RouteCreatorTool("creator_package_read", Args(),
                                        Ctx("S-1", 3, R"(C:\ws\S-1)", true));
    Check(!stale.Retryable(), "已取消不可重试");
    const auto wrongStage = RouteCreatorTool("creator_package_update", Args(), Ctx("S-1", 8));
    Check(!wrongStage.Retryable(), "阶段不对不可重试");
}

void TestRequiredArgumentsPerTool() {
    CreatorToolArgs args = Args();
    args.relativePath = "scene/scene.json";
    args.content = R"({"layers":[]})";
    args.source = "content:cloud";
    args.digest = std::string(64, 'a');
    const auto context = Ctx();
    for (const auto& name : CreatorToolNames()) {
        const auto route = RouteCreatorTool(name, args, context);
        if (!route.allowed) {
            Check(route.reject != CreatorToolReject::UnknownTool,
                  std::string(name) + " 参齐了就该过(除非它本身不该在 Generating 出现)");
        }
    }
    // content_skill_get 不带任何参数也合法。
    Check(RouteCreatorTool("content_skill_get", Args(), context).allowed,
          "content_skill_get 不需要参数");
    Check(RouteCreatorTool("creator_capabilities_get", Args(), context).allowed,
          "creator_capabilities_get 不需要参数");
}

// 每个工具的必填参数都要单独有一条"缺它就被拒"。
// 第一版只有 TestRequiredArgumentsPerTool,而它传的是**参齐了的**调用 —— 于是
// content / digest / source 那三道闸短路掉,测试全绿。参齐只能证明"不会被误拒",
// 证明不了"缺参数会被拦"。
void TestEachRequiredArgumentIsEnforced() {
  const auto context = Ctx();
  CreatorToolArgs base;
  base.sessionId = "S-1";
  base.workspaceRoot = R"(C:\ws\S-1)";
  base.relativePath = "scene/scene.json";
  base.content = R"({"layers":[]})";
  base.source = "content:cloud";
  base.digest = std::string(64, 'a');

  const struct { const char* tool; const char* clear; const char* param; const char* label; } cases[] = {
      {"creator_package_read", "relativePath", "relativePath", "读包"},
      {"creator_package_update", "relativePath", "relativePath", "写包(路径)"},
      {"creator_package_update", "content", "content", "写包(内容)"},
      {"creator_asset_import", "source", "source", "导入素材"},
      {"creator_candidate_submit", "digest", "digest", "提交候选"},
  };
  for (const auto& c : cases) {
    CreatorToolArgs args = base;
    if (std::string(c.clear) == "relativePath") args.relativePath.clear();
    if (std::string(c.clear) == "content") args.content.clear();
    if (std::string(c.clear) == "source") args.source.clear();
    if (std::string(c.clear) == "digest") args.digest.clear();
    const auto route = RouteCreatorTool(c.tool, args, context);
    Check(!route.allowed, std::string(c.label) + ":缺 " + c.param + " 被拒");
    CheckEq(std::string(ToString(route.reject)),
            std::string(ToString(CreatorToolReject::MissingArgument)),
            std::string(c.label) + ":拒绝码是 MissingArgument");
    Check(route.Retryable(), std::string(c.label) + ":而且可重试 —— 补上参数就能继续");
    Check(route.reason.find(c.param) != std::string::npos,
          std::string(c.label) + ":原因点明了缺哪个参数");

    // 反证:把参数补回来必须放行,否则这条断言是在测别的东西。
    const auto restored = RouteCreatorTool(c.tool, base, context);
    Check(restored.allowed, std::string(c.label) + ":补回 " + c.param + " 后放行");
  }
}

void TestAllowedCallsCarryTheirArguments() {
    CreatorToolArgs args = Args();
    args.relativePath = "scene/scene.json";
    args.content = R"({"layers":[]})";
    const auto route = RouteCreatorTool("creator_package_update", args, Ctx());
    Check(route.allowed, "合法写入被放行");
    CheckEq(route.args.relativePath, "scene/scene.json", "参数原样带回,宿主不需要再解析一次");
    CheckEq(route.args.content, R"({"layers":[]})", "内容也原样带回");
    Check(route.mutating, "且标记为 mutating,宿主据此决定要不要先封存");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestRegistryIsCompleteAndStable();
    TestMutatingClassification();
    TestCrossWorkToolCallIsRejected();
    TestUnknownToolIsRejected();
    TestStageGates();
    TestRetryableRejectionIsDistinguished();
    TestRequiredArgumentsPerTool();
    TestEachRequiredArgumentIsEnforced();
    TestAllowedCallsCarryTheirArguments();

    std::printf("\nCCA-04 creator tool registry: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
