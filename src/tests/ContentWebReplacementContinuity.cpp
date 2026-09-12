#include "miaodesk/WallpaperLibrary.h"

#include <windows.h>

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

} // namespace

int wmain() {
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
    return ok ? 0 : 1;
}
