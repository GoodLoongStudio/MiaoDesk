#pragma once

#include "miaodesk/MiaoContentModel.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace miaodesk::content {

enum class ManagedContentPackageOrigin {
    External,
    BuiltIn,
    UserManaged,
};

struct ManagedContentPackageInfo {
    std::string id;
    std::wstring source;
    std::wstring name;
    std::wstring author;
    std::wstring version;
    ContentKind kind{ContentKind::Wallpaper};
    ContentRuntimeKind runtime{ContentRuntimeKind::Scene};
    std::filesystem::path packageRoot;
    ManagedContentPackageOrigin origin{ManagedContentPackageOrigin::External};
};

struct ContentPackageInstallOptions {
    bool replaceExisting{true};
};

struct ContentPackageInstallResult {
    ManagedContentPackageInfo package;
    bool replacedExisting{};
};

struct ContentPackageUninstallResult {
    ManagedContentPackageInfo package;
    // The package is already outside the catalog when this is true. A stale
    // hidden .trash directory can be cleaned on a later maintenance pass.
    bool cleanupDeferred{};
};

class MiaoContentPackageManager {
public:
    static constexpr std::wstring_view kSourcePrefix = L"content:";

    static std::wstring MakeSource(std::string_view definitionId);
    static bool ParseSource(
        std::wstring_view source,
        std::string* definitionId,
        std::wstring* error = nullptr);

    static std::filesystem::path BuiltInRoot(ContentKind kind);
    static std::filesystem::path UserRoot(ContentKind kind);

    static bool Inspect(
        const std::filesystem::path& packageRoot,
        ManagedContentPackageInfo* package,
        std::wstring* error = nullptr);

    static bool Resolve(
        ContentKind expectedKind,
        std::wstring_view source,
        ManagedContentPackageInfo* package,
        std::wstring* error = nullptr);

    static bool Install(
        const std::filesystem::path& sourceRoot,
        ContentPackageInstallResult* result,
        const ContentPackageInstallOptions& options = {},
        std::wstring* error = nullptr);

    static bool Uninstall(
        ContentKind expectedKind,
        std::wstring_view source,
        ContentPackageUninstallResult* result,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace miaodesk::content
