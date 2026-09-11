#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace miaodesk::wallpaper {

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

// The first production Content package is intentionally routed through the
// proven GlassClock host surface while the generalized Content slot type is
// being introduced. This is source-file scoped by CMake to NativeWidgetHost.cpp
// and therefore does not relabel persisted Content widgets as Native elsewhere.
#if defined(MIAODESK_WIDGET_HOST_CONTENT_DOGFOOD)
inline constexpr std::wstring_view kGlassClockContentSource =
    L"content:com.goodloong.glass-clock";

inline bool ParseWidgetHostPresetSource(
    std::wstring_view source,
    NativeWidgetPreset* preset) noexcept {
    if (source == kGlassClockContentSource) {
        if (preset) *preset = NativeWidgetPreset::GlassClock;
        return true;
    }
    return ParseNativePreset(source, preset);
}

inline bool IsWidgetHostPresetSource(std::wstring_view source) noexcept {
    return source == kGlassClockContentSource || IsNativePresetSource(source);
}

#define ParseNativePreset(source, preset) \
    ::miaodesk::wallpaper::ParseWidgetHostPresetSource((source), (preset))
#define IsNativePresetSource(source) \
    ::miaodesk::wallpaper::IsWidgetHostPresetSource((source))
#endif

} // namespace miaodesk::wallpaper