#include "miaodesk/ContentCreatorBridge.h"

#include "miaodesk/AppPaths.h"

#include <shellapi.h>

#include <filesystem>
#include <string>

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

} // namespace

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
    if (SendToRunningApp(kind)) return true;

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
