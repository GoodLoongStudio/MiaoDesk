namespace miaodesk {
namespace {

constexpr wchar_t kConversationCornerResizeCoreProcProperty[] = L"MiaoDesk.Conversation.CornerResizeCoreProc";

int ConversationCornerResizeEdgeAtPoint(const ConversationState& state, POINT point) {
    RECT client{};
    GetClientRect(state.window, &client);

    // The visible panel radius is 24 logical px. Treat that whole corner square as the
    // diagonal resize zone so the curved corner itself always behaves like a normal
    // desktop-window corner instead of falling back to horizontal/vertical resize.
    const int corner = Px(state, 24);
    const bool left = point.x >= 0 && point.x < corner;
    const bool right = point.x < client.right && point.x >= client.right - corner;
    const bool top = point.y >= 0 && point.y < corner;
    const bool bottom = point.y < client.bottom && point.y >= client.bottom - corner;

    if (top && left) return WMSZ_TOPLEFT;
    if (top && right) return WMSZ_TOPRIGHT;
    if (bottom && left) return WMSZ_BOTTOMLEFT;
    if (bottom && right) return WMSZ_BOTTOMRIGHT;
    return 0;
}

LRESULT CALLBACK ConversationCornerResizeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kConversationCornerResizeCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (state && message == WM_LBUTTONDOWN) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int edge = ConversationCornerResizeEdgeAtPoint(*state, point);
        if (edge) {
            BeginConversationPointerMode(hwnd, ConversationPointerMode::Resize, edge);
            return 0;
        }
    }

    if (state && message == WM_SETCURSOR) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        switch (ConversationCornerResizeEdgeAtPoint(*state, point)) {
        case WMSZ_TOPLEFT:
        case WMSZ_BOTTOMRIGHT:
            // Standard Windows NW-SE DOUBLE-HEADED diagonal resize cursor.
            SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
            return TRUE;
        case WMSZ_TOPRIGHT:
        case WMSZ_BOTTOMLEFT:
            // Standard Windows NE-SW DOUBLE-HEADED diagonal resize cursor.
            SetCursor(LoadCursorW(nullptr, IDC_SIZENESW));
            return TRUE;
        default:
            break;
        }
    }

    if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, kConversationCornerResizeCoreProcProperty);
        return CallWindowProcW(core, hwnd, message, wParam, lParam);
    }

    return CallWindowProcW(core, hwnd, message, wParam, lParam);
}

void EnsureConversationCornerResizeBridge(HWND panel) {
    if (!panel || GetPropW(panel, kConversationCornerResizeCoreProcProperty)) return;
    const auto core = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(panel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationCornerResizeProc)));
    if (core) SetPropW(panel, kConversationCornerResizeCoreProcProperty, reinterpret_cast<HANDLE>(core));
}

} // namespace
} // namespace miaodesk
