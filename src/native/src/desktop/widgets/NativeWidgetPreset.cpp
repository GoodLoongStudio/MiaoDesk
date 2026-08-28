#include "turingdesk/NativeWidgetPreset.h"

#include <cwchar>

namespace turingdesk::wallpaper {

bool IsNativePresetSource(std::wstring_view source) noexcept {
    return source.size() > 7 && _wcsnicmp(source.data(), L"native:", 7) == 0;
}

bool ParseNativePreset(std::wstring_view source, NativeWidgetPreset* preset) noexcept {
    if (!preset || !IsNativePresetSource(source)) return false;
    const std::wstring_view key = source.substr(7);
    if (key == L"glass-clock") {
        *preset = NativeWidgetPreset::GlassClock;
        return true;
    }
    if (key == L"today-tasks") {
        *preset = NativeWidgetPreset::TodayTasks;
        return true;
    }
    if (key == L"weather-glass") {
        *preset = NativeWidgetPreset::WeatherGlass;
        return true;
    }
    return false;
}

std::wstring NativePresetSource(NativeWidgetPreset preset) {
    switch (preset) {
    case NativeWidgetPreset::TodayTasks:
        return L"native:today-tasks";
    case NativeWidgetPreset::WeatherGlass:
        return L"native:weather-glass";
    case NativeWidgetPreset::GlassClock:
    default:
        return L"native:glass-clock";
    }
}

const wchar_t* NativePresetTitle(NativeWidgetPreset preset) noexcept {
    switch (preset) {
    case NativeWidgetPreset::TodayTasks:
        return L"今日待办";
    case NativeWidgetPreset::WeatherGlass:
        return L"玻璃天气";
    case NativeWidgetPreset::GlassClock:
    default:
        return L"玻璃时钟";
    }
}

} // namespace turingdesk::wallpaper
