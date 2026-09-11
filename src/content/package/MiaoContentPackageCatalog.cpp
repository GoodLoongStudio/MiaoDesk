#include "miaodesk/MiaoContentPackageManager.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

const wchar_t* ExtensionFor(ContentKind kind) noexcept {
    return kind == ContentKind::Widget ? L".mdwidget" : L".mdwall";
}

bool AppendRoot(ContentKind kind,
                ManagedContentPackageOrigin origin,
                std::vector<ManagedContentPackageInfo>* packages,
                std::wstring* error) {
    const fs::path root = origin == ManagedContentPackageOrigin::BuiltIn
        ? MiaoContentPackageManager::BuiltInRoot(kind)
        : MiaoContentPackageManager::UserRoot(kind);
    if (root.empty()) return true;

    std::error_code ec;
    if (!fs::exists(root, ec)) {
        if (ec) return Fail(error, L"无法检查内容包目录：" + root.wstring());
        return true;
    }
    if (!fs::is_directory(root, ec) || ec)
        return Fail(error, L"内容包根路径不是目录：" + root.wstring());

    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        std::error_code entryError;
        if (!entry.is_directory(entryError) || entryError) continue;
        if (_wcsicmp(entry.path().extension().c_str(), ExtensionFor(kind)) != 0) continue;

        ManagedContentPackageInfo package;
        std::wstring inspectError;
        if (!MiaoContentPackageManager::Inspect(entry.path(), &package, &inspectError)) {
            // Invalid or partially damaged packages are intentionally ignored by
            // the normal installed-content list. They cannot be resolved by the
            // runtime either and can be diagnosed from logs/filesystem later.
            continue;
        }
        if (package.kind != kind) continue;
        package.origin = origin;
        packages->push_back(std::move(package));
    }
    if (ec) return Fail(error, L"扫描内容包目录失败：" + root.wstring());
    return true;
}

} // namespace

bool MiaoContentPackageManager::List(
    std::vector<ManagedContentPackageInfo>* packages,
    std::wstring* error) {
    if (!packages) return Fail(error, L"Content package list 输出不能为空。");
    packages->clear();

    for (const auto kind : {ContentKind::Wallpaper, ContentKind::Widget}) {
        if (!AppendRoot(kind, ManagedContentPackageOrigin::BuiltIn, packages, error)) return false;
        if (!AppendRoot(kind, ManagedContentPackageOrigin::UserManaged, packages, error)) return false;
    }

    std::sort(packages->begin(), packages->end(), [](const auto& left, const auto& right) {
        if (left.kind != right.kind)
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        if (left.origin != right.origin)
            return static_cast<int>(left.origin) < static_cast<int>(right.origin);
        const int byName = _wcsicmp(left.name.c_str(), right.name.c_str());
        if (byName != 0) return byName < 0;
        return left.id < right.id;
    });

    if (error) error->clear();
    return true;
}

} // namespace miaodesk::content
