#include "miaodesk/BuiltinWallpaperCatalog.h"
#include "miaodesk/AppPaths.h"
#include "miaodesk/UnicodeProfileFile.h"

#include <windows.h>

#include <array>
#include <cwctype>
#include <filesystem>
#include <fstream>

namespace miaodesk::wallpaper {
namespace {

namespace fs = std::filesystem;

constexpr std::array<BuiltinWallpaperDefinition, 3> kWallpapers{{
    {L"scene-aurora", L"aurora", L"aurora_flow", L"MiaoCloud.mdwall",
     L"妙喵云境", L"云端妙喵 · 星光花瓣"},
    {L"scene-neon", L"neon", L"neon_flow", L"NeonCity.mdwall",
     L"霓虹之城", L"未来都市 · 雨夜光轨"},
    {L"scene-grid", L"grid", L"ocean_flow", L"MysticMoon.mdwall",
     L"月影秘境", L"月湖秘境 · 萤火薄雾"},
}};

bool Same(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.empty() || left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i])) return false;
    }
    return true;
}

void PrepareUnicodeWallpaperLibrary() {
    // Get/WritePrivateProfileStringW still falls back to the system ANSI code
    // page when an INI file has no Unicode BOM. On Western Windows locales that
    // turns persisted Chinese titles into literal "????". Normalize the library
    // manifest before the built-in catalog is upserted; UpsertScene then repairs
    // any previously damaged built-in title from these canonical wide strings.
    const auto root = paths::WallpaperLibraryRoot();
    if (root.empty()) return;
    std::wstring ignored;
    text::EnsureUtf16LeProfileFile(root / L"library.ini", &ignored);
}

bool UnicodeProfileRoundTripSelfTest() noexcept {
    std::error_code ec;
    const fs::path tempRoot = fs::temp_directory_path(ec);
    if (ec || tempRoot.empty()) return false;
    const fs::path path = tempRoot /
        (L"MiaoDesk-UnicodeProfile-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L".ini");
    fs::remove(path, ec);

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << "[Theme]\nTitle=中文主题标题\n";
        if (!output.good()) {
            output.close();
            fs::remove(path, ec);
            return false;
        }
    }

    std::wstring error;
    if (!text::EnsureUtf16LeProfileFile(path, &error)) {
        fs::remove(path, ec);
        return false;
    }

    wchar_t buffer[128]{};
    constexpr DWORD bufferCount = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    GetPrivateProfileStringW(L"Theme", L"Title", L"", buffer, bufferCount, path.c_str());
    bool ok = std::wstring_view(buffer) == L"中文主题标题";
    ok = WritePrivateProfileStringW(L"Theme", L"Author", L"妙喵作者", path.c_str()) != FALSE && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    buffer[0] = L'\0';
    GetPrivateProfileStringW(L"Theme", L"Author", L"", buffer, bufferCount, path.c_str());
    ok = std::wstring_view(buffer) == L"妙喵作者" && ok;

    fs::remove(path, ec);
    return ok;
}

} // namespace

std::span<const BuiltinWallpaperDefinition> BuiltinWallpapers() noexcept {
    PrepareUnicodeWallpaperLibrary();
    return kWallpapers;
}

const BuiltinWallpaperDefinition* FindBuiltinWallpaper(std::wstring_view key) noexcept {
    for (const auto& wallpaper : kWallpapers) {
        if (Same(key, wallpaper.id) || Same(key, wallpaper.runtimeKey) ||
            Same(key, wallpaper.previewKey)) {
            return &wallpaper;
        }
    }

    // Compatibility aliases accepted by older AI/demo requests.
    if (Same(key, L"ocean")) return &kWallpapers[2];
    return nullptr;
}

const BuiltinWallpaperDefinition& DefaultBuiltinWallpaper() noexcept {
    return kWallpapers.front();
}

bool BuiltinWallpaperCatalogSelfTest() noexcept {
    if (kWallpapers.size() != 3 || !UnicodeProfileRoundTripSelfTest()) return false;
    for (std::size_t i = 0; i < kWallpapers.size(); ++i) {
        const auto& item = kWallpapers[i];
        if (item.id.empty() || item.runtimeKey.empty() || item.previewKey.empty() ||
            item.packageName.empty() || item.title.empty()) return false;
        if (FindBuiltinWallpaper(item.id) != &item ||
            FindBuiltinWallpaper(item.runtimeKey) != &item ||
            FindBuiltinWallpaper(item.previewKey) != &item) return false;
        for (std::size_t j = i + 1; j < kWallpapers.size(); ++j) {
            if (Same(item.id, kWallpapers[j].id) ||
                Same(item.runtimeKey, kWallpapers[j].runtimeKey) ||
                Same(item.previewKey, kWallpapers[j].previewKey)) return false;
        }
    }
    return FindBuiltinWallpaper(L"ocean") == &kWallpapers[2] &&
           FindBuiltinWallpaper(L"unknown") == nullptr;
}

} // namespace miaodesk::wallpaper
