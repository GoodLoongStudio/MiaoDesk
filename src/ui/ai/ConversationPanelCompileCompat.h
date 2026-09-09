#pragma once

#include <algorithm>
#include <windows.h>

// Win32 RECT/MONITORINFO coordinates are LONG while the Conversation Panel layout
// intentionally uses logical int pixels. MSVC does not deduce mixed-type std::max/clamp.
// Keep the conversion local to this translation unit instead of leaking casts across the UI.
namespace std {
inline LONG max(int left, LONG right) noexcept {
    return (std::max)(static_cast<LONG>(left), right);
}
inline LONG max(LONG left, int right) noexcept {
    return (std::max)(left, static_cast<LONG>(right));
}
inline int clamp(int value, LONG low, LONG high) noexcept {
    return static_cast<int>((std::clamp)(static_cast<LONG>(value), low, high));
}
} // namespace std

inline BOOL MiaoDeskMoveWindow(HWND hwnd, int x, int y, int width, int height) noexcept {
    return ::MoveWindow(hwnd, x, y, width, height, TRUE);
}
