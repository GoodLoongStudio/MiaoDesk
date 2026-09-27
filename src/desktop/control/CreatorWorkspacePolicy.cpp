#include "miaodesk/CreatorWorkspacePolicy.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace miaodesk::creator {
namespace {

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool HasPrefix(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// 扩展名(带点)。没有点的文件按"无扩展名"处理。
std::string ExtensionOf(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    return LowerAscii(std::string(name.substr(dot)));
}

// 规范化时统一用正斜杠;调用方拿到的 resolved 也要能匹配这个形状。
std::string NormalizeSeparators(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        out.push_back(ch == '\\' ? '/' : ch);
    }
    // "a//b" 与 "a/./b" 收敛成 "a/b":两种写法都要能归类到同一个角色,否则同一份
    // 内容换一种写法就会被判成 UnknownRole。
    std::string collapsed;
    for (std::size_t i = 0; i < out.size();) {
        if (out[i] == '/') {
            collapsed.push_back('/');
            while (i < out.size() && out[i] == '/') ++i;
            continue;
        }
        collapsed.push_back(out[i++]);
    }
    std::string result;
    bool changed = true;
    std::string current = collapsed;
    while (changed) {
        changed = false;
        if (HasPrefix(current, "./")) { current = current.substr(2); changed = true; }
        const std::string segment = "/./";
        const auto at = current.find(segment);
        if (at != std::string::npos) {
            current = current.substr(0, at) + current.substr(at + segment.size() - 1);
            changed = true;
        }
        result = current;
    }
    return result;
}

} // namespace

const char* ToString(CreatorWorkspaceReject reject) noexcept {
    switch (reject) {
    case CreatorWorkspaceReject::None: return "None";
    case CreatorWorkspaceReject::EmptySessionId: return "EmptySessionId";
    case CreatorWorkspaceReject::EmptyRelativePath: return "EmptyRelativePath";
    case CreatorWorkspaceReject::NotRelative: return "NotRelative";
    case CreatorWorkspaceReject::Traversal: return "Traversal";
    case CreatorWorkspaceReject::ForbiddenExtension: return "ForbiddenExtension";
    case CreatorWorkspaceReject::UnknownRole: return "UnknownRole";
    case CreatorWorkspaceReject::TooLarge: return "TooLarge";
    case CreatorWorkspaceReject::OutsideWorkspace: return "OutsideWorkspace";
    case CreatorWorkspaceReject::ReparsePoint: return "ReparsePoint";
    case CreatorWorkspaceReject::WrongWorkspace: return "WrongWorkspace";
    case CreatorWorkspaceReject::NoContentPackageYet: return "NoContentPackageYet";
    }
    return "Unknown";
}

const char* CreatorFileRoleName(CreatorFileRole role) noexcept {
    switch (role) {
    case CreatorFileRole::Manifest: return "manifest";
    case CreatorFileRole::Parameters: return "parameters";
    case CreatorFileRole::Scene: return "scene";
    case CreatorFileRole::Preview: return "preview";
    case CreatorFileRole::Asset: return "asset";
    }
    return "unknown";
}

bool ParseCreatorFileRole(std::string_view text, CreatorFileRole* role) noexcept {
    if (!role) return false;
    if (text == "manifest") *role = CreatorFileRole::Manifest;
    else if (text == "parameters") *role = CreatorFileRole::Parameters;
    else if (text == "scene") *role = CreatorFileRole::Scene;
    else if (text == "preview") *role = CreatorFileRole::Preview;
    else if (text == "asset") *role = CreatorFileRole::Asset;
    else return false;
    return true;
}

bool NormalizeCreatorRelativePath(std::string_view input, std::string* normalized) {
    if (!normalized) return false;
    normalized->clear();
    if (input.empty()) return false;

    // 绝对路径的每一种写法都要在这里被拒绝:盘符、UNC、根斜杠、以及反斜杠形式。
    // 只查 ".." 是不够的 —— "/etc/passwd" 和 "C:\Windows\x" 都不含 ".."。
    if (input.find(':') != std::string_view::npos) return false;
    if (input.front() == '/' || input.front() == '\\') return false;
    if (input.size() >= 2 && input[0] == '\\' && input[1] == '\\') return false;

    std::string path = NormalizeSeparators(input);
    if (path.empty()) return false;
    if (path.front() == '/') return false;

    // 逐段查:任何 ".." 段都不允许,即使是 "a/../../b" 这种后半段会回到工作区里的。
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = path.find('/', start);
        const std::string segment = path.substr(
            start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segment == "..") return false;
        if (segment.empty()) return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    *normalized = path;
    return true;
}

