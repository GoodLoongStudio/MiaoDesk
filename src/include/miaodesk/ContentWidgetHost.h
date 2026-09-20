#pragma once

#include <windows.h>

#include <string>

namespace miaodesk::wallpaper {

// Isolated process set for Scene-based .mdwidget content. Native widgets keep
// their existing host/fallback path; Content packages get their own renderer
// fault domain so third-party content cannot take down built-in widgets.
class ContentWidgetProcessSet {
public:
    ContentWidgetProcessSet();
    ~ContentWidgetProcessSet();

    ContentWidgetProcessSet(const ContentWidgetProcessSet&) = delete;
    ContentWidgetProcessSet& operator=(const ContentWidgetProcessSet&) = delete;

    bool Start(HWND parentWindow);
    void Stop();
    void SetPaused(bool paused);

    bool Active() const noexcept;
    std::wstring LastErrorText() const;
    std::wstring DiagnosticsText() const;

    static bool SelfTest() noexcept;

private:
    HWND parent_{};
    HANDLE process_{};
    HANDLE thread_{};
    HANDLE job_{};
    bool paused_{};
    std::wstring lastError_;
};

// Returns -1 when the command line is not a Content widget host invocation.
int TryRunContentWidgetHost(HINSTANCE instance);

inline constexpr wchar_t kContentWidgetHostMessageClass[] = L"MiaoDesk.Content.WidgetHostMessage";

} // namespace miaodesk::wallpaper
