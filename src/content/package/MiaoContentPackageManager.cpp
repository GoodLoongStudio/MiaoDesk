#include "miaodesk/MiaoContentPackageManager.h"

#include "miaodesk/AppPaths.h"
#include "miaodesk/MiaoContentPackage.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

struct PackageRoots {
    fs::path builtInWallpapers;
    fs::path builtInWidgets;
    fs::path userWallpapers;
    fs::path userWidgets;
};

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool IsStableId(std::string_view value) noexcept {
    if (value.empty() || value.size() > 160) return false;
    const unsigned char first = static_cast<unsigned char>(value.front());
    if (std::isalnum(first) == 0) return false;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-') continue;
        return false;
    }
    return true;
}

std::wstring AsciiToWide(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::optional<std::wstring> Utf8ToWide(std::string_view value) {
    if (value.empty()) return std::wstring{};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return std::nullopt;
    std::wstring output(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            output.data(), required) != required) return std::nullopt;
    return output;
}

const wchar_t* PackageExtension(ContentKind kind) noexcept {
    return kind == ContentKind::Widget ? L".mdwidget" : L".mdwall";
}

fs::path RootFor(const PackageRoots& roots, ContentKind kind, ManagedContentPackageOrigin origin) {
    if (origin == ManagedContentPackageOrigin::BuiltIn)
        return kind == ContentKind::Widget ? roots.builtInWidgets : roots.builtInWallpapers;
    return kind == ContentKind::Widget ? roots.userWidgets : roots.userWallpapers;
}

PackageRoots DefaultRoots() {
    return {
        paths::BuiltInWallpaperPackagesRoot(),
        paths::BuiltInWidgetPackagesRoot(),
        paths::WallpaperPackagesRoot(),
        paths::WidgetPackagesRoot(),
    };
}

fs::path NormalizedAbsolute(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) absolute = path;
    absolute = absolute.lexically_normal();
    ec.clear();
    fs::path canonical = fs::weakly_canonical(absolute, ec);
    return ec ? absolute : canonical;
}

bool SamePath(const fs::path& left, const fs::path& right) {
    const std::wstring a = NormalizedAbsolute(left).wstring();
    const std::wstring b = NormalizedAbsolute(right).wstring();
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool HasReparseAttribute(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool RejectReparseTree(const fs::path& packageRoot, std::wstring* error) {
    if (HasReparseAttribute(packageRoot))
        return Fail(error, L"Content package root cannot be a symlink, junction, or reparse point.");

    std::error_code ec;
    for (fs::recursive_directory_iterator it(
             packageRoot, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec)
            return Fail(error, L"Unable to enumerate content package while checking reparse points.");
        if (HasReparseAttribute(it->path()))
            return Fail(error, L"Content package cannot contain symlinks, junctions, or reparse points: " +
                               it->path().wstring());
    }
    if (ec)
        return Fail(error, L"Unable to enumerate content package while checking reparse points.");
    return true;
}

bool InspectWithOrigin(
    const fs::path& packageRoot,
    ManagedContentPackageOrigin origin,
    ManagedContentPackageInfo* package,
    std::wstring* error) {
    if (!package) return Fail(error, L"Managed content package output is null.");
    *package = {};

    LoadedMiaoContentPackage loaded;
    if (!MiaoContentPackage::Load(packageRoot, &loaded, error)) return false;
    if (!IsStableId(loaded.manifest.id))
        return Fail(error, L"Content package id exceeds the managed catalog limit or is invalid.");

    const auto name = Utf8ToWide(loaded.manifest.name);
    const auto author = Utf8ToWide(loaded.manifest.author);
    const auto version = Utf8ToWide(loaded.manifest.version);
    if (!name || !author || !version)
        return Fail(error, L"Content package metadata is not valid UTF-8.");

    ManagedContentPackageInfo inspected;
    inspected.id = loaded.manifest.id;
    inspected.source = std::wstring(MiaoContentPackageManager::kSourcePrefix) + AsciiToWide(loaded.manifest.id);
    inspected.name = *name;
    inspected.author = *author;
    inspected.version = *version;
    inspected.kind = loaded.manifest.kind;
    inspected.runtime = loaded.manifest.runtime;
    inspected.packageRoot = loaded.root;
    inspected.origin = origin;
    *package = std::move(inspected);
    if (error) error->clear();
    return true;
}

bool IsCandidateDirectory(const fs::directory_entry& entry, ContentKind kind) {
    std::error_code ec;
    if (!entry.is_directory(ec) || ec) return false;
    return _wcsicmp(entry.path().extension().c_str(), PackageExtension(kind)) == 0;
}

bool FindByIdInRoot(
    const fs::path& root,
    ContentKind kind,
    std::string_view id,
    ManagedContentPackageOrigin origin,
    ManagedContentPackageInfo* package,
    bool* found,
    std::wstring* error) {
    if (found) *found = false;
    if (root.empty()) return true;

    std::error_code ec;
    if (!fs::exists(root, ec)) return true;
    if (ec || !fs::is_directory(root, ec))
        return Fail(error, L"Content package root is not a directory: " + root.wstring());

    std::optional<ManagedContentPackageInfo> match;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!IsCandidateDirectory(entry, kind)) continue;

        MiaoContentPackageManifest manifest;
        std::wstring validationError;
        if (!MiaoContentPackage::Validate(entry.path(), &manifest, &validationError)) continue;
        if (manifest.kind != kind || manifest.id != id) continue;

        ManagedContentPackageInfo inspected;
        if (!InspectWithOrigin(entry.path(), origin, &inspected, &validationError)) continue;
        if (match)
            return Fail(error, L"Duplicate content package id in catalog root: " + AsciiToWide(id));
        match = std::move(inspected);
    }
    if (ec) return Fail(error, L"Unable to enumerate content package root: " + root.wstring());
    if (!match) return true;

    if (package) *package = std::move(*match);
    if (found) *found = true;
    if (error) error->clear();
    return true;
}

