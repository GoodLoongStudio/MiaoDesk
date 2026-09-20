#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/MiaoContentModel.h"

namespace miaodesk::content {

struct MiaoContentPackageManifest {
    std::uint32_t schema{1};
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    ContentKind kind{ContentKind::Wallpaper};
    ContentRuntimeKind runtime{ContentRuntimeKind::Scene};
    std::filesystem::path entry;
    std::filesystem::path parameters;
    std::filesystem::path preview;
    std::vector<std::string> capabilities;
};

struct LoadedMiaoContentPackage {
    std::filesystem::path root;
    MiaoContentPackageManifest manifest;
    std::string entrySourceUtf8;
    std::string parameterSourceUtf8;
};

class MiaoContentPackage {
public:
    static constexpr std::uint32_t kSchemaVersion = 1;
    static constexpr std::size_t kManifestMaxBytes = 1024 * 1024;
    static constexpr std::size_t kSceneEntryMaxBytes = 16 * 1024 * 1024;
    static constexpr std::size_t kWebEntryMaxBytes = 8 * 1024 * 1024;
    static constexpr std::size_t kParameterMaxBytes = 1024 * 1024;
    static constexpr std::size_t kPreviewMaxBytes = 32 * 1024 * 1024;

    static bool Load(
        const std::filesystem::path& packageRoot,
        LoadedMiaoContentPackage* package,
        std::wstring* error = nullptr);

    static bool Validate(
        const std::filesystem::path& packageRoot,
        MiaoContentPackageManifest* manifest = nullptr,
        std::wstring* error = nullptr);

    static bool IsSafeRelativePath(const std::filesystem::path& path) noexcept;

    static bool ResolvePackagePath(
        const std::filesystem::path& packageRoot,
        const std::filesystem::path& relativePath,
        std::filesystem::path* resolved,
        std::wstring* error = nullptr);

    static const char* KindKey(ContentKind kind) noexcept;
    static const char* RuntimeKey(ContentRuntimeKind runtime) noexcept;
    static bool ParseKind(std::string_view value, ContentKind* kind) noexcept;
    static bool ParseRuntime(std::string_view value, ContentRuntimeKind* runtime) noexcept;

    static bool SelfTest();
};

} // namespace miaodesk::content
