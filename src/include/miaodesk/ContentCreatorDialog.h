#pragma once

#include "miaodesk/ContentCreatorBridge.h"
#include "miaodesk/L3Agent.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::creator {

// Purpose-built authoring surface for Wallpaper / Widget creation. This is not an
// alias for ConversationPanel: it owns its own native controls, transcript, Skill preview
// and profile-selectable L3Agent. Pi process/tool policy stays shared, but API profile
// selection belongs to this window and never rewrites the central profile database.
bool ShowContentCreatorDialog(
    HINSTANCE instance,
    HWND owner,
    L3Agent& agent,
    ContentCreatorKind kind);

// HWNDs of the creator surfaces open in this thread that should take part in dialog
// keyboard navigation.
//
// The creator is modeless: its messages are dispatched by the search window's pump,
// which is in a different translation unit and cannot see the dialog's DialogState.
// Every control here is WS_TABSTOP, so without this list that pump has no way to know
// the creator exists, and Tab does nothing anywhere in it -- a keyboard-only user
// cannot reach the prompt, the preset chips, the skill list or the apply buttons.
//
// Windows in fullscreen preview are deliberately left out: that mode owns every key
// (Esc exits, Space toggles playback, R reloads), and handing it to the dialog
// manager would swallow Esc into a WM_COMMAND/IDCANCEL that this window does not
// handle, so the exit would silently stop working.
std::vector<HWND> DialogManagedCreatorWindows();

// Pure layout contract used by the native dialog and contract tests. Coordinates are
// expressed in 96-DPI logical pixels and are scaled at the window boundary.
struct ContentCreatorLayout {
    int margin{};
    int gap{};
    int headerHeight{};
    int footerHeight{};
    int leftWidth{};
    int rightWidth{};
    int bodyHeight{};
};

ContentCreatorLayout ResolveContentCreatorLayout(int clientWidth, int clientHeight) noexcept;

} // namespace miaodesk::creator