bool ResolveWithRoots(
    const PackageRoots& roots,
    ContentKind expectedKind,
    std::wstring_view source,
    ManagedContentPackageInfo* package,
    std::wstring* error) {
    if (!package) return Fail(error, L"Resolved content package output is null.");
    *package = {};

    std::string id;
    if (!MiaoContentPackageManager::ParseSource(source, &id, error)) return false;

    for (const auto origin : {ManagedContentPackageOrigin::BuiltIn, ManagedContentPackageOrigin::UserManaged}) {
        bool found = false;
        ManagedContentPackageInfo candidate;
        if (!FindByIdInRoot(RootFor(roots, expectedKind, origin), expectedKind, id, origin,
                            &candidate, &found, error)) return false;
        if (!found) continue;
        *package = std::move(candidate);
        if (error) error->clear();
        return true;
    }
    return Fail(error, L"Content package was not found: " + AsciiToWide(id));
}

bool FindBuiltInConflict(
    const PackageRoots& roots,
    std::string_view id,
    ManagedContentPackageInfo* conflict,
    bool* found,
    std::wstring* error) {
    if (found) *found = false;
    for (const auto kind : {ContentKind::Wallpaper, ContentKind::Widget}) {
        bool localFound = false;
        ManagedContentPackageInfo candidate;
        if (!FindByIdInRoot(RootFor(roots, kind, ManagedContentPackageOrigin::BuiltIn), kind, id,
                            ManagedContentPackageOrigin::BuiltIn, &candidate, &localFound, error)) return false;
        if (!localFound) continue;
        if (conflict) *conflict = std::move(candidate);
        if (found) *found = true;
        return true;
    }
    return true;
}

bool FindUserConflictOfOtherKind(
    const PackageRoots& roots,
    ContentKind kind,
    std::string_view id,
    ManagedContentPackageInfo* conflict,
    bool* found,
    std::wstring* error) {
    const ContentKind other = kind == ContentKind::Widget ? ContentKind::Wallpaper : ContentKind::Widget;
    return FindByIdInRoot(RootFor(roots, other, ManagedContentPackageOrigin::UserManaged), other, id,
                          ManagedContentPackageOrigin::UserManaged, conflict, found, error);
}

std::wstring OperationToken() {
    return std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
}

bool EnsureDirectory(const fs::path& directory, std::wstring* error) {
    if (directory.empty()) return Fail(error, L"Managed content package root is empty.");
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) return Fail(error, L"Unable to create managed content package directory: " + directory.wstring());
    return true;
}

