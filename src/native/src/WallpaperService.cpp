#include "turingdesk/WallpaperService.h"

#include "turingdesk/WallpaperPackage.h"

#include <windows.h>

#include <filesystem>
#include <iterator>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::desktop {
namespace {

fs::path LocalTuringDeskDirectory() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path base = (length > 0 && length < std::size(local)) ? fs::path(local) : fs::temp_directory_path();
    fs::path directory = base / L"TuringDesk";
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
    const fs::path config = LocalTuringDeskDirectory() / L"wallpaper.ini";
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

} // namespace

WallpaperServiceResult WallpaperService::GetState(WallpaperState* state) const {
    if (!state) return {false, L"WallpaperState 输出不能为空。"};
    const fs::path config = LocalTuringDeskDirectory() / L"wallpaper.ini";

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

WallpaperServiceResult WallpaperService::ApplyWebPackage(const fs::path& package) const {
    wallpaper::WallpaperPackageManifest manifest;
    std::wstring error;
    if (!wallpaper::WallpaperPackage::Validate(package, &manifest, &error))
        return {false, error.empty() ? L".tdwall 校验失败。" : error};
    if (manifest.type != wallpaper::WallpaperPackageType::Web)
        return {false, L"当前壁纸服务只接受 Web 类型 .tdwall。"};

    std::error_code ec;
    const fs::path source = fs::absolute(package / manifest.entry, ec).lexically_normal();
    if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec))
        return {false, L".tdwall Web entry 不存在。"};

    const auto persisted = PersistWallpaperSelection(L"web", source, {});
    if (!persisted.success) return persisted;
    return {true, L"已准备 Web 桌面：" + manifest.title + L" · " + source.wstring()};
}

WallpaperServiceResult WallpaperService::ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const {
    if (item.id.empty()) return {false, L"壁纸库项目缺少 id。"};

    std::error_code ec;
    switch (item.kind) {
    case wallpaper::LibraryWallpaperKind::Scene: {
        if (item.id != L"aurora" && item.id != L"neon" && item.id != L"grid")
            return {false, L"未知 Scene 壁纸：" + item.id};
        const auto persisted = PersistWallpaperSelection(item.id, {}, {});
        return persisted.success ? WallpaperServiceResult{true, L"已选择 Scene：" + item.title} : persisted;
    }
    case wallpaper::LibraryWallpaperKind::Image: {
        const fs::path source = fs::absolute(item.source, ec).lexically_normal();
        if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec))
            return {false, L"图片壁纸文件不存在。"};
        const auto persisted = PersistWallpaperSelection(L"image", source, {});
        return persisted.success ? WallpaperServiceResult{true, L"已选择图片壁纸：" + item.title} : persisted;
    }
    case wallpaper::LibraryWallpaperKind::Video: {
        const fs::path source = fs::absolute(item.source, ec).lexically_normal();
        if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec))
            return {false, L"视频壁纸文件不存在。"};
        const auto persisted = PersistWallpaperSelection(L"video", {}, source);
        return persisted.success ? WallpaperServiceResult{true, L"已选择视频壁纸：" + item.title} : persisted;
    }
    case wallpaper::LibraryWallpaperKind::Web:
        return {false, L"Web 库项目必须通过已验证的 .tdwall 包路径应用。"};
    case wallpaper::LibraryWallpaperKind::Unknown:
        break;
    }
    return {false, L"不支持的壁纸库项目类型。"};
}

} // namespace turingdesk::desktop
