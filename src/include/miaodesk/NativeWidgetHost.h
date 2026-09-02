#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace miaodesk::wallpaper {

// One lightweight host process renders every enabled native desktop widget with
// Direct2D. The widget runtime coordinator keeps at most one of these alive.
class NativeWidgetProcessSet {
public:
    NativeWidgetProcessSet();
    ~NativeWidgetProcessSet();

    NativeWidgetProcessSet(const NativeWidgetProcessSet&) = delete;
    NativeWidgetProcessSet& operator=(const NativeWidgetProcessSet&) = delete;

    bool Start(HWND parentWindow);
    void Stop();
    void SetPaused(bool paused);

    bool Active() const noexcept;
    std::wstring LastErrorText() const;
    std::wstring DiagnosticsText() const;

    static bool SelfTest() noexcept;

private:
    HWND parent_{};
    HANDLE job_{};
    HANDLE process_{};
    HANDLE thread_{};
    bool paused_{};
    std::wstring lastError_;
};

// Returns -1 when the command line is not a native widget host invocation.
int TryRunNativeWidgetHost(HINSTANCE instance);

inline constexpr wchar_t kNativeWidgetSurfaceClass[] = L"MiaoDesk.Native.WidgetSurface";
inline constexpr wchar_t kNativeWidgetPaintReadyProperty[] = L"MiaoDesk.Native.WidgetPaintReady";

} // namespace miaodesk::wallpaper
