#pragma once

#include <span>
#include <string_view>

namespace miaodesk::wallpaper {

struct BuiltinWallpaperDefinition {
    std::wstring_view id;
    std::wstring_view runtimeKey;
    std::wstring_view previewKey;
    std::wstring_view packageName;
    // Canonical package identity. Existing persisted scene-* ids remain valid
    // during the compatibility phase; callers may resolve either identity but
    // must not rewrite user state merely because this alias exists.
    std::wstring_view contentSource;
    std::wstring_view title;
    std::wstring_view description;
};

std::span<const BuiltinWallpaperDefinition> BuiltinWallpapers() noexcept;
const BuiltinWallpaperDefinition* FindBuiltinWallpaper(std::wstring_view key) noexcept;

// Resolve aliases without mutating persisted user state. These helpers are the
// compatibility boundary for code that needs canonical package identity while
// legacy scene-* assignments are still supported.
std::wstring_view CanonicalBuiltinWallpaperSource(std::wstring_view key) noexcept;
std::wstring_view LegacyBuiltinWallpaperId(std::wstring_view key) noexcept;

const BuiltinWallpaperDefinition& DefaultBuiltinWallpaper() noexcept;
bool BuiltinWallpaperCatalogSelfTest() noexcept;

} // namespace miaodesk::wallpaper
