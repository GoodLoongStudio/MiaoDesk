#pragma once
#include "miaodesk/L3Agent.h"
#include <windows.h>
#include <string>

namespace miaodesk {

class PiRuntime;

enum class ConversationPanelMode {
    General = 0,
    WallpaperCreator,
    WidgetCreator,
};

// Canonical AI conversation surface API. The implementation remains Pi-first and
// retains Direct Model only as the transport fallback; UI presentation does not own runtime policy.
// Creator modes are presentation/session scopes only: they reuse the same Pi/L3 backend while
// keeping wallpaper and widget authoring visually and conversationally distinct.
bool ShowConversationPanel(
    HINSTANCE instance,
    HWND owner,
    L3Agent& agent,
    const std::wstring& initialPrompt,
    ConversationPanelMode mode = ConversationPanelMode::General);

// Single canonical Pi runtime shared by the general conversation panel and the
// purpose-built Wallpaper / Widget creator surfaces. UI surfaces stay separate,
// but model/tool session policy has one owner.
PiRuntime& SharedConversationPiRuntime() noexcept;

// Temporary source/API compatibility only. New UI code must call ShowConversationPanel.
// The panel owns its profile-selectable L3Agent; the caller reference remains only to
// preserve the existing source ABI while that migration settles.
inline bool ShowL3CliWindow(
    HINSTANCE instance,
    HWND owner,
    L3Agent& agent,
    const std::wstring& initialPrompt) {
    return ShowConversationPanel(instance, owner, agent, initialPrompt);
}

} // namespace miaodesk