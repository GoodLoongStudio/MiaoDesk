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

// Runtime compatibility definitions keep the established scene-* identity.
// FindBuiltinWallpaper()/LegacyBuiltinWallpaperId() use this table so existing
// persisted monitor assignments and runtime lookups remain byte-for-byte stable.
constexpr std::array<BuiltinWallpaperDefinition, 3> kRuntimeWallpapers{{
    {L"scene-aurora", L"aurora", L"aurora_flow", L"MiaoCloud.mdwall",
     L"content:com.goodloong.miaodesk.theme.miao-cloud",
     L"妙喵云境", L"云端妙喵 · 星光花瓣"},
    {L"scene-neon", L"neon", L"neon_flow", L"NeonCity.mdwall",
     L"content:com.goodloong.miaodesk.theme.neon-city",
     L"霓虹之城", L"未来都市 · 雨夜光轨"},
    {L"scene-grid", L"grid", L"ocean_flow", L"MysticMoon.mdwall",
     L"content:com.goodloong.miaodesk.theme.mystic-moon",
     L"月影秘境", L"月湖秘境 · 萤火薄雾"},
}};

// Library/UI enumeration is canonical. The legacy runtime identity is still
// available through FindBuiltinWallpaper() and LegacyBuiltinWallpaperId(). This
// lets bootstrap/upsert stop creating new visible scene-* library rows without
// rewriting any existing monitor assignment on disk.
constexpr std::array<BuiltinWallpaperDefinition, 3> kLibraryWallpapers{{
    {L"content:com.goodloong.miaodesk.theme.miao-cloud", L"aurora", L"aurora_flow", L"MiaoCloud.mdwall",
     L"content:com.goodloong.miaodesk.theme.miao-cloud",
     L"妙喵云境", L"云端妙喵 · 星光花瓣"},
    {L"content:com.goodloong.miaodesk.theme.neon-city", L"neon", L"neon_flow", L"NeonCity.mdwall",
     L"content:com.goodloong.miaodesk.theme.neon-city",
     L"霓虹之城", L"未来都市 · 雨夜光轨"},
    {L"content:com.goodloong.miaodesk.theme.mystic-moon", L"grid", L"ocean_flow", L"MysticMoon.mdwall",
     L"content:com.goodloong.miaodesk.theme.mystic-moon",
     L"月影秘境", L"月湖秘境 · 萤火薄雾"},
}};

bool Same(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.empty() || left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i])) return false;
    }
    return true;
}

bool AcceptedIdentityCollision(const BuiltinWallpaperDefinition& left,
                               const BuiltinWallpaperDefinition& right) noexcept {
    const std::array<std::wstring_view, 4> leftKeys{
        left.id, left.runtimeKey, left.previewKey, left.contentSource};
    const std::array<std::wstring_view, 4> rightKeys{
        right.id, right.runtimeKey, right.previewKey, right.contentSource};
    for (const auto leftKey : leftKeys) {
        for (const auto rightKey : rightKeys) {
            if (Same(leftKey, rightKey)) return true;
        }
    }
    return false;
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
    return kLibraryWallpapers;
}

const BuiltinWallpaperDefinition* FindBuiltinWallpaper(std::wstring_view key) noexcept {
    for (const auto& wallpaper : kRuntimeWallpapers) {
        if (Same(key, wallpaper.id) || Same(key, wallpaper.runtimeKey) ||
            Same(key, wallpaper.previewKey) || Same(key, wallpaper.contentSource)) {
            return &wallpaper;
        }
    }

    // Compatibility aliases accepted by older AI/demo requests.
    if (Same(key, L"ocean")) return &kRuntimeWallpapers[2];
    return nullptr;
}

const BuiltinWallpaperDefinition* FindLegacyBuiltinWallpaper(std::wstring_view key) noexcept {
    for (const auto& wallpaper : kRuntimeWallpapers) {
        if (Same(key, wallpaper.id)) return &wallpaper;
    }
    return nullptr;
}

std::wstring_view CanonicalBuiltinWallpaperSource(std::wstring_view key) noexcept {
    const auto* wallpaper = FindBuiltinWallpaper(key);
    return wallpaper ? wallpaper->contentSource : std::wstring_view{};
}