bool InstallWithRoots(
    const PackageRoots& roots,
    const fs::path& sourceRoot,
    ContentPackageInstallResult* result,
    const ContentPackageInstallOptions& options,
    std::wstring* error) {
    if (!result) return Fail(error, L"Content package install result is null.");
    *result = {};

    if (!RejectReparseTree(sourceRoot, error)) return false;

    ManagedContentPackageInfo incoming;
    if (!InspectWithOrigin(sourceRoot, ManagedContentPackageOrigin::External, &incoming, error)) return false;

    bool builtInFound = false;
    ManagedContentPackageInfo builtIn;
    if (!FindBuiltInConflict(roots, incoming.id, &builtIn, &builtInFound, error)) return false;
    if (builtInFound)
        return Fail(error, L"A built-in content package already owns id " + AsciiToWide(incoming.id) +
                           L"; user packages cannot shadow built-in content.");

    bool otherKindFound = false;
    ManagedContentPackageInfo otherKind;
    if (!FindUserConflictOfOtherKind(roots, incoming.kind, incoming.id, &otherKind, &otherKindFound, error)) return false;
    if (otherKindFound)
        return Fail(error, L"Content package id is already installed with a different kind: " +
                           AsciiToWide(incoming.id));

    const fs::path userRoot = RootFor(roots, incoming.kind, ManagedContentPackageOrigin::UserManaged);
    if (!EnsureDirectory(userRoot, error)) return false;

    bool existingFound = false;
    ManagedContentPackageInfo existing;
    if (!FindByIdInRoot(userRoot, incoming.kind, incoming.id, ManagedContentPackageOrigin::UserManaged,
                        &existing, &existingFound, error)) return false;
    if (existingFound && !options.replaceExisting)
        return Fail(error, L"Content package is already installed: " + AsciiToWide(incoming.id));

    const fs::path canonicalTarget = userRoot /
        (AsciiToWide(incoming.id) + std::wstring(PackageExtension(incoming.kind)));

    std::error_code ec;
    if (fs::exists(canonicalTarget, ec) &&
        (!existingFound || !SamePath(canonicalTarget, existing.packageRoot))) {
        return Fail(error, L"Managed content destination is occupied by another or invalid package: " +
                           canonicalTarget.wstring());
    }
    if (ec) return Fail(error, L"Unable to inspect managed content destination.");

    if (existingFound && SamePath(sourceRoot, existing.packageRoot) && SamePath(existing.packageRoot, canonicalTarget)) {
        result->package = existing;
        result->replacedExisting = false;
        if (error) error->clear();
        return true;
    }

    const std::wstring token = OperationToken();
    const fs::path stagingContainer = userRoot / L".staging" / token;
    const fs::path stagingRoot = stagingContainer / canonicalTarget.filename();
    const fs::path backupContainer = userRoot / L".backup" / token;
    const fs::path backupRoot = backupContainer / canonicalTarget.filename();

    fs::remove_all(stagingContainer, ec);
    ec.clear();
    fs::create_directories(stagingContainer, ec);
    if (ec) return Fail(error, L"Unable to create content package staging directory.");

    auto cleanupStaging = [&]() {
        std::error_code cleanupError;
        fs::remove_all(stagingContainer, cleanupError);
    };
    auto cleanupBackup = [&]() {
        std::error_code cleanupError;
        fs::remove_all(backupContainer, cleanupError);
    };

    fs::copy(sourceRoot, stagingRoot, fs::copy_options::recursive, ec);
    if (ec) {
        cleanupStaging();
        return Fail(error, L"Unable to copy content package into managed staging: error=" +
                           std::to_wstring(ec.value()));
    }

    ManagedContentPackageInfo staged;
    if (!InspectWithOrigin(stagingRoot, ManagedContentPackageOrigin::UserManaged, &staged, error)) {
        cleanupStaging();
        return false;
    }
    if (staged.id != incoming.id || staged.kind != incoming.kind) {
        cleanupStaging();
        return Fail(error, L"Content package changed identity while staging.");
    }

    fs::path originalExistingRoot;
    bool backupCreated = false;
    if (existingFound) {
        originalExistingRoot = existing.packageRoot;
        fs::create_directories(backupContainer, ec);
        if (ec) {
            cleanupStaging();
            return Fail(error, L"Unable to create content package backup directory.");
        }
        ec.clear();
        fs::rename(originalExistingRoot, backupRoot, ec);
        if (ec) {
            cleanupStaging();
            cleanupBackup();
            return Fail(error, L"Unable to move existing content package to backup: error=" +
                               std::to_wstring(ec.value()));
        }
        backupCreated = true;
    }

    ec.clear();
    fs::rename(stagingRoot, canonicalTarget, ec);
    if (ec) {
        if (backupCreated) {
            std::error_code rollbackError;
            fs::rename(backupRoot, originalExistingRoot, rollbackError);
        }
        cleanupStaging();
        cleanupBackup();
        return Fail(error, L"Unable to activate staged content package: error=" + std::to_wstring(ec.value()));
    }

    cleanupStaging();

    ManagedContentPackageInfo installed;
    std::wstring activationError;
    const bool activationValid =
        InspectWithOrigin(canonicalTarget, ManagedContentPackageOrigin::UserManaged, &installed, &activationError) &&
        installed.id == incoming.id && installed.kind == incoming.kind;
    if (!activationValid) {
        std::error_code removeError;
        fs::remove_all(canonicalTarget, removeError);

        std::error_code rollbackError;
        if (backupCreated) fs::rename(backupRoot, originalExistingRoot, rollbackError);
        cleanupBackup();

        std::wstring message = L"Activated content package failed final validation";
        if (!activationError.empty()) message += L": " + activationError;
        if (removeError) message += L"; failed to remove invalid target, error=" + std::to_wstring(removeError.value());
        if (rollbackError) message += L"; rollback failed, error=" + std::to_wstring(rollbackError.value());
        else if (backupCreated) message += L"; previous package restored";
        return Fail(error, std::move(message));
    }

    // The old version stays in .backup until the final target has been loaded
    // successfully from its canonical location. Only then is replacement final.
    cleanupBackup();

    result->package = std::move(installed);
    result->replacedExisting = existingFound;
    if (error) error->clear();
    return true;
}

