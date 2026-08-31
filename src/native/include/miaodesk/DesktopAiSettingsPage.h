#pragma once

#include <windows.h>

// The settings page owns both per-profile credentials and the active MiaoDesk model
// credential. Install the shared guard before the save bridge so profile secrets pass
// through unchanged while MiaoDesk/ModelApiKey is validated/recovered consistently with
// Pi and Direct Model runtime paths.
#include "miaodesk/ModelCredentialGuard.h"
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