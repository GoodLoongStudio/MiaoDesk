#pragma once

#include <windows.h>

namespace miaodesk::api_profile_notifications {

inline constexpr wchar_t kChangedMessageName[] = L"MiaoDesk.ApiProfilesChanged.v1";
inline constexpr wchar_t kSearchWindowClass[] = L"MiaoDesk.Native.SearchWindow";
inline constexpr wchar_t kHarnessStopEvent[] = L"Local\\MiaoDesk.Native.Harness.Background.Stop";

inline UINT ChangedMessage() {
    static const UINT message = RegisterWindowMessageW(kChangedMessageName);
    return message;
}

inline void NotifyRuntimeConsumers() {
    const UINT message = ChangedMessage();
    if (message != 0) {
        if (const HWND search = FindWindowW(kSearchWindowClass, nullptr))
            PostMessageW(search, message, 0, 0);
    }

    // The background Harness owner materializes provider settings at startup.
    // Stop it after a profile change so the next launch consumes the new default.
    if (HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kHarnessStopEvent)) {
        SetEvent(event);
        CloseHandle(event);
    }
}

} // namespace miaodesk::api_profile_notifications
