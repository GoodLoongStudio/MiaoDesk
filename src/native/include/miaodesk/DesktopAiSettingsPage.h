#pragma once

#include <windows.h>

namespace miaodesk::wallpaper {

// Shows the native API configuration center inside the existing Desktop Settings window.
// The configuration center owns persisted API profiles; Pi Agent, DeepSeek Harness and
// Direct Model consume the selected default profile through the shared runtime profile
// reader. No legacy active-model mirror or settings-page hook participates in this UI.
bool ShowDesktopAiSettingsPage(HWND desktopSettingsWindow);
void HideDesktopAiSettingsPage(HWND desktopSettingsWindow);
bool DesktopAiSettingsPageVisible(HWND desktopSettingsWindow);

} // namespace miaodesk::wallpaper
