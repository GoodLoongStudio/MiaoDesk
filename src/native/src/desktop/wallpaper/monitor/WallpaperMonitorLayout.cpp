#include "miaodesk/WallpaperMonitorLayout.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace miaodesk::wallpaper {
namespace {

using GetDpiForMonitorFn = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);

struct EnumContext {
    MonitorTopology* topology{};
    const std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>>* identities{};
    bool first{true};
};

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

void QueryMonitorDpi(HMONITOR monitor, UINT& dpiX, UINT& dpiY) {
    dpiX = 96;
    dpiY = 96;
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (!shcore) return;
    const auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
    if (getDpiForMonitor) {
        UINT x = 96;
        UINT y = 96;
        if (SUCCEEDED(getDpiForMonitor(monitor, 0, &x, &y))) {
            dpiX = x;
            dpiY = y;
        }
    }
    FreeLibrary(shcore);
}

using MonitorIdentityMap = std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>>;

MonitorIdentityMap QueryMonitorIdentities() {
    MonitorIdentityMap result;
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    LONG status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (status != ERROR_SUCCESS || pathCount == 0) return result;

    for (int attempt = 0; attempt < 3; ++attempt) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        UINT32 actualPaths = pathCount;
        UINT32 actualModes = modeCount;
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &actualPaths, paths.data(), &actualModes, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) {
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return result;
            continue;
        }
        if (status != ERROR_SUCCESS) return result;
        paths.resize(actualPaths);

        for (const auto& path : paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = path.sourceInfo.adapterId;
            source.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || source.viewGdiDeviceName[0] == L'\0') continue;

            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target.header.size = sizeof(target);
            target.header.adapterId = path.targetInfo.adapterId;
            target.header.id = path.targetInfo.id;
            const LONG targetStatus = DisplayConfigGetDeviceInfo(&target.header);

            std::wstring stableId = source.viewGdiDeviceName;
            std::wstring friendlyName;
            if (targetStatus == ERROR_SUCCESS) {
                if (target.monitorDevicePath[0] != L'\0') stableId = target.monitorDevicePath;
                if (target.monitorFriendlyDeviceName[0] != L'\0') friendlyName = target.monitorFriendlyDeviceName;
            }
            result[Lower(source.viewGdiDeviceName)] = {std::move(stableId), std::move(friendlyName)};
        }
        break;
    }
    return result;
}

RECT NormalizeBounds(const RECT& rect) noexcept {
    RECT normalized = rect;
    if (normalized.right < normalized.left) std::swap(normalized.right, normalized.left);
    if (normalized.bottom < normalized.top) std::swap(normalized.bottom, normalized.top);
    return normalized;
}

RECT OffsetIntoHost(const RECT& desktopRect, const RECT& hostDesktopBounds) noexcept {
    RECT result = desktopRect;
    OffsetRect(&result, -hostDesktopBounds.left, -hostDesktopBounds.top);
    return result;
}

void AddSystemMetricFallback(MonitorTopology& topology) {
    if (!topology.monitors.empty()) return;
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }
    if (width <= 0 || height <= 0) return;

    MonitorInfo fallback;
    fallback.desktopRect = RECT{x, y, x + width, y + height};
    fallback.primary = true;
    fallback.deviceName = L"SYSTEM_METRICS_FALLBACK";
    fallback.stableId = L"fallback:virtual-desktop";
    fallback.friendlyName = L"Virtual Desktop";
    topology.monitors.push_back(fallback);
    topology.virtualBounds = fallback.desktopRect;
    topology.primaryBounds = fallback.desktopRect;
}

} // namespace

bool MonitorTopology::Valid() const noexcept {
    return !monitors.empty() && virtualBounds.right > virtualBounds.left && virtualBounds.bottom > virtualBounds.top &&
           primaryBounds.right > primaryBounds.left && primaryBounds.bottom > primaryBounds.top;
}

