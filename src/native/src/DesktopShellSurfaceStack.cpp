#include "turingdesk/DesktopShellHost.h"

namespace turingdesk::wallpaper {

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
        return snapshot_.shellDefView && IsWindow(snapshot_.shellDefView) &&
               snapshot_.workerW && IsWindow(snapshot_.workerW) &&
               GetParent(snapshot_.shellDefView) == snapshot_.progman &&
               GetParent(snapshot_.workerW) == snapshot_.progman;
    }
    if (snapshot_.mode == DesktopShellMode::LegacyWorkerW)
        return snapshot_.workerW && IsWindow(snapshot_.workerW);
    return snapshot_.mode == DesktopShellMode::ProgmanFallback;
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
    // applies child/layered styles and restores the desktop surface stack.
    if (!AttachSurface(surface, role, desktopBounds, visible, error)) return false;

    const auto health = InspectSurface(surface, role);
    if (!health.parent || !health.childStyle || !health.layered || !health.geometry) {
        if (error) {
            *error = health.detail.empty()
                ? L"DesktopShellHost: ensured surface failed attachment health validation"
                : health.detail;
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

    RepairKnownTuringDeskSurfaces();
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

    if (!EnsureCurrent(error)) return false;
    const auto health = InspectSurface(surface, role);
    if (health.parent && health.childStyle && health.layered && health.geometry) {
        return RepairSurfaceStack(surface, error);
    }

    return EnsureSurface(surface, role, desktopBounds, wasVisible, error);
}

} // namespace turingdesk::wallpaper
