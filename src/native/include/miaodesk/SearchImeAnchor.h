#pragma once

#include <windows.h>
#include <imm.h>
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "imm32.lib")

namespace miaodesk::search_ime_detail {

constexpr int kSearchEditControlId = 100;
constexpr int kVisibleEditLeft = 52;
constexpr int kVisibleEditRight = 594;
constexpr int kVisibleBarHeight = 56;
constexpr int kImeProxyTop = 13;
constexpr int kImeProxyHeight = 30;
constexpr UINT kDeferredImeAnchorMessage = WM_APP + 0x2A1;

inline thread_local bool gImeAnchorBusy = false;
inline thread_local bool gImeAnchorPending = false;

inline bool IsMiaoDeskSearchWindow(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t className[128]{};
    if (!GetClassNameW(hwnd, className, static_cast<int>(std::size(className)))) return false;
    return wcscmp(className, L"MiaoDesk.Native.SearchWindow") == 0;
}

inline bool IsMiaoDeskSearchEdit(HWND edit) {
    if (!edit || GetDlgCtrlID(edit) != kSearchEditControlId) return false;
    return IsMiaoDeskSearchWindow(GetParent(edit));
}

inline int MeasureCaretOffset(HWND edit) {
    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessageW(edit, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&selectionStart),
                 reinterpret_cast<LPARAM>(&selectionEnd));

    const int length = GetWindowTextLengthW(edit);
    if (length <= 0 || selectionEnd == 0) return 0;

    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    const int count = std::clamp<int>(static_cast<int>(selectionEnd), 0, length);
    if (count <= 0) return 0;

    HDC dc = GetDC(edit);
    if (!dc) return 0;
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    SIZE extent{};
    GetTextExtentPoint32W(dc, text.c_str(), count, &extent);
    if (oldFont) SelectObject(dc, oldFont);
    ReleaseDC(edit, dc);
    return static_cast<int>(std::max(0L, extent.cx));
}

class ImeAnchorBusyScope final {
public:
    ImeAnchorBusyScope() { gImeAnchorBusy = true; }
    ~ImeAnchorBusyScope() { gImeAnchorBusy = false; }

    ImeAnchorBusyScope(const ImeAnchorBusyScope&) = delete;
    ImeAnchorBusyScope& operator=(const ImeAnchorBusyScope&) = delete;
};

inline void EnsureImeProxyGeometry(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit)) return;

    // Microsoft Pinyin/TSF samples the focused HWND geometry during focus and candidate
    // creation. SearchWindow's legacy 1x1 keyboard proxy is therefore unsafe even for a
    // visually custom-rendered field. Keep the native EDIT permanently aligned with the
    // DirectWrite text rectangle; SearchWindow still suppresses EDIT painting.
    SetWindowPos(
        edit, nullptr,
        kVisibleEditLeft, kImeProxyTop,
        kVisibleEditRight - kVisibleEditLeft, kImeProxyHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // SearchWindow draws the authoritative caret itself. The native caret remains hidden, but
    // its actual position is updated below so TSF/GetGUIThreadInfo still sees useful geometry.
    HideCaret(edit);
}

inline void AnchorImeToVisibleCaret(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit) || gImeAnchorBusy) return;
    if (GetFocus() != edit) return;

    ImeAnchorBusyScope busyScope;
    EnsureImeProxyGeometry(edit);

    const int proxyWidth = kVisibleEditRight - kVisibleEditLeft;
    const int caretX = std::clamp(MeasureCaretOffset(edit), 0, proxyWidth - 4);

    // Modern Microsoft Pinyin is TSF-backed and can use the Win32 thread caret rectangle even
    // when IMM32 positioning calls are present. Publish the same caret geometry that MiaoDesk
    // draws so the TSF composition UI does not fall back to the monitor origin.
    SetCaretPos(caretX, 6);

    HIMC context = ImmGetContext(edit);
    if (!context) return;

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = POINT{caretX, 6};
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = POINT{caretX, kImeProxyHeight};
    candidate.rcArea = RECT{0, 0, proxyWidth, kImeProxyHeight};
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(edit, context);
}

inline void RequestImeAnchor(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit) || gImeAnchorBusy || gImeAnchorPending) return;
    gImeAnchorPending = true;
    if (!PostMessageW(edit, kDeferredImeAnchorMessage, 0, 0))
        gImeAnchorPending = false;
}

inline LRESULT CALLBACK SearchImeCallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message) {
            if (IsMiaoDeskSearchEdit(message->hwnd)) {
                if (message->message == kDeferredImeAnchorMessage) {
                    gImeAnchorPending = false;
                    AnchorImeToVisibleCaret(message->hwnd);
                } else {
                    switch (message->message) {
                    case WM_SETFOCUS:
                        // Geometry must already be correct while the EDIT/TSF focus transaction
                        // is running. SetWindowPos is safe here; IMM32 calls remain deferred.
                        EnsureImeProxyGeometry(message->hwnd);
                        RequestImeAnchor(message->hwnd);
                        break;
                    case WM_KEYUP:
                    case WM_CHAR:
                    case WM_IME_STARTCOMPOSITION:
                    case WM_IME_COMPOSITION:
                    case WM_INPUTLANGCHANGE:
                        RequestImeAnchor(message->hwnd);
                        break;
                    case WM_IME_NOTIFY:
                        // Reposition only after the IME has opened its candidate UI. Never call
                        // ImmSet* synchronously from this notification: RequestImeAnchor posts to
                        // the next message turn and gImeAnchorBusy blocks setter feedback loops.
                        if (message->wParam == IMN_OPENCANDIDATE ||
                            message->wParam == IMN_CHANGECANDIDATE)
                            RequestImeAnchor(message->hwnd);
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

inline LRESULT CALLBACK SearchImeCallWndRetProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
        if (message && message->message == WM_SIZE && IsMiaoDeskSearchWindow(message->hwnd)) {
            // SearchWindow's WM_SIZE handler still collapses the infrastructure EDIT to 1x1.
            // Restore the real rectangle immediately after that handler returns, before TSF can
            // consume another queued input/candidate message.
            const HWND edit = GetDlgItem(message->hwnd, kSearchEditControlId);
            if (edit) {
                EnsureImeProxyGeometry(edit);
                if (GetFocus() == edit) RequestImeAnchor(edit);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class SearchImeAnchorBridge final {
public:
    SearchImeAnchorBridge()
        : callHook_(SetWindowsHookExW(
              WH_CALLWNDPROC, SearchImeCallWndProc, nullptr, GetCurrentThreadId())),
          returnHook_(SetWindowsHookExW(
              WH_CALLWNDPROCRET, SearchImeCallWndRetProc, nullptr, GetCurrentThreadId())) {}

    ~SearchImeAnchorBridge() {
        if (returnHook_) UnhookWindowsHookEx(returnHook_);
        if (callHook_) UnhookWindowsHookEx(callHook_);
    }

    SearchImeAnchorBridge(const SearchImeAnchorBridge&) = delete;
    SearchImeAnchorBridge& operator=(const SearchImeAnchorBridge&) = delete;

private:
    HHOOK callHook_{};
    HHOOK returnHook_{};
};

// SearchWindow is created and pumped on the executable's startup/UI thread. Keeping this
// bridge inline makes it process-local and guarantees one pair of thread hooks across TUs.
inline SearchImeAnchorBridge gSearchImeAnchorBridge;

} // namespace miaodesk::search_ime_detail
