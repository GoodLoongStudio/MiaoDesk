// Production bridge for the legacy WallpaperAutomationWindow implementation.
//
// The historical window is written against WallpaperAutomationStore. In the
// shipping build we substitute the store-shaped AutomationUiAdapter so every UI
// mutation/read goes through AutomationService. Keep the original source file as
// the migration body until the V2 UI replaces this window.

#include "turingdesk/AutomationUiAdapter.h"

#define WallpaperAutomationStore AutomationUiAdapter
#include "WallpaperAutomationWindow.cpp"
#undef WallpaperAutomationStore
