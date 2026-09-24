#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace miaodesk::creator {

enum class ContentCreatorKind : std::uint32_t {
    None = 0,
    Wallpaper = 1,
    Widget = 2,
};

// Cross-process request from MiaoDeskWallpaper.exe to the single MiaoDesk.exe
// conversation owner. The payload is exactly one uint32_t ContentCreatorKind.
inline constexpr ULONG_PTR kContentCreatorCopyDataTag = 0x4D444352u; // "MDCR"

ContentCreatorKind ParseCommandLine(std::wstring_view commandLine) noexcept;
std::wstring InitialPrompt(ContentCreatorKind kind);

bool DecodeCopyData(const COPYDATASTRUCT* data, ContentCreatorKind* kind) noexcept;
bool SendToRunningApp(ContentCreatorKind kind, DWORD timeoutMs = 3000) noexcept;

// Product-facing entry point used by wallpaper/widget management UI. If the
// main app is already running this reuses its ConversationPanel. Otherwise it
// starts MiaoDesk.exe with a creator-mode command line; the new main instance
// opens the same panel after startup.
bool OpenConversation(ContentCreatorKind kind, HWND owner = nullptr,
                      std::wstring* error = nullptr);

} // namespace miaodesk::creator
