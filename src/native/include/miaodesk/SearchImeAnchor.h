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

constexpr int kConversationEditControlId = 3102;
constexpr UINT kDeferredConversationImeAnchorMessage = WM_APP + 0x2A2;
constexpr wchar_t kConversationImePaintCoreProcProperty[] =
    L"MiaoDesk.Conversation.ImePaintCoreProc";

inline thread_local bool gImeAnchorBusy = false;
inline thread_local bool gImeAnchorPending = false;
inline thread_local bool gConversationImeAnchorBusy = false;
inline thread_local bool gConversationImeAnchorPending = false;
inline thread_local bool gConversationImeGeometryBusy = false;

inline bool HasWindowClass(HWND hwnd, const wchar_t* expected) {
    if (!hwnd || !expected) return false;
    wchar_t className[128]{};
    if (!GetClassNameW(hwnd, className, static_cast<int>(std::size(className)))) return false;
    return wcscmp(className, expected) == 0;
}

inline bool IsMiaoDeskSearchWindow(HWND hwnd) {
    return HasWindowClass(hwnd, L"MiaoDesk.Native.SearchWindow");
}

inline bool IsMiaoDeskSearchEdit(HWND edit) {
    if (!edit || GetDlgCtrlID(edit) != kSearchEditControlId) return false;
    return IsMiaoDeskSearchWindow(GetParent(edit));
}

inline bool IsMiaoDeskConversationWindow(HWND hwnd) {
    return HasWindowClass(hwnd, L"MiaoDesk.Native.ConversationPanel");
}

inline bool IsMiaoDeskConversationEdit(HWND edit) {
    if (!edit || GetDlgCtrlID(edit) != kConversationEditControlId) return false;
    return IsMiaoDeskConversationWindow(GetParent(edit));
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

class ConversationImeAnchorBusyScope final {
public:
    ConversationImeAnchorBusyScope() { gConversationImeAnchorBusy = true; }
    ~ConversationImeAnchorBusyScope() { gConversationImeAnchorBusy = false; }

    ConversationImeAnchorBusyScope(const ConversationImeAnchorBusyScope&) = delete;
    ConversationImeAnchorBusyScope& operator=(const ConversationImeAnchorBusyScope&) = delete;
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
    HideCaret(edit);

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
    HideCaret(edit);
}

inline void RequestImeAnchor(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit) || gImeAnchorBusy || gImeAnchorPending) return;
    gImeAnchorPending = true;
    if (!PostMessageW(edit, kDeferredImeAnchorMessage, 0, 0))
        gImeAnchorPending = false;
}

inline int ConversationPx(HWND parent, int value) {
    UINT dpi = parent ? GetDpiForWindow(parent) : 96;
    if (dpi == 0) dpi = 96;
    return MulDiv(value, static_cast<int>(dpi), 96);
}

inline RECT ConversationEditRect(HWND edit) {
    RECT result{};
    const HWND parent = edit ? GetParent(edit) : nullptr;
    if (!parent) return result;

    RECT client{};
    GetClientRect(parent, &client);
    const int inputTop = client.bottom - ConversationPx(parent, 64) - ConversationPx(parent, 16);
    const int left = ConversationPx(parent, 38);
    const int top = inputTop + ConversationPx(parent, 13);
    const int width = std::max(
        ConversationPx(parent, 120),
        client.right - ConversationPx(parent, 76) - ConversationPx(parent, 62));
    const int height = ConversationPx(parent, 38);
    result = RECT{left, top, left + width, top + height};
    return result;
}

inline LRESULT CALLBACK ConversationImePaintProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(
        GetPropW(hwnd, kConversationImePaintCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);

    // ConversationPanel is a per-pixel-alpha custom surface. The native EDIT exists only to
    // give Windows keyboard/TSF infrastructure a truthful focus and caret geometry. Never let
    // the EDIT paint text/background over the DirectWrite input field.
    if (message == WM_PAINT) {
        ValidateRect(hwnd, nullptr);
        return 0;
    }
    if (message == WM_ERASEBKGND || message == WM_PRINTCLIENT) return 1;

    if (message == WM_NCDESTROY) {
        const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
        RemovePropW(hwnd, kConversationImePaintCoreProcProperty);
        return result;
    }
    return CallWindowProcW(core, hwnd, message, wParam, lParam);
}

