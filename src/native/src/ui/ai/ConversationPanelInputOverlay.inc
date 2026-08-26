#include <commdlg.h>

namespace turingdesk {
namespace {

constexpr int kConversationInputControlId = 3102;
constexpr int kConversationAttachControlId = 3191;
constexpr int kConversationMicControlId = 3192;
constexpr wchar_t kConversationInputActionClass[] = L"TuringDesk.Native.ConversationInputAction";

HFONT gConversationActionFont{};

int OverlayPx(HWND hwnd, int value) {
    const UINT dpi = (std::max)(96u, GetDpiForWindow(hwnd));
    return MulDiv(value, static_cast<int>(dpi), 96);
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
}

void PickConversationAttachment(HWND actionWindow) {
    HWND panel = GetParent(actionWindow);
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

void StartWindowsVoiceTyping(HWND actionWindow) {
    HWND panel = GetParent(actionWindow);
    HWND input = GetDlgItem(panel, kConversationInputControlId);
    if (!input) return;
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

LRESULT CALLBACK ConversationInputActionProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONUP: {
        const int action = GetDlgCtrlID(hwnd);
        if (action == kConversationAttachControlId) PickConversationAttachment(hwnd);
        else if (action == kConversationMicControlId) StartWindowsVoiceTyping(hwnd);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(103, 115, 135));
        if (gConversationActionFont) SelectObject(dc, gConversationActionFont);
        const wchar_t* glyph = GetDlgCtrlID(hwnd) == kConversationAttachControlId ? L"\xE723" : L"\xE720";
        DrawTextW(dc, glyph, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterConversationInputActionClass(HINSTANCE instance, HWND panel) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &ConversationInputActionProc;
    wc.lpszClassName = kConversationInputActionClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    if (!gConversationActionFont) {
        gConversationActionFont = CreateFontW(-OverlayPx(panel, 18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    }
}

void EnsureConversationInputOverlay(HINSTANCE instance) {
    HWND panel = FindWindowW(L"TuringDesk.Native.ConversationPanel", nullptr);
    if (!panel) return;
    HWND input = GetDlgItem(panel, kConversationInputControlId);
    if (!input) return;

    RegisterConversationInputActionClass(instance, panel);
    RECT client{};
    GetClientRect(panel, &client);
    const int inputTop = client.bottom - OverlayPx(panel, 64) - OverlayPx(panel, 16);
    const int sendSize = OverlayPx(panel, 42);
    const int inputRight = client.right - OverlayPx(panel, 22);
    const int sendLeft = inputRight - sendSize - OverlayPx(panel, 10);
    const int actionSize = OverlayPx(panel, 34);
    const int gap = OverlayPx(panel, 4);
    const int micLeft = sendLeft - OverlayPx(panel, 8) - actionSize;
    const int attachLeft = micLeft - gap - actionSize;
    const int actionTop = inputTop + OverlayPx(panel, 15);

    HWND attach = GetDlgItem(panel, kConversationAttachControlId);
    if (!attach) {
        attach = CreateWindowExW(0, kConversationInputActionClass, L"", WS_CHILD | WS_VISIBLE,
                                 attachLeft, actionTop, actionSize, actionSize, panel,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kConversationAttachControlId)), instance, nullptr);
    }
    HWND mic = GetDlgItem(panel, kConversationMicControlId);
    if (!mic) {
        mic = CreateWindowExW(0, kConversationInputActionClass, L"", WS_CHILD | WS_VISIBLE,
                              micLeft, actionTop, actionSize, actionSize, panel,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kConversationMicControlId)), instance, nullptr);
    }
    if (attach) MoveWindow(attach, attachLeft, actionTop, actionSize, actionSize, TRUE);
    if (mic) MoveWindow(mic, micLeft, actionTop, actionSize, actionSize, TRUE);

    const int inputX = OverlayPx(panel, 38);
    const int inputY = inputTop + OverlayPx(panel, 13);
    const int inputWidth = (std::max)(OverlayPx(panel, 120), attachLeft - inputX - OverlayPx(panel, 6));
    MoveWindow(input, inputX, inputY, inputWidth, OverlayPx(panel, 38), TRUE);
}

} // namespace
} // namespace turingdesk
