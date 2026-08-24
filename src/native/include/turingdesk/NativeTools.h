#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace turingdesk {

struct NativeToolResult {
    bool success{};
    std::wstring message;
};

// NativeTools.cpp owns only TuringDesk-specific product capabilities. Generic
// file/document/shell work belongs to Pi built-in tools, Skills and Extensions,
// not a parallel bespoke TuringDesk agent stack.
std::string NativeToolDefinitionsJsonRaw();

#ifdef TURINGDESK_NATIVE_TOOLS_IMPL
#define NativeToolDefinitionsJson NativeToolDefinitionsJsonRaw
#else
inline std::string NativeToolDefinitionsJson() {
    std::string json = R"JSON([
{"type":"function","name":"settings_open","description":"Open the native TuringDesk Settings Center. Use this only for TuringDesk settings/configuration UI.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"type":"function","name":"wallpaper_create_web_package","description":"Create a validated TuringDesk .tdwall Web wallpaper package. This is a TuringDesk-specific package format.","inputSchema":{"type":"object","properties":{"name":{"type":"string"},"title":{"type":"string"},"html":{"type":"string"},"open_after_create":{"type":"boolean"}},"required":["name","title","html"],"additionalProperties":false}},
{"type":"function","name":"wallpaper_validate_package","description":"Validate an existing TuringDesk .tdwall package directory.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}}
])JSON";
    json.erase(std::remove(json.begin(), json.end(), '\r'), json.end());
    json.erase(std::remove(json.begin(), json.end(), '\n'), json.end());
    return json;
}
#endif

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
