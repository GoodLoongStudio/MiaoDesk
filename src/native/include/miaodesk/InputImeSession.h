#pragma once

#include "miaodesk/InputImeAnchor.h"

#include <windows.h>
#include <imm.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#pragma comment(lib, "imm32.lib")

namespace miaodesk::input_ime_session_detail {

// InputImeSession is the shared composition/input-state half of MiaoDesk's custom text-input
// framework. InputImeAnchor owns geometry; this file owns transient IME text, insertion range,
// result commits and the rule that product shortcuts never steal keys from an active IME.
// Search and Conversation remain separate product surfaces and keep their own business logic.

constexpr wchar_t kInputImeSessionCoreProcProperty[] =
    L"MiaoDesk.InputImeSession.CoreProc";

struct InputImeSessionState {
    bool active{};
    DWORD replaceStart{};
    DWORD replaceEnd{};
    std::wstring composition;
};

inline thread_local std::unordered_map<HWND, InputImeSessionState> gInputImeSessions;

inline bool IsSupportedInput(HWND edit) {
    return input_ime_detail::IsMiaoDeskSearchEdit(edit) ||
           input_ime_detail::IsMiaoDeskConversationEdit(edit);
}

inline std::wstring ReadImeString(HWND edit, DWORD index) {
    std::wstring result;
    if (!edit) return result;
    HIMC context = ImmGetContext(edit);
    if (!context) return result;
    const LONG bytes = ImmGetCompositionStringW(context, index, nullptr, 0);
    if (bytes > 0) {
        result.resize(static_cast<std::size_t>(bytes) / sizeof(wchar_t));
        ImmGetCompositionStringW(
            context, index, result.data(), static_cast<DWORD>(bytes));
    }
    ImmReleaseContext(edit, context);
    return result;
}

inline void ReadCoreSelection(HWND edit, WNDPROC core, DWORD& start, DWORD& end) {
    start = 0;
    end = 0;
    if (!edit || !core) return;
    CallWindowProcW(
        core, edit, EM_GETSEL,
        reinterpret_cast<WPARAM>(&start),
        reinterpret_cast<LPARAM>(&end));
}

inline std::wstring ReadCoreText(HWND edit, WNDPROC core) {
    if (!edit || !core) return {};
    const LRESULT rawLength = CallWindowProcW(core, edit, WM_GETTEXTLENGTH, 0, 0);
    const int length = static_cast<int>(std::max<LRESULT>(0, rawLength));
    std::wstring text(static_cast<std::size_t>(length) + 1u, L'\0');
    if (length > 0) {
        CallWindowProcW(
            core, edit, WM_GETTEXT, static_cast<WPARAM>(length + 1),
            reinterpret_cast<LPARAM>(text.data()));
    }
    text.resize(static_cast<std::size_t>(length));
    return text;
}

inline InputImeSessionState& EnsureSessionState(HWND edit, WNDPROC core) {
    auto [it, inserted] = gInputImeSessions.try_emplace(edit);
    if (inserted && core) {
        ReadCoreSelection(edit, core, it->second.replaceStart, it->second.replaceEnd);
    }
    return it->second;
}

inline InputImeSessionState* FindSessionState(HWND edit) {
    const auto it = gInputImeSessions.find(edit);
    return it == gInputImeSessions.end() ? nullptr : &it->second;
}

inline bool HasActiveComposition(HWND edit) {
    const auto* state = FindSessionState(edit);
    if (state && state->active) return true;
    return input_ime_detail::HasImeComposition(edit);
}

inline void RequestSurfaceAnchor(HWND edit) {
    if (input_ime_detail::IsMiaoDeskSearchEdit(edit))
        input_ime_detail::RequestSearchImeAnchor(edit);
    else if (input_ime_detail::IsMiaoDeskConversationEdit(edit))
        input_ime_detail::RequestConversationImeAnchor(edit);
}

inline void NotifySearchVisibleQuery(HWND edit) {
    if (!input_ime_detail::IsMiaoDeskSearchEdit(edit)) return;
    const HWND parent = GetParent(edit);
    if (!parent) return;

    // SearchWindow already owns query execution through EN_CHANGE. Reuse that business path,
    // but let its ReadText() see the virtual committed+composition text below. Posting avoids
    // doing app/file queries recursively inside WM_IME_COMPOSITION.
    PostMessageW(
        parent, WM_COMMAND,
        MAKEWPARAM(input_ime_detail::kSearchEditControlId, EN_CHANGE),
        reinterpret_cast<LPARAM>(edit));
}

inline void NotifyConversationInputVisual(HWND edit) {
    if (!input_ime_detail::IsMiaoDeskConversationEdit(edit)) return;
    const HWND parent = GetParent(edit);
    if (!parent) return;

    // ConversationPanel already redraws its custom Direct2D input on EN_CHANGE. Reuse that
    // surface-specific path while InputImeSession remains the sole owner of transient IME state.
    PostMessageW(
        parent, WM_COMMAND,
        MAKEWPARAM(input_ime_detail::kConversationEditControlId, EN_CHANGE),
        reinterpret_cast<LPARAM>(edit));
}

inline void NotifySurfaceVisibleInput(HWND edit) {
    NotifySearchVisibleQuery(edit);
    NotifyConversationInputVisual(edit);
}

inline std::wstring VisibleText(HWND edit, WNDPROC core) {
    std::wstring committed = ReadCoreText(edit, core);
    const auto* state = FindSessionState(edit);
    if (!state || !state->active || state->composition.empty()) return committed;

    const std::size_t start = std::min<std::size_t>(state->replaceStart, committed.size());
    const std::size_t end = std::clamp<std::size_t>(
        state->replaceEnd, start, committed.size());
    std::wstring visible;
    visible.reserve(committed.size() - (end - start) + state->composition.size());
    visible.append(committed, 0, start);
    visible += state->composition;
    visible.append(committed, end, std::wstring::npos);
    return visible;
}

inline LRESULT VirtualizeVisibleGetText(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, WNDPROC core) {
    const auto* state = FindSessionState(hwnd);
    if (!state || !state->active || state->composition.empty())
        return CallWindowProcW(core, hwnd, message, wParam, lParam);

    const std::wstring visible = VisibleText(hwnd, core);
    if (message == WM_GETTEXTLENGTH)
        return static_cast<LRESULT>(visible.size());

    const int capacity = static_cast<int>(wParam);
    if (capacity <= 0 || !lParam) return 0;
    const int copyLength = std::min(capacity - 1, static_cast<int>(visible.size()));
    auto* destination = reinterpret_cast<wchar_t*>(lParam);
    if (copyLength > 0)
        std::wmemcpy(destination, visible.data(), static_cast<std::size_t>(copyLength));
    destination[copyLength] = L'\0';
    return copyLength;
}

inline LRESULT VirtualizeSearchSelection(
    HWND hwnd, WPARAM wParam, LPARAM lParam, WNDPROC core) {
    const auto* state = FindSessionState(hwnd);
    if (!state || !state->active || state->composition.empty())
        return CallWindowProcW(core, hwnd, EM_GETSEL, wParam, lParam);

    const DWORD visibleCaret = state->replaceStart +
        static_cast<DWORD>(state->composition.size());
    if (wParam) *reinterpret_cast<DWORD*>(wParam) = visibleCaret;
    if (lParam) *reinterpret_cast<DWORD*>(lParam) = visibleCaret;
    return MAKELRESULT(
        static_cast<WORD>(std::min<DWORD>(visibleCaret, 0xFFFFu)),
        static_cast<WORD>(std::min<DWORD>(visibleCaret, 0xFFFFu)));
}

inline void BeginComposition(HWND hwnd, WNDPROC core) {
    auto& state = EnsureSessionState(hwnd, core);
    state.active = true;
    state.composition.clear();
    ReadCoreSelection(hwnd, core, state.replaceStart, state.replaceEnd);
    NotifySurfaceVisibleInput(hwnd);
    RequestSurfaceAnchor(hwnd);
}

inline void CommitImeResult(HWND hwnd, WNDPROC core, const std::wstring& result) {
    auto& state = EnsureSessionState(hwnd, core);
    const DWORD start = state.replaceStart;
    const DWORD end = state.replaceEnd;

    // Hide the provisional text before the real EDIT emits EN_CHANGE for the committed result.
    state.composition.clear();
    SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
    if (!result.empty()) {
        SendMessageW(
            hwnd, EM_REPLACESEL, TRUE,
            reinterpret_cast<LPARAM>(result.c_str()));
    }

    state.replaceStart = start + static_cast<DWORD>(result.size());
    state.replaceEnd = state.replaceStart;
}

inline void UpdateComposition(HWND hwnd, LPARAM lParam, WNDPROC core) {
    auto& state = EnsureSessionState(hwnd, core);
    if (!state.active) {
        state.active = true;
        ReadCoreSelection(hwnd, core, state.replaceStart, state.replaceEnd);
    }

    if ((lParam & GCS_RESULTSTR) != 0) {
        const std::wstring result = ReadImeString(hwnd, GCS_RESULTSTR);
        CommitImeResult(hwnd, core, result);
    }

    if ((lParam & GCS_COMPSTR) != 0)
        state.composition = ReadImeString(hwnd, GCS_COMPSTR);
    else if ((lParam & GCS_RESULTSTR) != 0)
        state.composition.clear();

    NotifySurfaceVisibleInput(hwnd);
    RequestSurfaceAnchor(hwnd);
}

inline void EndComposition(HWND hwnd) {
    if (auto* state = FindSessionState(hwnd)) {
        state->active = false;
        state->composition.clear();
    }
    NotifySurfaceVisibleInput(hwnd);
    RequestSurfaceAnchor(hwnd);
}

inline bool IsImeOwnedNavigationKey(WPARAM key) {
    switch (key) {
    case VK_RETURN:
    case VK_ESCAPE:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_HOME:
    case VK_END:
    case VK_TAB:
        return true;
    default:
        return false;
    }
}

inline LRESULT CALLBACK InputImeSessionProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(
        GetPropW(hwnd, kInputImeSessionCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);

    const bool search = input_ime_detail::IsMiaoDeskSearchEdit(hwnd);
    const bool conversation = input_ime_detail::IsMiaoDeskConversationEdit(hwnd);

    // Both custom-rendered surfaces consume the same virtual visible string. Search additionally
    // virtualizes EM_GETSEL because its shared anchor measures from visible text; Conversation's
    // anchor intentionally keeps the native committed selection and adds IMM composition width.
    if ((search || conversation) &&
        (message == WM_GETTEXT || message == WM_GETTEXTLENGTH)) {
        return VirtualizeVisibleGetText(hwnd, message, wParam, lParam, core);
    }
    if (search && message == EM_GETSEL)
        return VirtualizeSearchSelection(hwnd, wParam, lParam, core);

    // MiaoDesk custom-renders composition text. Do not forward these messages to the stock EDIT
    // control: doing so lets the stock control create a second inline composition visual and a
    // second caret over the Direct2D surface.
    if (message == WM_IME_STARTCOMPOSITION) {
        BeginComposition(hwnd, core);
        return 0;
    }
    if (message == WM_IME_COMPOSITION) {
        UpdateComposition(hwnd, lParam, core);
        return 0;
    }
    if (message == WM_IME_ENDCOMPOSITION) {
        EndComposition(hwnd);
        return 0;
    }

    // Product-level Enter/Esc/arrow behavior must never steal a key while Microsoft Pinyin (or
    // another IME) owns an active composition/candidate session.
    if (message == WM_KEYDOWN && HasActiveComposition(hwnd) &&
        IsImeOwnedNavigationKey(wParam)) {
        RequestSurfaceAnchor(hwnd);
        return 0;
    }

    if (message == WM_SETTEXT) {
        gInputImeSessions.erase(hwnd);
    }

    // Child EDIT receives mouse input directly once it has real geometry. Make focus ownership
    // explicit so Conversation and Search behave identically after framework refactors.
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
        SetFocus(hwnd);

    const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);

    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_LBUTTONDBLCLK) {
        RequestSurfaceAnchor(hwnd);
        NotifySurfaceVisibleInput(hwnd);
    }

    if (message == WM_KILLFOCUS) {
        gInputImeSessions.erase(hwnd);
        NotifySurfaceVisibleInput(hwnd);
    }

    if (message == WM_NCDESTROY) {
        gInputImeSessions.erase(hwnd);
        RemovePropW(hwnd, kInputImeSessionCoreProcProperty);
    }
    return result;
}

