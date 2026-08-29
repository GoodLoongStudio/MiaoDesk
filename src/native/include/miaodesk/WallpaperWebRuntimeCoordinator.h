#pragma once

#include <memory>
#include <string>

#include "miaodesk/WallpaperLibrary.h"

namespace miaodesk::wallpaper {

// Web wallpaper and Widget coordination intentionally run in different process
// fault domains. Both reuse the same coordinator implementation and
// DesktopShellHost contract, but a failure in one scope must not tear down the
// other scope.
enum class WallpaperWebRuntimeScope {
    WebWallpaper,
    Widgets,
};

class WallpaperWebRuntimeCoordinator {
public:
    explicit WallpaperWebRuntimeCoordinator(WallpaperWebRuntimeScope scope);
    ~WallpaperWebRuntimeCoordinator();

    WallpaperWebRuntimeCoordinator(const WallpaperWebRuntimeCoordinator&) = delete;
    WallpaperWebRuntimeCoordinator& operator=(const WallpaperWebRuntimeCoordinator&) = delete;

    bool Start();
    void Stop();
    bool Running() const noexcept;
    WallpaperWebRuntimeScope Scope() const noexcept;

    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Persists a Web library item into the same global/per-monitor wallpaper state
// consumed by the native engine and the Web wallpaper coordinator. Global Web
// uses the legacy Image string as a source carrier while Scene="web"
// distinguishes the backend; this avoids a breaking wallpaper.ini schema
// migration.
bool ActivateWebWallpaperItem(const WallpaperLibraryItem& item,
                              const std::wstring& targetMonitorId,
                              std::wstring* error = nullptr);

} // namespace miaodesk::wallpaper
