#include "miaodesk/MiaoCreatorPathScope.h"

#include <algorithm>
#include <cwctype>   // std::towlower。少了它 mingw 上的 <cwctype> 不会顺带被拉进来。

namespace miaodesk::creator_scope {
namespace {

// 路径比较前统一分隔符与大小写。
//
// 大小写：Windows 的 NTFS 默认大小写不敏感，而 workspace 名是宿主分配的
// （会话 ID），扫描器产出的是模型写的路径 —— 两者大小写不一致时它们是同一个目录。
// 这一条与 `NativeSingletonKey` 对 monitorId 的处理同一个理由。
std::wstring Fold(std::wstring value) {
    for (auto& ch : value) {
        if (ch == L'/') ch = L'\\';
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    // 去掉结尾分隔符:`...\ws` 与 `...\ws\` 是同一个目录。根目录(`\`)留着。
    while (value.size() > 1 && value.back() == L'\\') value.pop_back();
    return value;
}

} // namespace

const char* ToString(CreatorPathScope scope) noexcept {
    switch (scope) {
        case CreatorPathScope::Inside: return "inside";
        case CreatorPathScope::EmptyPath: return "empty-path";
        case CreatorPathScope::EmptyWorkspace: return "empty-workspace";
        case CreatorPathScope::Outside: return "outside";
        case CreatorPathScope::IsWorkspaceItself: return "is-workspace-itself";
    }
    return "unknown";
}

CreatorPathScope ClassifyCreatorPath(std::wstring_view workspaceRoot,
                                     std::wstring_view candidatePath) noexcept {
    if (candidatePath.empty()) return CreatorPathScope::EmptyPath;
    // workspace 未知是**独立的一种结局**，不并进 Outside：并进去会让
    // "对话框不知道自己在做哪个作品"显示成"这条路在别处"，
    // 而前者是缺陷，后者只是模型的漂移。
    if (workspaceRoot.empty()) return CreatorPathScope::EmptyWorkspace;

    const std::wstring root = Fold(std::wstring(workspaceRoot));
    const std::wstring path = Fold(std::wstring(candidatePath));

    // workspace 目录自己不是包。用户/模型把根目录当结果交上来时，
    // 收下它会让后续每一步都在一个目录上打转。
    if (path == root) return CreatorPathScope::IsWorkspaceItself;

    // 前缀判断要走完整的一段目录名:`ws2` 不是 `ws` 的子目录。
    // 这一条不是洁癖 —— 会话 ID 同名前缀是很可能的（同 kind 的作品共用父目录）。
    std::wstring prefix = root;
    if (prefix.back() != L'\\') prefix.push_back(L'\\');
    if (path.size() > prefix.size() &&
        path.compare(0, prefix.size(), prefix) == 0) {
        return CreatorPathScope::Inside;
    }
    return CreatorPathScope::Outside;
}

bool CreatorPathScopeIsUsable(CreatorPathScope scope) noexcept {
    return scope == CreatorPathScope::Inside;
}

const char* ExplainCreatorPathScope(CreatorPathScope scope) noexcept {
    switch (scope) {
        case CreatorPathScope::Inside: return "";
        case CreatorPathScope::EmptyPath: return "这一轮没有给出内容包路径";
        case CreatorPathScope::EmptyWorkspace:
            return "当前没有确定的作品工作区，无法判断这个路径属于哪个作品";
        case CreatorPathScope::Outside:
            return "给出的路径不在当前作品的工作区里，已忽略（它可能属于另一个作品）";
        case CreatorPathScope::IsWorkspaceItself:
            return "给出的路径是工作区目录自己，不是内容包";
    }
    return "";
}

} // namespace miaodesk::creator_scope
