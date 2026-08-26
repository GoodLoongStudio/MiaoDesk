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
    const bool shown = ShowConversationPanelCore(instance, owner, agent, initialPrompt);
    if (shown) EnsureConversationInputOverlay(instance);
    return shown;
}

} // namespace turingdesk