bool CreatorWorkspacePolicy::IsForbiddenExtension(std::string_view extension) noexcept {
    // 声明式内容 + 受控素材。计划 §1:"本轮内容生成限定为当前受支持的声明式 Scene
    // 内容及受控素材;沿用现有 Skill 对 Web、脚本与 3D 的限制"。
    // 所以这些不是"暂时没实现",而是明确不在创作范围里 —— 放在这里让范围变成
    // 一条可引用的规则,而不是散落在各处的黑名单。
    static const std::set<std::string> kForbidden = {
        ".html", ".htm", ".css", ".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx",
        ".json5", ".exe", ".dll", ".bat", ".cmd", ".ps1", ".sh", ".py", ".rb",
        ".pl", ".vbs", ".reg", ".msi", ".appx", ".jar", ".so", ".dylib", ".com",
        ".scr", ".lnk", ".url", ".sys", ".drv", ".cpl",
    };
    return extension.empty() ? false : kForbidden.count(std::string(extension)) > 0;
}

bool CreatorWorkspacePolicy::IsAllowedAssetExtension(std::string_view extension) noexcept {
    static const std::set<std::string> kAllowed = {
        ".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp",
        ".mp4", ".webm", ".mov", ".mkv",
        ".mp3", ".wav", ".ogg", ".flac", ".m4a",
        ".ttf", ".otf",
    };
    return extension.empty() ? false : kAllowed.count(std::string(extension)) > 0;
}

bool CreatorWorkspacePolicy::IsAllowedImageExtension(std::string_view extension) noexcept {
    static const std::set<std::string> kAllowed = {".png", ".jpg", ".jpeg", ".webp"};
    return extension.empty() ? false : kAllowed.count(std::string(extension)) > 0;
}

CreatorWorkspacePolicy::CreatorWorkspacePolicy(CreatorSessionBinding binding,
                                             CreatorWorkspaceLimits limits)
    : binding_(std::move(binding)), limits_(limits) {}

bool CreatorWorkspacePolicy::SessionMatches() const noexcept {
    if (binding_.sessionId.empty() || binding_.workspaceRoot.empty()) return false;
    if (binding_.claimedWorkspace.empty()) return false;
    // 大小写与分隔符不重要 —— 模型换一种写法不代表换了作品。真正要比的是
    // "归一化之后是不是同一个根",所以这里只做形状级比较,严格的同盘判定由
    // Resolve + WorkspaceContainment 完成。
    std::string a = binding_.workspaceRoot;
    std::string b = binding_.claimedWorkspace;
    std::replace(a.begin(), a.end(), '\\', '/');
    std::replace(b.begin(), b.end(), '\\', '/');
    while (!a.empty() && a.back() == '/') a.pop_back();
    while (!b.empty() && b.back() == '/') b.pop_back();
    return LowerAscii(a) == LowerAscii(b);
}

bool CreatorWorkspacePolicy::Classify(std::string_view relativePath,
                                     CreatorFileRole* role) const {
    if (!role) return false;
    std::string normalized;
    if (!NormalizeCreatorRelativePath(relativePath, &normalized)) return false;

    if (normalized == "manifest.json") { *role = CreatorFileRole::Manifest; return true; }
    if (normalized == "parameters.json") { *role = CreatorFileRole::Parameters; return true; }

    const std::string extension = ExtensionOf(normalized);
    if (HasPrefix(normalized, "scene/")) {
        // scene 目录下只接受 .json:scene 是声明式内容,不是任意文件堆。
        if (extension != ".json") return false;
        const std::string name = normalized.substr(6);
        if (name.empty() || name.find('/') != std::string::npos) return false;
        *role = CreatorFileRole::Scene;
        return true;
    }
    if (HasPrefix(normalized, "assets/")) {
        const std::string name = normalized.substr(7);
        // 不允许在 assets 下再建深层目录:越深的层级越容易藏越界路径,
        // 而现有包契约里素材本来就是平铺的。
        if (name.empty() || name.find('/') != std::string::npos) return false;
        if (!IsAllowedAssetExtension(extension)) return false;
        *role = CreatorFileRole::Asset;
        return true;
    }
    if (HasPrefix(normalized, "preview.")) {
        if (!IsAllowedImageExtension(extension)) return false;
        *role = CreatorFileRole::Preview;
        return true;
    }
    return false;
}

bool CreatorWorkspacePolicy::Resolve(std::string_view relativePath,
                                    std::string* resolved) const {
    if (!resolved) return false;
    resolved->clear();
    std::string normalized;
    if (!NormalizeCreatorRelativePath(relativePath, &normalized)) return false;
    std::string root = binding_.workspaceRoot;
    std::replace(root.begin(), root.end(), '\\', '/');
    while (!root.empty() && root.back() == '/') root.pop_back();
    if (root.empty()) return false;
    *resolved = root + "/" + normalized;
    return true;
}

