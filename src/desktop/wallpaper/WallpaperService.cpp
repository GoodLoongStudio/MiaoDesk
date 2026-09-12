#include "miaodesk/WallpaperService.h"
#include "miaodesk/AppPaths.h"
#include "miaodesk/BuiltinWallpaperCatalog.h"
#include "miaodesk/MiaoContentDefinitionLoader.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoContentPackageManager.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include "miaodesk/WallpaperMonitorAssignments.h"
#include "miaodesk/WallpaperPackage.h"
#include "miaodesk/RuntimeLogger.h"

#include <windows.h>

#include <filesystem>
#include <iterator>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::desktop {
namespace {

fs::path LocalMiaoDeskDirectory() {
    return paths::EnsureStateRoot();
}

std::wstring ReadProfile(const fs::path& path, const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(L"Wallpaper", key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

WallpaperServiceResult PersistWallpaperSelection(
    std::wstring_view scene,
    const fs::path& imageOrWebSource,
    const fs::path& videoSource) {
    const fs::path config = LocalMiaoDeskDirectory() / L"wallpaper.ini";
    const std::wstring sceneText(scene);
    const std::wstring imageText = imageOrWebSource.wstring();
    const std::wstring videoText = videoSource.wstring();
    bool ok = true;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Enabled", L"1", config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Scene", sceneText.c_str(), config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Image", imageText.c_str(), config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Video", videoText.c_str(), config.c_str()) != FALSE && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    return ok ? WallpaperServiceResult{true, L"壁纸选择已保存。"}
              : WallpaperServiceResult{false, L"无法保存当前壁纸状态。"};
}

std::wstring RuntimeSceneKey(const wallpaper::WallpaperLibraryItem& item) {
    if (item.kind != wallpaper::LibraryWallpaperKind::Scene) return {};
    const auto* definition = wallpaper::FindBuiltinWallpaper(item.id);
    return definition ? std::wstring(definition->runtimeKey) : std::wstring{};
}

bool IsContentId(std::wstring_view id) noexcept {
    return id.size() > content::MiaoContentPackageManager::kSourcePrefix.size() &&
           id.substr(0, content::MiaoContentPackageManager::kSourcePrefix.size()) ==
               content::MiaoContentPackageManager::kSourcePrefix;
}

fs::path CanonicalScenePackageRoot(const wallpaper::WallpaperLibraryItem& item) {
    if (IsContentId(item.id)) {
        content::ManagedContentPackageInfo package;
        std::wstring error;
        if (content::MiaoContentPackageManager::Resolve(
                content::ContentKind::Wallpaper, item.id, &package, &error)) {
            return package.packageRoot;
        }
        return {};
    }

    const fs::path& source = item.source;
    if (source.empty()) return {};
    std::error_code ec;
    const fs::path normalized = fs::absolute(source, ec).lexically_normal();
    if (ec) return {};
    if (fs::is_directory(normalized, ec) && _wcsicmp(normalized.extension().c_str(), L".mdwall") == 0)
        return normalized;
    ec.clear();
    if (fs::is_regular_file(normalized, ec)) {
        fs::path current = normalized.parent_path();
        while (!current.empty()) {
            if (_wcsicmp(current.extension().c_str(), L".mdwall") == 0) return current;
            const fs::path parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }
    }
    return {};
}

WallpaperServiceResult ValidateCanonicalScene(const wallpaper::WallpaperLibraryItem& item) {
    const fs::path packageRoot = CanonicalScenePackageRoot(item);
    if (packageRoot.empty()) return {false, L"Scene 壁纸既不是内置场景，也不是有效的 .mdwall 内容包。"};

    content::LoadedMiaoContentPackage package;
    std::wstring error;
    if (!content::MiaoContentPackage::Load(packageRoot, &package, &error))
        return {false, error.empty() ? L"无法加载 .mdwall 内容包。" : error};

    content::ContentDefinition contentDefinition;
    if (!content::MiaoContentDefinitionLoader::FromPackage(package, &contentDefinition, &error))
        return {false, error.empty() ? L"ContentDefinition / ParameterSchema 无效。" : error};
    if (contentDefinition.kind != content::ContentKind::Wallpaper ||
        contentDefinition.runtime != content::ContentRuntimeKind::Scene)
        return {false, L".mdwall 内容包不是 Wallpaper Scene Runtime。"};

    content::SceneRuntimeDefinition sceneDefinition;
    if (!content::MiaoSceneSerializer::DeserializePackage(package, &sceneDefinition, &error))
        return {false, error.empty() ? L"Scene 内容定义无效。" : error};
    return {true, L"配置化 Scene 内容包已通过 ContentDefinition + Scene Runtime 校验。"};
}

WallpaperServiceResult ResolveCanonicalWebEntry(
    const wallpaper::WallpaperLibraryItem& item,
    fs::path* resolvedEntry) {
    content::ManagedContentPackageInfo managed;
    std::wstring error;
    if (!content::MiaoContentPackageManager::Resolve(
            content::ContentKind::Wallpaper, item.id, &managed, &error)) {
        return {false, error.empty() ? L"找不到已安装的 Web 内容包。" : error};
    }
    if (managed.runtime != content::ContentRuntimeKind::Web)
        return {false, L"内容包不是 Wallpaper Web Runtime。"};

    content::LoadedMiaoContentPackage package;
    if (!content::MiaoContentPackage::Load(managed.packageRoot, &package, &error))
        return {false, error.empty() ? L"无法加载 Web .mdwall 内容包。" : error};
    if (package.manifest.kind != content::ContentKind::Wallpaper ||
        package.manifest.runtime != content::ContentRuntimeKind::Web)
        return {false, L".mdwall 内容包不是 Wallpaper Web Runtime。"};

    fs::path entry;
    if (!content::MiaoContentPackage::ResolvePackagePath(
            package.root, package.manifest.entry, &entry, &error))
        return {false, error.empty() ? L"Web 内容包 entry 无效。" : error};

    std::error_code ec;
    if (!fs::is_regular_file(entry, ec))
        return {false, L"Web 内容包 entry 文件不存在。"};
    const auto extension = entry.extension().wstring();
    if (_wcsicmp(extension.c_str(), L".html") != 0 && _wcsicmp(extension.c_str(), L".htm") != 0)
        return {false, L"Web 内容包 entry 必须是 HTML 文件。"};
    if (resolvedEntry) *resolvedEntry = std::move(entry);
    return {true, L"配置化 Web 内容包已通过 Runtime + entry 校验。"};
}

WallpaperServiceResult ValidateCanonicalWeb(const wallpaper::WallpaperLibraryItem& item) {
    return ResolveCanonicalWebEntry(item, nullptr);
}

WallpaperServiceResult ValidateAssignableItem(const wallpaper::WallpaperLibraryItem& item) {
    if (item.id.empty()) return {false, L"壁纸库项目缺少 id。"};
    if (item.kind == wallpaper::LibraryWallpaperKind::Unknown)
        return {false, L"不支持的壁纸库项目类型。"};
    if (item.kind == wallpaper::LibraryWallpaperKind::Scene) {
        if (!RuntimeSceneKey(item).empty()) return {true, L"内置 Scene 可用于显示器分配。"};
        return ValidateCanonicalScene(item);
    }
    if (item.kind == wallpaper::LibraryWallpaperKind::Web) {
        if (IsContentId(item.id)) return ValidateCanonicalWeb(item);
        if (!wallpaper::WallpaperLibrary::IsTrustedWebUrl(item.source.wstring()))
            return {false, L"Web 壁纸必须是可信 HTTPS 地址。"};
    }
    if (item.kind == wallpaper::LibraryWallpaperKind::Image ||
        item.kind == wallpaper::LibraryWallpaperKind::Video) {
        std::error_code ec;
        const auto source = fs::absolute(item.source, ec).lexically_normal();
        if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec))
            return {false, item.kind == wallpaper::LibraryWallpaperKind::Image
                ? L"图片壁纸文件不存在。" : L"视频壁纸文件不存在。"};
    }
    return {true, L"壁纸库项目可用于显示器分配。"};
}

} // namespace

