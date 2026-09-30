#include "miaodesk/ContentCreatorBridge.h"

#include "miaodesk/AppPaths.h"
#include "miaodesk/CreatorWorkspaceState.h"

#include <shellapi.h>

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::creator {
namespace {

constexpr wchar_t kSearchWindowClass[] = L"MiaoDesk.Native.SearchWindow";

const wchar_t* CommandLineToken(ContentCreatorKind kind) noexcept {
    switch (kind) {
    case ContentCreatorKind::Wallpaper: return L"--content-creator=wallpaper";
    case ContentCreatorKind::Widget: return L"--content-creator=widget";
    case ContentCreatorKind::None: break;
    }
    return L"";
}

bool Valid(ContentCreatorKind kind) noexcept {
    return kind == ContentCreatorKind::Wallpaper || kind == ContentCreatorKind::Widget;
}

const wchar_t* KindFolder(ContentCreatorKind kind) noexcept {
    switch (kind) {
    case ContentCreatorKind::Wallpaper: return L"wallpaper";
    case ContentCreatorKind::Widget: return L"widget";
    case ContentCreatorKind::None: break;
    }
    return L"";
}

std::wstring Utf8ToWideForPath(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), count);
    return out;
}

std::string WideToUtf8ForPath(std::wstring_view text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        out.data(), count, nullptr, nullptr);
    return out;
}

fs::path CreatorKindRoot(ContentCreatorKind kind) {
    const fs::path root = miaodesk::paths::CreatorWorkspacesRoot();
    if (root.empty() || !Valid(kind)) return {};
    return root / KindFolder(kind);
}

// active 文件读出来的是**这段会话身份本身**,不是别的什么。写成别的形状(JSON、
// 带前后缀的一行)会让"重开复用的到底是哪个目录"变成一个要解析的问题 —— 而这里要的
// 只是一个目录名。读失败或内容不可用都当"没有",由调用方新建。
std::string ReadActiveSession(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return {};
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    // 过滤规则不在这一行里,而在 CreatorWorkspaceState::SanitizeCreatorSessionId ——
    // 生成方与读取方共用同一个函数,"读回来还是它自己"才是一条等式。
    return miaodesk::creator::SanitizeCreatorSessionId(text);
}

// 写一半比不写更糟:下次打开会读到半个目录名,而那个目录不存在,于是工具调用以
// "路径不在布局内"被拒,原因看起来像模型写错了路径。所以先写临时文件再换上去。
void WriteActiveSession(const fs::path& file, const std::string& session) {
    auto temporary = file;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return;
        stream.write(session.data(), static_cast<std::streamsize>(session.size()));
        if (!stream) return;
    }
    MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace

std::wstring ResolveCreatorWorkspaceRoot(ContentCreatorKind kind) {
    if (!Valid(kind)) return {};
    const fs::path perKind = CreatorKindRoot(kind);
    if (perKind.empty()) return {};
    miaodesk::paths::EnsureDirectory(perKind);

    const fs::path activeFile = perKind / L"active";
    std::string session = ReadActiveSession(activeFile);
    if (session.empty()) {
        const auto now = static_cast<std::uint64_t>(::GetTickCount64());
        session = miaodesk::creator::NewCreatorSessionId(now);
        WriteActiveSession(activeFile, session);
    }

    const fs::path workspace = perKind / fs::path(Utf8ToWideForPath(session));
    miaodesk::paths::EnsureDirectory(workspace);
    return workspace.wstring();
}

std::wstring StartNewCreatorWorkspace(ContentCreatorKind kind) {
    if (!Valid(kind)) return {};
    const fs::path perKind = CreatorKindRoot(kind);
    if (perKind.empty()) return {};
    miaodesk::paths::EnsureDirectory(perKind);

    std::string session;
    fs::path workspace;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto seed = static_cast<std::uint64_t>(::GetTickCount64()) +
                          static_cast<std::uint64_t>(attempt);
        session = miaodesk::creator::NewCreatorSessionId(seed);
        workspace = perKind / fs::path(Utf8ToWideForPath(session));
        std::error_code ec;
        if (!fs::exists(workspace, ec)) break;
        session.clear();
    }
    if (session.empty()) return {};

    if (miaodesk::paths::EnsureDirectory(workspace).empty()) return {};
    WriteActiveSession(perKind / L"active", session);
    return workspace.wstring();
}

bool ActivateCreatorWorkspace(ContentCreatorKind kind, std::wstring_view workspaceRoot) {
    if (!Valid(kind) || workspaceRoot.empty()) return false;
    const fs::path perKind = CreatorKindRoot(kind);
    if (perKind.empty()) return false;

    std::error_code ec;
    const fs::path requested = fs::weakly_canonical(fs::path(workspaceRoot), ec);
    if (ec || !fs::is_directory(requested, ec)) return false;
    const fs::path expectedParent = fs::weakly_canonical(perKind, ec);
    if (ec || requested.parent_path() != expectedParent) return false;

    const std::string session = miaodesk::creator::SanitizeCreatorSessionId(
        WideToUtf8ForPath(requested.filename().wstring()));
    if (session.empty() ||
        Utf8ToWideForPath(session) != requested.filename().wstring()) return false;

    WriteActiveSession(perKind / L"active", session);
    return true;
}

