#include "miaodesk/NativeWidgetPreset.h"

#include <array>
#include <cwctype>

namespace miaodesk::wallpaper {
namespace {

// 宽字符的大小写不敏感比较,与 _wcsnicmp 在本文件的用途上等价。
//
// 为什么换掉:_wcsnicmp 是 Windows CRT 专属,于是整个文件(连同它那些纯算术的预设表)
// 在本机一行都编不过。三个内置 source 全是 ASCII,逐字符 towlower 就够了;
// 而依赖区域设置的比较反而会让"同一份 source 在不同语言 Windows 上拼法不同"
// 变成一个新问题 —— 那正是不该有的行为。
//
// 调用方已保证两者等长,所以这里不比长度。
bool WideEqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i])) return false;
    }
    return true;
}

constexpr std::array<NativeWidgetDefinition, 3> kDefinitions{{
    // Fractions are relative to the target monitor. On a 16:9 desktop these
    // produce the intended product proportions: wide clock/weather cards and a
    // taller task card rather than the old uniformly wide demo rectangles.
    // Painter geometry is expressed in Direct2D DIPs; the host converts the
    // HWND render target to DIP size before invoking a renderer on high-DPI PCs.
    // Built-in native presets are singleton per target monitor: the controller
    // prevents overlapping creation, the store enforces identity at its write
    // boundary, and Load repairs duplicate records left by older builds.
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
        if (WideEqualsIgnoreCase(source, definition.source)) return &definition;
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

} // namespace miaodesk::wallpaper