WallpaperServiceResult WallpaperService::GetState(WallpaperState* state) const {
    if (!state) return {false, L"WallpaperState 输出不能为空。"};
    const fs::path config = LocalMiaoDeskDirectory() / L"wallpaper.ini";

    WallpaperState next;
    next.enabled = GetPrivateProfileIntW(L"Wallpaper", L"Enabled", 1, config.c_str()) != 0;
    next.scene = ReadProfile(config, L"Scene", L"aurora");
    next.layout = ReadProfile(config, L"Layout", L"span");
    next.scale = ReadProfile(config, L"Scale", L"cover");
    next.fpsCap = GetPrivateProfileIntW(L"Wallpaper", L"FpsCap", 30, config.c_str());
    next.imageOrWebSource = ReadProfile(config, L"Image", L"");
    next.videoSource = ReadProfile(config, L"Video", L"");
    *state = next;
    return {true, L"壁纸状态读取完成。"};
}

WallpaperServiceResult WallpaperService::SetEnabled(const bool enabled) const {
    const fs::path config = LocalMiaoDeskDirectory() / L"wallpaper.ini";
    const wchar_t* value = enabled ? L"1" : L"0";
    if (WritePrivateProfileStringW(L"Wallpaper", L"Enabled", value, config.c_str()) == FALSE) {
        miaodesk::log::Error(L"WallpaperService", L"SetEnabled(" + std::wstring(value) + L") 写入 ini 失败: " + config.wstring());
        return {false, L"无法保存壁纸启用状态。"};
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    miaodesk::log::Info(L"WallpaperService", L"SetEnabled(" + std::wstring(value) + L") 成功保存至 " + config.wstring());
    return {true, enabled ? L"壁纸已启用，小组件不受影响。" : L"壁纸已停用，小组件仍可显示。"};
}

WallpaperServiceResult WallpaperService::ApplyWebPackage(const fs::path& package) const {
    miaodesk::log::Info(L"WallpaperService", L"ApplyWebPackage 请求: " + package.wstring());
    wallpaper::WallpaperPackageManifest manifest;
    std::wstring error;
    if (!wallpaper::WallpaperPackage::Validate(package, &manifest, &error)) {
        miaodesk::log::Error(L"WallpaperService", L"ApplyWebPackage 校验失败: " + error);
        return {false, error.empty() ? L".mdwall 校验失败。" : error};
    }
    if (manifest.type != wallpaper::WallpaperPackageType::Web) {
        miaodesk::log::Error(L"WallpaperService", L"ApplyWebPackage 类型不匹配: 不是 Web 类型");
        return {false, L"当前壁纸服务只接受 Web 类型 .mdwall。"};
    }

    std::error_code ec;
    const fs::path source = fs::absolute(package / manifest.entry, ec).lexically_normal();
    if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec)) {
        miaodesk::log::Error(L"WallpaperService", L"ApplyWebPackage entry 文件不存在: " + source.wstring());
        return {false, L".mdwall Web entry 不存在。"};
    }

    const auto persisted = PersistWallpaperSelection(L"web", source, {});
    if (!persisted.success) {
        miaodesk::log::Error(L"WallpaperService", L"ApplyWebPackage 保存配置失败: " + persisted.message);
        return persisted;
    }
    miaodesk::log::Info(L"WallpaperService", L"ApplyWebPackage 成功: \"" + manifest.title + L"\" -> " + source.wstring());
    return {true, L"已准备 Web 桌面：" + manifest.title + L" · " + source.wstring()};
}

