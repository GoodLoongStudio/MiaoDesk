#include <commdlg.h>

namespace turingdesk {
namespace {

constexpr int kConversationInputControlId = 3102;
constexpr UINT kConversationPiActivityMessage = WM_APP + 34;
constexpr wchar_t kConversationCoreProcProperty[] = L"TuringDesk.Conversation.CoreProc";

struct ConversationActivityPayload {
    PiActivityEvent event;
};

void AppendAttachmentToInput(HWND input, const std::wstring& path) {
    const int length = GetWindowTextLengthW(input);
    std::wstring current(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(input, current.data(), length + 1);
    current.resize(static_cast<std::size_t>(length));
    if (!current.empty() && current.back() != L'\n') current += L"\r\n";
    current += L"附件：\"" + path + L"\"\r\n";
    SetWindowTextW(input, current.c_str());
    SendMessageW(input, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SetFocus(input);
}

void PickConversationAttachment(HWND panel) {
    HWND input = GetDlgItem(panel, kConversationInputControlId);
    if (!input) return;

    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = panel;
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter = L"所有文件\0*.*\0文档和图片\0*.txt;*.md;*.pdf;*.doc;*.docx;*.ppt;*.pptx;*.xls;*.xlsx;*.png;*.jpg;*.jpeg;*.webp\0\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    HMODULE module = LoadLibraryW(L"comdlg32.dll");
    if (!module) return;
    using Picker = BOOL(WINAPI*)(LPOPENFILENAMEW);
    const auto picker = reinterpret_cast<Picker>(GetProcAddress(module, "GetOpenFileNameW"));
    const BOOL picked = picker ? picker(&dialog) : FALSE;
    FreeLibrary(module);
    if (picked && path[0]) AppendAttachmentToInput(input, path);
}

void StartWindowsVoiceTyping(HWND panel) {
    HWND input = GetDlgItem(panel, kConversationInputControlId);
    if (!input) return;
    SetForegroundWindow(panel);
    SetFocus(input);

    INPUT keys[4]{};
    keys[0].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_LWIN;
    keys[1].type = INPUT_KEYBOARD;
    keys[1].ki.wVk = L'H';
    keys[2] = keys[1];
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3] = keys[0];
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(std::size(keys)), keys, sizeof(INPUT));
}

void EnsureConversationInputOverlay(HINSTANCE) {
    HWND panel = FindWindowW(L"TuringDesk.Native.ConversationPanel", nullptr);
    if (!panel) return;
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(panel, GWLP_USERDATA));
    if (state) PositionConversationInputProxy(*state);
}

void ApplySemanticActivity(ConversationState& state, const PiActivityEvent& event) {
    state.activityLogOffset = CurrentPiLogSize();
    switch (event.kind) {
    case PiActivityKind::Understanding:
        state.activity.visible = true;
        state.activity.failed = false;
        state.activity.title = L"正在理解你的请求";
        state.activity.detail = L"Pi Agent 正在分析并准备执行";
        break;
    case PiActivityKind::ToolStarted:
        UpdateToolStart(state, event.toolName);
        return;
    case PiActivityKind::ToolFinished:
        UpdateToolEnd(state, event.toolName, event.error);
        return;
    case PiActivityKind::Retrying:
        state.activity.visible = true;
        state.activity.failed = false;
        state.activity.title = L"正在重试";
        state.activity.detail = event.message.empty() ? L"服务暂时没有响应，图灵正在自动恢复" : event.message;
        break;
    case PiActivityKind::Recovered:
        state.activity.visible = true;
        state.activity.failed = false;
        state.activity.title = L"连接已恢复";
        state.activity.detail = L"继续执行你的任务";
        break;
    case PiActivityKind::Succeeded:
        if (!state.activity.steps.empty()) {
            state.activity.visible = true;
            state.activity.terminal = true;
            state.activity.failed = false;
            state.activity.title = L"任务完成";
            state.activity.detail = L"所有操作均已反馈";
        }
        break;
    case PiActivityKind::Failed:
        state.activity.visible = true;
        state.activity.terminal = true;
        state.activity.failed = true;
        state.activity.title = L"任务遇到问题";
        state.activity.detail = event.message.empty() ? L"图灵未能完成这次任务" : event.message;
        break;
    case PiActivityKind::Cancelled:
        state.activity.visible = true;
        state.activity.terminal = true;
        state.activity.failed = true;
        state.activity.title = L"已停止";
        state.activity.detail = L"任务已由你取消";
        break;
    }
    InvalidateConversation(state, true);
}

bool KeepStreamingDelta(ConversationState& state, UiMessage& payload) {
    if (!state.streaming.empty() || payload.text.empty()) return !payload.text.empty();
    const auto firstVisible = std::find_if(payload.text.begin(), payload.text.end(), [](wchar_t ch) {
        return !std::iswspace(ch);
    });
    if (firstVisible == payload.text.end()) return false;
    payload.text.erase(payload.text.begin(), firstVisible);
    return !payload.text.empty();
}

LRESULT CALLBACK ConversationOverlayPanelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kConversationCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == kConversationPiActivityMessage) {
        std::unique_ptr<ConversationActivityPayload> payload(reinterpret_cast<ConversationActivityPayload*>(lParam));
        if (payload && state && state->activeRuntime == ActiveRuntime::Pi) {
            ApplySemanticActivity(*state, payload->event);
            RenderConversationLayerSurface(*state);
        }
        return 0;
    }

    if (message == kDeltaMessage && state) {
        auto* payload = reinterpret_cast<UiMessage*>(lParam);
        if (payload && !KeepStreamingDelta(*state, *payload)) {
            delete payload;
            return 0;
        }
        const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
        RenderConversationLayerSurface(*state);
        return result;
    }

    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (state) RenderConversationLayerSurface(*state);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;

    if (message == WM_NCHITTEST && state) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!ConversationPointInsideRoundedPanel(hwnd, point, Px(*state, 24))) return HTTRANSPARENT;
    }

    if (message == WM_LBUTTONUP && state) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointIn(gConversationSurface.attachRect, point)) {
            PickConversationAttachment(hwnd);
            RenderConversationLayerSurface(*state);
            return 0;
        }
        if (PointIn(gConversationSurface.micRect, point)) {
            StartWindowsVoiceTyping(hwnd);
            return 0;
        }
    }

    if (message == WM_SETCURSOR && state) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        if (PointIn(gConversationSurface.attachRect, point) || PointIn(gConversationSurface.micRect, point)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
    }

    // Semantic events are the UI source of truth. Keep pi-runtime.log for diagnostics only.
    if (message == WM_TIMER && wParam == kActivityTimer) return 0;

    if (message == WM_NCDESTROY) {
        gPiRuntime.SetActivityCallback({});
        ReleaseConversationLayerSurface();
        RemovePropW(hwnd, kConversationCoreProcProperty);
        return CallWindowProcW(core, hwnd, message, wParam, lParam);
    }

    const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
    if (!state) return result;

    if (message == WM_SIZE) {
        PositionConversationInputProxy(*state);
        RenderConversationLayerSurface(*state);
    } else if (message == WM_MOUSEWHEEL ||
               message == kPiDoneMessage || message == kDirectDoneMessage ||
               (message == WM_COMMAND && LOWORD(wParam) == kConversationInputControlId && HIWORD(wParam) == EN_CHANGE)) {
        RenderConversationLayerSurface(*state);
    }
    return result;
}

void EnsureConversationActivityBridge(HWND panel) {
    if (!panel) return;
    if (!GetPropW(panel, kConversationCoreProcProperty)) {
        const auto core = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(panel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationOverlayPanelProc)));
        if (core) SetPropW(panel, kConversationCoreProcProperty, reinterpret_cast<HANDLE>(core));
    }

    gPiRuntime.SetActivityCallback([panel](PiActivityEvent event) {
        auto payload = std::make_unique<ConversationActivityPayload>();
        payload->event = std::move(event);
        if (PostMessageW(panel, kConversationPiActivityMessage, 0, reinterpret_cast<LPARAM>(payload.get()))) payload.release();
    });
}

} // namespace
} // namespace turingdesk