bool WriteSelfTestPackage(
    const fs::path& root,
    std::string_view id,
    ContentKind kind,
    std::string_view version) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return false;

    std::ofstream manifest(root / L"manifest.json", std::ios::binary | std::ios::trunc);
    if (!manifest) return false;
    manifest << "{\n"
             << "  \"schema\": 1,\n"
             << "  \"id\": \"" << id << "\",\n"
             << "  \"name\": \"Managed Package Self Test\",\n"
             << "  \"author\": \"MiaoDesk\",\n"
             << "  \"version\": \"" << version << "\",\n"
             << "  \"kind\": \"" << MiaoContentPackage::KindKey(kind) << "\",\n"
             << "  \"runtime\": \"scene\",\n"
             << "  \"entry\": \"scene.json\",\n"
             << "  \"parameters\": \"parameters.json\",\n"
             << "  \"capabilities\": []\n"
             << "}\n";
    manifest.close();

    std::ofstream scene(root / L"scene.json", std::ios::binary | std::ios::trunc);
    scene << R"JSON({"schema":1,"id":"scene://managed-package-self-test"})JSON";
    scene.close();
    std::ofstream parameters(root / L"parameters.json", std::ios::binary | std::ios::trunc);
    parameters << R"JSON({"schema":1,"parameters":[]})JSON";
    return static_cast<bool>(scene) && static_cast<bool>(parameters);
}

} // namespace

std::wstring MiaoContentPackageManager::MakeSource(std::string_view definitionId) {
    if (!IsStableId(definitionId)) return {};
    return std::wstring(kSourcePrefix) + AsciiToWide(definitionId);
}

bool MiaoContentPackageManager::ParseSource(
    std::wstring_view source,
    std::string* definitionId,
    std::wstring* error) {
    if (!definitionId) return Fail(error, L"Content definition id output is null.");
    definitionId->clear();
    if (source.size() <= kSourcePrefix.size() || source.substr(0, kSourcePrefix.size()) != kSourcePrefix)
        return Fail(error, L"Content source must use content:<id>.");

    const std::wstring_view id = source.substr(kSourcePrefix.size());
    std::string ascii;
    ascii.reserve(id.size());
    for (const wchar_t ch : id) {
        if (ch < 0 || ch > 0x7f) return Fail(error, L"Content id must be ASCII.");
        ascii.push_back(static_cast<char>(ch));
    }
    if (!IsStableId(ascii)) return Fail(error, L"Content id is invalid.");
    *definitionId = std::move(ascii);
    if (error) error->clear();
    return true;
}