WallpaperServiceResult WallpaperService::ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const {
    if (item.id.empty()) {
        miaodesk::log::Error(L"WallpaperService", L"ApplyLibraryItem 失败: 缺少 id");
        return {false, L"壁纸库项目缺少 id。"};
    }

    miaodesk::log::Info(L"WallpaperService", L"ApplyLibraryItem: id=" + item.id + L", title=\"" + item.title + L"\"");
    std::error_code ec;
    switch (item.kind) {
    case wallpaper::LibraryWallpaperKind::Scene: {
        const auto scene = RuntimeSceneKey(item);
        if (scene.empty()) {
            const auto canonical = ValidateCanonicalScene(item);
            if (!canonical.success) {
                miaodesk::log::Error(L"WallpaperService", L"配置化 Scene 校验失败: " + canonical.message);
                return canonical;
            }
            return {false, L"该配置化 Scene 已进入内容框架；当前全局/跨屏入口仍只接受内置 Scene，请在目标显示器上分配该壁纸。"};
        }
        const auto persisted = PersistWallpaperSelection(scene, {}, {});
        if (persisted.success) miaodesk::log::Info(L"WallpaperService", L"已选择 Scene: " + scene);
        return persisted.success ? WallpaperServiceResult{true, L"已选择 Scene：" + item.title} : persisted;
    }
    case wallpaper::LibraryWallpaperKind::Image: {
        const fs::path source = fs::absolute(item.source, ec).lexically_normal();
        if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec)) {
            miaodesk::log::Error(L"WallpaperService", L"图片壁纸文件不存在: " + source.wstring());
            return {false, L"图片壁纸文件不存在。"};
        }
        const auto persisted = PersistWallpaperSelection(L"image", source, {});
        if (persisted.success) miaodesk::log::Info(L"WallpaperService", L"已选择图片壁纸: " + source.wstring());
        return persisted.success ? WallpaperServiceResult{true, L"已选择图片壁纸：" + item.title} : persisted;
    }
    case wallpaper::LibraryWallpaperKind::Video: {
        const fs::path source = fs::absolute(item.source, ec).lexically_normal();
        if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec)) {
            miaodesk::log::Error(L"WallpaperService", L"视频壁纸文件不存在: " + source.wstring());
            return {false, L"视频壁纸文件不存在。"};
        }
        const auto persisted = PersistWallpaperSelection(L"video", {}, source);
        if (persisted.success) miaodesk::log::Info(L"WallpaperService", L"已选择视频壁纸: " + source.wstring());
        return persisted.success ? WallpaperServiceResult{true, L"已选择视频壁纸：" + item.title} : persisted;
    }
    case wallpaper::LibraryWallpaperKind::Web: {
        if (!IsContentId(item.id))
            return {false, L"Web 库项目必须通过已验证的 .mdwall 包路径应用。"};
        fs::path source;
        const auto valid = ResolveCanonicalWebEntry(item, &source);
        if (!valid.success) {
            miaodesk::log::Error(L"WallpaperService", L"配置化 Web 校验失败: " + valid.message);
            return valid;
        }
        const auto persisted = PersistWallpaperSelection(L"web", source, {});
        if (persisted.success)
            miaodesk::log::Info(L"WallpaperService", L"已选择 Content Web 壁纸: " + source.wstring());
        return persisted.success ? WallpaperServiceResult{true, L"已选择 Web 壁纸：" + item.title} : persisted;
    }
    case wallpaper::LibraryWallpaperKind::Unknown:
        break;
    }
    return {false, L"不支持的壁纸库项目类型。"};
}

