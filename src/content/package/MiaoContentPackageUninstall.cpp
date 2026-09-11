#include "miaodesk/MiaoContentPackageManager.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

fs::path NormalizedAbsolute(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) absolute = path;
    absolute = absolute.lexically_normal();
    ec.clear();
    const fs::path canonical = fs::weakly_canonical(absolute, ec);
    return ec ? absolute : canonical;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return (ch >= L'A' && ch <= L'Z') ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
    });
    return value;
}

bool PathIsInside(const fs::path& candidate, const fs::path& root) {
    const std::wstring value = Lower(NormalizedAbsolute(candidate).wstring());
    std::wstring base = Lower(NormalizedAbsolute(root).wstring());
    if (value == base) return true;
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/')
        base.push_back(fs::path::preferred_separator);
    return value.size() >= base.size() && value.compare(0, base.size(), base) == 0;
}

std::wstring OperationToken() {
    return std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
}

void CleanupStaleTrash(const fs::path& userRoot) {
    const fs::path trashRoot = userRoot / L".trash";
    std::error_code ec;
    if (!fs::is_directory(trashRoot, ec) || ec) return;

    constexpr auto kMinimumAge = std::chrono::hours(1);
    const auto now = fs::file_time_type::clock::now();
    for (const auto& entry : fs::directory_iterator(trashRoot, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;

        std::error_code timeError;
        const auto modified = fs::last_write_time(entry.path(), timeError);
        if (timeError || now - modified < kMinimumAge) continue;

        std::error_code cleanupError;
        fs::remove_all(entry.path(), cleanupError);
    }

    // Keep maintenance strictly best-effort. Removing an empty hidden parent is
    // cosmetic and must never make uninstall fail.
    ec.clear();
    if (fs::is_empty(trashRoot, ec) && !ec) fs::remove(trashRoot, ec);
}

} // namespace

bool MiaoContentPackageManager::Uninstall(
    ContentKind expectedKind,
    std::wstring_view source,
    ContentPackageUninstallResult* result,
    std::wstring* error) {
    if (!result) return Fail(error, L"Content package uninstall result is null.");
    *result = {};

    ManagedContentPackageInfo package;
    if (!Resolve(expectedKind, source, &package, error)) return false;
    if (package.origin == ManagedContentPackageOrigin::BuiltIn)
        return Fail(error, L"Built-in content packages cannot be uninstalled: " + package.source);
    if (package.origin != ManagedContentPackageOrigin::UserManaged)
        return Fail(error, L"Only user-managed content packages can be uninstalled.");

    const fs::path userRoot = UserRoot(expectedKind);
    if (userRoot.empty() || !PathIsInside(package.packageRoot, userRoot))
        return Fail(error, L"Resolved user package is outside the managed content root.");

    // Previous uninstall operations may have intentionally left locked files in
    // hidden .trash. Reclaim only old entries so an overlapping uninstall in
    // another process retains a generous window to finish its own cleanup.
    CleanupStaleTrash(userRoot);

    const fs::path trashContainer = userRoot / L".trash" / OperationToken();
    const fs::path trashRoot = trashContainer / package.packageRoot.filename();
    std::error_code ec;
    fs::create_directories(trashContainer, ec);
    if (ec)
        return Fail(error, L"Unable to create content package trash directory: error=" +
                           std::to_wstring(ec.value()));

    ec.clear();
    fs::rename(package.packageRoot, trashRoot, ec);
    if (ec) {
        std::error_code cleanupError;
        fs::remove_all(trashContainer, cleanupError);
        return Fail(error, L"Unable to move content package out of the catalog: error=" +
                           std::to_wstring(ec.value()));
    }

    // The rename above is the logical uninstall point: .trash is hidden from
    // every catalog scan. Physical cleanup is best-effort so a locked file can
    // never leave a half-deleted package visible to the runtime.
    std::error_code cleanupError;
    fs::remove_all(trashContainer, cleanupError);

    result->package = std::move(package);
    result->cleanupDeferred = static_cast<bool>(cleanupError);
    if (error) error->clear();
    return true;
}

} // namespace miaodesk::content
