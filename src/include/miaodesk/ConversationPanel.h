#pragma once
#include "miaodesk/L3Agent.h"
#include <windows.h>
#include <string>

namespace miaodesk {

// Canonical AI conversation surface API. The implementation remains Pi-first and
// retains Direct Model only as the transport fallback; UI presentation does not own runtime policy.
bool ShowConversationPanel(
    HINSTANCE instance,
    HWND owner,
    L3Agent& agent,
    const std::wstring& initialPrompt);

// Temporary source/API compatibility only. New UI code must call ShowConversationPanel.
inline bool ShowL3CliWindow(
    HINSTANCE instance,
    HWND owner,
    L3Agent& agent,
    const std::wstring& initialPrompt) {
    // API settings are edited by the separate MiaoDeskWallpaper settings process.
    // Always refresh the shared active provider/model snapshot before entering chat.
    agent.ReloadConfig();
    return ShowConversationPanel(instance, owner, agent, initialPrompt);
}

} // namespace miaodesk