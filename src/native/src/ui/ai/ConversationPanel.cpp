#include "turingdesk/ConversationPanel.h"
#include "ConversationPanelCompileCompat.h"
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <cstring>

namespace {

BOOL TuringDeskSkipLegacyLayerAlpha(HWND, COLORREF, BYTE, DWORD) {
    // ConversationPanel is presented with UpdateLayeredWindow + premultiplied alpha.
    // Uniform layered alpha would reintroduce the hard/dirty edge fixed in Search Bar.
    return TRUE;
}

int TuringDeskSkipLegacyWindowRegion(HWND, HRGN region, BOOL) {
    // Direct2D is the only owner of the visible rounded edge. The legacy GDI region is
    // intentionally discarded so integer clipping cannot fight per-pixel antialiasing.
    if (region) DeleteObject(region);
    return 1;
}

} // namespace

// Keep the historical implementation body private while the public entry stays canonical.
// The second macro keeps the existing runtime-contract marker during the migration.
#define ShowConversationPanel ShowConversationPanelCore
#define ShowL3CliWindow ShowConversationPanel
#define SetLayeredWindowAttributes TuringDeskSkipLegacyLayerAlpha
#define SetWindowRgn TuringDeskSkipLegacyWindowRegion
#include "ConversationPanelImpl.inc"
#undef SetWindowRgn
#undef SetLayeredWindowAttributes
#undef ShowL3CliWindow
#undef ShowConversationPanel

// rpcndr.h from the Windows SDK still defines `small` as a legacy IDL macro. It must not
// leak into modern C++ parameter names in the Direct2D renderer.
#ifdef small
#undef small
#endif

#include "ConversationPanelLayeredSurface.inc"
#include "ConversationPanelInputOverlay.inc"

namespace turingdesk {

bool ShowConversationPanel(HINSTANCE instance, HWND owner, L3Agent& agent, const std::wstring& initialPrompt) {
    // Install the visible Direct2D surface and semantic bridge before the first model turn.
    const bool shown = ShowConversationPanelCore(instance, owner, agent, L"");
    if (!shown) return false;

    EnsureConversationInputOverlay(instance);
    if (gConversationState && IsWindow(gConversationState->window)) {
        EnsureConversationActivityBridge(gConversationState->window);
        PositionConversationInputProxy(*gConversationState);
        RenderConversationLayerSurface(*gConversationState);
        if (!Trim(initialPrompt).empty() && !gConversationState->busy && !gConversationState->pendingConfirmation) {
            SetWindowTextW(gConversationState->input, initialPrompt.c_str());
            SendPrompt(*gConversationState);
        }
        SetFocus(gConversationState->input);
    }
    return true;
}

} // namespace turingdesk
