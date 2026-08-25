#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/DesktopWidgetStore.h"

namespace turingdesk::desktop {

struct WidgetServiceResult {
    bool success{};
    std::wstring message;
};

struct WebWidgetCreateRequest {
    std::wstring title{L"Desktop Widget"};
    std::string htmlUtf8;
    std::wstring monitorId;
    float x{0.68f};
    float y{0.05f};
    float width{0.28f};
    float height{0.18f};
};

struct WidgetUpdateRequest {
    std::wstring id;
    std::optional<std::wstring> title;
    std::optional<std::string> htmlUtf8;
    std::optional<std::wstring> monitorId;
    std::optional<float> x;
    std::optional<float> y;
    std::optional<float> width;
    std::optional<float> height;
    std::optional<int> zIndex;
    std::optional<bool> enabled;
};

// One configured Web Widget matched to its isolated child surface. M3 grows
// this contract in-place: process/HWND/shell visibility are reported now;
// WebView2 lifecycle and authoritative z-order become reported when the child
// runtime publishes those stages instead of callers inferring them.
struct WidgetSurfaceHealth {
    std::wstring widgetId;
    std::uint32_t processId{};
    std::uintptr_t hwndValue{};
    bool configured{};
    bool processRunning{};
    bool hwndReady{};
    bool parentValid{};
    bool childStyleValid{};
    bool visible{};
    bool environmentReady{};
    bool environmentReported{};
    bool controllerReady{};
    bool controllerReported{};
    bool navigationReady{};
    bool navigationReported{};
    bool zOrderValid{};
    bool zOrderReported{};
    bool renderingHealthy{};
    std::wstring detail;

    bool SurfaceReady() const noexcept {
        return configured && processRunning && hwndReady && parentValid && childStyleValid && visible;
    }
};

// Caller-facing Widget runtime summary. UI/Pi/editor clients consume this
// through DesktopSnapshot rather than reading wallpaper.ini or enumerating HWNDs.
struct WidgetRuntimeHealth {
    std::size_t configuredCount{};
    std::size_t enabledWebCount{};
    bool runtimeReported{};
    bool runtimeHealthy{};
    std::vector<WidgetSurfaceHealth> surfaces;
    std::wstring detail;

    bool Healthy() const noexcept {
        return enabledWebCount == 0 || (runtimeReported && runtimeHealthy);
    }
};

// Widget domain service. DesktopWidgetStore is an implementation detail behind
// this boundary rather than a public UI/AI product API.
class WidgetService {
public:
    WidgetServiceResult CreateWeb(
        const WebWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;
    WidgetServiceResult Update(const WidgetUpdateRequest& request) const;
    WidgetServiceResult Remove(std::wstring_view id) const;
    WidgetServiceResult List(std::vector<wallpaper::DesktopWidget>* widgets) const;
    WidgetServiceResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    WidgetServiceResult GetRuntimeHealth(WidgetRuntimeHealth* health) const;
};

} // namespace turingdesk::desktop
