#pragma once

#include <span>
#include <string_view>

namespace miaodesk::wallpaper {

struct BuiltinWallpaperDefinition {
    std::wstring_view id;
    std::wstring_view runtimeKey;
    std::wstring_view previewKey;
    std::wstring_view packageName;
    std::wstring_view title;
    std::wstring_view description;
};

std::span<const BuiltinWallpaperDefinition> BuiltinWallpapers() noexcept;
const BuiltinWallpaperDefinition* FindBuiltinWallpaper(std::wstring_view key) noexcept;
const BuiltinWallpaperDefinition& DefaultBuiltinWallpaper() noexcept;
bool BuiltinWallpaperCatalogSelfTest() noexcept;

} // namespace miaodesk::wallpaper
