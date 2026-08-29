#pragma once

#include <windows.h>

#include <algorithm>

namespace miaodesk::window_placement {

namespace detail {

constexpr wchar_t kRegistryPath[] = L"Software\\MiaoDesk\\WindowPlacement";
constexpr DWORD kRecordVersion = 1;

struct StoredRect {
    DWORD version{kRecordVersion};
    LONG left{};
    LONG top{};
    LONG width{};
    LONG height{};
};

inline bool ReadRecord(const wchar_t* valueName, StoredRect& record) {
    if (!valueName || !*valueName) return false;
    DWORD type = 0;
    DWORD size = sizeof(record);
    StoredRect candidate{};
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, kRegistryPath, valueName,
        RRF_RT_REG_BINARY, &type, &candidate, &size);
    if (status != ERROR_SUCCESS || type != REG_BINARY || size != sizeof(candidate) ||
        candidate.version != kRecordVersion) {
        return false;
    }
    if (candidate.width < 80 || candidate.height < 80 ||
        candidate.width > 32768 || candidate.height > 32768 ||
        candidate.left < -131072 || candidate.left > 131072 ||
        candidate.top < -131072 || candidate.top > 131072) {
        return false;
    }
    record = candidate;
    return true;
}

inline bool WriteRecord(const wchar_t* valueName, const StoredRect& record) {
    if (!valueName || !*valueName) return false;
    HKEY key{};
    DWORD disposition = 0;
    const LSTATUS create = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, &disposition);
    if (create != ERROR_SUCCESS || !key) return false;
    const LSTATUS write = RegSetValueExW(
        key, valueName, 0, REG_BINARY,
        reinterpret_cast<const BYTE*>(&record), sizeof(record));
    RegCloseKey(key);
    return write == ERROR_SUCCESS;
}

inline RECT ClampToVisibleWorkArea(RECT rect, LONG minWidth, LONG minHeight) {
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return rect;

    const RECT work = info.rcWork;
    const LONG workWidth = (std::max)(1L, work.right - work.left);
    const LONG workHeight = (std::max)(1L, work.bottom - work.top);
    LONG width = rect.right - rect.left;
    LONG height = rect.bottom - rect.top;
    width = (std::clamp)(width, (std::min)(minWidth, workWidth), workWidth);
    height = (std::clamp)(height, (std::min)(minHeight, workHeight), workHeight);

    const LONG maxLeft = work.right - width;
    const LONG maxTop = work.bottom - height;
    const LONG left = (std::clamp)(rect.left, work.left, maxLeft);
    const LONG top = (std::clamp)(rect.top, work.top, maxTop);
    return RECT{left, top, left + width, top + height};
}

} // namespace detail

inline bool Load(const wchar_t* valueName, RECT& rect, LONG minWidth = 320, LONG minHeight = 220) {
    detail::StoredRect record{};
    if (!detail::ReadRecord(valueName, record)) return false;
    RECT candidate{
        record.left,
        record.top,
        record.left + record.width,
        record.top + record.height,
    };
    rect = detail::ClampToVisibleWorkArea(candidate, minWidth, minHeight);
    return true;
}

inline bool Save(HWND window, const wchar_t* valueName) {
    if (!window || !IsWindow(window) || IsIconic(window)) return false;
    RECT rect{};
    if (!GetWindowRect(window, &rect)) return false;
    const LONG width = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;
    if (width < 80 || height < 80) return false;
    const detail::StoredRect record{
        detail::kRecordVersion,
        rect.left,
        rect.top,
        width,
        height,
    };
    return detail::WriteRecord(valueName, record);
}

inline bool Restore(HWND window, const wchar_t* valueName,
                    LONG minWidth = 320, LONG minHeight = 220,
                    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE) {
    if (!window || !IsWindow(window)) return false;
    RECT rect{};
    if (!Load(valueName, rect, minWidth, minHeight)) return false;
    return SetWindowPos(
               window, nullptr, rect.left, rect.top,
               rect.right - rect.left, rect.bottom - rect.top, flags) != FALSE;
}

} // namespace miaodesk::window_placement
