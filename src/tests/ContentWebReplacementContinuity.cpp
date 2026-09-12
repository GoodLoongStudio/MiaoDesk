#include "miaodesk/DesktopControlService.h"
#include "miaodesk/WallpaperLibrary.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using miaodesk::wallpaper::LibraryWallpaperKind;
using miaodesk::wallpaper::WallpaperLibrary;

namespace {

bool WriteUtf8(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

bool SameExistingPath(const fs::path& left, const fs::path& right) {
    std::error_code ec;
    const bool same = fs::equivalent(left, right, ec);
    return !ec && same;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), out.data(), count) != count)
        return {};
    return out;
}

bool WaitForWidgetSurface(const miaodesk::desktop::DesktopControlService& service,
                          std::wstring_view widgetId,
                          bool expectedPresent,
                          DWORD timeoutMs = 15000) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    do {
        miaodesk::desktop::DesktopSnapshot snapshot;
        const auto result = service.GetSnapshot(&snapshot);
        if (!result.success) return false;
        const auto surface = std::find_if(
            snapshot.widgetRuntime.surfaces.begin(), snapshot.widgetRuntime.surfaces.end(),
            [&](const auto& item) { return item.widgetId == widgetId; });
        if (expectedPresent) {
            if (surface != snapshot.widgetRuntime.surfaces.end() &&
                surface->SurfaceReady() && surface->renderingHealthy)
                return true;
        } else if (surface == snapshot.widgetRuntime.surfaces.end()) {
            return true;
        }
        Sleep(100);
    } while (GetTickCount64() < deadline);
    return false;
}

bool RunContentWidgetLifecycle(const fs::path& packagePath) {
    miaodesk::desktop::DesktopControlService service;
    miaodesk::content::ContentPackageInstallResult installed;
    const auto install = service.InstallContentPackage(packagePath, &installed);
    if (!install.success || installed.package.kind != miaodesk::content::ContentKind::Widget ||
        installed.package.source.empty())
        return false;

    const std::wstring definitionId = Utf8ToWide(installed.package.id);
    if (definitionId.empty()) return false;

    miaodesk::desktop::ContentWidgetCreateRequest request;
    request.definitionId = definitionId;
    request.title = L"CI Content widget lifecycle";
    request.x = 0.05f;
    request.y = 0.05f;
    request.width = 0.30f;
    request.height = 0.30f;
    request.enabled = true;

    miaodesk::wallpaper::DesktopWidget created;
    const auto create = service.CreateContentWidget(request, &created);
    if (!create.success || created.id.empty() || created.source.wstring() != installed.package.source)
        return false;
    if (!WaitForWidgetSurface(service, created.id, true)) return false;

    miaodesk::content::ContentPackageUninstallResult blockedResult;
    const auto blocked = service.UninstallContentPackage(
        miaodesk::content::ContentKind::Widget, installed.package.source, &blockedResult);
    if (blocked.success) return false;

    const auto remove = service.RemoveWidget(created.id);
    if (!remove.success) return false;
    if (!WaitForWidgetSurface(service, created.id, false)) return false;

    miaodesk::content::ContentPackageUninstallResult removedPackage;
    const auto uninstall = service.UninstallContentPackage(
        miaodesk::content::ContentKind::Widget, installed.package.source, &removedPackage);
    return uninstall.success && removedPackage.package.source == installed.package.source;
}

bool RunContentWebReplacementContinuity() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-ContentWebReplacement-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));

    bool ok = true;
    std::wstring error;
    WallpaperLibrary library(root / L"Library");
    ok = library.Load(&error);

    const fs::path package = library.PackageDirectory() / L"replacement.mdwall";
    fs::create_directories(package, ec);
    ok = ok && !ec;
    ok = ok && WriteUtf8(package / L"index.html", "<html><body>version one</body></html>");
    ok = ok && WriteUtf8(package / L"manifest.json", R"json({
  "schema":1,
  "id":"com.goodloong.selftest.web-replacement",
  "name":"Replacement Web",
  "author":"MiaoDesk",
  "version":"1.0.0",
  "kind":"wallpaper",
  "runtime":"web",
  "entry":"index.html",
  "capabilities":[]
})json");

    WallpaperLibrary first(root / L"Library");
    ok = ok && first.Load(&error);
    const std::wstring stableId = L"content:com.goodloong.selftest.web-replacement";
    auto before = first.Find(stableId);
    ok = ok && before.has_value();
    if (before) {
        ok = ok && before->kind == LibraryWallpaperKind::Web;
        ok = ok && SameExistingPath(before->source, package / L"index.html");
        ok = ok && first.SetFavorite(stableId, true, &error);
        ok = ok && first.MarkUsed(stableId, &error);
        before = first.Find(stableId);
        ok = ok && before.has_value();
    }

    const auto importedBefore = before ? before->importedUnixSeconds : 0;
    const auto lastUsedBefore = before ? before->lastUsedUnixSeconds : 0;
    ok = ok && importedBefore != 0;
    ok = ok && lastUsedBefore != 0;

    ok = ok && WriteUtf8(package / L"wallpaper-v2.html", "<html><body>version two</body></html>");
    ok = ok && WriteUtf8(package / L"manifest.json", R"json({
  "schema":1,
  "id":"com.goodloong.selftest.web-replacement",
  "name":"Replacement Web v2",
  "author":"MiaoDesk",
  "version":"2.0.0",
  "kind":"wallpaper",
  "runtime":"web",
  "entry":"wallpaper-v2.html",
  "capabilities":[]
})json");

    WallpaperLibrary replaced(root / L"Library");
    ok = ok && replaced.Load(&error);
    const auto after = replaced.Find(stableId);
    ok = ok && after.has_value();
    if (after) {
        ok = ok && after->kind == LibraryWallpaperKind::Web;
        ok = ok && SameExistingPath(after->source, package / L"wallpaper-v2.html");
        ok = ok && after->favorite;
        ok = ok && after->importedUnixSeconds == importedBefore;
        ok = ok && after->lastUsedUnixSeconds == lastUsedBefore;
        ok = ok && replaced.Search(L"Replacement Web v2").size() == 1;
    }

    fs::remove_all(root, ec);
    return ok;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2) return RunContentWidgetLifecycle(fs::path(argv[1])) ? 0 : 2;
    return RunContentWebReplacementContinuity() ? 0 : 1;
}
