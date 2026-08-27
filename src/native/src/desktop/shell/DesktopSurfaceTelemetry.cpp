#include "turingdesk/DesktopSurfaceTelemetry.h"

#include <cwchar>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kProgmanClass[] = L"Progman";
constexpr wchar_t kWorkerWClass[] = L"WorkerW";
constexpr wchar_t kDefViewClass[] = L"SHELLDLL_DefView";
constexpr wchar_t kWallpaperHostClass[] = L"TuringDesk.Native.WallpaperHost";
constexpr wchar_t kWebHostClass[] = L"TuringDesk.Native.WebWallpaperHost";
constexpr LONG_PTR kRaisedDesktopFlag = WS_EX_NOREDIRECTIONBITMAP;

bool IsClass(HWND window, const wchar_t* expected) noexcept {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[160]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, expected) == 0;
}

bool StartsWith(const wchar_t* value, const wchar_t* prefix) noexcept {
    if (!value || !prefix) return false;
    return std::wcsncmp(value, prefix, std::wcslen(prefix)) == 0;
}

bool IsKnownTuringDeskSurface(HWND window) noexcept {
    return IsClass(window, kWallpaperHostClass) || IsClass(window, kWebHostClass);
}

DesktopSurfaceTelemetryRole InferRole(HWND window) noexcept {
    if (!IsClass(window, kWebHostClass)) return DesktopSurfaceTelemetryRole::Wallpaper;
    wchar_t title[320]{};
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    return StartsWith(title, L"widget-") || StartsWith(title, L"widget_")
        ? DesktopSurfaceTelemetryRole::Widget
        : DesktopSurfaceTelemetryRole::Wallpaper;
}

struct ChildEntry {
    HWND window{};
    bool defView{};
    bool turingDesk{};
    DesktopSurfaceTelemetryRole role{DesktopSurfaceTelemetryRole::Wallpaper};
};

} // namespace

DesktopSurfaceParentTelemetry InspectDesktopSurfaceParent() noexcept {
    DesktopSurfaceParentTelemetry result;
    const HWND progman = FindWindowW(kProgmanClass, nullptr);
    if (!progman || !IsWindow(progman)) {
        result.detail = L"Progman not found";
        return result;
    }

    const bool raisedDesktop =
        (GetWindowLongPtrW(progman, GWL_EXSTYLE) & kRaisedDesktopFlag) != 0;
    if (raisedDesktop) {
        const HWND defView = FindWindowExW(progman, nullptr, kDefViewClass, nullptr);
        if (defView && IsWindow(defView)) {
            result.reported = true;
            result.parent = progman;
            result.mode = L"raised-desktop";
            result.detail = L"current Explorer raised-desktop parent is Progman";
            return result;
        }
    }

    struct LegacySearch {
        HWND defView{};
        HWND defViewParent{};
        HWND worker{};
    } legacy;
    EnumWindows([](HWND top, LPARAM raw) -> BOOL {
        auto* found = reinterpret_cast<LegacySearch*>(raw);
        const HWND defView = FindWindowExW(top, nullptr, kDefViewClass, nullptr);
        if (!defView) return TRUE;
        found->defView = defView;
        found->defViewParent = top;
        found->worker = FindWindowExW(nullptr, top, kWorkerWClass, nullptr);
        return found->worker ? FALSE : TRUE;
    }, reinterpret_cast<LPARAM>(&legacy));

    if (legacy.worker && IsWindow(legacy.worker)) {
        result.reported = true;
        result.parent = legacy.worker;
        result.mode = L"legacy-workerw";
        result.detail = L"current Explorer legacy desktop parent is WorkerW";
        return result;
    }

    // Read-only telemetry never sends the private WorkerW spawn message. If no
    // WorkerW exists, report the same conservative Progman fallback used by the
    // shell host after discovery.
    result.reported = true;
    result.parent = progman;
    result.mode = L"progman-fallback";
    result.detail = legacy.defView
        ? L"WorkerW absent; using Progman fallback while DefView is present"
        : L"WorkerW/DefView not observed; using Progman fallback";
    return result;
}

DesktopSurfaceZOrderHealth InspectDesktopSurfaceZOrder(
    HWND surface,
    DesktopSurfaceTelemetryRole role) noexcept {
    DesktopSurfaceZOrderHealth result;
    if (!surface || !IsWindow(surface)) {
        result.detail = L"surface HWND missing";
        return result;
    }
    const HWND parent = GetParent(surface);
    if (!parent || !IsWindow(parent)) {
        result.detail = L"surface parent missing";
        return result;
    }

    std::vector<ChildEntry> children;
    std::optional<std::size_t> surfaceIndex;
    std::optional<std::size_t> defViewIndex;
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        ChildEntry entry;
        entry.window = child;
        entry.defView = IsClass(child, kDefViewClass);
        entry.turingDesk = IsKnownTuringDeskSurface(child);
        if (entry.turingDesk) entry.role = InferRole(child);
        const std::size_t index = children.size();
        if (child == surface) surfaceIndex = index;
        if (entry.defView && !defViewIndex) defViewIndex = index;
        children.push_back(entry);
    }

    if (!surfaceIndex) {
        result.detail = L"surface not found in parent z-order";
        return result;
    }
    result.reported = true;

    // GetWindow(GW_CHILD/GW_HWNDNEXT) enumerates sibling z-order from top to
    // bottom. If DefView shares this parent it must remain above every TuringDesk
    // surface so desktop icons stay interactive/visible.
    if (defViewIndex && *defViewIndex >= *surfaceIndex) {
        result.detail = L"desktop icon layer is not above surface";
        return result;
    }

    for (std::size_t index = 0; index < children.size(); ++index) {
        const auto& sibling = children[index];
        if (!sibling.turingDesk || sibling.window == surface) continue;
        if (role == DesktopSurfaceTelemetryRole::Widget &&
            sibling.role == DesktopSurfaceTelemetryRole::Wallpaper && index < *surfaceIndex) {
            result.detail = L"wallpaper surface is above Widget";
            return result;
        }
        if (role == DesktopSurfaceTelemetryRole::Wallpaper &&
            sibling.role == DesktopSurfaceTelemetryRole::Widget && index > *surfaceIndex) {
            result.detail = L"Widget surface is below wallpaper";
            return result;
        }
    }

    result.valid = true;
    result.detail = role == DesktopSurfaceTelemetryRole::Widget
        ? L"Widget is above TuringDesk wallpaper and below icon layer"
        : L"wallpaper is below Widget/icon layers";
    return result;
}

} // namespace turingdesk::wallpaper
