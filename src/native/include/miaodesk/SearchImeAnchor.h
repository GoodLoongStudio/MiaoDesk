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

inline bool IsMiaoDeskSearchEdit(HWND edit) {
    if (!edit || GetDlgCtrlID(edit) != kSearchEditControlId) return false;
    const HWND parent = GetParent(edit);
    if (!parent) return false;
    wchar_t className[128]{};
    if (!GetClassNameW(parent, className, static_cast<int>(std::size(className)))) return false;
    return wcscmp(className, L"MiaoDesk.Native.SearchWindow") == 0;
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

inline void AnchorImeToVisibleCaret(HWND edit) {
    if (!IsMiaoDeskSearchEdit(edit)) return;
    const HWND parent = GetParent(edit);
    if (!parent) return;

    const int caretX = std::clamp(
        kVisibleEditLeft + MeasureCaretOffset(edit),
        kVisibleEditLeft,
        kVisibleEditRight - 4);

    // The visible search text is DirectWrite-rendered by the parent. The real EDIT is a
    // 1x1 keyboard/IME proxy, so its native caret has no useful screen position. Translate
    // the visible caret back into the proxy EDIT's client space before talking to IMM32.
    POINT caret{caretX, 40};
    MapWindowPoints(parent, edit, &caret, 1);

    RECT exclusion{kVisibleEditLeft, 5, kVisibleEditRight, kVisibleBarHeight - 4};
    MapWindowPoints(parent, edit, reinterpret_cast<POINT*>(&exclusion), 2);

    HIMC context = ImmGetContext(edit);
    if (!context) return;

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = caret;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = caret;
    candidate.rcArea = exclusion;
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(edit, context);
}

inline LRESULT CALLBACK SearchImeCallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message && IsMiaoDeskSearchEdit(message->hwnd)) {
            switch (message->message) {
            case WM_SETFOCUS:
            case WM_KEYUP:
            case WM_CHAR:
            case WM_IME_STARTCOMPOSITION:
            case WM_IME_COMPOSITION:
            case WM_IME_NOTIFY:
            case WM_INPUTLANGCHANGE:
                AnchorImeToVisibleCaret(message->hwnd);
                break;
            default:
                break;
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
