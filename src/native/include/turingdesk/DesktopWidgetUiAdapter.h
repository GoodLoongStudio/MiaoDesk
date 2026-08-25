#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/DesktopControlService.h"

namespace turingdesk::wallpaper {

// Transitional adapter for the legacy production WallpaperLibraryWindow.
// It preserves the old UI call shape while routing every operation through
// DesktopControlService. Remove this adapter when the V2 UI reaches parity.
class DesktopWidgetUiAdapter {
public:
    bool Load(std::wstring* error = nullptr);
    const std::vector<DesktopWidget>& Items() const noexcept;
    std::optional<DesktopWidget> Find(std::wstring_view id) const;
    bool RuntimeHealth(desktop::WidgetRuntimeHealth* health, std::wstring* error = nullptr) const;

    std::optional<DesktopWidget> CreateManagedWeb(
        std::wstring title,
        std::string_view htmlUtf8,
        std::wstring monitorId = {},
        float x = 0.68f,
        float y = 0.05f,
        float width = 0.28f,
        float height = 0.18f,
        std::wstring* error = nullptr);

    std::optional<DesktopWidget> Upsert(DesktopWidget widget, std::wstring* error = nullptr);
    bool Remove(std::wstring_view id, bool deleteManagedSource = true, std::wstring* error = nullptr);

private:
    bool Refresh(std::wstring* error = nullptr);
    static bool AssignError(const desktop::DesktopControlResult& result, std::wstring* error);

    desktop::DesktopControlService service_;
    std::vector<DesktopWidget> items_;
};

} // namespace turingdesk::wallpaper