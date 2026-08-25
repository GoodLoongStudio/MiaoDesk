// Compile-only M4 candidate bridge.
//
// WallpaperLibraryWindowV2 is intentionally not the production UI yet. This
// translation unit keeps the candidate compiling in every Windows build while
// routing its temporary DesktopWidgetStore-shaped calls through the existing
// DesktopWidgetUiAdapter -> DesktopControlService boundary.
//
// When V2 is migrated to controllers/services directly, delete this bridge and
// compile WallpaperLibraryWindowV2.cpp through the guarded production switch.

#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/DesktopWidgetUiAdapter.h"

#define DesktopWidgetStore DesktopWidgetUiAdapter
#include "WallpaperLibraryWindowV2.cpp"
#undef DesktopWidgetStore
