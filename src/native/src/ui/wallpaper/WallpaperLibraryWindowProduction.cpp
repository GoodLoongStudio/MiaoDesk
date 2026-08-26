// Production Desktop Library entry.
//
// The legacy WallpaperLibraryWindow implementation has been retired from the
// shipping path. Production now compiles the V2 product shell directly so
// users and real-Windows acceptance always exercise the same UI implementation.
// Persistence and Widget runtime state continue to flow through domain
// controllers/services; this translation unit owns no Store/INI/Shell state.

#include "WallpaperLibraryWindowV2.cpp"
