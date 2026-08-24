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

    const fs::path config = LocalTuringDeskDirectory() / L"wallpaper.ini";
    bool ok = true;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Enabled", L"1", config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Scene", L"web", config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Image", source.c_str(), config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Video", L"", config.c_str()) != FALSE && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    if (!ok) return {false, L"无法保存当前 Web 桌面状态。"};

    return {true, L"已准备 Web 桌面：" + manifest.title + L" · " + source.wstring()};
}

} // namespace turingdesk::desktop
