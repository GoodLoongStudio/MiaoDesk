#include "turingdesk/ConversationPanel.h"
#include "ConversationPanelCompileCompat.h"
#include <dwmapi.h>
#include <windowsx.h>

// Keep the historical implementation body private while the public entry stays canonical.
// The second macro keeps the existing runtime-contract marker during the migration.
#define ShowConversationPanel ShowConversationPanelCore
#define ShowL3CliWindow ShowConversationPanel
#include "ConversationPanelImpl.inc"
#undef ShowL3CliWindow
#undef ShowConversationPanel

#include "ConversationPanelInputOverlay.inc"

namespace turingdesk {

bool ShowConversationPanel(HINSTANCE instance, HWND owner, L3Agent& agent, const std::wstring& initialPrompt) {
    // Install input/semantic bridges before the first model turn so even the prompt coming
    // directly from Search Bar gets real tool activity events instead of log polling.
    const bool shown = ShowConversationPanelCore(instance, owner, agent, L"");
    if (!shown) return false;

    EnsureConversationInputOverlay(instance);
    if (gConversationState && IsWindow(gConversationState->window)) {
        EnsureConversationActivityBridge(gConversationState->window);
        if (!Trim(initialPrompt).empty() && !gConversationState->busy && !gConversationState->pendingConfirmation) {
            SetWindowTextW(gConversationState->input, initialPrompt.c_str());
            SendPrompt(*gConversationState);
        }
        SetFocus(gConversationState->input);
    }
    return true;
}

} // namespace turingdesk
