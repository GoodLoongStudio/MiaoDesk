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

    // Windows 11 Microsoft Pinyin/TSF still derives parts of its composition UI from the
    // focused HWND geometry even when IMM32 candidate coordinates are provided. Keeping the
    // keyboard proxy at 1x1 makes the pinyin pre-edit box fall back to the monitor origin.
    // Give the native EDIT the same real geometry as the visible DirectWrite text field while
    // SearchWindow continues suppressing its paint. This keeps IME geometry native without
    // introducing a second visible rectangle or duplicate text renderer.
    SetWindowPos(
        edit, nullptr,
        kVisibleEditLeft, kImeProxyTop,
        kVisibleEditRight - kVisibleEditLeft, kImeProxyHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // SearchWindow draws the authoritative caret itself.
    HideCaret(edit);
}

inline void AnchorImeToVisibleCaret(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit) || gImeAnchorBusy) return;
    if (GetFocus() != edit) return;

    ImeAnchorBusyScope busyScope;
    EnsureImeProxyGeometry(edit);

    const int proxyWidth = kVisibleEditRight - kVisibleEditLeft;
    const int caretX = std::clamp(MeasureCaretOffset(edit), 0, proxyWidth - 4);

    HIMC context = ImmGetContext(edit);
    if (!context) return;

    // Force the phonetic composition UI to the real caret inside the search field instead of
    // allowing Microsoft Pinyin to choose the screen origin from the historical 1x1 proxy.
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = POINT{caretX, 6};
    ImmSetCompositionWindow(context, &composition);

    // Keep the candidate strip immediately below the search text area. CFS_EXCLUDE lets the
    // system choose left/right placement near screen edges while guaranteeing it does not cover
    // the field itself.
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
                    case WM_KEYUP:
                    case WM_CHAR:
                    case WM_IME_STARTCOMPOSITION:
                    case WM_IME_COMPOSITION:
                    case WM_INPUTLANGCHANGE:
                        RequestImeAnchor(message->hwnd);
                        break;
                    default:
                        break;
                    }
                }
            } else if (message->message == WM_SIZE && IsMiaoDeskSearchWindow(message->hwnd)) {
                // SearchWindow historically shrinks the native proxy during resize. Restore the
                // real IME geometry asynchronously after its WM_SIZE handler has completed.
                const HWND edit = GetDlgItem(message->hwnd, kSearchEditControlId);
                if (edit && GetFocus() == edit) RequestImeAnchor(edit);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class SearchImeAnchorBridge final {
public:
    SearchImeAnchorBridge()
        : hook_(SetWindowsHookExW(
              WH_CALLWNDPROC, SearchImeCallWndProc, nullptr, GetCurrentThreadId())) {}

    ~SearchImeAnchorBridge() {
        if (hook_) UnhookWindowsHookEx(hook_);
    }

    SearchImeAnchorBridge(const SearchImeAnchorBridge&) = delete;
    SearchImeAnchorBridge& operator=(const SearchImeAnchorBridge&) = delete;

private:
    HHOOK hook_{};
};

// SearchWindow is created and pumped on the executable's startup/UI thread. Keeping this
// bridge inline makes it process-local and guarantees a single thread hook across TUs.
inline SearchImeAnchorBridge gSearchImeAnchorBridge;

} // namespace miaodesk::search_ime_detail
