#pragma once

#include <string>
#include <string_view>

#include <windows.h>

#include "miaodesk/DesktopWidgetController.h"

namespace miaodesk::wallpaper {

// Opens a modal schema-driven editor for a Content widget. Returns true when
// parameter state was changed (apply or restore-default), false for cancel or
// when the selected widget has no editable Content parameter schema.
bool ShowContentWidgetSettingsDialog(
    HINSTANCE instance,
    HWND owner,
    desktop::DesktopWidgetController& controller,
    std::wstring_view widgetId,
    std::wstring* status = nullptr);

} // namespace miaodesk::wallpaper
