#pragma once

#include <windows.h>
#include <commctrl.h>
#include <array>
#include <iterator>
#include <new>

namespace miaodesk::api_settings_autosave_bridge {

constexpr wchar_t kPageClass[] = L"MiaoDesk.Native.ApiConfigurationCenter";
constexpr wchar_t kInstalledProperty[] = L"MiaoDesk.ApiAutoSave.Installed";
constexpr UINT_PTR kSubclassId = 0x4D415553; // MAUS
constexpr UINT kAutoSaveMessage = WM_APP + 0x35C;

constexpr int kProfileListId = 7300;
constexpr int kNameId = 7301;
constexpr int kTypeId = 7302;
constexpr int kApiUrlId = 7303;
constexpr int kApiKeyId = 7304;
constexpr int kModelId = 7305;
constexpr int kTimeoutId = 7306;
constexpr int kTemperatureId = 7307;
constexpr int kDefaultId = 7308;
constexpr int kStreamingId = 7309;
constexpr int kToolsId = 7310;
constexpr int kRetriesId = 7311;
constexpr int kNewId = 7312;
constexpr int kTestId = 7313;
constexpr int kSaveId = 7314;
constexpr int kDeleteId = 7315;

struct AutoSaveState {
    HWND panel{};
    bool dirty{};
    bool saving{};
};

inline bool IsApiPage(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t className[128]{};
    return GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) &&
           wcscmp(className, kPageClass) == 0;
}

inline bool IsTextField(int id) {
    switch (id) {
    case kNameId:
    case kApiUrlId:
    case kApiKeyId:
    case kModelId:
    case kTimeoutId:
    case kTemperatureId:
    case kRetriesId:
        return true;
    default:
        return false;
    }
}

inline void HideManualSave(HWND panel) {
    if (HWND save = GetDlgItem(panel, kSaveId)) ShowWindow(save, SW_HIDE);
}

inline void Flush(AutoSaveState& state) {
    if (!state.panel || !IsWindow(state.panel) || !state.dirty || state.saving) return;
    state.saving = true;
    state.dirty = false;
    SendMessageW(state.panel, WM_COMMAND, MAKEWPARAM(kSaveId, BN_CLICKED),
                 reinterpret_cast<LPARAM>(GetDlgItem(state.panel, kSaveId)));
    state.saving = false;
    HideManualSave(state.panel);
}

inline LRESULT CALLBACK PageSubclass(HWND panel, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR refData) {
    auto* state = reinterpret_cast<AutoSaveState*>(refData);
    if (!state) return DefSubclassProc(panel, message, wParam, lParam);

    if (message == kAutoSaveMessage) {
        Flush(*state);
        return 0;
    }

    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == kSaveId && code == BN_CLICKED) {
            state->dirty = false;
            return DefSubclassProc(panel, message, wParam, lParam);
        }

        if (IsTextField(id)) {
            if (code == EN_CHANGE && GetFocus() == GetDlgItem(panel, id)) {
                state->dirty = true;
            } else if (code == EN_KILLFOCUS && state->dirty) {
                PostMessageW(panel, kAutoSaveMessage, 0, 0);
            }
        } else if (id == kTypeId && code == CBN_SELCHANGE) {
            state->dirty = true;
            PostMessageW(panel, kAutoSaveMessage, 0, 0);
        } else if ((id == kDefaultId || id == kStreamingId || id == kToolsId) && code == BN_CLICKED) {
            state->dirty = true;
            PostMessageW(panel, kAutoSaveMessage, 0, 0);
        }

        // Persist the current profile before the original page swaps/reloads its form.
        if ((id == kProfileListId && code == LBN_SELCHANGE) ||
            ((id == kNewId || id == kTestId || id == kDeleteId) && code == BN_CLICKED)) {
            Flush(*state);
        }
    }

    if (message == WM_SHOWWINDOW && wParam == FALSE) Flush(*state);

    const LRESULT result = DefSubclassProc(panel, message, wParam, lParam);

    if (message == WM_SHOWWINDOW || message == WM_SIZE || message == WM_WINDOWPOSCHANGED) {
        HideManualSave(panel);
    }

    if (message == WM_NCDESTROY) {
        RemovePropW(panel, kInstalledProperty);
        RemoveWindowSubclass(panel, PageSubclass, kSubclassId);
        delete state;
    }
    return result;
}

inline void Install(HWND panel) {
    if (!panel || GetPropW(panel, kInstalledProperty)) return;
    auto* state = new (std::nothrow) AutoSaveState{};
    if (!state) return;
    state->panel = panel;
    if (!SetWindowSubclass(panel, PageSubclass, kSubclassId, reinterpret_cast<DWORD_PTR>(state))) {
        delete state;
        return;
    }
    SetPropW(panel, kInstalledProperty, reinterpret_cast<HANDLE>(1));
    HideManualSave(panel);

    // Settings and chat are separate native processes. Re-saving the already-selected profile
    // once after the page finishes creating synchronizes an existing configured/default profile
    // into the shared model-settings.json + active Credential Manager target even when the user
    // does not touch a field in this session.
    state->dirty = true;
    PostMessageW(panel, kAutoSaveMessage, 0, 0);
}

inline LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message && IsApiPage(message->hwnd)) Install(message->hwnd);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class Bridge final {
public:
    Bridge() : hook_(SetWindowsHookExW(WH_CALLWNDPROC, HookProc, nullptr, GetCurrentThreadId())) {}
    ~Bridge() { if (hook_) UnhookWindowsHookEx(hook_); }
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
private:
    HHOOK hook_{};
};

inline Bridge gBridge;

} // namespace miaodesk::api_settings_autosave_bridge