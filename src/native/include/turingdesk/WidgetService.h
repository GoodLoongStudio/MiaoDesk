#pragma once

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
};

} // namespace turingdesk::desktop
