#include "turingdesk/NativeWidgetPreset.h"

#include <array>
#include <cwchar>

namespace turingdesk::wallpaper {
namespace {

constexpr std::array<NativeWidgetDefinition, 3> kDefinitions{{
    {NativeWidgetPreset::GlassClock, L"native:glass-clock", L"玻璃时钟", 0.30f, 0.20f, 1000},
    {NativeWidgetPreset::TodayTasks, L"native:today-tasks", L"今日待办", 0.26f, 0.24f, 0},
    {NativeWidgetPreset::WeatherGlass, L"native:weather-glass", L"玻璃天气", 0.24f, 0.20f, 0},
}};

} // namespace

const NativeWidgetDefinition* NativePresetDefinition(NativeWidgetPreset preset) noexcept {
    for (const auto& definition : kDefinitions) {
        if (definition.preset == preset) return &definition;
    }
    return nullptr;
}

const NativeWidgetDefinition* FindNativePresetDefinition(std::wstring_view source) noexcept {
    for (const auto& definition : kDefinitions) {
        if (source.size() == definition.source.size() &&
            _wcsnicmp(source.data(), definition.source.data(), definition.source.size()) == 0) {
            return &definition;
        }
    }
    return nullptr;
}

bool IsNativePresetSource(std::wstring_view source) noexcept {
    return FindNativePresetDefinition(source) != nullptr;
}

bool ParseNativePreset(std::wstring_view source, NativeWidgetPreset* preset) noexcept {
    if (!preset) return false;
    const auto* definition = FindNativePresetDefinition(source);
    if (!definition) return false;
    *preset = definition->preset;
    return true;
}

std::wstring NativePresetSource(NativeWidgetPreset preset) {
    const auto* definition = NativePresetDefinition(preset);
    return definition ? std::wstring(definition->source) : std::wstring{};
}

const wchar_t* NativePresetTitle(NativeWidgetPreset preset) noexcept {
    const auto* definition = NativePresetDefinition(preset);
    return definition ? definition->title.data() : L"原生小组件";
}

std::uint32_t NativePresetRefreshIntervalMs(NativeWidgetPreset preset) noexcept {
    const auto* definition = NativePresetDefinition(preset);
    return definition ? definition->periodicRefreshMs : 0;
}

} // namespace turingdesk::wallpaper
