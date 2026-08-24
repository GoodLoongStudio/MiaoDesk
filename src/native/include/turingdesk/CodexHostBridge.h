#pragma once
#include <string>
#include <string_view>

namespace turingdesk {

// Handles server-initiated Codex app-server requests that require a desktop host
// response (approvals, granular permissions, request_user_input and MCP
// elicitations). Returns true when the method belongs to the host bridge and
// fills replyJson with the complete JSON-RPC response line.
bool HandleCodexHostRequest(std::string_view method, std::string_view requestJson, std::string& replyJson);

} // namespace turingdesk