WallpaperServiceResult WallpaperService::AssignLibraryItemToMonitor(
    const wallpaper::WallpaperLibraryItem& item,
    std::wstring_view monitorId,
    std::wstring_view friendlyName) const {
    if (monitorId.empty()) return {false, L"显示器 id 不能为空。"};
    const auto valid = ValidateAssignableItem(item);
    if (!valid.success) return valid;

    wallpaper::WallpaperMonitorAssignments assignments;
    std::wstring error;
    if (!assignments.Load(&error))
        return {false, error.empty() ? L"无法读取显示器壁纸分配。" : error};
    if (!assignments.AssignById(std::wstring(monitorId), item.id, std::wstring(friendlyName), &error))
        return {false, error.empty() ? L"无法保存显示器壁纸分配。" : error};
    return {true, L"已将壁纸分配到显示器：" + item.title};
}

WallpaperServiceResult WallpaperService::ClearMonitorAssignment(std::wstring_view monitorId) const {
    if (monitorId.empty()) return {false, L"显示器 id 不能为空。"};
    wallpaper::WallpaperMonitorAssignments assignments;
    std::wstring error;
    if (!assignments.Load(&error))
        return {false, error.empty() ? L"无法读取显示器壁纸分配。" : error};
    if (!assignments.Clear(monitorId, &error))
        return {false, error.empty() ? L"无法清除显示器壁纸分配。" : error};
    return {true, L"显示器壁纸分配已清除。"};
}

} // namespace miaodesk::desktop
