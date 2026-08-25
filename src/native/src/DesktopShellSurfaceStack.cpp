#include "turingdesk/DesktopShellHost.h"

namespace turingdesk::wallpaper {

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

} // namespace turingdesk::wallpaper
