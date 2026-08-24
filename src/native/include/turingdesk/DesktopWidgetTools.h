#pragma once

#include <string_view>

#include "turingdesk/NativeTools.h"

namespace turingdesk {

bool IsDesktopControlTool(std::string_view toolName) noexcept;
NativeToolResult ExecuteDesktopControlTool(std::string_view toolName, std::string_view argumentsJson);

} // namespace turingdesk
