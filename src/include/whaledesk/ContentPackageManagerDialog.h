#pragma once

#include "whaledesk/WhaleContentModel.h"

#include <windows.h>

namespace whaledesk::wallpaper {

// Shows installed wallpaper/widget Content packages and lets the user inspect
// locations or safely uninstall user-managed packages. Returns true when the
// installed package set changed while the dialog was open.
bool ShowContentPackageManagerDialog(
    HINSTANCE instance,
    HWND owner,
    content::ContentKind initialKind);

} // namespace whaledesk::wallpaper
