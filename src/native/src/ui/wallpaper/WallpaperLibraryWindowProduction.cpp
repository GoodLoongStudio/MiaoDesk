// Production bridge for the legacy WallpaperLibraryWindow implementation.
//
// The old implementation still uses the DesktopWidgetStore-shaped API in its
// source text. For production builds we compile it through DesktopWidgetUiAdapter,
// which routes all widget CRUD through DesktopControlService. This keeps the
// shipping UI on the domain boundary without a risky 58k-line mechanical edit.
// Remove this bridge when WallpaperLibraryWindowV2 reaches full parity.

#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/DesktopWidgetUiAdapter.h"

#define DesktopWidgetStore DesktopWidgetUiAdapter
#include "WallpaperLibraryWindow.cpp"
#undef DesktopWidgetStore