inline void EnsureInputImeSessionSubclass(HWND edit) {
    if (!IsSupportedInput(edit) ||
        GetPropW(edit, kInputImeSessionCoreProcProperty)) return;

    const auto core = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(edit, GWLP_WNDPROC));
    if (!core || core == InputImeSessionProc) return;
    if (!SetPropW(edit, kInputImeSessionCoreProcProperty, reinterpret_cast<HANDLE>(core)))
        return;

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(
        edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InputImeSessionProc));
    if (!previous && GetLastError() != ERROR_SUCCESS)
        RemovePropW(edit, kInputImeSessionCoreProcProperty);
}

inline LRESULT CALLBACK InputImeSessionCallWndRetProc(
    int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
        if (message && message->message == WM_SETFOCUS && IsSupportedInput(message->hwnd)) {
            EnsureInputImeSessionSubclass(message->hwnd);
            RequestSurfaceAnchor(message->hwnd);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class InputImeSessionBridge final {
public:
    InputImeSessionBridge()
        : returnHook_(SetWindowsHookExW(
              WH_CALLWNDPROCRET, InputImeSessionCallWndRetProc,
              nullptr, GetCurrentThreadId())) {}

    ~InputImeSessionBridge() {
        if (returnHook_) UnhookWindowsHookEx(returnHook_);
    }

    InputImeSessionBridge(const InputImeSessionBridge&) = delete;
    InputImeSessionBridge& operator=(const InputImeSessionBridge&) = delete;

private:
    HHOOK returnHook_{};
};

inline InputImeSessionBridge gInputImeSessionBridge;

} // namespace miaodesk::input_ime_session_detail