std::wstring_view LegacyBuiltinWallpaperId(std::wstring_view key) noexcept {
    const auto* wallpaper = FindBuiltinWallpaper(key);
    return wallpaper ? wallpaper->id : std::wstring_view{};
}

const BuiltinWallpaperDefinition& DefaultBuiltinWallpaper() noexcept {
    return kRuntimeWallpapers.front();
}

bool BuiltinWallpaperCatalogSelfTest() noexcept {
    if (kRuntimeWallpapers.size() != 3 || kLibraryWallpapers.size() != 3 ||
        !UnicodeProfileRoundTripSelfTest()) return false;

    for (std::size_t i = 0; i < kRuntimeWallpapers.size(); ++i) {
        const auto& runtime = kRuntimeWallpapers[i];
        const auto& library = kLibraryWallpapers[i];
        if (runtime.id.empty() || runtime.runtimeKey.empty() || runtime.previewKey.empty() ||
            runtime.packageName.empty() || runtime.contentSource.empty() || runtime.title.empty()) return false;
        if (library.id != runtime.contentSource || library.contentSource != runtime.contentSource ||
            library.runtimeKey != runtime.runtimeKey || library.previewKey != runtime.previewKey ||
            library.packageName != runtime.packageName || library.title != runtime.title) return false;
        if (FindBuiltinWallpaper(runtime.id) != &runtime ||
            FindBuiltinWallpaper(runtime.runtimeKey) != &runtime ||
            FindBuiltinWallpaper(runtime.previewKey) != &runtime ||
            FindBuiltinWallpaper(runtime.contentSource) != &runtime ||
            FindLegacyBuiltinWallpaper(runtime.id) != &runtime ||
            FindLegacyBuiltinWallpaper(runtime.runtimeKey) != nullptr ||
            FindLegacyBuiltinWallpaper(runtime.previewKey) != nullptr ||
            FindLegacyBuiltinWallpaper(runtime.contentSource) != nullptr ||
            CanonicalBuiltinWallpaperSource(runtime.id) != runtime.contentSource ||
            CanonicalBuiltinWallpaperSource(runtime.runtimeKey) != runtime.contentSource ||
            CanonicalBuiltinWallpaperSource(runtime.previewKey) != runtime.contentSource ||
            CanonicalBuiltinWallpaperSource(runtime.contentSource) != runtime.contentSource ||
            LegacyBuiltinWallpaperId(runtime.id) != runtime.id ||
            LegacyBuiltinWallpaperId(runtime.runtimeKey) != runtime.id ||
            LegacyBuiltinWallpaperId(runtime.previewKey) != runtime.id ||
            LegacyBuiltinWallpaperId(runtime.contentSource) != runtime.id) return false;
        for (std::size_t j = i + 1; j < kRuntimeWallpapers.size(); ++j) {
            if (AcceptedIdentityCollision(runtime, kRuntimeWallpapers[j])) return false;
        }
    }

    const auto libraryEntries = BuiltinWallpapers();
    if (libraryEntries.size() != kLibraryWallpapers.size()) return false;
    for (std::size_t i = 0; i < libraryEntries.size(); ++i) {
        if (libraryEntries[i].id != kRuntimeWallpapers[i].contentSource ||
            LegacyBuiltinWallpaperId(libraryEntries[i].id) != kRuntimeWallpapers[i].id) return false;
    }

    return FindBuiltinWallpaper(L"ocean") == &kRuntimeWallpapers[2] &&
           FindLegacyBuiltinWallpaper(L"ocean") == nullptr &&
           FindBuiltinWallpaper(L"content:com.goodloong.miaodesk.theme.miao-cloud") == &kRuntimeWallpapers[0] &&
           FindBuiltinWallpaper(L"content:com.goodloong.miaodesk.theme.neon-city") == &kRuntimeWallpapers[1] &&
           FindBuiltinWallpaper(L"content:com.goodloong.miaodesk.theme.mystic-moon") == &kRuntimeWallpapers[2] &&
           CanonicalBuiltinWallpaperSource(L"unknown").empty() &&
           LegacyBuiltinWallpaperId(L"unknown").empty() &&
           FindLegacyBuiltinWallpaper(L"unknown") == nullptr &&
           FindBuiltinWallpaper(L"unknown") == nullptr;
}

} // namespace miaodesk::wallpaper