#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace turingdesk {

struct NativeToolResult {
    bool success{};
    std::wstring message;
};

// NativeTools.cpp owns the raw pretty-printed registry. Every consumer gets the
// compact wrapper below so Codex app-server stdio (one JSON-RPC message per line)
// can never have thread/start split by CR/LF inside dynamicTools.
std::string NativeToolDefinitionsJsonRaw();

#ifdef TURINGDESK_NATIVE_TOOLS_IMPL
#define NativeToolDefinitionsJson NativeToolDefinitionsJsonRaw
#else
inline std::string NativeToolDefinitionsJson() {
    auto json = NativeToolDefinitionsJsonRaw();
    json.erase(std::remove(json.begin(), json.end(), '\r'), json.end());
    json.erase(std::remove(json.begin(), json.end(), '\n'), json.end());
    return json;
}
#endif

NativeToolResult ExecuteNativeTool(std::string_view toolName, std::string_view argumentsJson);

} // namespace turingdesk
