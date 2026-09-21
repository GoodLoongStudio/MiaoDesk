#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace miaodesk::wallpaper {

enum class WallpaperPackageType {
    Image,
    Video,
    Web,
    Scene,
    Unknown,
};

struct WallpaperPackageManifest {
    int schema{1};
    WallpaperPackageType type{WallpaperPackageType::Unknown};
    std::wstring title;
    std::wstring author;
    std::filesystem::path entry;
    std::wstring provenance;
    int fpsCap{30};
    bool audio{};
};

class WallpaperPackage {
public:
    static bool CreateWeb(
        const std::filesystem::path& packageDirectory,
        std::wstring title,
        std::string_view htmlUtf8,
        std::wstring provenance = L"user-authored",
        std::wstring author = L"MiaoDesk",
        std::wstring* error = nullptr);

    // Copy an existing local image or video into a new .mdwall package. The manifest
    // type decides which entry extensions Validate accepts, so a declarative
    // "video wallpaper" is one manifest plus one asset with nothing else required.
    static bool CreateImage(
        const std::filesystem::path& packageDirectory,
        std::wstring title,
        const std::filesystem::path& sourceFile,
        std::wstring provenance = L"user-authored",
        std::wstring author = L"MiaoDesk",
        std::wstring* error = nullptr);

    static bool CreateVideo(
        const std::filesystem::path& packageDirectory,
        std::wstring title,
        const std::filesystem::path& sourceFile,
        std::wstring provenance = L"user-authored",
        std::wstring author = L"MiaoDesk",
        std::wstring* error = nullptr);

    static bool Validate(
        const std::filesystem::path& packageDirectory,
        WallpaperPackageManifest* manifest = nullptr,
        std::wstring* error = nullptr);

    static const wchar_t* TypeKey(WallpaperPackageType type) noexcept;
    static WallpaperPackageType ParseType(std::wstring_view value) noexcept;
    static bool SelfTest();
};

} // namespace miaodesk::wallpaper
