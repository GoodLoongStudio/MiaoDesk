#pragma once

#include <windows.h>

namespace turingdesk::wallpaper {

// Shows the AI/API configuration page inside the existing Desktop Settings
// window. This never creates a competing top-level settings surface.
bool ShowDesktopAiSettingsPage(HWND desktopSettingsWindow);
void HideDesktopAiSettingsPage(HWND desktopSettingsWindow);
bool DesktopAiSettingsPageVisible(HWND desktopSettingsWindow);

} // namespace turingdesk::wallpaper
