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

// NativeTools.cpp is compiled with TURINGDESK_NATIVE_TOOLS_IMPL, so its existing
// ExecuteNativeTool definition becomes the in-process/raw implementation. Normal
// consumers use the isolated worker wrapper instead. A blocked Office/WPS COM
// server (or any future native tool) therefore cannot freeze the Codex read loop.
NativeToolResult ExecuteNativeToolRaw(std::string_view toolName, std::string_view argumentsJson);

#ifdef TURINGDESK_NATIVE_TOOLS_IMPL
#define ExecuteNativeTool ExecuteNativeToolRaw
#else
NativeToolResult ExecuteNativeToolIsolated(std::string_view toolName, std::string_view argumentsJson);
inline NativeToolResult ExecuteNativeTool(std::string_view toolName, std::string_view argumentsJson) {
    return ExecuteNativeToolIsolated(toolName, argumentsJson);
}
#endif

} // namespace turingdesk
