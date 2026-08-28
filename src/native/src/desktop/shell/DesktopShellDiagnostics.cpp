#include "turingdesk/DesktopShellDiagnostics.h"

#include <iterator>
#include <sstream>

namespace turingdesk::wallpaper {
namespace {

bool AppearsBelow(HWND surface, HWND anchor) noexcept {
    if (!surface || !anchor || !IsWindow(surface) || !IsWindow(anchor)) return false;
    if (GetParent(surface) != GetParent(anchor)) return false;
    for (HWND cursor = GetWindow(anchor, GW_HWNDNEXT); cursor; cursor = GetWindow(cursor, GW_HWNDNEXT)) {
        if (cursor == surface) return true;
    }
    return false;
}

bool IsKnownWallpaperSurface(HWND window) noexcept {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[160]{};
    if (GetClassNameW(window, className, static_cast<int>(std::size(className))) <= 0) return false;
    return _wcsicmp(className, L"TuringDesk.Native.WallpaperHost") == 0 ||
           _wcsicmp(className, L"TuringDesk.Native.WebWallpaperHost") == 0;
}

bool LegacyRoleOrderValid(HWND surface, DesktopSurfaceRole role) noexcept {
    const HWND parent = surface ? GetParent(surface) : nullptr;
    if (!parent || !IsWindow(parent)) return false;

    if (role == DesktopSurfaceRole::Wallpaper) {
        for (HWND cursor = GetWindow(surface, GW_HWNDNEXT); cursor; cursor = GetWindow(cursor, GW_HWNDNEXT)) {
            if (!IsKnownWallpaperSurface(cursor)) return false;
        }
        return true;
    }

    for (HWND cursor = GetWindow(parent, GW_CHILD); cursor && cursor != surface; cursor = GetWindow(cursor, GW_HWNDNEXT)) {
        if (IsKnownWallpaperSurface(cursor)) return false;
    }
    return true;
}

bool ZOrderValid(const DesktopShellSnapshot& snapshot, HWND surface, DesktopSurfaceRole role) noexcept {
    if (!surface || !IsWindow(surface)) return false;
    const HWND parent = GetParent(surface);
    if (!parent || !IsWindow(parent)) return false;

    if (snapshot.mode == DesktopShellMode::RaisedDesktop || snapshot.mode == DesktopShellMode::ProgmanFallback) {
        if (snapshot.shellDefView && GetParent(snapshot.shellDefView) == parent) {
            if (role == DesktopSurfaceRole::Widget) return !AppearsBelow(surface, snapshot.shellDefView);
            return AppearsBelow(surface, snapshot.shellDefView);
        }
        return parent == snapshot.progman;
    }
    if (snapshot.mode == DesktopShellMode::LegacyWorkerW)
        return parent == snapshot.workerW && LegacyRoleOrderValid(surface, role);
    return false;
}

} // namespace

DesktopAttachmentDiagnostics InspectDesktopAttachment(
    const DesktopShellHost& shell,
    HWND surface,
    DesktopSurfaceRole role,
    bool layeredRequired) noexcept {
    DesktopAttachmentDiagnostics diagnostics;
    diagnostics.role = role;
    diagnostics.layeredRequired = layeredRequired;

    const auto& snapshot = shell.Snapshot();
    diagnostics.shellMode = DesktopShellHost::ModeKey(snapshot.mode);
    diagnostics.shellGeneration = snapshot.generation;

    const auto health = shell.InspectSurface(surface, role);
    diagnostics.parentValid = health.parent;
    diagnostics.layeredApplied = health.layered;
    diagnostics.visible = health.visible;
    diagnostics.geometryValid = health.geometry;
    diagnostics.actualParent = health.actualParent;
    diagnostics.zOrderValid = ZOrderValid(snapshot, surface, role);
    diagnostics.detail = health.detail;

    if (!surface || !IsWindow(surface)) diagnostics.lastError = ERROR_INVALID_WINDOW_HANDLE;
    else if (!snapshot.Valid()) diagnostics.lastError = ERROR_INVALID_PARAMETER;
    else if (!diagnostics.parentValid) diagnostics.lastError = ERROR_INVALID_PARAMETER;
    else if (diagnostics.layeredRequired && !diagnostics.layeredApplied) diagnostics.lastError = ERROR_INVALID_DATA;
    else if (!diagnostics.geometryValid) diagnostics.lastError = ERROR_INVALID_DATA;
    else if (!diagnostics.zOrderValid) diagnostics.lastError = ERROR_INVALID_PARAMETER;
    else diagnostics.lastError = ERROR_SUCCESS;

    if (diagnostics.detail.empty() && !diagnostics.zOrderValid)
        diagnostics.detail = L"desktop surface z-order is outside the DesktopShellHost contract";
    if (diagnostics.detail.empty() && diagnostics.layeredRequired && !diagnostics.layeredApplied)
        diagnostics.detail = L"desktop surface is missing WS_EX_LAYERED";
    if (diagnostics.detail.empty() && !diagnostics.parentValid)
        diagnostics.detail = L"desktop surface parent is stale or owned by another shell path";
    return diagnostics;
}

std::wstring DescribeDesktopAttachment(const DesktopAttachmentDiagnostics& diagnostics) {
    std::wostringstream text;
    text << L"shellMode=" << diagnostics.shellMode
         << L" role=" << DesktopShellHost::RoleKey(diagnostics.role)
         << L" parentValid=" << (diagnostics.parentValid ? 1 : 0)
         << L" layeredRequired=" << (diagnostics.layeredRequired ? 1 : 0)
         << L" layeredApplied=" << (diagnostics.layeredApplied ? 1 : 0)
         << L" zOrderValid=" << (diagnostics.zOrderValid ? 1 : 0)
         << L" visible=" << (diagnostics.visible ? 1 : 0)
         << L" geometryValid=" << (diagnostics.geometryValid ? 1 : 0)
         << L" lastError=" << diagnostics.lastError
         << L" generation=" << diagnostics.shellGeneration;
    if (!diagnostics.detail.empty()) text << L" detail=" << diagnostics.detail;
    return text.str();
}

} // namespace turingdesk::wallpaper
