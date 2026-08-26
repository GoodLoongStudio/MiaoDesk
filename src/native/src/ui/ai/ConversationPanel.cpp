#include "turingdesk/ConversationPanel.h"
#include "ConversationPanelCompileCompat.h"
#include <dwmapi.h>
#include <windowsx.h>

// Compile the migrated implementation under the canonical public symbol while the
// implementation body is still being mechanically separated from its historical name.
#define ShowL3CliWindow ShowConversationPanel
#include "ConversationPanelImpl.inc"
#undef ShowL3CliWindow
