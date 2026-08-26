#include <commdlg.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")

namespace turingdesk {
namespace {

constexpr int kConversationInputControlId = 3102;
constexpr UINT kConversationPiActivityMessage = WM_APP + 34;
constexpr UINT_PTR kConversationCaretTimer = 42;
constexpr wchar_t kConversationCoreProcProperty[] = L"TuringDesk.Conversation.CoreProc";
constexpr wchar_t kConversationInputCoreProcProperty[] = L"TuringDesk.Conversation.InputCoreProc";

enum class ConversationPointerMode { None, Move, Resize };

struct ConversationActivityPayload {
    PiActivityEvent event;
};

std::wstring gConversationImeComposition;
ConversationPointerMode gConversationPointerMode{ConversationPointerMode::None};
int gConversationResizeEdge{};
POINT gConversationPointerStart{};
RECT gConversationPointerStartRect{};
RECT gConversationUserRect{};
bool gConversationUserRectValid{};

std::wstring ReadInputCoreText(HWND input, WNDPROC core) {
    if (!input || !core) return {};
    const LRESULT lengthResult = CallWindowProcW(core, input, WM_GETTEXTLENGTH, 0, 0);
    const int length = static_cast<int>((std::max<LRESULT>)(0, lengthResult));
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0) {
        CallWindowProcW(core, input, WM_GETTEXT, static_cast<WPARAM>(length + 1), reinterpret_cast<LPARAM>(text.data()));
    }
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::wstring ReadImeComposition(HWND input) {
    std::wstring result;
    if (!input) return result;
    HIMC context = ImmGetContext(input);
    if (!context) return result;
    const LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
    if (bytes > 0) {
        result.resize(static_cast<std::size_t>(bytes) / sizeof(wchar_t));
        ImmGetCompositionStringW(context, GCS_COMPSTR, result.data(), static_cast<DWORD>(bytes));
    }
    ImmReleaseContext(input, context);
    return result;
}

RECT ConversationInputTextRect(const ConversationState& state) {
    RECT client{};
    GetClientRect(state.window, &client);
    const int inputContainerH = Px(state, 64);
    const int inputTop = client.bottom - inputContainerH - Px(state, 16);
    const int actionSize = Px(state, 34);
    const int sendLeft = state.sendRect.left > 0 ? state.sendRect.left : client.right - Px(state, 74);
    const int micLeft = sendLeft - Px(state, 8) - actionSize;
    const int attachLeft = micLeft - Px(state, 4) - actionSize;
    return RECT{
        Px(state, 38), inputTop + Px(state, 13),
        (std::max)(Px(state, 80), attachLeft - Px(state, 8)), inputTop + Px(state, 51)};
}

POINT ConversationVisualCaretPoint(ConversationState& state, WNDPROC inputCore = nullptr) {
    RECT client{};
    GetClientRect(state.window, &client);
    const RECT textRect = ConversationInputTextRect(state);
    POINT caret{textRect.left, textRect.top + Px(state, 24)};
    if (!state.input || textRect.right <= textRect.left) return caret;

    if (!inputCore) inputCore = reinterpret_cast<WNDPROC>(GetPropW(state.input, kConversationInputCoreProcProperty));
    std::wstring committed = inputCore ? ReadInputCoreText(state.input, inputCore) : ReadText(state.input);
    std::wstring visible = committed + gConversationImeComposition;
    if (visible.empty() || !gConversationSurface.dwrite) return caret;

    auto format = LayerFormat(static_cast<float>(Px(state, 15)));
    auto layout = ConversationRawInputLayout(
        visible, format.Get(), static_cast<float>(textRect.right - textRect.left),
        static_cast<float>(textRect.bottom - textRect.top));
    if (!layout) return caret;

    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessageW(state.input, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    UINT32 caretIndex = gConversationImeComposition.empty()
        ? (std::min)(static_cast<UINT32>(selectionEnd), static_cast<UINT32>(committed.size()))
        : static_cast<UINT32>(visible.size());

    if (caretIndex > 0) {
        const bool atEnd = caretIndex >= visible.size();
        const UINT32 hitPosition = atEnd ? static_cast<UINT32>(visible.size() - 1) : caretIndex;
        FLOAT hitX = 0.0f;
        FLOAT hitY = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (SUCCEEDED(layout->HitTestTextPosition(hitPosition, atEnd ? TRUE : FALSE, &hitX, &hitY, &metrics))) {
            caret.x = textRect.left + static_cast<LONG>(std::lround(hitX));
            caret.y = textRect.top + static_cast<LONG>(std::lround(hitY + metrics.height));
        }
    }
    caret.x = (std::clamp)(caret.x, textRect.left, textRect.right - 1);
    caret.y = (std::clamp)(caret.y, textRect.top + 1, textRect.bottom - 1);
    return caret;
}

void PositionConversationInputProxyAnchored(ConversationState& state, WNDPROC inputCore = nullptr) {
    if (!state.input || !state.window) return;
    const POINT caret = ConversationVisualCaretPoint(state, inputCore);
    MoveWindow(state.input, caret.x, caret.y, 1, 1, FALSE);

    HIMC context = ImmGetContext(state.input);
    if (!context) return;
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = POINT{0, 0};
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = POINT{0, Px(state, 24)};
    ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(state.input, context);
}

LRESULT CALLBACK ConversationInputOverlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kConversationInputCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_GETTEXTLENGTH && !gConversationImeComposition.empty()) {
        return CallWindowProcW(core, hwnd, message, wParam, lParam) + static_cast<LRESULT>(gConversationImeComposition.size());
    }
    if (message == WM_GETTEXT && !gConversationImeComposition.empty()) {
        const int capacity = static_cast<int>(wParam);
        if (capacity <= 0 || !lParam) return 0;
        std::wstring visible = ReadInputCoreText(hwnd, core) + gConversationImeComposition;
        const int copyLength = (std::min)(capacity - 1, static_cast<int>(visible.size()));
        auto* destination = reinterpret_cast<wchar_t*>(lParam);
        if (copyLength > 0) std::wmemcpy(destination, visible.data(), static_cast<std::size_t>(copyLength));
        destination[copyLength] = L'\0';
        return copyLength;
    }

