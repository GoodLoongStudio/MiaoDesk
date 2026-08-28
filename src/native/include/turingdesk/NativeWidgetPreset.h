#pragma once

#include <string>
#include <string_view>

namespace turingdesk::wallpaper {

enum class NativeWidgetPreset {
    GlassClock,
    TodayTasks,
    WeatherGlass,
};

bool IsNativePresetSource(std::wstring_view source) noexcept;
bool ParseNativePreset(std::wstring_view source, NativeWidgetPreset* preset) noexcept;
std::wstring NativePresetSource(NativeWidgetPreset preset);
const wchar_t* NativePresetTitle(NativeWidgetPreset preset) noexcept;

} // namespace turingdesk::wallpaper
