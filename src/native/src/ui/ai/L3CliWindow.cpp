// Compatibility build shim. The production AI presentation implementation lives in
// ConversationPanel.cpp. Keep this file only until CMake and downstream references no
// longer require the historical source path.
#include "turingdesk/L3CliWindow.h"

#define ShowL3CliWindow ShowConversationPanel
#include "ConversationPanel.cpp"
#undef ShowL3CliWindow
