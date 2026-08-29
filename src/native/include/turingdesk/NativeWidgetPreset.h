#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace turingdesk::wallpaper {

enum class NativeWidgetPreset {
    GlassClock,
    TodayTasks,
    WeatherGlass,
};

struct NativeWidgetDefinition {
    NativeWidgetPreset preset{};
    std::wstring_view source;
    std::wstring_view title;
    float defaultWidth{};
    float defaultHeight{};
    std::uint32_t periodicRefreshMs{};
};

const NativeWidgetDefinition* NativePresetDefinition(NativeWidgetPreset preset) noexcept;
const NativeWidgetDefinition* FindNativePresetDefinition(std::wstring_view source) noexcept;

bool IsNativePresetSource(std::wstring_view source) noexcept;
bool ParseNativePreset(std::wstring_view source, NativeWidgetPreset* preset) noexcept;
std::wstring NativePresetSource(NativeWidgetPreset preset);
const wchar_t* NativePresetTitle(NativeWidgetPreset preset) noexcept;
std::uint32_t NativePresetRefreshIntervalMs(NativeWidgetPreset preset) noexcept;

} // namespace turingdesk::wallpaper