fs::path MiaoContentPackageManager::BuiltInRoot(ContentKind kind) {
    return kind == ContentKind::Widget ? paths::BuiltInWidgetPackagesRoot() : paths::BuiltInWallpaperPackagesRoot();
}

fs::path MiaoContentPackageManager::UserRoot(ContentKind kind) {
    return kind == ContentKind::Widget ? paths::WidgetPackagesRoot() : paths::WallpaperPackagesRoot();
}

bool MiaoContentPackageManager::Inspect(
    const fs::path& packageRoot,
    ManagedContentPackageInfo* package,
    std::wstring* error) {
    return InspectWithOrigin(packageRoot, ManagedContentPackageOrigin::External, package, error);
}

bool MiaoContentPackageManager::Resolve(
    ContentKind expectedKind,
    std::wstring_view source,
    ManagedContentPackageInfo* package,
    std::wstring* error) {
    return ResolveWithRoots(DefaultRoots(), expectedKind, source, package, error);
}

bool MiaoContentPackageManager::Install(
    const fs::path& sourceRoot,
    ContentPackageInstallResult* result,
    const ContentPackageInstallOptions& options,
    std::wstring* error) {
    return InstallWithRoots(DefaultRoots(), sourceRoot, result, options, error);
}

bool MiaoContentPackageManager::SelfTest() {
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-ContentPackageManager-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    const PackageRoots roots{
        root / L"BuiltInWallpapers",
        root / L"BuiltInWidgets",
        root / L"UserWallpapers",
        root / L"UserWidgets",
    };
    const fs::path sourceRoot = root / L"Sources";
    const fs::path widgetV1 = sourceRoot / L"ClockDownload.mdwidget";
    const fs::path widgetV2 = sourceRoot / L"ClockUpgrade.mdwidget";
    const fs::path wallpaper = sourceRoot / L"WallpaperDownload.mdwall";
    const std::string widgetId = "com.goodloong.manager-clock";
    const std::string wallpaperId = "com.goodloong.manager-wallpaper";

    std::error_code ec;
    fs::remove_all(root, ec);
    bool ok = WriteSelfTestPackage(widgetV1, widgetId, ContentKind::Widget, "1.0.0") &&
              WriteSelfTestPackage(widgetV2, widgetId, ContentKind::Widget, "2.0.0") &&
              WriteSelfTestPackage(wallpaper, wallpaperId, ContentKind::Wallpaper, "1.0.0");

    std::wstring error;
    ContentPackageInstallResult installed;
    ok = ok && InstallWithRoots(roots, widgetV1, &installed, {}, &error) &&
         installed.package.id == widgetId && !installed.replacedExisting &&
         installed.package.packageRoot == roots.userWidgets / L"com.goodloong.manager-clock.mdwidget";

    const fs::path renamed = roots.userWidgets / L"RenamedClock.mdwidget";
    if (ok) {
        fs::rename(installed.package.packageRoot, renamed, ec);
        ok = !ec;
    }
    ManagedContentPackageInfo resolved;
    ok = ok && ResolveWithRoots(roots, ContentKind::Widget, MakeSource(widgetId), &resolved, &error) &&
         SamePath(resolved.packageRoot, renamed);

    ContentPackageInstallResult upgraded;
    ok = ok && InstallWithRoots(roots, widgetV2, &upgraded, {}, &error) && upgraded.replacedExisting &&
         upgraded.package.version == L"2.0.0" &&
         upgraded.package.packageRoot == roots.userWidgets / L"com.goodloong.manager-clock.mdwidget";

    ContentPackageInstallResult wallpaperInstalled;
    ok = ok && InstallWithRoots(roots, wallpaper, &wallpaperInstalled, {}, &error) &&
         wallpaperInstalled.package.kind == ContentKind::Wallpaper &&
         wallpaperInstalled.package.packageRoot == roots.userWallpapers / L"com.goodloong.manager-wallpaper.mdwall";

    const fs::path builtInClock = roots.builtInWidgets / L"OfficialClock.mdwidget";
    ok = ok && WriteSelfTestPackage(builtInClock, widgetId, ContentKind::Widget, "9.0.0");
    ContentPackageInstallResult conflict;
    ok = ok && !InstallWithRoots(roots, widgetV2, &conflict, {}, &error);

    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::content