bool CreatorWorkspacePolicy::Allows(std::string_view relativePath, const CreatorFileFacts& facts,
                                  std::string* reason, CreatorWorkspaceReject* rejectCode) const {
    const auto reject = [&](CreatorWorkspaceReject code) {
        if (reason) *reason = Explain(code, relativePath);
        if (rejectCode) *rejectCode = code;
        return false;
    };

    if (binding_.sessionId.empty()) return reject(CreatorWorkspaceReject::EmptySessionId);
    if (relativePath.empty()) return reject(CreatorWorkspaceReject::EmptyRelativePath);
    // 归属先判:工作区对不上,后面每一步都不必做。这不是冗余 —— 一个越界路径
    // 也可能碰巧形状合法,那时唯一还站得住的就是归属。
    if (!SessionMatches()) return reject(CreatorWorkspaceReject::WrongWorkspace);

    std::string normalized;
    if (!NormalizeCreatorRelativePath(relativePath, &normalized)) {
        // 区分"结构上就不是相对路径"和"含 .. 遍历":两种要给的提示不同。
        if (relativePath.find(':') != std::string_view::npos ||
            relativePath.front() == '/' || relativePath.front() == '\\') {
            return reject(CreatorWorkspaceReject::NotRelative);
        }
        return reject(CreatorWorkspaceReject::Traversal);
    }

    const std::string extension = ExtensionOf(normalized);
    if (IsForbiddenExtension(extension)) return reject(CreatorWorkspaceReject::ForbiddenExtension);

    CreatorFileRole role{};
    if (!Classify(normalized, &role)) return reject(CreatorWorkspaceReject::UnknownRole);

    std::uint64_t limit = limits_.maxAssetBytes;
    switch (role) {
    case CreatorFileRole::Manifest: limit = limits_.maxManifestBytes; break;
    case CreatorFileRole::Parameters: limit = limits_.maxParameterBytes; break;
    case CreatorFileRole::Scene: limit = limits_.maxSceneBytes; break;
    case CreatorFileRole::Preview: limit = limits_.maxPreviewBytes; break;
    case CreatorFileRole::Asset: limit = limits_.maxAssetBytes; break;
    }
    if (facts.byteCount > limit) return reject(CreatorWorkspaceReject::TooLarge);

    if (facts.isDirectory) return reject(CreatorWorkspaceReject::UnknownRole);
    // reparse point 是"路径形状完全合法、指向却在外面"的那一类。必须由宿主提供事实,
    // 因为判定它需要系统调用,而策略自己不能碰盘。
    if (facts.isReparsePoint) return reject(CreatorWorkspaceReject::ReparsePoint);

    if (rejectCode) *rejectCode = CreatorWorkspaceReject::None;
    return true;
}

std::string CreatorWorkspacePolicy::Explain(CreatorWorkspaceReject reject,
                                          std::string_view relativePath) {
    const std::string path(relativePath);
    switch (reject) {
    case CreatorWorkspaceReject::None:
        return {};
    case CreatorWorkspaceReject::EmptySessionId:
        return "工具调用没有带会话 ID。";
    case CreatorWorkspaceReject::EmptyRelativePath:
        return "没有给出包内路径。";
    case CreatorWorkspaceReject::NotRelative:
        return "只接受包内相对路径,不接受绝对路径:" + path;
    case CreatorWorkspaceReject::Traversal:
        return "包内路径不能包含 ..:" + path;
    case CreatorWorkspaceReject::ForbiddenExtension:
        return "声明式内容包不接受这类文件(代码/可执行文件不在创作范围内):" + path;
    case CreatorWorkspaceReject::UnknownRole:
        return "这个路径不在获准的包文件布局内(manifest.json / parameters.json / "
               "scene/*.json / preview.<图片> / assets/<受控素材>):" + path;
    case CreatorWorkspaceReject::TooLarge:
        return "文件超出该类型的上限:" + path;
    case CreatorWorkspaceReject::OutsideWorkspace:
        return "解析后落在本作品工作区之外:" + path;
    case CreatorWorkspaceReject::ReparsePoint:
        return "这个路径是符号链接/联接点,已拒绝:" + path;
    case CreatorWorkspaceReject::WrongWorkspace:
        return "工具调用的工作区与当前作品不符,已拒绝。";
    case CreatorWorkspaceReject::NoContentPackageYet:
        return "还没有 manifest.json,不能先写 scene 或素材。";
    }
    return "已拒绝:" + path;
}

std::string DeriveCreatorSessionId(std::string_view workspaceRoot) noexcept {
    // 末尾的分隔符要先去掉:"C:\ws\S-1\" 的最后一段是空串,不是 "S-1"。
    std::size_t end = workspaceRoot.size();
    while (end > 0 && (workspaceRoot[end - 1] == '\\' || workspaceRoot[end - 1] == '/')) --end;
    if (end == 0) return {};
    const std::size_t slash = workspaceRoot.find_last_of("\\/", end - 1);
    const std::size_t begin = slash == std::string_view::npos ? 0 : slash + 1;
    if (begin >= end) return {};
    // 裸盘符不是目录名。"C:\" 与 "D:\" 去掉尾分隔符后分别是 "C:" 和 "D:" ——
    // 把它们当会话 ID 的话,同一个盘上所有作品共用一个会话,而归属判断正是靠这个
    // 字符串区分作品的。根路径没有"最后一个目录",所以它没有会话。
    if (end - begin == 2 && workspaceRoot[begin + 1] == ':') return {};
    return std::string(workspaceRoot.substr(begin, end - begin));
}

} // namespace miaodesk::creator
