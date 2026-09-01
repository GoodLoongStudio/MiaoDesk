#pragma once

#include <string_view>

#include "miaodesk/NativeTools.h"

namespace miaodesk {

bool IsDesktopControlTool(std::string_view toolName) noexcept;
NativeToolResult ExecuteDesktopControlTool(std::string_view toolName, std::string_view argumentsJson);

} // namespace miaodesk
