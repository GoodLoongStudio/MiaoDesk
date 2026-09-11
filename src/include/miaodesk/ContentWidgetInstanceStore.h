#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "miaodesk/MiaoContentModel.h"

namespace miaodesk::content {

// Persists only per-instance parameter overrides. Geometry/lifecycle remains in
// DesktopWidgetStore, while package defaults remain in the .mdwidget itself.
// This keeps instance customization stable across package upgrades without
// duplicating the entire package parameter schema into widgets.ini.
class ContentWidgetInstanceStore {
public:
    static bool LoadOverrides(
        std::wstring_view widgetId,
        const ContentDefinition& definition,
        ContentParameterValues* overrides,
        std::wstring* error = nullptr);

    static bool SaveOverrides(
        std::wstring_view widgetId,
        const ContentDefinition& definition,
        const ContentParameterValues& overrides,
        std::wstring* error = nullptr);

    static bool SetParameter(
        std::wstring_view widgetId,
        const ContentDefinition& definition,
        std::wstring_view key,
        ContentParameterValue value,
        std::wstring* error = nullptr);

    static bool Remove(std::wstring_view widgetId, std::wstring* error = nullptr);

    static std::filesystem::path InstanceDirectory();
    static std::filesystem::path InstancePath(std::wstring_view widgetId);
    static bool SelfTest();
};

} // namespace miaodesk::content
