#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "turingdesk/WallpaperLibrary.h"
#include "turingdesk/WallpaperRuntimeControl.h"

namespace turingdesk::wallpaper {

struct WallpaperLibraryTarget {
    std::wstring monitorId;
    std::wstring displayName;
    bool primary{};
};

enum class WallpaperSettingsSection {
    Installed,
    Widgets,
    Playlists,
    Displays,
    Rules,
    Performance,
    AI,
};

class WallpaperLibraryWindow {
public:
    using ApplyCallback = std::function<void(const WallpaperLibraryItem&, const std::wstring& targetMonitorId)>;
    using GlobalApplyCallback = std::function<void(const WallpaperLibraryItem&)>;
    using NavigateCallback = std::function<void(WallpaperSettingsSection)>;
    using WallpaperEnabledCallback = std::function<void(bool enabled)>;

    WallpaperLibraryWindow();
    ~WallpaperLibraryWindow();

    WallpaperLibraryWindow(const WallpaperLibraryWindow&) = delete;
    WallpaperLibraryWindow& operator=(const WallpaperLibraryWindow&) = delete;

    bool Show(HINSTANCE instance, WallpaperLibrary* library,
              const std::vector<WallpaperLibraryTarget>& targets,
              ApplyCallback applyCallback,
              NavigateCallback navigateCallback = {},
              WallpaperEnabledCallback wallpaperEnabledCallback = {});
    bool Show(HINSTANCE instance, WallpaperLibrary* library, GlobalApplyCallback applyCallback) {
        return Show(instance, library, {},
                    [callback = std::move(applyCallback)](const WallpaperLibraryItem& item, const std::wstring&) {
                        if (callback) callback(item);
                    });
    }
    void SetTargets(const std::vector<WallpaperLibraryTarget>& targets);
    void Close();
    void Refresh();
    void SetWallpaperEnabledState(bool enabled);
    bool Visible() const noexcept;
    HWND Window() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace turingdesk::wallpaper
