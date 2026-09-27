#pragma once

#include <windows.h>
#include <shlobj.h>

#include <array>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace miaodesk::paths {
namespace fs = std::filesystem;

// Resolve mutable application state from one place. LOCALAPPDATA is checked
// first so isolated self-tests can redirect state without touching a real user
// profile. Known Folder and the Windows temporary directory are fallbacks.
inline fs::path LocalAppDataDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD count = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count > 0 && count < buffer.size()) {
        return fs::path(std::wstring(buffer.data(), count));
    }

    PWSTR knownFolder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &knownFolder)) &&
        knownFolder) {
        fs::path result(knownFolder);
        CoTaskMemFree(knownFolder);
        return result;
    }

    const DWORD tempCount = GetTempPathW(
        static_cast<DWORD>(buffer.size()), buffer.data());
    if (tempCount > 0 && tempCount < buffer.size()) {
        return fs::path(std::wstring(buffer.data(), tempCount));
    }
    return {};
}

inline fs::path StateRoot() {
    const fs::path base = LocalAppDataDirectory();
    return base.empty() ? fs::path{} : base / L"MiaoDesk";
}

inline fs::path EnsureDirectory(fs::path directory) {
    if (directory.empty()) return {};
    std::error_code ec;
    fs::create_directories(directory, ec);
    return ec ? fs::path{} : directory;
}

inline fs::path EnsureStateRoot() {
    return EnsureDirectory(StateRoot());
}

inline fs::path StateFile(std::wstring_view name) {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / name;
}

inline fs::path WallpaperLibraryRoot() {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / L"WallpaperLibrary";
}

inline fs::path WallpaperPackagesRoot() {
    const fs::path root = WallpaperLibraryRoot();
    return root.empty() ? fs::path{} : root / L"Packages";
}

inline fs::path DesktopWidgetsRoot() {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / L"DesktopWidgets";
}

inline fs::path WidgetPackagesRoot() {
    const fs::path root = DesktopWidgetsRoot();
    return root.empty() ? fs::path{} : root / L"Packages";
}

inline fs::path PiAgentRoot() {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / L"PiAgent";
}

inline fs::path HarnessStateRoot() {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / L"HarnessState";
}

inline fs::path WebView2Root() {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / L"WebView2";
}

// 创作会话的工作区(CCA-03/CCA-04/CCA-05 的落盘点)。
//
// 为什么必须有这一项:`PiRuntime` 只在 launchProfile_.mode == Creator 时导出
// MIAODESK_CREATOR_WORKSPACE,而 src/app/main.cpp 的 RunCreatorTool 正是从那个环境
// 变量取工作区。于是"工作区根目录放在哪"一直是**未决定**的状态,而只要它未决定,
// 创作 profile 就装不上(装上了也只能导出一个空路径)。
//
// 形状与上面每一项一致:<StateRoot>/<名字>。此外 main.cpp 的 FilesystemCreatorWorkspace
// 用 root_.parent_path() 放 revisions/ 与 candidate-ledger.state,所以一次作品占
// **一个子目录**:包目录在里,两份宿主持账在它旁边。
inline fs::path CreatorWorkspacesRoot() {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / L"CreatorWorkspaces";
}

inline fs::path CreatorWorkspaceRoot(std::wstring_view sessionId) {
    const fs::path root = CreatorWorkspacesRoot();
    if (root.empty() || sessionId.empty()) return {};
    return root / fs::path(sessionId);
}

inline fs::path GeneratedWallpapersRoot() {
    const fs::path root = StateRoot();
    return root.empty() ? fs::path{} : root / L"GeneratedWallpapers";
}

inline fs::path ExecutableDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD count = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) return {};
    return fs::path(std::wstring(buffer.data(), count)).parent_path();
}

inline fs::path BuiltInWallpaperPackagesRoot() {
    const fs::path directory = ExecutableDirectory();
    return directory.empty() ? fs::path{} : directory / L"Wallpapers";
}

inline fs::path BuiltInWidgetPackagesRoot() {
    const fs::path directory = ExecutableDirectory();
    return directory.empty() ? fs::path{} : directory / L"Widgets";
}

inline fs::path ProductConfigFile() {
    const fs::path directory = ExecutableDirectory();
    return directory.empty() ? fs::path{} : directory / L"Config" / L"product.ini";
}

} // namespace miaodesk::paths
