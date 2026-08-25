#pragma once

#include <windows.h>

#include <string>

#include "turingdesk/DesktopShellHost.h"

namespace turingdesk::wallpaper {

struct DesktopAttachmentDiagnostics {
    std::wstring shellMode{L"none"};
    DesktopSurfaceRole role{DesktopSurfaceRole::Wallpaper};
    bool parentValid{};
    bool layeredRequired{true};
    bool layeredApplied{};
    bool zOrderValid{};
    bool visible{};
    bool geometryValid{};
    HWND actualParent{};
    DWORD lastError{};
    unsigned long long shellGeneration{};
    std::wstring detail;

    bool Healthy() const noexcept {
        return parentValid && (!layeredRequired || layeredApplied) && zOrderValid &&
               visible && geometryValid && lastError == ERROR_SUCCESS;
    }
};

// Read-only diagnostics for a surface already managed by DesktopShellHost.
// This helper must never discover Progman/WorkerW or mutate shell ownership.
DesktopAttachmentDiagnostics InspectDesktopAttachment(
    const DesktopShellHost& shell,
    HWND surface,
    DesktopSurfaceRole role,
    bool layeredRequired = true) noexcept;

std::wstring DescribeDesktopAttachment(const DesktopAttachmentDiagnostics& diagnostics);

} // namespace turingdesk::wallpaper
