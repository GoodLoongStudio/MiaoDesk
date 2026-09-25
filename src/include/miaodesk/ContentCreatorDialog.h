#pragma once

#include "miaodesk/ContentCreatorBridge.h"
#include "miaodesk/L3Agent.h"

#include <windows.h>

namespace miaodesk::creator {

// Purpose-built authoring surface for Wallpaper / Widget creation. This is not an
// alias for ConversationPanel: it owns its own native controls, transcript and Skill
// preview while borrowing the caller's single canonical L3/Pi runtime.
bool ShowContentCreatorDialog(
    HINSTANCE instance,
    HWND owner,
    L3Agent& agent,
    ContentCreatorKind kind);

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
