#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace whaledesk {

struct NativeToolResult {
    bool success{};
    std::wstring message;
};

// NativeTools.cpp owns only WhaleDesK-specific product capabilities. Generic
// file/document/shell work belongs to Pi built-in tools, Skills and Extensions,
// not a parallel bespoke WhaleDesK agent stack. Generated desktop content is
// deliberately absent here: AI routes may create only host-owned sandbox previews.
std::string NativeToolDefinitionsJsonRaw();

#ifdef WHALEDESK_NATIVE_TOOLS_IMPL
#define NativeToolDefinitionsJson NativeToolDefinitionsJsonRaw
#else
inline std::string NativeToolDefinitionsJson() {
    std::string json = R"JSON([
{"type":"function","name":"settings_open","description":"Open the native WhaleDesK Settings Center. Use this only for WhaleDesK settings/configuration UI.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"type":"function","name":"wallpaper_validate_package","description":"Validate an existing WhaleDesK .mdwall package directory without applying it.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
{"type":"function","name":"content_skill_get","description":"Load a WhaleDesK content-creation skill specification before writing any .mdwall/.mdwidget package. Omit name to get the index of available skills.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Skill name. One of: content-package-basics, wallpaper-content, widget-content, content-review. Omit to list them."}},"additionalProperties":false}}
])JSON";
    json.erase(std::remove(json.begin(), json.end(), '\r'), json.end());
    json.erase(std::remove(json.begin(), json.end(), '\n'), json.end());
    return json;
}
#endif

NativeToolResult ExecuteNativeToolRaw(std::string_view toolName, std::string_view argumentsJson);

#ifdef WHALEDESK_NATIVE_TOOLS_IMPL
#define ExecuteNativeTool ExecuteNativeToolRaw
#else
NativeToolResult ExecuteNativeToolIsolated(std::string_view toolName, std::string_view argumentsJson);
inline NativeToolResult ExecuteNativeTool(std::string_view toolName, std::string_view argumentsJson) {
    return ExecuteNativeToolIsolated(toolName, argumentsJson);
}
#endif

} // namespace whaledesk
