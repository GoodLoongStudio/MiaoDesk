#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "miaodesk/WallpaperLibrary.h"
#include "miaodesk/WallpaperRuntimeControl.h"

namespace miaodesk::wallpaper {

struct WallpaperLibraryTarget {
    std::wstring monitorId;
    std::wstring displayName;
    bool primary{};
};

class WallpaperLibraryWindow {
public:
    // Returns an empty string when the apply happened, and the reason it did not when it
    // did not. The window owns the status line -- it is the surface the user is looking
    // at -- so the callback reports rather than messages on its own.
    //
    // This used to return void, and that quietly made `applied` unconditionally true on
    // the engine path: the window said "已应用到桌面", stamped MarkUsed, and moved on,
    // while the engine had actually bailed out and put the reason in its own diagnostics
    // text, one window away, in a page the user has to navigate to. A void callback is
    // not a signal that the action succeeded.
    using ApplyCallback = std::function<std::wstring(const WallpaperLibraryItem&, const std::wstring& targetMonitorId)>;
    using GlobalApplyCallback = std::function<std::wstring(const WallpaperLibraryItem&)>;

    WallpaperLibraryWindow();
    ~WallpaperLibraryWindow();

    WallpaperLibraryWindow(const WallpaperLibraryWindow&) = delete;
    WallpaperLibraryWindow& operator=(const WallpaperLibraryWindow&) = delete;

    bool Show(HINSTANCE instance, WallpaperLibrary* library,
              const std::vector<WallpaperLibraryTarget>& targets,
              ApplyCallback applyCallback);
    bool Show(HINSTANCE instance, WallpaperLibrary* library, GlobalApplyCallback applyCallback) {
        return Show(instance, library, {},
                    [callback = std::move(applyCallback)](const WallpaperLibraryItem& item, const std::wstring& target) {
                        return callback ? callback(item) : std::wstring();
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

} // namespace miaodesk::wallpaper
