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
