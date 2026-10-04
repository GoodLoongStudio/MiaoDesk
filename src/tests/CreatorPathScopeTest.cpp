// CREATE-06「改的是哪个 workspace」的归属判定回归。
//
// 逮到的不是"它会算错",而是**它在本机一行都验不到,而且对话框结构上问不出这个问题**:
// `InspectForGeneratedPackage` 扫到路径就直接 `SetGeneratedPackage`,
// 而 `DialogState` 里**没有 workspaceRoot 字段** —— 它在 UseWorkspace / ResetSession /
// CreatorConversationPath 里各自临时解析一次,用完就丢。
//
// 后果:模型回复里提到另一个 workspace 的路径时,那个包会成为本轮结果并可应用到桌面。
// 用户在 A 作品上说"再小一点",落到桌面上的是 B 作品。
#include "miaodesk/MiaoCreatorPathScope.h"

#include <cstdio>
#include <string>

namespace miaodesk {
namespace creator_scope {
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

constexpr std::wstring_view kWorkspace = L"C:\\Users\\u\\AppData\\Local\\MiaoDesk\\CreatorWorkspaces\\wallpaper\\1710000000";

} // namespace
} // namespace creator_scope
} // namespace miaodesk

int wmain() {
    using namespace miaodesk::creator_scope;

    // ---- 1. 主要失败形态:路径在别的 workspace ----
    {
        Check(ClassifyCreatorPath(kWorkspace,
                                  L"C:\\Users\\u\\AppData\\Local\\MiaoDesk\\CreatorWorkspaces\\wallpaper\\1799999999\\scene.mdwall") ==
                  CreatorPathScope::Outside,
              "同 kind 另一个 session 的路径 → Outside(用户在 A 上说'再小一点',不能落到 B)");
        Check(ClassifyCreatorPath(kWorkspace,
                                  L"C:\\Users\\u\\AppData\\Local\\MiaoDesk\\CreatorWorkspaces\\widget\\1710000000\\x.mdwidget") ==
                  CreatorPathScope::Outside,
              "另一个 kind 的同 session ID → Outside(kind 不同作品也不同)");
        Check(ClassifyCreatorPath(kWorkspace, L"D:\\somewhere\\else\\scene.mdwall") == CreatorPathScope::Outside,
              "完全另一个盘 → Outside");
        Check(ClassifyCreatorPath(kWorkspace, L"C:\\Users\\u\\AppData\\Local\\MiaoDesk\\CreatorWorkspaces\\wallpaper\\scene.mdwall") ==
                  CreatorPathScope::Outside,
              "父目录（kind 根）下的路径 → Outside(那不是任何作品的工作区)");
    }

    // ---- 2. 同名前缀不是子目录 ----
    // 会话 ID 同名前缀完全可能（同 kind 的作品共用父目录，ID 由时间戳派生）。
    // 前缀判断必须走完整一段目录名，否则 ws=1710000000 会把 17100000007 也当自己家。
    {
        Check(ClassifyCreatorPath(L"C:\\ws\\171", L"C:\\ws\\1711\\scene.mdwall") == CreatorPathScope::Outside,
              "1711 不是 171 的子目录(同名前缀)");
        Check(ClassifyCreatorPath(L"C:\\ws\\171", L"C:\\ws\\171\\scene.mdwall") == CreatorPathScope::Inside,
              "171 自己下面的路径 → Inside");
    }

    // ---- 3. 在里面 ----
    {
        Check(ClassifyCreatorPath(kWorkspace, std::wstring(kWorkspace) + L"\\scene.mdwall") == CreatorPathScope::Inside,
              "workspace 直属的包 → Inside");
        Check(ClassifyCreatorPath(kWorkspace, std::wstring(kWorkspace) + L"\\assets\\a.png") == CreatorPathScope::Inside,
              "深层子路径 → Inside");
        Check(ClassifyCreatorPath(kWorkspace, std::wstring(kWorkspace) + L"\\a\\b\\c\\scene.mdwall") == CreatorPathScope::Inside,
              "任意深层 → Inside");
    }

    // ---- 4. 分隔符与大小写 ----
    // Windows 上 `/` 与 `\\` 都合法，paths::EnsureDirectory 与模型输出都可能用任一种；
    // NTFS 默认大小写不敏感，而 workspace 名是宿主分配的、路径是模型写的。
    {
        Check(ClassifyCreatorPath(kWorkspace, L"c:/users/u/appdata/local/miaodesk/creatorworkspaces/wallpaper/1710000000/scene.mdwall") ==
                  CreatorPathScope::Inside,
              "全小写 + 正斜杠 → Inside(与 workspace 是同一个目录)");
        Check(ClassifyCreatorPath(L"C:\\WS", L"c:\\ws\\scene.mdwall") == CreatorPathScope::Inside,
              "盘符/目录大小写不同 → Inside");
        Check(ClassifyCreatorPath(L"C:\\ws", L"C:\\ws\\scene.mdwall") == CreatorPathScope::Inside,
              "结尾无分隔符不影响");
        Check(ClassifyCreatorPath(L"C:\\ws\\", L"C:\\ws\\scene.mdwall") == CreatorPathScope::Inside,
              "workspace 自己带结尾分隔符 → 仍 Inside");
        Check(ClassifyCreatorPath(L"\\\\?\\C:\\ws", L"C:\\ws\\scene.mdwall") == CreatorPathScope::Outside,
              "长路径前缀(\\\\?\\)与普通路径当前判为不同 —— 登记为已知口径,不假装兼容");
    }

    // ---- 5. workspace 目录自己不是包 ----
    {
        Check(ClassifyCreatorPath(kWorkspace, kWorkspace) == CreatorPathScope::IsWorkspaceItself,
              "路径就是 workspace 自己 → IsWorkspaceItself");
        Check(ClassifyCreatorPath(kWorkspace, std::wstring(kWorkspace) + L"\\") == CreatorPathScope::IsWorkspaceItself,
              "带结尾分隔符的 workspace 自己 → 同样");
    }

    // ---- 6. 空与未知 ----
    {
        Check(ClassifyCreatorPath(kWorkspace, L"") == CreatorPathScope::EmptyPath, "空路径 → EmptyPath");
        // 这一条是设计:workspace 未知是**独立结局**,不并进 Outside。
        // 并进去会让"对话框不知道自己在做哪个作品"显示成"路在别处" ——
        // 而前者是缺陷,后者只是模型漂移。
        Check(ClassifyCreatorPath(L"", L"C:\\ws\\171\\scene.mdwall") == CreatorPathScope::EmptyWorkspace,
              "workspace 未知 → EmptyWorkspace(不并进 Outside)");
        Check(ClassifyCreatorPath(L"", L"") == CreatorPathScope::EmptyPath,
              "两者都空 → 先报路径(它更靠前,也更能说清这一轮发生了什么)");
    }

    // ---- 7. 只有 Inside 能用 ----
    {
        Check(CreatorPathScopeIsUsable(CreatorPathScope::Inside), "Inside 可用");
        Check(!CreatorPathScopeIsUsable(CreatorPathScope::Outside), "Outside 不可用");
        Check(!CreatorPathScopeIsUsable(CreatorPathScope::EmptyWorkspace),
              "EmptyWorkspace 不可用(不知道在哪 ≠ 在里面)");
        Check(!CreatorPathScopeIsUsable(CreatorPathScope::EmptyPath), "EmptyPath 不可用");
        Check(!CreatorPathScopeIsUsable(CreatorPathScope::IsWorkspaceItself), "workspace 自己不可用");
    }

    // ---- 8. 五种说法各有各的内容 ----
    {
        const CreatorPathScope all[] = {CreatorPathScope::Inside, CreatorPathScope::EmptyPath,
                                        CreatorPathScope::EmptyWorkspace, CreatorPathScope::Outside,
                                        CreatorPathScope::IsWorkspaceItself};
        std::string seen;
        for (auto s : all) {
            const std::string text = ExplainCreatorPathScope(s);
            if (s == CreatorPathScope::Inside) {
                Check(text.empty(), "Inside 没有要说的话");
                continue;
            }
            Check(!text.empty(), std::string("结局 ") + ToString(s) + " 有话说");
            Check(seen.find(text) == std::string::npos, std::string("结局 ") + ToString(s) + " 的说法独一无二");
            seen += text;
        }
        // Outside 那句必须点明"可能属于另一个作品" —— 那是用户唯一能自个儿发现的信息。
        Check(std::string(ExplainCreatorPathScope(CreatorPathScope::Outside)).find("另一个作品") != std::string::npos,
              "Outside 的话点明它可能属于另一个作品");
        Check(std::string(ExplainCreatorPathScope(CreatorPathScope::EmptyWorkspace)).find("工作区") != std::string::npos,
              "EmptyWorkspace 的话点明是工作区未知");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\ncreator 路径归属:全部 %d 项通过\n", g_checks);
    return 0;
}
