#include "miaodesk/BuiltinWallpaperCatalog.h"

#include <array>
#include <cwctype>

namespace miaodesk::wallpaper {
namespace {

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

} // namespace

std::span<const BuiltinWallpaperDefinition> BuiltinWallpapers() noexcept {
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
    if (kWallpapers.size() != 3) return false;
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
