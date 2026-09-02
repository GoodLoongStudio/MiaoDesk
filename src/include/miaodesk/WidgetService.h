#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/NativeWidgetPreset.h"

namespace miaodesk::desktop {

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

struct NativeWidgetCreateRequest {
    wallpaper::NativeWidgetPreset preset{wallpaper::NativeWidgetPreset::GlassClock};
    std::wstring title;
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

// One configured Widget matched to its live desktop surface. Runtime inspection
// remains owned by the Widget domain. UI/Pi receive both machine-readable
// issueCode and human-readable recommendedAction so callers never need to infer
// remediation from HWND/WebView2/Native rendering implementation details.
struct WidgetSurfaceHealth {
    std::wstring widgetId;
    std::wstring monitorId;
    std::uint32_t processId{};
    std::uintptr_t hwndValue{};
    bool configured{};
    bool processRunning{};
    bool hwndReady{};
    bool parentValid{};
    bool childStyleValid{};
    bool visible{};
    bool monitorReported{};
    bool monitorValid{};
    bool geometryReported{};
    bool geometryValid{};
    int expectedLeft{};
    int expectedTop{};
    int expectedRight{};
    int expectedBottom{};
    int actualLeft{};
    int actualTop{};
    int actualRight{};
    int actualBottom{};
    bool environmentReady{};
    bool environmentReported{};
    bool controllerReady{};
    bool controllerReported{};
    bool navigationReady{};
    bool navigationReported{};
    bool zOrderValid{};
    bool zOrderReported{};
    bool renderingHealthy{};
    std::wstring issueCode;
    std::wstring recommendedAction;
    std::wstring detail;

    bool SurfaceReady() const noexcept {
        return configured && processRunning && hwndReady && parentValid && childStyleValid && visible &&
               monitorReported && monitorValid && geometryReported && geometryValid;
    }
};

// Caller-facing Widget runtime summary. UI/Pi clients consume this through
// DesktopSnapshot rather than reading wallpaper.ini or enumerating HWNDs.
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
    WidgetServiceResult CreateNative(
        const NativeWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;
    WidgetServiceResult Update(const WidgetUpdateRequest& request) const;
    WidgetServiceResult Remove(std::wstring_view id) const;
    WidgetServiceResult List(std::vector<wallpaper::DesktopWidget>* widgets) const;
    WidgetServiceResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    WidgetServiceResult GetRuntimeHealth(WidgetRuntimeHealth* health) const;
};

} // namespace miaodesk::desktop
