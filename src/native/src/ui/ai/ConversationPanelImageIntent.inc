namespace miaodesk {
namespace {

constexpr wchar_t kConversationImageIntentPanelCoreProcProperty[] = L"MiaoDesk.Conversation.ImageIntentPanelCoreProc";
constexpr wchar_t kConversationImageIntentInputCoreProcProperty[] = L"MiaoDesk.Conversation.ImageIntentInputCoreProc";

std::wstring NormalizeImageIntentText(std::wstring value) {
    value = Lower(Trim(std::move(value)));
    for (const wchar_t ch : {L' ', L'\t', L'\r', L'\n', L'。', L'，', L',', L'.', L'！', L'!', L'？', L'?'}) {
        value.erase(std::remove(value.begin(), value.end(), ch), value.end());
    }
    return value;
}

bool LooksLikeImageCreationRequest(const std::wstring& raw) {
    const auto text = Lower(raw);
    const bool imageNoun = text.find(L"图片") != std::wstring::npos ||
                           text.find(L"图像") != std::wstring::npos ||
                           text.find(L"海报") != std::wstring::npos ||
                           text.find(L"插画") != std::wstring::npos ||
                           text.find(L"picture") != std::wstring::npos ||
                           text.find(L"image") != std::wstring::npos;
    const bool creationVerb = text.find(L"生成") != std::wstring::npos ||
                              text.find(L"画") != std::wstring::npos ||
                              text.find(L"绘制") != std::wstring::npos ||
                              text.find(L"渲染") != std::wstring::npos ||
                              text.find(L"做一张") != std::wstring::npos ||
                              text.find(L"create") != std::wstring::npos ||
                              text.find(L"generate") != std::wstring::npos ||
                              text.find(L"draw") != std::wstring::npos ||
                              text.find(L"render") != std::wstring::npos;
    return imageNoun && creationVerb;
}

bool IsBareImageCreationRequest(const std::wstring& raw) {
    const auto text = NormalizeImageIntentText(raw);
    static const wchar_t* kBareRequests[] = {
        L"生成图片", L"生成一张图片", L"帮我生成图片", L"帮我生成一张图片",
        L"创建图片", L"创建一张图片", L"做图片", L"做一张图片",
        L"画图", L"画一张图", L"画一张图片", L"生成图像", L"生成一张图像",
        L"generateimage", L"generateanimage", L"createimage", L"createanimage",
        L"drawimage", L"drawanimage"
    };
    for (const auto* request : kBareRequests) {
        if (text == request) return true;
    }
    return false;
}

bool HandleConversationImageIntent(ConversationState& state) {
    if (state.busy || state.pendingConfirmation || !state.input) return false;
    const std::wstring typed = Trim(ReadText(state.input));
    if (!LooksLikeImageCreationRequest(typed)) return false;

    if (IsBareImageCreationRequest(typed)) {
        SetWindowTextW(state.input, L"");
        AppendCompleted(state, typed, L"想生成什么内容？直接告诉我画面主体或场景就可以。" );
        gConversationCaretVisible = true;
        PositionConversationInputProxyAnchored(state);
        RenderConversationLayerSurface(state);
        return true;
    }

    if (!state.agent || !state.agent->HasApiKey()) return false;

    SetWindowTextW(state.input, L"");
    std::wstring actual = typed;
    actual += L"\n\n[MiaoDesk image intent: The user has already supplied image content. Call image_generate now. Do not ask for style, dimensions, purpose, or other optional preferences before attempting the tool. Only report a configuration limitation if the image tool itself returns one.]";
    StartModelTurn(state, typed, actual, false, true);
    gConversationCaretVisible = true;
    PositionConversationInputProxyAnchored(state);
    RenderConversationLayerSurface(state);
    return true;
}

LRESULT CALLBACK ConversationImageIntentInputProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kConversationImageIntentInputCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (state && message == WM_KEYDOWN && wParam == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
        if (HandleConversationImageIntent(*state)) return 0;
    }

    if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, kConversationImageIntentInputCoreProcProperty);
        return CallWindowProcW(core, hwnd, message, wParam, lParam);
    }
    return CallWindowProcW(core, hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ConversationImageIntentPanelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kConversationImageIntentPanelCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (state && message == WM_LBUTTONDOWN) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointIn(state->sendRect, point) && !state->busy && HandleConversationImageIntent(*state)) return 0;
    }

    if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, kConversationImageIntentPanelCoreProcProperty);
        return CallWindowProcW(core, hwnd, message, wParam, lParam);
    }
    return CallWindowProcW(core, hwnd, message, wParam, lParam);
}

void EnsureConversationImageIntentBridge(ConversationState& state) {
    if (state.input && !GetPropW(state.input, kConversationImageIntentInputCoreProcProperty)) {
        const auto core = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(state.input, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationImageIntentInputProc)));
        if (core) SetPropW(state.input, kConversationImageIntentInputCoreProcProperty, reinterpret_cast<HANDLE>(core));
    }
    if (state.window && !GetPropW(state.window, kConversationImageIntentPanelCoreProcProperty)) {
        const auto core = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(state.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationImageIntentPanelProc)));
        if (core) SetPropW(state.window, kConversationImageIntentPanelCoreProcProperty, reinterpret_cast<HANDLE>(core));
    }
}

} // namespace
} // namespace miaodesk
