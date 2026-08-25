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

    if (!AttachSurface(surface, role, desktopBounds, wasVisible, error)) return false;
    return RepairSurfaceStack(surface, error);
}

} // namespace turingdesk::wallpaper
