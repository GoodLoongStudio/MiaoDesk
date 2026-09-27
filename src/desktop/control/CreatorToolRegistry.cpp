#include "miaodesk/CreatorToolRegistry.h"

#include <algorithm>
#include <array>

namespace miaodesk::creator {
namespace {

// 名字表。顺序固定:CreationStage 与文本都对它做了断言,改变顺序要先改那边。
constexpr std::array<const char*, 8> kToolNames = {
    "creator_capabilities_get",
    "content_skill_get",
    "creator_package_read",
    "creator_package_update",
    "creator_asset_import",
    "creator_image_generate",
    "creator_candidate_submit",
    "creator_preview_evidence",
};

std::vector<std::string> BuildToolNames() {
    std::vector<std::string> names;
    names.reserve(kToolNames.size());
    for (const char* name : kToolNames) names.emplace_back(name);
    return names;
}

// CreationStage 的整数值,与 CreationWorkflow.h 的枚举顺序一致。
// 这里故意不 include 那个头:名册是 Pure 逻辑,不应该为了一个整数把整个工作流
// (以及它的依赖)拖进来。代价是两处顺序要同步,所以测试里逐值断言过。
enum StageIndex {
    StageDraft = 0,
    StageNeedsInput = 1,
    StagePreparing = 2,
    StageGenerating = 3,
    StageValidating = 4,
    StageRendering = 5,
    StageReviewing = 6,
    StageRepairing = 7,
    StageReady = 8,
    StageApplying = 9,
    StageApplied = 10,
    StageCancelling = 11,
    StageCancelled = 12,
    StageFailed = 13,
    StageQueued = 14,
};

} // namespace

const char* ToString(CreatorToolName tool) noexcept {
    switch (tool) {
    case CreatorToolName::CapabilitiesGet: return "creator_capabilities_get";
    case CreatorToolName::SkillGet: return "content_skill_get";
    case CreatorToolName::PackageRead: return "creator_package_read";
    case CreatorToolName::PackageUpdate: return "creator_package_update";
    case CreatorToolName::AssetImport: return "creator_asset_import";
    case CreatorToolName::ImageGenerate: return "creator_image_generate";
    case CreatorToolName::CandidateSubmit: return "creator_candidate_submit";
    case CreatorToolName::PreviewEvidence: return "creator_preview_evidence";
    }
    return "unknown";
}

bool ParseCreatorTool(std::string_view text, CreatorToolName* tool) noexcept {
    if (!tool) return false;
    if (text == "creator_capabilities_get") *tool = CreatorToolName::CapabilitiesGet;
    else if (text == "content_skill_get") *tool = CreatorToolName::SkillGet;
    else if (text == "creator_package_read") *tool = CreatorToolName::PackageRead;
    else if (text == "creator_package_update") *tool = CreatorToolName::PackageUpdate;
    else if (text == "creator_asset_import") *tool = CreatorToolName::AssetImport;
    else if (text == "creator_image_generate") *tool = CreatorToolName::ImageGenerate;
    else if (text == "creator_candidate_submit") *tool = CreatorToolName::CandidateSubmit;
    else if (text == "creator_preview_evidence") *tool = CreatorToolName::PreviewEvidence;
    else return false;
    return true;
}

const std::vector<std::string>& CreatorToolNames() noexcept {
    // 函数局部静态:第一次调用时构造,之后只读。比命名空间静态安全 ——
    // 它不会在别的翻译单元的静态初始化之前被读取。
    static const std::vector<std::string> names = BuildToolNames();
    return names;
}

bool IsCreatorTool(std::string_view tool) noexcept {
    return std::any_of(kToolNames.begin(), kToolNames.end(),
                       [tool](const char* name) { return tool == name; });
}

bool IsMutatingCreatorTool(CreatorToolName tool) noexcept {
    switch (tool) {
    case CreatorToolName::PackageUpdate:
    case CreatorToolName::AssetImport:
    case CreatorToolName::ImageGenerate:
    case CreatorToolName::CandidateSubmit:
        return true;
    default:
        return false;
    }
}

bool IsMutatingCreatorTool(std::string_view tool) noexcept {
    CreatorToolName parsed{};
    return ParseCreatorTool(tool, &parsed) ? IsMutatingCreatorTool(parsed) : false;
}

const char* ToString(CreatorToolReject reject) noexcept {
    switch (reject) {
    case CreatorToolReject::None: return "None";
    case CreatorToolReject::UnknownTool: return "UnknownTool";
    case CreatorToolReject::MissingSession: return "MissingSession";
    case CreatorToolReject::SessionMismatch: return "SessionMismatch";
    case CreatorToolReject::EpochStale: return "EpochStale";
    case CreatorToolReject::MissingWorkspace: return "MissingWorkspace";
    case CreatorToolReject::StageNotAllowed: return "StageNotAllowed";
    case CreatorToolReject::MissingArgument: return "MissingArgument";
    }
    return "Unknown";
}

CreatorToolRoute RouteCreatorTool(std::string_view tool, const CreatorToolArgs& args,
                                 const CreatorToolContext& context) {
    CreatorToolRoute route;

    if (!IsCreatorTool(tool)) {
        route.reject = CreatorToolReject::UnknownTool;
        route.reason = "这个工具不在创作工具名册里:" + std::string(tool);
        return route;
    }
    ParseCreatorTool(tool, &route.tool);
    route.mutating = IsMutatingCreatorTool(route.tool);
    route.args = args;

    // 归属先判。模型伪造/复用别的 sessionId 一律拒绝 —— 这不是权限问题,是
    // "能不能访问另一个作品"的问题,所以它排在阶段与参数之前。
    if (args.sessionId.empty()) {
        route.reject = CreatorToolReject::MissingSession;
        route.reason = "工具调用没有带会话 ID。";
        return route;
    }
    if (args.sessionId != context.sessionId) {
        route.reject = CreatorToolReject::SessionMismatch;
        route.reason = "工具调用的会话 ID 不属于当前作品。";
        return route;
    }
    // 工作区以宿主记录为准。模型声称的只用于对账,对不上就是跨作品。
    if (args.workspaceRoot != context.workspaceRoot) {
        route.reject = CreatorToolReject::MissingWorkspace;
        route.reason = "工具调用的工作区与当前作品不符。";
        return route;
    }
    // 取消之后一律不再接受:这一轮已经结束,再让它改内容只会制造不可解释的变更。
    if (context.cancelRequested) {
        route.reject = CreatorToolReject::EpochStale;
        route.reason = "作品已请求停止,不再接受工具调用。";
        return route;
    }

    // 阶段。读与查能力放得宽(Ready 之后用户仍要能看自己拿到的东西);
    // 会改候选的工具只在制作与修复的那几个阶段出现。
    const int stage = context.stage;
    bool stageOk = false;
    switch (route.tool) {
    case CreatorToolName::PackageRead:
    case CreatorToolName::SkillGet:
    case CreatorToolName::CapabilitiesGet:
        stageOk = true;   // 任何阶段都可以读
        break;
    case CreatorToolName::PreviewEvidence:
        // 就绪之后可以再采一份证据,但不能在应用之后。
        stageOk = stage == StageRendering || stage == StageReviewing || stage == StageReady;
        break;
    case CreatorToolName::PackageUpdate:
    case CreatorToolName::AssetImport:
    case CreatorToolName::ImageGenerate:
    case CreatorToolName::CandidateSubmit:
        stageOk = stage == StagePreparing || stage == StageGenerating ||
                  stage == StageValidating || stage == StageRepairing;
        break;
    }
    if (!stageOk) {
        route.reject = CreatorToolReject::StageNotAllowed;
        route.reason = std::string("当前阶段不接受 ") + ToString(route.tool);
        return route;
    }

    // 必填参数。缺参数的拒绝是可重试的,所以它排在最后:先让调用本身站得住。
    if (route.tool == CreatorToolName::PackageRead ||
        route.tool == CreatorToolName::PackageUpdate) {
        if (args.relativePath.empty()) {
            route.reject = CreatorToolReject::MissingArgument;
            route.reason = std::string(ToString(route.tool)) + " 需要 relativePath。";
            return route;
        }
    }
    if (route.tool == CreatorToolName::PackageUpdate && args.content.empty()) {
        route.reject = CreatorToolReject::MissingArgument;
        route.reason = "creator_package_update 需要 content。";
        return route;
    }
    if (route.tool == CreatorToolName::AssetImport && args.source.empty()) {
        route.reject = CreatorToolReject::MissingArgument;
        route.reason = "creator_asset_import 需要 source(只能是宿主托管来源)。";
        return route;
    }
    if (route.tool == CreatorToolName::CandidateSubmit && args.digest.empty()) {
        route.reject = CreatorToolReject::MissingArgument;
        route.reason = "creator_candidate_submit 需要 digest(候选摘要)。摘要由宿主对封存快照计算,"
                       "不能由模型提供字符串代替。";
        return route;
    }

    route.allowed = true;
    route.reject = CreatorToolReject::None;
    route.reason.clear();
    return route;
}

} // namespace miaodesk::creator