inline void EnsureConversationImePaintSubclass(HWND edit) {
    if (!IsMiaoDeskConversationEdit(edit) ||
        GetPropW(edit, kConversationImePaintCoreProcProperty)) return;

    const auto core = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(edit, GWLP_WNDPROC));
    if (!core || core == ConversationImePaintProc) return;
    if (!SetPropW(edit, kConversationImePaintCoreProcProperty, reinterpret_cast<HANDLE>(core)))
        return;

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(
        edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationImePaintProc));
    if (!previous && GetLastError() != ERROR_SUCCESS)
        RemovePropW(edit, kConversationImePaintCoreProcProperty);
}

inline bool ConversationEditAlreadyHasRealGeometry(HWND edit, const RECT& desired) {
    RECT current{};
    if (!GetWindowRect(edit, &current)) return false;
    POINT points[2]{{current.left, current.top}, {current.right, current.bottom}};
    const HWND parent = GetParent(edit);
    if (!parent || MapWindowPoints(nullptr, parent, points, 2) == 0) {
        if (!parent) return false;
    }
    current = RECT{points[0].x, points[0].y, points[1].x, points[1].y};
    return current.left == desired.left && current.top == desired.top &&
           current.right == desired.right && current.bottom == desired.bottom;
}

inline void EnsureConversationImeGeometry(HWND edit) {
    if (!IsMiaoDeskConversationEdit(edit) || gConversationImeGeometryBusy) return;

    EnsureConversationImePaintSubclass(edit);
    const RECT desired = ConversationEditRect(edit);
    if (desired.right <= desired.left || desired.bottom <= desired.top) return;

    if (!ConversationEditAlreadyHasRealGeometry(edit, desired)) {
        gConversationImeGeometryBusy = true;
        SetWindowPos(
            edit, nullptr,
            desired.left, desired.top,
            desired.right - desired.left, desired.bottom - desired.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        gConversationImeGeometryBusy = false;
    }
    HideCaret(edit);
}

inline POINT ConversationNativeCaretPoint(HWND edit) {
    POINT caret{};
    if (!edit) return caret;

    RECT client{};
    GetClientRect(edit, &client);
    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessageW(edit, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&selectionStart),
                 reinterpret_cast<LPARAM>(&selectionEnd));

    const LRESULT position = SendMessageW(
        edit, EM_POSFROMCHAR, static_cast<WPARAM>(selectionEnd), 0);
    if (position != -1) {
        caret.x = static_cast<short>(LOWORD(position));
        caret.y = static_cast<short>(HIWORD(position));
    } else {
        caret.x = MeasureCaretOffset(edit);
        caret.y = 0;
    }

    caret.x = std::clamp<LONG>(caret.x, 0, std::max<LONG>(0, client.right - 2));
    caret.y = std::clamp<LONG>(caret.y, 0, std::max<LONG>(0, client.bottom - 2));
    return caret;
}

inline void AnchorConversationImeToNativeCaret(HWND edit) {
    if (!IsMiaoDeskConversationEdit(edit) || gConversationImeAnchorBusy) return;
    if (GetFocus() != edit) return;

    ConversationImeAnchorBusyScope busyScope;
    EnsureConversationImeGeometry(edit);

    RECT client{};
    GetClientRect(edit, &client);
    if (client.right <= 0 || client.bottom <= 0) return;

    const POINT caret = ConversationNativeCaretPoint(edit);

    // The old ConversationPanel code still attempts to move this EDIT to a 1x1 proxy at the
    // visual caret. Restore a full-sized input HWND and publish the real Win32 caret instead.
    // This is what modern Microsoft Pinyin/TSF reads through GetGUIThreadInfo.
    SetCaretPos(caret.x, caret.y);
    HideCaret(edit);

    HIMC context = ImmGetContext(edit);
    if (!context) return;

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = caret;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = POINT{caret.x, client.bottom};
    candidate.rcArea = client;
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(edit, context);
    HideCaret(edit);
}

inline void RequestConversationImeAnchor(HWND edit) {
    if (!IsMiaoDeskConversationEdit(edit) || gConversationImeAnchorBusy ||
        gConversationImeAnchorPending) return;
    gConversationImeAnchorPending = true;
    if (!PostMessageW(edit, kDeferredConversationImeAnchorMessage, 0, 0))
        gConversationImeAnchorPending = false;
}

