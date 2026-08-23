#include "turingdesk/ModelSettingsWindow.h"
#include "turingdesk/SettingsCenterWindow.h"

namespace turingdesk {

bool ShowModelSettingsWindow(HINSTANCE instance, HWND owner, L3Agent& agent) {
    // Product baseline: AI/API configuration lives inside the unified
    // Wallpaper Engine-style TuringDesk Settings Center. Keep this legacy
    // entry point only as a compatibility bridge so older callers do not open
    // a second, competing settings surface.
    ShowSettingsCenterWindow(instance, owner, agent);
    return false;
}

} // namespace turingdesk
