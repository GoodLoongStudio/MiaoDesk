#pragma once

#include <windows.h>
#include <objbase.h>
#include <string_view>

#include "turingdesk/NativeTools.h"

namespace turingdesk::preview {

inline constexpr ULONG_PTR kCopyDataTag = 0x54445052u; // 'TDPR'

bool IsGeneratedPreviewTool(std::string_view toolName) noexcept;
NativeToolResult ExecuteGeneratedPreviewTool(std::string_view toolName, std::string_view argumentsJson);

// Called only by the main TuringDesk UI process. A native-tool worker may create
// an ephemeral preview description, but it cannot apply it. The running host
// receives the path and owns the visible WebView2 sandbox + Apply/Reject actions.
bool HandleGeneratedPreviewCopyData(HWND owner, const COPYDATASTRUCT* data);

} // namespace turingdesk::preview
