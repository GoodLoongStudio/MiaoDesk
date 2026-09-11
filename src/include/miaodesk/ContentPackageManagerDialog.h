#pragma once

#include "miaodesk/MiaoContentModel.h"

#include <windows.h>

namespace miaodesk::wallpaper {

// Shows installed wallpaper/widget Content packages and lets the user inspect
// locations or safely uninstall user-managed packages. Returns true when the
// installed package set changed while the dialog was open.
bool ShowContentPackageManagerDialog(
    HINSTANCE instance,
    HWND owner,
    content::ContentKind initialKind);

} // namespace miaodesk::wallpaper