    if (message == WM_IME_STARTCOMPOSITION) {
        gConversationImeComposition.clear();
        const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
        if (state) {
            PositionConversationInputProxyAnchored(*state, core);
            gConversationCaretVisible = true;
            RenderConversationLayerSurface(*state);
        }
        return result;
    }
    if (message == WM_IME_COMPOSITION) {
        const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
        gConversationImeComposition = ReadImeComposition(hwnd);
        if (state) {
            PositionConversationInputProxyAnchored(*state, core);
            gConversationCaretVisible = true;
            RenderConversationLayerSurface(*state);
        }
        return result;
    }
    if (message == WM_IME_ENDCOMPOSITION) {
        const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
        gConversationImeComposition.clear();
        if (state) {
            PositionConversationInputProxyAnchored(*state, core);
            gConversationCaretVisible = true;
            RenderConversationLayerSurface(*state);
        }
        return result;
    }

    const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
    if (!state) return result;

    if (message == WM_SETFOCUS || message == WM_KEYUP || message == WM_CHAR) {
        PositionConversationInputProxyAnchored(*state, core);
        gConversationCaretVisible = true;
        RenderConversationLayerSurface(*state);
    } else if (message == WM_KILLFOCUS) {
        gConversationImeComposition.clear();
        gConversationCaretVisible = true;
        RenderConversationLayerSurface(*state);
    } else if (message == WM_NCDESTROY) {
        gConversationImeComposition.clear();
        RemovePropW(hwnd, kConversationInputCoreProcProperty);
    }
    return result;
}

void EnsureConversationInputSubclass(ConversationState& state) {
    if (!state.input || GetPropW(state.input, kConversationInputCoreProcProperty)) return;
    const auto core = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(state.input, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationInputOverlayProc)));
    if (core) SetPropW(state.input, kConversationInputCoreProcProperty, reinterpret_cast<HANDLE>(core));
}

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
    gConversationCaretVisible = true;
    if (gConversationState) PositionConversationInputProxyAnchored(*gConversationState);
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
    gConversationCaretVisible = true;
    if (gConversationState) PositionConversationInputProxyAnchored(*gConversationState);

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
    if (!state) return;

    if (gConversationUserRectValid) {
        const int width = gConversationUserRect.right - gConversationUserRect.left;
        const int height = gConversationUserRect.bottom - gConversationUserRect.top;
        SetWindowPos(panel, nullptr, gConversationUserRect.left, gConversationUserRect.top, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    EnsureConversationInputSubclass(*state);
    PositionConversationInputProxyAnchored(*state);
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

int ConversationResizeEdgeAtPoint(const ConversationState& state, POINT point) {
    RECT client{};
    GetClientRect(state.window, &client);
    const int grip = Px(state, 8);
    const bool left = point.x >= 0 && point.x < grip;
    const bool right = point.x < client.right && point.x >= client.right - grip;
    const bool top = point.y >= 0 && point.y < grip;
    const bool bottom = point.y < client.bottom && point.y >= client.bottom - grip;
    if (top && left) return WMSZ_TOPLEFT;
    if (top && right) return WMSZ_TOPRIGHT;
    if (bottom && left) return WMSZ_BOTTOMLEFT;
    if (bottom && right) return WMSZ_BOTTOMRIGHT;
    if (left) return WMSZ_LEFT;
    if (right) return WMSZ_RIGHT;
    if (top) return WMSZ_TOP;
    if (bottom) return WMSZ_BOTTOM;
    return 0;
}

HCURSOR ConversationResizeCursor(int edge) {
    switch (edge) {
    case WMSZ_LEFT:
    case WMSZ_RIGHT: return LoadCursorW(nullptr, IDC_SIZEWE);
    case WMSZ_TOP:
    case WMSZ_BOTTOM: return LoadCursorW(nullptr, IDC_SIZENS);
    case WMSZ_TOPLEFT:
    case WMSZ_BOTTOMRIGHT: return LoadCursorW(nullptr, IDC_SIZENWSE);
    case WMSZ_TOPRIGHT:
    case WMSZ_BOTTOMLEFT: return LoadCursorW(nullptr, IDC_SIZENESW);
    default: return nullptr;
    }
}

bool ConversationHeaderDragPoint(const ConversationState& state, POINT point) {
    if (point.y < Px(state, 8) || point.y >= Px(state, 70)) return false;
    if (PointIn(state.closeRect, point) || PointIn(state.pinRect, point) ||
        PointIn(state.historyRect, point) || PointIn(state.moreRect, point)) return false;
    return true;
}

void BeginConversationPointerMode(HWND hwnd, ConversationPointerMode mode, int resizeEdge = 0) {
    gConversationPointerMode = mode;
    gConversationResizeEdge = resizeEdge;
    GetCursorPos(&gConversationPointerStart);
    GetWindowRect(hwnd, &gConversationPointerStartRect);
    SetCapture(hwnd);
}

void SaveConversationUserRect(HWND hwnd) {
    if (GetWindowRect(hwnd, &gConversationUserRect)) gConversationUserRectValid = true;
}

void UpdateConversationPointerMode(HWND hwnd, ConversationState& state) {
    if (gConversationPointerMode == ConversationPointerMode::None || GetCapture() != hwnd) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    const int dx = cursor.x - gConversationPointerStart.x;
    const int dy = cursor.y - gConversationPointerStart.y;
    RECT rect = gConversationPointerStartRect;

    if (gConversationPointerMode == ConversationPointerMode::Move) {
        OffsetRect(&rect, dx, dy);
    } else {
        switch (gConversationResizeEdge) {
        case WMSZ_LEFT: rect.left += dx; break;
        case WMSZ_RIGHT: rect.right += dx; break;
        case WMSZ_TOP: rect.top += dy; break;
        case WMSZ_BOTTOM: rect.bottom += dy; break;
        case WMSZ_TOPLEFT: rect.left += dx; rect.top += dy; break;
        case WMSZ_TOPRIGHT: rect.right += dx; rect.top += dy; break;
        case WMSZ_BOTTOMLEFT: rect.left += dx; rect.bottom += dy; break;
        case WMSZ_BOTTOMRIGHT: rect.right += dx; rect.bottom += dy; break;
        }

        const int minWidth = Px(state, 420);
        const int minHeight = Px(state, 360);
        if (rect.right - rect.left < minWidth) {
            if (gConversationResizeEdge == WMSZ_LEFT || gConversationResizeEdge == WMSZ_TOPLEFT || gConversationResizeEdge == WMSZ_BOTTOMLEFT)
                rect.left = rect.right - minWidth;
            else rect.right = rect.left + minWidth;
        }
        if (rect.bottom - rect.top < minHeight) {
            if (gConversationResizeEdge == WMSZ_TOP || gConversationResizeEdge == WMSZ_TOPLEFT || gConversationResizeEdge == WMSZ_TOPRIGHT)
                rect.top = rect.bottom - minHeight;
            else rect.bottom = rect.top + minHeight;
        }
    }

    SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SaveConversationUserRect(hwnd);
    PositionConversationInputProxyAnchored(state);
}

void EndConversationPointerMode(HWND hwnd) {
    if (gConversationPointerMode == ConversationPointerMode::None) return;
    gConversationPointerMode = ConversationPointerMode::None;
    gConversationResizeEdge = 0;
    if (GetCapture() == hwnd) ReleaseCapture();
    SaveConversationUserRect(hwnd);
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

    if (message == WM_LBUTTONDOWN && state) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int resizeEdge = ConversationResizeEdgeAtPoint(*state, point);
        if (resizeEdge) {
            BeginConversationPointerMode(hwnd, ConversationPointerMode::Resize, resizeEdge);
            return 0;
        }
        if (PointIn(ConversationInputHitRect(*state), point)) {
            FocusConversationInputAtPoint(*state, point);
            PositionConversationInputProxyAnchored(*state);
            RenderConversationLayerSurface(*state);
            return 0;
        }
        if (ConversationHeaderDragPoint(*state, point)) {
            BeginConversationPointerMode(hwnd, ConversationPointerMode::Move);
            return 0;
        }
    }

    if (message == WM_MOUSEMOVE && state && gConversationPointerMode != ConversationPointerMode::None) {
        UpdateConversationPointerMode(hwnd, *state);
        return 0;
    }

    if (message == WM_LBUTTONUP && state) {
        if (gConversationPointerMode != ConversationPointerMode::None) {
            EndConversationPointerMode(hwnd);
            return 0;
        }
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointIn(gConversationSurface.attachRect, point)) {
            PickConversationAttachment(hwnd);
            RenderConversationLayerSurface(*state);
            return 0;
        }
        if (PointIn(gConversationSurface.micRect, point)) {
            StartWindowsVoiceTyping(hwnd);
            RenderConversationLayerSurface(*state);
            return 0;
        }
    }

    if (message == WM_CAPTURECHANGED && gConversationPointerMode != ConversationPointerMode::None) {
        gConversationPointerMode = ConversationPointerMode::None;
        gConversationResizeEdge = 0;
        SaveConversationUserRect(hwnd);
    }

    if (message == WM_SETCURSOR && state) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        const int resizeEdge = ConversationResizeEdgeAtPoint(*state, point);
        if (HCURSOR cursor = ConversationResizeCursor(resizeEdge)) {
            SetCursor(cursor);
            return TRUE;
        }
        if (PointIn(ConversationInputHitRect(*state), point)) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            return TRUE;
        }
        if (PointIn(gConversationSurface.attachRect, point) || PointIn(gConversationSurface.micRect, point)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        if (ConversationHeaderDragPoint(*state, point)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
    }

    if (message == WM_TIMER && wParam == kConversationCaretTimer) {
        if (state && GetFocus() == state->input) {
            gConversationCaretVisible = !gConversationCaretVisible;
            RenderConversationLayerSurface(*state);
        } else {
            gConversationCaretVisible = true;
        }
        return 0;
    }

    // Semantic events are the UI source of truth. Keep pi-runtime.log for diagnostics only.
    if (message == WM_TIMER && wParam == kActivityTimer) return 0;

    if (message == WM_NCDESTROY) {
        KillTimer(hwnd, kConversationCaretTimer);
        gPiRuntime.SetActivityCallback({});
        ReleaseConversationLayerSurface();
        RemovePropW(hwnd, kConversationCoreProcProperty);
        return CallWindowProcW(core, hwnd, message, wParam, lParam);
    }

    const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
    if (!state) return result;

    if (message == WM_SIZE) {
        PositionConversationInputProxyAnchored(*state);
        RenderConversationLayerSurface(*state);
        if (gConversationPointerMode != ConversationPointerMode::None) SaveConversationUserRect(hwnd);
    } else if (message == WM_MOUSEWHEEL ||
               message == kPiDoneMessage || message == kDirectDoneMessage) {
        RenderConversationLayerSurface(*state);
    } else if (message == WM_COMMAND && LOWORD(wParam) == kConversationInputControlId) {
        const WORD notification = HIWORD(wParam);
        if (notification == EN_CHANGE || notification == EN_SETFOCUS || notification == EN_KILLFOCUS) {
            if (notification != EN_KILLFOCUS) gConversationCaretVisible = true;
            if (notification != EN_KILLFOCUS) PositionConversationInputProxyAnchored(*state);
            RenderConversationLayerSurface(*state);
        }
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

    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(panel, GWLP_USERDATA));
    if (state) {
        EnsureConversationInputSubclass(*state);
        PositionConversationInputProxyAnchored(*state);
    }

    UINT blinkMs = GetCaretBlinkTime();
    if (blinkMs == 0 || blinkMs == INFINITE) blinkMs = 530;
    SetTimer(panel, kConversationCaretTimer, (std::max)(250u, blinkMs), nullptr);

    gPiRuntime.SetActivityCallback([panel](PiActivityEvent event) {
        auto payload = std::make_unique<ConversationActivityPayload>();
        payload->event = std::move(event);
        if (PostMessageW(panel, kConversationPiActivityMessage, 0, reinterpret_cast<LPARAM>(payload.get()))) payload.release();
    });
}

} // namespace
} // namespace turingdesk
