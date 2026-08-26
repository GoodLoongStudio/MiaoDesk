#include "turingdesk/ConversationPanel.h"

// Compile the migrated implementation under the canonical public symbol while the
// implementation body is still being mechanically separated from its historical name.
#define ShowL3CliWindow ShowConversationPanel
#include "ConversationPanelImpl.inc"
#undef ShowL3CliWindow
