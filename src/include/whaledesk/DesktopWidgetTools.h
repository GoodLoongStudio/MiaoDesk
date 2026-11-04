#pragma once

#include <string_view>

#include "whaledesk/NativeTools.h"

namespace whaledesk {

NativeToolResult ExecuteDesktopControlTool(std::string_view toolName, std::string_view argumentsJson);

} // namespace whaledesk
