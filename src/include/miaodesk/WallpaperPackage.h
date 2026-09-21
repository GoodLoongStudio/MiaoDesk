#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

struct WallpaperPackageValidation {
    bool ok{};
    std::wstring message;
    std::filesystem::path packageRoot;
    std::filesystem::path resolvedEntry;
    WallpaperPackageManifest manifest;
    std::uintmax_t totalBytes{};
};

class WallpaperPackage {
public:
    // Legacy callers remain supported while .mdwall migrates to the canonical
    // content-package manifest. New package code should prefer LoadAndValidate.
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

    static WallpaperPackageValidation LoadAndValidate(
        const std::filesystem::path& packageRoot,
        std::uintmax_t maxBytes = 256ull * 1024ull * 1024ull);

    static bool WriteCanonicalManifest(
        const std::filesystem::path& packageRoot,
        const WallpaperPackageManifest& manifest,
        std::string_view stableId);

    static const wchar_t* TypeKey(WallpaperPackageType type) noexcept;
    static WallpaperPackageType ParseType(std::wstring_view value) noexcept;
    static bool SelfTest();
};

inline bool WallpaperPackage::CreateWeb(const std::filesystem::path& packageDirectory,
                                        std::wstring title,
                                        std::string_view htmlUtf8,
                                        std::wstring provenance,
                                        std::wstring author,
                                        std::wstring* error) {
    if (error) error->clear();
    if (packageDirectory.empty()) {
        if (error) *error = L"壁纸包目录不能为空";
        return false;
    }
    if (htmlUtf8.empty() || htmlUtf8.size() > 2 * 1024 * 1024) {
        if (error) *error = L"Web 壁纸 HTML 不能为空且不能超过 2 MB";
        return false;
    }
    if (title.empty()) title = L"MiaoDesk Wallpaper";
    if (title.size() > 256) title.resize(256);
    if (author.empty()) author = L"MiaoDesk";
    if (provenance.empty()) provenance = L"user-authored";

    std::error_code ec;
    std::filesystem::create_directories(packageDirectory, ec);
    if (ec) {
        if (error) *error = L"无法创建 .mdwall 目录：" + packageDirectory.wstring();
        return false;
    }

    const auto entry = packageDirectory / L"index.html";
    {
        std::ofstream html(entry, std::ios::binary | std::ios::trunc);
        if (!html) {
            if (error) *error = L"无法写入 Web 壁纸入口文件";
            return false;
        }
        html.write(htmlUtf8.data(), static_cast<std::streamsize>(htmlUtf8.size()));
        if (!html) {
            if (error) *error = L"Web 壁纸入口文件写入失败";
            return false;
        }
    }

    WallpaperPackageManifest packageManifest;
    packageManifest.schema = 1;
    packageManifest.type = WallpaperPackageType::Web;
    packageManifest.title = std::move(title);
    packageManifest.author = std::move(author);
    packageManifest.entry = L"index.html";
    packageManifest.provenance = std::move(provenance);
    packageManifest.fpsCap = 30;
    packageManifest.audio = false;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string stableId = "com.goodloong.miaodesk.theme.web." + std::to_string(stamp);
    if (!WriteCanonicalManifest(packageDirectory, packageManifest, stableId)) {
        if (error) *error = L"无法写入壁纸主题包 manifest.json";
        return false;
    }
    return Validate(packageDirectory, nullptr, error);
}

inline bool WallpaperPackage::Validate(const std::filesystem::path& packageDirectory,
                                       WallpaperPackageManifest* manifest,
                                       std::wstring* error) {
    if (error) error->clear();
    const auto result = LoadAndValidate(packageDirectory);
    if (!result.ok) {
        if (error) *error = result.message;
        return false;
    }
    if (result.manifest.title.size() > 256) {
        if (error) *error = L"壁纸包 title 无效";
        return false;
    }
    if (result.manifest.type == WallpaperPackageType::Web) {
        auto extension = result.resolvedEntry.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
            if (ch >= L'A' && ch <= L'Z') return static_cast<wchar_t>(ch - L'A' + L'a');
            return ch;
        });
        if (extension != L".html" && extension != L".htm") {
            if (error) *error = L"Web .mdwall 的 entry 必须是 HTML 文件";
            return false;
        }
    }
    if (manifest) *manifest = result.manifest;
    return true;
}

} // namespace miaodesk::wallpaper
