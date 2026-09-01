#include "miaodesk/WallpaperService.h"

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
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path base = (length > 0 && length < std::size(local)) ? fs::path(local) : fs::temp_directory_path();
    fs::path directory = base / L"MiaoDesk";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory;
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
    if (item.id == L"scene-aurora" || item.id == L"aurora") return L"aurora";
    if (item.id == L"scene-neon" || item.id == L"neon") return L"neon";
    if (item.id == L"scene-grid" || item.id == L"grid") return L"grid";
    return {};
}

WallpaperServiceResult ValidateAssignableItem(const wallpaper::WallpaperLibraryItem& item) {
    if (item.id.empty()) return {false, L"壁纸库项目缺少 id。"};
    if (item.kind == wallpaper::LibraryWallpaperKind::Unknown)
        return {false, L"不支持的壁纸库项目类型。"};
    if (item.kind == wallpaper::LibraryWallpaperKind::Scene && RuntimeSceneKey(item).empty())
        return {false, L"未知 Scene 壁纸：" + item.id};
    if (item.kind == wallpaper::LibraryWallpaperKind::Web &&
        !wallpaper::WallpaperLibrary::IsTrustedWebUrl(item.source.wstring()))
        return {false, L"Web 壁纸必须是可信 HTTPS 地址。"};
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
            miaodesk::log::Error(L"WallpaperService", L"未知 Scene 壁纸: " + item.id);
            return {false, L"未知 Scene 壁纸：" + item.id};
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
    case wallpaper::LibraryWallpaperKind::Web:
        return {false, L"Web 库项目必须通过已验证的 .mdwall 包路径应用。"};
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
