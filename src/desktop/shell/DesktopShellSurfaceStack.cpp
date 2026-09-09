#include "miaodesk/DesktopShellHost.h"

#include <algorithm>
#include <cstdlib>

namespace miaodesk::wallpaper {
namespace {

bool RectMatchesWithinTolerance(const RECT& actual, const RECT& expected, LONG tolerance = 2) noexcept {
    return std::abs(actual.left - expected.left) <= tolerance &&
           std::abs(actual.top - expected.top) <= tolerance &&
           std::abs(actual.right - expected.right) <= tolerance &&
           std::abs(actual.bottom - expected.bottom) <= tolerance;
}

bool WindowStillParented(HWND window, HWND expectedParent) noexcept {
    return window && expectedParent && IsWindow(window) && IsWindow(expectedParent) &&
           GetParent(window) == expectedParent;
}

bool SurfaceRequiresLayered(HWND surface) noexcept {
    if (!DesktopShellHost::IsWidgetNativeSurface(surface)) return true;
    const HWND parent = surface ? GetParent(surface) : nullptr;
    if (!parent || !IsWindow(parent)) return true;
    // Raised desktop (Progman with no-redirection composition) uses a direct
    // HwndRenderTarget for native widgets. WorkerW/legacy parents keep the
    // layered presentation path.
    return (GetWindowLongPtrW(parent, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) == 0;
}

bool AttachmentHealthValid(const DesktopSurfaceHealth& health, bool layeredRequired) noexcept {
    return health.parent && health.childStyle && (!layeredRequired || health.layered) && health.geometry;
}

} // namespace

bool DesktopShellHost::CurrentGenerationValid() const noexcept {
    if (!snapshot_.Valid()) return false;
    const HWND currentProgman = FindWindowW(L"Progman", nullptr);
    if (!currentProgman || currentProgman != snapshot_.progman) return false;

    DWORD currentExplorerPid = 0;
    GetWindowThreadProcessId(currentProgman, &currentExplorerPid);
    if (!currentExplorerPid || currentExplorerPid != snapshot_.explorerPid) return false;

    const HWND parent = SurfaceParent();
    if (!parent || !IsWindow(parent)) return false;
    if (snapshot_.mode == DesktopShellMode::RaisedDesktop) {
        return WindowStillParented(snapshot_.shellDefView, snapshot_.progman) &&
               WindowStillParented(snapshot_.workerW, snapshot_.progman);
    }
    if (snapshot_.mode == DesktopShellMode::LegacyWorkerW) {
        if (!snapshot_.workerW || !IsWindow(snapshot_.workerW)) return false;
        if (snapshot_.shellDefView && snapshot_.legacyDefViewParent) {
            return WindowStillParented(snapshot_.shellDefView, snapshot_.legacyDefViewParent);
        }
        return true;
    }
    if (snapshot_.mode == DesktopShellMode::ProgmanFallback) {
        if (snapshot_.shellDefView && GetParent(snapshot_.shellDefView) != snapshot_.progman &&
            snapshot_.legacyDefViewParent != GetParent(snapshot_.shellDefView)) {
            return false;
        }
        return true;
    }
    return false;
}

bool DesktopShellHost::EnsureSurface(HWND surface,
                                     DesktopSurfaceRole role,
                                     const RECT& desktopBounds,
                                     bool visible,
                                     std::wstring* error) {
    if (!surface || !IsWindow(surface)) {
        if (error) *error = L"DesktopShellHost: cannot ensure a stale surface HWND";
        return false;
    }
    if (desktopBounds.right <= desktopBounds.left || desktopBounds.bottom <= desktopBounds.top) {
        if (error) *error = L"DesktopShellHost: cannot ensure a surface with invalid desktop geometry";
        return false;
    }

    // AttachSurface is deliberately idempotent and is the sole operation that
    // chooses the shell parent, maps screen geometry to parent client space,
    // applies renderer-appropriate child styles and restores the desktop stack.
    if (!AttachSurface(surface, role, desktopBounds, visible, error)) return false;

    const auto health = InspectSurface(surface, role);
    const bool layeredRequired = SurfaceRequiresLayered(surface);
    if (!AttachmentHealthValid(health, layeredRequired)) {
        if (error) {
            *error = health.detail.empty()
                ? L"DesktopShellHost: ensured surface failed attachment health validation"
                : health.detail;
        }
        return false;
    }

    // InspectSurface validates that the HWND has drawable geometry, but M2 also
    // needs the attachment owner to prove that mixed/negative desktop-space
    // coordinates survived the parent-client mapping round-trip.
    RECT actualBounds{};
    if (!GetWindowRect(surface, &actualBounds) || !RectMatchesWithinTolerance(actualBounds, desktopBounds)) {
        if (error) {
            *error = L"DesktopShellHost: ensured surface geometry does not match requested desktop bounds";
        }
        return false;
    }

    return RepairSurfaceStack(surface, error);
}

bool DesktopShellHost::RepairSurfaceStack(HWND expectedSurface, std::wstring* error) {
    if (!EnsureCurrent(error)) return false;

    const HWND parent = SurfaceParent();
    if (!parent || !IsWindow(parent)) {
        if (error) *error = L"DesktopShellHost: no valid surface parent while repairing stack";
        return false;
    }

    if (expectedSurface) {
        if (!IsWindow(expectedSurface)) {
            if (error) *error = L"DesktopShellHost: expected surface HWND is stale";
            return false;
        }
        if (GetParent(expectedSurface) != parent) {
            if (error) *error = L"DesktopShellHost: expected surface belongs to a stale desktop parent";
            return false;
        }
    }

    RepairKnownMiaoDeskSurfaces();
    if (error) error->clear();
    return true;
}

bool DesktopShellHost::RecoverSurface(HWND surface, DesktopSurfaceRole role, std::wstring* error) {
    if (!surface || !IsWindow(surface)) {
        if (error) *error = L"DesktopShellHost: cannot recover a stale surface HWND";
        return false;
    }

    RECT desktopBounds{};
    if (!GetWindowRect(surface, &desktopBounds) || desktopBounds.right <= desktopBounds.left ||
        desktopBounds.bottom <= desktopBounds.top) {
        if (error) *error = L"DesktopShellHost: cannot recover surface without valid screen geometry";
        return false;
    }
    const bool wasVisible = IsWindowVisible(surface) != FALSE;
    const bool sameGeneration = CurrentGenerationValid();

    if (!EnsureCurrent(error)) return false;
    const auto health = InspectSurface(surface, role);
    const bool layeredRequired = SurfaceRequiresLayered(surface);
    if (sameGeneration && AttachmentHealthValid(health, layeredRequired)) {
        return RepairSurfaceStack(surface, error);
    }

    // A refreshed Explorer generation always goes through EnsureSurface even if
    // a recycled HWND happens to resemble the old parent. This keeps stale
    // Explorer ownership from surviving a restart by accident.
    std::wstring recoveryError;
    if (EnsureSurface(surface, role, desktopBounds, wasVisible, &recoveryError)) {
        if (error) error->clear();
        return true;
    }
    if (recoveryError.empty()) recoveryError = L"surface reattachment failed without a Win32 detail";
    if (error) {
        *error = sameGeneration
            ? recoveryError
            : L"DesktopShellHost: Explorer generation changed; reattach failed: " + recoveryError;
    }
    return false;
}

} // namespace miaodesk::wallpaper
