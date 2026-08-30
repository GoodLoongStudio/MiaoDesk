#pragma once

#include <windows.h>
#include <imm.h>
#include <algorithm>
#include <iterator>
#include <string>

#pragma comment(lib, "imm32.lib")

namespace miaodesk::input_ime_detail {

// InputImeAnchor is the shared Windows IME/TSF infrastructure for custom-rendered
// MiaoDesk text fields. Surface-specific geometry remains explicitly named Search
// or Conversation; only the message hook/anchor framework is shared.
constexpr int kSearchEditControlId = 100;
constexpr int kSearchEditLeft = 52;
constexpr int kSearchEditRight = 594;
constexpr int kSearchEditTop = 13;
constexpr int kSearchEditHeight = 30;
constexpr UINT kDeferredSearchImeAnchorMessage = WM_APP + 0x2A1;

constexpr int kConversationEditControlId = 3102;
constexpr UINT kDeferredConversationImeAnchorMessage = WM_APP + 0x2A2;
constexpr wchar_t kConversationImePaintCoreProcProperty[] =
    L"MiaoDesk.Conversation.ImePaintCoreProc";

inline thread_local bool gSearchImeAnchorBusy = false;
inline thread_local bool gSearchImeAnchorPending = false;
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

inline int MeasureEditCaretOffset(HWND edit) {
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

inline bool HasImeComposition(HWND edit) {
    if (!edit) return false;
    HIMC context = ImmGetContext(edit);
    if (!context) return false;
    const LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
    ImmReleaseContext(edit, context);
    return bytes > 0;
}

class SearchImeAnchorBusyScope final {
public:
    SearchImeAnchorBusyScope() { gSearchImeAnchorBusy = true; }
    ~SearchImeAnchorBusyScope() { gSearchImeAnchorBusy = false; }

    SearchImeAnchorBusyScope(const SearchImeAnchorBusyScope&) = delete;
    SearchImeAnchorBusyScope& operator=(const SearchImeAnchorBusyScope&) = delete;
};

class ConversationImeAnchorBusyScope final {
public:
    ConversationImeAnchorBusyScope() { gConversationImeAnchorBusy = true; }
    ~ConversationImeAnchorBusyScope() { gConversationImeAnchorBusy = false; }

    ConversationImeAnchorBusyScope(const ConversationImeAnchorBusyScope&) = delete;
    ConversationImeAnchorBusyScope& operator=(const ConversationImeAnchorBusyScope&) = delete;
};

// ---- Search-specific surface profile ---------------------------------------------------------

inline void EnsureSearchImeGeometry(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit)) return;

    // SearchWindow is DirectWrite-rendered. The native EDIT is input infrastructure, but
    // Microsoft Pinyin/TSF still samples the focused HWND rectangle. Keep it aligned with the
    // real search text field rather than the historical 1x1 keyboard proxy.
    SetWindowPos(
        edit, nullptr,
        kSearchEditLeft, kSearchEditTop,
        kSearchEditRight - kSearchEditLeft, kSearchEditHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    HideCaret(edit);
}

inline void AnchorSearchImeToVisibleCaret(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit) || gSearchImeAnchorBusy) return;
    if (GetFocus() != edit) return;

    SearchImeAnchorBusyScope busyScope;
    EnsureSearchImeGeometry(edit);

    const int width = kSearchEditRight - kSearchEditLeft;
    const int caretX = std::clamp(MeasureEditCaretOffset(edit), 0, width - 4);

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
    candidate.ptCurrentPos = POINT{caretX, kSearchEditHeight};
    candidate.rcArea = RECT{0, 0, width, kSearchEditHeight};
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(edit, context);
    HideCaret(edit);
}

inline void RequestSearchImeAnchor(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit) || gSearchImeAnchorBusy || gSearchImeAnchorPending) return;
    gSearchImeAnchorPending = true;
    if (!PostMessageW(edit, kDeferredSearchImeAnchorMessage, 0, 0))
        gSearchImeAnchorPending = false;
}

// ---- Conversation-specific surface profile ---------------------------------------------------

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
    const int inputTop =
        static_cast<int>(client.bottom) - ConversationPx(parent, 64) - ConversationPx(parent, 16);
    const int left = ConversationPx(parent, 38);
    const int top = inputTop + ConversationPx(parent, 13);
    const int availableWidth =
        static_cast<int>(client.right) - ConversationPx(parent, 76) - ConversationPx(parent, 62);
    const int width = std::max(ConversationPx(parent, 120), availableWidth);
    const int height = ConversationPx(parent, 38);
    result = RECT{left, top, left + width, top + height};
    return result;
}

