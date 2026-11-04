#pragma once

#include <windows.h>

namespace whaledesk::wallpaper {

enum class ContentSkillBrowserDomain {
    Wallpaper,
    Widget,
};

// Shows the exact content-creation SKILL.md files that the AI receives through
// content_skill_get. The browser is read-only; creation remains in the canonical
// WhaleDesK ConversationPanel.
bool ShowContentSkillBrowserDialog(
    HINSTANCE instance,
    HWND owner,
    ContentSkillBrowserDomain domain);

} // namespace whaledesk::wallpaper