std::vector<std::wstring> ListCreatorWorkspaces(ContentCreatorKind kind) {
    std::vector<std::pair<fs::file_time_type, std::wstring>> ordered;
    const fs::path perKind = CreatorKindRoot(kind);
    std::error_code ec;
    if (perKind.empty() || !fs::is_directory(perKind, ec)) return {};

    for (const auto& entry : fs::directory_iterator(perKind, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        const std::string session = miaodesk::creator::SanitizeCreatorSessionId(
            WideToUtf8ForPath(entry.path().filename().wstring()));
        if (session.empty()) continue;
        const auto updated = fs::last_write_time(entry.path(), ec);
        ordered.emplace_back(ec ? fs::file_time_type::min() : updated,
                             entry.path().wstring());
        ec.clear();
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) {
                  return left.first > right.first;
              });
    std::vector<std::wstring> result;
    result.reserve(ordered.size());
    for (auto& item : ordered) result.push_back(std::move(item.second));
    return result;
}

ContentCreatorKind ParseCommandLine(std::wstring_view commandLine) noexcept {
    if (commandLine.find(L"--content-creator=wallpaper") != std::wstring_view::npos)
        return ContentCreatorKind::Wallpaper;
    if (commandLine.find(L"--content-creator=widget") != std::wstring_view::npos)
        return ContentCreatorKind::Widget;
    return ContentCreatorKind::None;
}

std::wstring InitialPrompt(ContentCreatorKind kind) {
    if (kind == ContentCreatorKind::Wallpaper) {
        return
            L"我要制作一个 MiaoDesk 壁纸。请进入壁纸创作模式：先调用 content_skill_get "
            L"读取 content-package-basics、wallpaper-content 和 content-review，"
            L"然后先问我想做成什么样。生成时先做可预览的 .mdwall 内容包，"
            L"不要未经我确认直接应用到桌面。";
    }
    if (kind == ContentCreatorKind::Widget) {
        return
            L"我要制作一个 MiaoDesk 桌面组件。请进入组件创作模式：先调用 content_skill_get "
            L"读取 content-package-basics、widget-content 和 content-review，"
            L"然后先问我组件要显示什么、尺寸和交互需求。生成时先做可预览的 .mdwidget 内容包，"
            L"不要未经我确认直接放到桌面。";
    }
    return {};
}

bool DecodeCopyData(const COPYDATASTRUCT* data, ContentCreatorKind* kind) noexcept {
    if (kind) *kind = ContentCreatorKind::None;
    if (!data || data->dwData != kContentCreatorCopyDataTag || !data->lpData ||
        data->cbData != sizeof(std::uint32_t))
        return false;
    const auto raw = *static_cast<const std::uint32_t*>(data->lpData);
    const auto decoded = static_cast<ContentCreatorKind>(raw);
    if (!Valid(decoded)) return false;
    if (kind) *kind = decoded;
    return true;
}

bool SendToRunningApp(ContentCreatorKind kind, DWORD timeoutMs) noexcept {
    if (!Valid(kind)) return false;
    const HWND target = FindWindowW(kSearchWindowClass, nullptr);
    if (!target) return false;

    // OpenConversation is commonly called from the foreground Wallpaper Library
    // process while the creator itself is constructed by the already-running
    // MiaoDesk process. Grant that target process foreground activation rights
    // before the synchronous WM_COPYDATA hand-off, otherwise Windows can legally
    // create the requested window behind the library and the caller still reports
    // "opened".
    DWORD targetProcessId = 0;
    GetWindowThreadProcessId(target, &targetProcessId);
    if (targetProcessId && targetProcessId != GetCurrentProcessId())
        AllowSetForegroundWindow(targetProcessId);

    const std::uint32_t raw = static_cast<std::uint32_t>(kind);
    COPYDATASTRUCT data{};
    data.dwData = kContentCreatorCopyDataTag;
    data.cbData = sizeof(raw);
    data.lpData = const_cast<std::uint32_t*>(&raw);
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(
               target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
               SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &result) != 0 &&
           result != 0;
}

bool OpenConversation(ContentCreatorKind kind, HWND owner, std::wstring* error) {
    if (error) error->clear();
    if (!Valid(kind)) {
        if (error) *error = L"未知的内容创作类型。";
        return false;
    }
    const HWND running = FindWindowW(kSearchWindowClass, nullptr);
    if (running) {
        if (SendToRunningApp(kind)) return true;
        if (error) {
            *error = L"已找到正在运行的 MiaoDesk，但 AI 创作窗口没有成功创建。"
                     L"请先关闭旧版 MiaoDesk 后重试，或使用 ARM64 快速测试入口重新启动当前版本。";
        }
        return false;
    }

    const fs::path root = miaodesk::paths::ExecutableDirectory();
    const fs::path exe = root / L"MiaoDesk.exe";
    std::error_code ec;
    if (root.empty() || !fs::is_regular_file(exe, ec)) {
        if (error) *error = L"没有找到 MiaoDesk.exe，无法打开 AI 创作窗口。";
        return false;
    }

    const auto launched = reinterpret_cast<INT_PTR>(ShellExecuteW(
        owner, L"open", exe.c_str(), CommandLineToken(kind), root.c_str(), SW_SHOWNORMAL));
    if (launched <= 32) {
        if (error) *error = L"启动 MiaoDesk AI 创作窗口失败，ShellExecute=" + std::to_wstring(launched);
        return false;
    }
    return true;
}

} // namespace miaodesk::creator
