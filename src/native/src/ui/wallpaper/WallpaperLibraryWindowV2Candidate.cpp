// Compile-only M4 candidate bridge.
//
// WallpaperLibraryWindowV2 is intentionally not the production UI yet. This
// translation unit keeps the candidate compiling in every Windows build while
// routing its temporary DesktopWidgetStore-shaped calls through the existing
// DesktopWidgetUiAdapter -> DesktopControlService boundary.
//
// The candidate source predates the normal production compile path, so this
// bridge also provides compatibility for two Win32/MSVC details without
// changing shipping behavior: windowsx coordinate helpers and explicit int
// normalization for layout-only std::max calls that mix int with Win32 LONG.
// Delete these shims when V2 is edited directly during the M4 migration.

#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/DesktopWidgetUiAdapter.h"
#include "turingdesk/WallpaperLibraryWindow.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#define DesktopWidgetStore DesktopWidgetUiAdapter
#define max(a, b) max<int>(static_cast<int>(a), static_cast<int>(b))
#include "WallpaperLibraryWindowV2.cpp"
#undef max
#undef DesktopWidgetStore
