#pragma once
#include "whaledesk/L3Agent.h"
#include <windows.h>

namespace whaledesk {

// Opens or activates the single primary WhaleDesK Settings Center.
// Product baseline: this is the Wallpaper Engine-style desktop library/settings
// page (desktop cards on the left, details/actions on the right). AI/API,
// displays, rules, performance and DeepSeek Harness are sections of that same
// settings information architecture rather than a separate launcher window.
bool ShowSettingsCenterWindow(HINSTANCE instance, HWND owner, L3Agent& agent);

} // namespace whaledesk
