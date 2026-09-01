#pragma once

#include <windows.h>

#include <string>
#include <string_view>

#include "miaodesk/DesktopControlService.h"

namespace miaodesk::demo {

// Store Demo v0.1 product scope. Keeps the first Windows Store package narrow,
// demoable without an API key, and free of advanced-workbench entry points.

bool IsStoreDemoScopeEnabled() noexcept;
bool HideAdvancedWorkbench() noexcept;

bool NeedsFirstRun();
void MarkFirstRunCompleted();

// Applies a built-in Scene showcase wallpaper through DesktopControlService.
desktop::DesktopControlResult ApplyShowcaseWallpaper(std::wstring_view sceneId = L"scene-aurora");

// Ensures the three fixed M3 widget presets exist (creates any missing ones).
desktop::DesktopControlResult EnsureShowcaseWidgets();

// Golden path: showcase wallpaper + three widgets + EnsureRuntime.
desktop::DesktopControlResult RunGoldenPath();

// Local no-Key demo agent. Returns true when the prompt was handled without Pi.
bool TryHandleDemoPrompt(std::wstring_view prompt, std::wstring* reply);

// First-run welcome. Offers one-click golden path. Safe to call repeatedly;
// only shows while NeedsFirstRun() is true.
void MaybeShowFirstRun(HWND owner);

// Explicit one-click entry used by Settings / Widget page.
void OfferGoldenPath(HWND owner, bool quietStatus = false);

} // namespace miaodesk::demo
