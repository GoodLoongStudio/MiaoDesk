#pragma once

#include <string_view>

#include "miaodesk/NativeTools.h"

namespace miaodesk {

NativeToolResult ExecuteDesktopControlTool(std::string_view toolName, std::string_view argumentsJson);

} // namespace miaodesk
