#pragma once

#include <windows.h>

#include <string>

#include "whaledesk/DesktopWidgetController.h"

namespace whaledesk::wallpaper {

// Opens a modal editor for the shared Today Tasks store. Returns true when the
// task data changed while the editor was open.
bool ShowTodayTaskEditorDialog(
    HINSTANCE instance,
    HWND owner,
    desktop::DesktopWidgetController& controller,
    std::wstring* resultMessage = nullptr);

} // namespace whaledesk::wallpaper