inline LRESULT CALLBACK ConversationImePaintProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(
        GetPropW(hwnd, kConversationImePaintCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);

    // Direct2D owns all visible text/caret rendering. The native EDIT only owns keyboard/IME
    // semantics and must never paint a second text field or a native caret.
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
    if (!parent) return false;
    MapWindowPoints(nullptr, parent, points, 2);
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

    const LRESULT position =
        SendMessageW(edit, EM_POSFROMCHAR, static_cast<WPARAM>(selectionEnd), 0);
    if (position != -1) {
        caret.x = static_cast<short>(LOWORD(position));
        caret.y = static_cast<short>(HIWORD(position));
    } else {
        caret.x = MeasureEditCaretOffset(edit);
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

// Explicit surface entry point used by ConversationPanel. This is deliberately not named
// "Search": the framework is InputImeAnchor; the caller-specific behavior remains Conversation.
inline void SyncConversationImeAnchor(HWND edit) {
    if (!IsMiaoDeskConversationEdit(edit)) return;
    EnsureConversationImeGeometry(edit);
    HideCaret(edit);
    if (GetFocus() == edit) RequestConversationImeAnchor(edit);
}

// ---- Shared hook dispatch --------------------------------------------------------------------

inline void HandleSearchEditMessageBefore(const CWPSTRUCT& message) {
    if (!IsMiaoDeskSearchEdit(message.hwnd)) return;

    if (message.message == kDeferredSearchImeAnchorMessage) {
        gSearchImeAnchorPending = false;
        AnchorSearchImeToVisibleCaret(message.hwnd);
        return;
    }

    switch (message.message) {
    case WM_SETFOCUS:
        EnsureSearchImeGeometry(message.hwnd);
        RequestSearchImeAnchor(message.hwnd);
        break;
    case WM_KEYUP:
    case WM_CHAR:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_INPUTLANGCHANGE:
        RequestSearchImeAnchor(message.hwnd);
        break;
    case WM_IME_NOTIFY:
        if (message.wParam == IMN_OPENCANDIDATE ||
            message.wParam == IMN_CHANGECANDIDATE)
            RequestSearchImeAnchor(message.hwnd);
        break;
    default:
        break;
    }
}

inline void HandleSearchEditMessageAfter(const CWPRETSTRUCT& message) {
    if (!IsMiaoDeskSearchEdit(message.hwnd)) return;

    if (message.message == WM_WINDOWPOSCHANGED) {
        EnsureSearchImeGeometry(message.hwnd);
        HideCaret(message.hwnd);
        if (GetFocus() == message.hwnd) RequestSearchImeAnchor(message.hwnd);
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
        if (GetFocus() == message.hwnd) RequestSearchImeAnchor(message.hwnd);
        break;
    default:
        break;
    }
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
        EnsureConversationImeGeometry(message.hwnd);
        HideCaret(message.hwnd);
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

inline LRESULT CALLBACK InputImeCallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message) {
            HandleSearchEditMessageBefore(*message);
            HandleConversationEditMessageBefore(*message);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

inline LRESULT CALLBACK InputImeCallWndRetProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
        if (message) {
            HandleSearchEditMessageAfter(*message);
            HandleConversationEditMessageAfter(*message);

            if (message->message == WM_SIZE && IsMiaoDeskSearchWindow(message->hwnd)) {
                const HWND edit = GetDlgItem(message->hwnd, kSearchEditControlId);
                if (edit) {
                    EnsureSearchImeGeometry(edit);
                    HideCaret(edit);
                    if (GetFocus() == edit) RequestSearchImeAnchor(edit);
                }
            }

            if (message->message == WM_SIZE && IsMiaoDeskConversationWindow(message->hwnd)) {
                const HWND edit = GetDlgItem(message->hwnd, kConversationEditControlId);
                if (edit) SyncConversationImeAnchor(edit);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class InputImeAnchorBridge final {
public:
    InputImeAnchorBridge()
        : callHook_(SetWindowsHookExW(
              WH_CALLWNDPROC, InputImeCallWndProc, nullptr, GetCurrentThreadId())),
          returnHook_(SetWindowsHookExW(
              WH_CALLWNDPROCRET, InputImeCallWndRetProc, nullptr, GetCurrentThreadId())) {}

    ~InputImeAnchorBridge() {
        if (returnHook_) UnhookWindowsHookEx(returnHook_);
        if (callHook_) UnhookWindowsHookEx(callHook_);
    }

    InputImeAnchorBridge(const InputImeAnchorBridge&) = delete;
    InputImeAnchorBridge& operator=(const InputImeAnchorBridge&) = delete;

private:
    HHOOK callHook_{};
    HHOOK returnHook_{};
};

// SearchWindow and ConversationPanel live on the same UI thread. One shared input bridge handles
// Win32 caret publication and IME/TSF re-anchoring, while each surface keeps its own geometry.
inline InputImeAnchorBridge gInputImeAnchorBridge;

} // namespace miaodesk::input_ime_detail