inline void HandleConversationEditMessageBefore(const CWPSTRUCT& message) {
    if (!IsMiaoDeskConversationEdit(message.hwnd)) return;

    if (message.message == kDeferredConversationImeAnchorMessage) {
        gConversationImeAnchorPending = false;
        AnchorConversationImeToNativeCaret(message.hwnd);
        return;
    }

    switch (message.message) {
    case WM_SETFOCUS:
        // TSF samples the focused HWND during the focus transaction, so correct the geometry
        // synchronously before the native EDIT continues processing WM_SETFOCUS.
        EnsureConversationImeGeometry(message.hwnd);
        RequestConversationImeAnchor(message.hwnd);
        break;
    case WM_KEYUP:
    case WM_CHAR:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_INPUTLANGCHANGE:
        RequestConversationImeAnchor(message.hwnd);
        break;
    case WM_IME_NOTIFY:
        if (message.wParam == IMN_OPENCANDIDATE ||
            message.wParam == IMN_CHANGECANDIDATE)
            RequestConversationImeAnchor(message.hwnd);
        break;
    default:
        break;
    }
}

inline void HandleConversationEditMessageAfter(const CWPRETSTRUCT& message) {
    if (!IsMiaoDeskConversationEdit(message.hwnd)) return;

    if (message.message == WM_WINDOWPOSCHANGED && !gConversationImeGeometryBusy) {
        // Legacy ConversationPanel code repeatedly collapses the native EDIT to 1x1. Repair it
        // immediately after that SetWindowPos/MoveWindow completes, then defer IMM32 anchoring
        // until the surrounding input message has finished changing IME state.
        EnsureConversationImeGeometry(message.hwnd);
        if (GetFocus() == message.hwnd) RequestConversationImeAnchor(message.hwnd);
        return;
    }

    switch (message.message) {
    case WM_SETFOCUS:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_INPUTLANGCHANGE:
        HideCaret(message.hwnd);
        EnsureConversationImeGeometry(message.hwnd);
        if (GetFocus() == message.hwnd) RequestConversationImeAnchor(message.hwnd);
        break;
    default:
        break;
    }
}

inline LRESULT CALLBACK SearchImeCallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message) {
            HandleConversationEditMessageBefore(*message);

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
        if (message) {
            HandleConversationEditMessageAfter(*message);
        }

        if (message && IsMiaoDeskSearchEdit(message->hwnd)) {
            switch (message->message) {
            case WM_SETFOCUS:
            case WM_KEYUP:
            case WM_CHAR:
            case WM_IME_STARTCOMPOSITION:
            case WM_IME_COMPOSITION:
            case WM_IME_ENDCOMPOSITION:
            case WM_INPUTLANGCHANGE:
                // DefWindowProc/EDIT creates and may reposition its own Win32 caret after our
                // pre-dispatch hook. Hide it again after the native control has finished so the
                // only visible caret is SearchWindow's DirectWrite-aligned blue caret.
                HideCaret(message->hwnd);
                if (GetFocus() == message->hwnd) RequestImeAnchor(message->hwnd);
                break;
            default:
                break;
            }
        }

        if (message && message->message == WM_SIZE && IsMiaoDeskSearchWindow(message->hwnd)) {
            // SearchWindow's WM_SIZE handler still collapses the infrastructure EDIT to 1x1.
            // Restore the real rectangle immediately after that handler returns, before TSF can
            // consume another queued input/candidate message.
            const HWND edit = GetDlgItem(message->hwnd, kSearchEditControlId);
            if (edit) {
                EnsureImeProxyGeometry(edit);
                HideCaret(edit);
                if (GetFocus() == edit) RequestImeAnchor(edit);
            }
        }

        if (message && message->message == WM_SIZE &&
            IsMiaoDeskConversationWindow(message->hwnd)) {
            const HWND edit = GetDlgItem(message->hwnd, kConversationEditControlId);
            if (edit) {
                EnsureConversationImeGeometry(edit);
                HideCaret(edit);
                if (GetFocus() == edit) RequestConversationImeAnchor(edit);
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

// SearchWindow and ConversationPanel are created and pumped on the executable's startup/UI
// thread. Keeping this bridge inline makes it process-local and guarantees one pair of thread
// hooks across TUs for both custom input surfaces.
inline SearchImeAnchorBridge gSearchImeAnchorBridge;

} // namespace miaodesk::search_ime_detail