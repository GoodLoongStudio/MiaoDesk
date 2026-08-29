#include "turingdesk/NativeWidgetPreset.h"

#include <array>
#include <cwchar>

namespace turingdesk::wallpaper {
namespace {

constexpr std::array<NativeWidgetDefinition, 3> kDefinitions{{
    // Fractions are relative to the target monitor. On a 16:9 desktop these
    // produce the intended product proportions: wide clock/weather cards and a
    // taller task card rather than the old uniformly wide demo rectangles.
    // Painter geometry is expressed in Direct2D DIPs; the host converts the
    // HWND render target to DIP size before invoking a renderer on high-DPI PCs.
    // Built-in native presets are singleton per target monitor; the controller
    // rejects overlapping duplicates and the store repairs legacy duplicates.
    // periodicRefreshMs == 0 means static/event-driven: the host does not poll
    // and repaint that widget just because the scheduler heartbeat fired.
    {NativeWidgetPreset::GlassClock, L"native:glass-clock", L"玻璃时钟", 0.30f, 0.30f, 60000},
    {NativeWidgetPreset::TodayTasks, L"native:today-tasks", L"今日待办", 0.22f, 0.48f, 0},
    {NativeWidgetPreset::WeatherGlass, L"native:weather-glass", L"玻璃天气", 0.28f, 0.28f, 0},
}};
static_assert(kDefinitions.size() == 3, "Update the built-in native widget acceptance set when the catalog changes.");

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