MonitorTopology QueryMonitorTopology() {
    MonitorTopology topology;
    const MonitorIdentityMap identities = QueryMonitorIdentities();
    EnumContext context{&topology, &identities, true};

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM raw) -> BOOL {
            auto* target = reinterpret_cast<EnumContext*>(raw);
            if (!target || !target->topology) return FALSE;
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) return TRUE;

            MonitorInfo item;
            item.handle = monitor;
            item.desktopRect = info.rcMonitor;
            item.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            item.deviceName = info.szDevice;
            item.stableId = item.deviceName;
            if (target->identities) {
                const auto found = target->identities->find(Lower(item.deviceName));
                if (found != target->identities->end()) {
                    if (!found->second.first.empty()) item.stableId = found->second.first;
                    item.friendlyName = found->second.second;
                }
            }
            QueryMonitorDpi(monitor, item.dpiX, item.dpiY);

            if (target->first) {
                target->topology->virtualBounds = item.desktopRect;
                target->first = false;
            } else {
                target->topology->virtualBounds.left = std::min(target->topology->virtualBounds.left, item.desktopRect.left);
                target->topology->virtualBounds.top = std::min(target->topology->virtualBounds.top, item.desktopRect.top);
                target->topology->virtualBounds.right = std::max(target->topology->virtualBounds.right, item.desktopRect.right);
                target->topology->virtualBounds.bottom = std::max(target->topology->virtualBounds.bottom, item.desktopRect.bottom);
            }
            if (item.primary) target->topology->primaryBounds = item.desktopRect;
            target->topology->monitors.push_back(std::move(item));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));

    AddSystemMetricFallback(topology);
    if (!topology.monitors.empty() &&
        (topology.primaryBounds.right <= topology.primaryBounds.left || topology.primaryBounds.bottom <= topology.primaryBounds.top)) {
        topology.monitors.front().primary = true;
        topology.primaryBounds = topology.monitors.front().desktopRect;
    }
    std::sort(topology.monitors.begin(), topology.monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.primary != b.primary) return a.primary > b.primary;
        if (a.desktopRect.top != b.desktopRect.top) return a.desktopRect.top < b.desktopRect.top;
        return a.desktopRect.left < b.desktopRect.left;
    });
    return topology;
}

const MonitorInfo* FindMonitorByStableId(const MonitorTopology& topology, std::wstring_view stableId) noexcept {
    if (stableId.empty()) return nullptr;
    const std::wstring requested(stableId);
    for (const auto& monitor : topology.monitors) {
        const std::wstring key = StableMonitorKey(monitor);
        if (_wcsicmp(key.c_str(), requested.c_str()) == 0) return &monitor;
    }
    return nullptr;
}

std::wstring StableMonitorKey(const MonitorInfo& monitor) {
    if (!monitor.stableId.empty()) return monitor.stableId;
    if (!monitor.deviceName.empty()) return monitor.deviceName;
    std::wostringstream fallback;
    fallback << L"rect:" << monitor.desktopRect.left << L"," << monitor.desktopRect.top << L"," << monitor.desktopRect.right << L"," << monitor.desktopRect.bottom;
    return fallback.str();
}

LayoutMode ParseLayoutMode(const std::wstring& value) noexcept {
    if (_wcsicmp(value.c_str(), L"independent") == 0 || _wcsicmp(value.c_str(), L"per-monitor") == 0) return LayoutMode::Independent;
    if (_wcsicmp(value.c_str(), L"primary") == 0 || _wcsicmp(value.c_str(), L"primary-only") == 0) return LayoutMode::PrimaryOnly;

    // Product contract: one wallpaper selection may render on only one display.
    // Legacy span/clone values are migrated to the primary display instead of
    // allowing an old wallpaper.ini to silently light up multiple monitors.
    // Explicit per-monitor targeting still uses Independent mode and can target
    // a non-primary monitor by stable display id.
    return LayoutMode::PrimaryOnly;
}

const wchar_t* LayoutModeKey(LayoutMode mode) noexcept {
    switch (mode) {
    case LayoutMode::Clone: return L"clone";
    case LayoutMode::Independent: return L"independent";
    case LayoutMode::PrimaryOnly: return L"primary";
    case LayoutMode::Span: break;
    }
    return L"span";
}

const wchar_t* LayoutModeDisplayName(LayoutMode mode) noexcept {
    switch (mode) {
    case LayoutMode::Clone: return L"每屏复制";
    case LayoutMode::Independent: return L"每屏独立壁纸";
    case LayoutMode::PrimaryOnly: return L"仅主显示器";
    case LayoutMode::Span: break;
    }
    return L"跨屏延展";
}

RECT HostDesktopBounds(const MonitorTopology& topology, LayoutMode mode) noexcept {
    if (!topology.Valid()) return {};
    return mode == LayoutMode::PrimaryOnly ? topology.primaryBounds : topology.virtualBounds;
}

std::vector<RECT> DrawRegionsInHost(const MonitorTopology& topology, LayoutMode mode) {
    std::vector<RECT> regions;
    if (!topology.Valid()) return regions;
    const RECT hostBounds = HostDesktopBounds(topology, mode);
    if (mode == LayoutMode::Clone || mode == LayoutMode::Independent) {
        regions.reserve(topology.monitors.size());
        for (const auto& monitor : topology.monitors) regions.push_back(OffsetIntoHost(monitor.desktopRect, hostBounds));
        return regions;
    }
    RECT full{};
    full.right = hostBounds.right - hostBounds.left;
    full.bottom = hostBounds.bottom - hostBounds.top;
    regions.push_back(full);
    return regions;
}
