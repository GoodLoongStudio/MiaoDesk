#include <windows.h>

#include "miaodesk/AppPaths.h"

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path ExecutableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}

bool DirectoryExists(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::wstring CurrentPath() {
    const DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (needed == 0) return {};
    std::wstring value(static_cast<std::size_t>(needed), L'\0');
    const DWORD length = GetEnvironmentVariableW(L"PATH", value.data(), needed);
    if (length == 0 || length >= needed) return {};
    value.resize(length);
    return value;
}

void PrependBundledHarnessRuntimeToPath() {
    const fs::path appDir = ExecutableDirectory();
    if (appDir.empty()) return;

    const fs::path nodeDir = appDir / L"Runtime" / L"Node";
    if (!DirectoryExists(nodeDir)) return;

    std::wstring prefix = nodeDir.wstring();

    // Runtime V3 owns exactly one Agent dependency graph. Prefer its .bin
    // directory; keep V2 fallbacks only while older installed packages remain
    // supported during the migration.
    fs::path binDir = appDir / L"Runtime" / L"Agent" / L"node_modules" / L".bin";
    if (!DirectoryExists(binDir)) binDir = appDir / L"node_modules" / L".bin";
    if (!DirectoryExists(binDir)) binDir = nodeDir / L"node_modules" / L".bin";
    if (DirectoryExists(binDir)) prefix += L";" + binDir.wstring();

    const std::wstring oldPath = CurrentPath();
    if (!oldPath.empty()) prefix += L";" + oldPath;
    SetEnvironmentVariableW(L"PATH", prefix.c_str());

    const fs::path stateRoot = miaodesk::paths::EnsureDirectory(
        miaodesk::paths::HarnessStateRoot());
    if (!stateRoot.empty()) {
        const std::wstring dshHome = (stateRoot / L"dsh-home").wstring();
        const std::wstring npmCache = (stateRoot / L"npm-cache").wstring();
        SetEnvironmentVariableW(L"DSH_HOME", dshHome.c_str());
        SetEnvironmentVariableW(L"npm_config_cache", npmCache.c_str());
    }
}

struct BundledRuntimeBootstrap final {
    BundledRuntimeBootstrap() { PrependBundledHarnessRuntimeToPath(); }
};

BundledRuntimeBootstrap g_bundledRuntimeBootstrap;

} // namespace
