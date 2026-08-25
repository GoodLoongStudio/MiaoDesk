#pragma once

#include <windows.h>

#include <string>

namespace turingdesk::wallpaper {

enum class DesktopShellMode {
    None,
    RaisedDesktop,
    LegacyWorkerW,
    ProgmanFallback,
};

enum class DesktopSurfaceRole {
    Wallpaper,
    Widget,
};

struct DesktopShellSnapshot {
    HWND progman{};
    HWND shellDefView{};
    HWND workerW{};
    HWND legacyDefViewParent{};
    DWORD explorerPid{};
    DesktopShellMode mode{DesktopShellMode::None};
    unsigned long long generation{};

    bool Valid() const noexcept {
        return progman != nullptr && IsWindow(progman) != FALSE && mode != DesktopShellMode::None;
    }
};

struct DesktopSurfaceHealth {
    bool window{};
    bool parent{};
    bool childStyle{};
    bool layered{};
    bool visible{};
    bool geometry{};
    DesktopShellMode mode{DesktopShellMode::None};
    DesktopSurfaceRole role{DesktopSurfaceRole::Wallpaper};
    HWND actualParent{};
    std::wstring detail;

    bool Healthy() const noexcept {
        return window && parent && childStyle && layered && visible && geometry;
    }
};

// Clean-room native shell integration informed by the public behavior of mature
// Windows wallpaper projects and Microsoft shell behavior. Renderers must use
// this service instead of discovering Progman/WorkerW independently.
class DesktopShellHost {
public:
    DesktopShellHost() = default;

    bool Refresh(std::wstring* error = nullptr);
    bool EnsureCurrent(std::wstring* error = nullptr);

    const DesktopShellSnapshot& Snapshot() const noexcept { return snapshot_; }
    HWND SurfaceParent() const noexcept;

    bool AttachSurface(HWND surface,
                       DesktopSurfaceRole role,
                       const RECT& desktopBounds,
                       bool visible,
                       std::wstring* error = nullptr);

    bool PrepareSurface(HWND surface,
                        bool clickThrough,
                        std::wstring* error = nullptr) const;

    // Revalidates Explorer/shell generation and repairs the ordering of all
    // known TuringDesk wallpaper + Widget surfaces. Coordinators should call
    // this instead of enumerating desktop siblings or issuing SetWindowPos.
    bool RepairSurfaceStack(HWND expectedSurface = nullptr,
                            std::wstring* error = nullptr);

    void RepairKnownTuringDeskSurfaces() const;
    DesktopSurfaceHealth InspectSurface(HWND surface, DesktopSurfaceRole role) const;

    static DesktopSurfaceRole InferRole(HWND window) noexcept;
    static const wchar_t* ModeKey(DesktopShellMode mode) noexcept;
    static const wchar_t* RoleKey(DesktopSurfaceRole role) noexcept;
    static bool SelfTest() noexcept;

private:
    static bool IsWindowClass(HWND window, const wchar_t* expected) noexcept;
    static bool IsWidgetWebSurface(HWND window) noexcept;
    static HWND LastChild(HWND parent) noexcept;
    static bool TrySetParent(HWND child, HWND parent) noexcept;
    static RECT MapDesktopRectToParent(HWND parent, const RECT& desktopBounds) noexcept;
    static DWORD ExplorerProcessId(HWND progman) noexcept;

    void RequestWallpaperLayer() const noexcept;
    void RepairRaisedDesktopWorkerOrder() const noexcept;
    void RepairRoleOrder(HWND parent) const noexcept;

    DesktopShellSnapshot snapshot_{};
    unsigned long long generation_{};
};

} // namespace turingdesk::wallpaper
