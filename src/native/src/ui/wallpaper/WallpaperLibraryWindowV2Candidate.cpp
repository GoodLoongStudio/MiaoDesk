// Compile-only M4 candidate bridge.
//
// WallpaperLibraryWindowV2 is intentionally not the production UI yet. This
// translation unit keeps the candidate compiling in every Windows build so the
// new product shell cannot silently rot behind the production compatibility
// wrapper. V2 now talks to DesktopWidgetController directly; no persistence or
// Win32 compatibility macro interception remains here.

#include "WallpaperLibraryWindowV2.cpp"
