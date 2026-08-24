#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/DesktopWidgetStore.h"

namespace turingdesk::desktop {

struct DesktopControlResult {
    bool success{};
    std::wstring message;
};

struct DesktopState {
    bool enabled{true};
    std::wstring scene{L"aurora"};
    std::wstring layout{L"span"};
    std::wstring scale{L"cover"};
    int fpsCap{30};
    std::wstring imageOrWebSource;
    std::wstring videoSource;
    std::size_t widgetCount{};
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

// Domain boundary shared by UI, Pi native tools and future editor clients.
// Callers describe intent; this service owns persistence/runtime activation.
class DesktopControlService {
public:
    DesktopControlService() = default;

    DesktopControlResult GetState(DesktopState* state) const;
    DesktopControlResult ApplyWebPackage(const std::filesystem::path& package) const;

    DesktopControlResult CreateWebWidget(
        const WebWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;
    DesktopControlResult UpdateWidget(const WidgetUpdateRequest& request) const;
    DesktopControlResult RemoveWidget(std::wstring_view id) const;
    DesktopControlResult ListWidgets(std::vector<wallpaper::DesktopWidget>* widgets) const;

    // Runtime activation belongs here rather than in UI/tool adapters.
    DesktopControlResult EnsureRuntime() const;
};

} // namespace turingdesk::desktop
