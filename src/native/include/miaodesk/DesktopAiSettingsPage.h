#pragma once

#include <windows.h>

#include "miaodesk/ApiSettingsSaveBridge.h"
#include "miaodesk/ApiSettingsAutoSaveBridge.h"

namespace miaodesk::wallpaper {

// Shows the native multi-profile API configuration center inside the existing
// Desktop Settings window. Provider metadata lives in the settings domain while
// secrets remain in Windows Credential Manager. This never creates a competing
// top-level settings surface.
bool ShowDesktopAiSettingsPage(HWND desktopSettingsWindow);
void HideDesktopAiSettingsPage(HWND desktopSettingsWindow);
bool DesktopAiSettingsPageVisible(HWND desktopSettingsWindow);

} // namespace miaodesk::wallpaper