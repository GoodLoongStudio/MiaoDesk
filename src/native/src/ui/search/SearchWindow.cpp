#include "turingdesk/SearchWindow.h"
#include "turingdesk/L3CliWindow.h"
#include "turingdesk/SettingsCenterWindow.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <climits>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr int kHotkeyId = 1;
constexpr int kSearchEditId = 100;
constexpr int kWindowWidth = 720;
constexpr int kCollapsedHeight = 56;
constexpr int kExpandedHeight = 408;
constexpr int kBarTop = 0;
constexpr int kBarHeight = 56;
constexpr int kEditLeft = 50;
constexpr int kEditTop = 8;
constexpr int kEditRight = 596;
constexpr int kEditHeight = 40;
constexpr int kVoiceCenterX = 628;
constexpr int kDividerX = 658;
constexpr int kAiCenterX = 688;
constexpr UINT kTrayMessage = WM_APP + 91;
constexpr UINT kTrayShow = 5101;
constexpr UINT kTraySettings = 5102;
constexpr UINT kTrayExit = 5103;

constexpr DWM_WINDOW_ATTRIBUTE kDwmUseImmersiveDarkMode = static_cast<DWM_WINDOW_ATTRIBUTE>(20);
constexpr DWM_WINDOW_ATTRIBUTE kDwmWindowCornerPreference = static_cast<DWM_WINDOW_ATTRIBUTE>(33);
constexpr DWM_WINDOW_ATTRIBUTE kDwmBorderColor = static_cast<DWM_WINDOW_ATTRIBUTE>(34);
constexpr DWM_WINDOW_ATTRIBUTE kDwmSystemBackdropType = static_cast<DWM_WINDOW_ATTRIBUTE>(38);

constexpr COLORREF kText = RGB(38, 43, 52);
constexpr COLORREF kEditSurface = RGB(246, 248, 251);

bool IsLaunchable(ResultKind kind) {
    return kind == ResultKind::App || kind == ResultKind::File || kind == ResultKind::Folder;
}

const wchar_t* KindLabel(ResultKind kind) {
    switch (kind) {
    case ResultKind::App: return L"应用";
    case ResultKind::File: return L"文件";
    case ResultKind::Folder: return L"文件夹";
    case ResultKind::Answer: return L"图灵 AI";
    case ResultKind::Status: return L"状态";
    }
    return L"";
}

HICON ResolveShellIcon(const SearchResult& result) {
    if (!IsLaunchable(result.kind) || result.target.empty()) return nullptr;
    SHFILEINFOW info{};
    constexpr UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
    if (SHGetFileInfoW(result.target.c_str(), 0, &info, sizeof(info), flags) != 0 && info.hIcon)
        return info.hIcon;
    const DWORD attributes = result.kind == ResultKind::Folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    info = {};
    if (SHGetFileInfoW(result.target.c_str(), attributes, &info, sizeof(info),
                       flags | SHGFI_USEFILEATTRIBUTES) != 0 && info.hIcon)
        return info.hIcon;
    return nullptr;
}

bool ShellIconSelfTest() {
    SearchResult synthetic{ResultKind::File, L"Shell icon self-test", L"", L"turingdesk-self-test.txt", 0};
    HICON icon = ResolveShellIcon(synthetic);
    if (!icon) return false;
    DestroyIcon(icon);
    return true;
}

std::wstring ReadText(HWND control) {
    const int len = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(len) + 1, L'\0');
    if (len > 0) GetWindowTextW(control, value.data(), len + 1);
    value.resize(static_cast<std::size_t>(len));
    return value;
}

fs::path SearchIniPath() {
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData,
                                                  static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) return {};
    const fs::path directory = fs::path(localAppData) / L"TuringDesk";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory / L"search.ini";
}

void ApplyRoundedWindowRegion(HWND hwnd, bool expanded) {
    if (!hwnd) return;
    const int height = expanded ? kExpandedHeight : kCollapsedHeight;
    HRGN region = CreateRoundRectRgn(0, 0, kWindowWidth + 1, height + 1, 56, 56);
    if (!region) return;
    if (!SetWindowRgn(hwnd, region, TRUE)) DeleteObject(region);
}

void DrawSearchGlyph(ID2D1HwndRenderTarget* target, ID2D1Brush* brush) {
    if (!target || !brush) return;
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(24.0f, 27.0f), 8.0f, 8.0f), brush, 1.8f);
    target->DrawLine(D2D1::Point2F(29.8f, 32.8f), D2D1::Point2F(35.5f, 38.5f), brush, 1.8f);
}

void DrawMicrophoneGlyph(ID2D1HwndRenderTarget* target, ID2D1Brush* brush) {
    if (!target || !brush) return;
    const float x = static_cast<float>(kVoiceCenterX);
    target->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x - 4.5f, 16.0f, x + 4.5f, 31.5f), 4.5f, 4.5f), brush, 1.7f);
    target->DrawLine(D2D1::Point2F(x - 8.0f, 28.0f), D2D1::Point2F(x - 8.0f, 31.0f), brush, 1.7f);
    target->DrawLine(D2D1::Point2F(x + 8.0f, 28.0f), D2D1::Point2F(x + 8.0f, 31.0f), brush, 1.7f);
    target->DrawLine(D2D1::Point2F(x - 8.0f, 31.0f), D2D1::Point2F(x - 5.0f, 36.0f), brush, 1.7f);
    target->DrawLine(D2D1::Point2F(x + 8.0f, 31.0f), D2D1::Point2F(x + 5.0f, 36.0f), brush, 1.7f);
    target->DrawLine(D2D1::Point2F(x - 5.0f, 36.0f), D2D1::Point2F(x + 5.0f, 36.0f), brush, 1.7f);
    target->DrawLine(D2D1::Point2F(x, 36.0f), D2D1::Point2F(x, 41.0f), brush, 1.7f);
    target->DrawLine(D2D1::Point2F(x - 4.5f, 41.0f), D2D1::Point2F(x + 4.5f, 41.0f), brush, 1.7f);
}

void DrawSparkleGlyph(ID2D1HwndRenderTarget* target, ID2D1Brush* brush) {
    if (!target || !brush) return;
    const float x = static_cast<float>(kAiCenterX);
    const float y = 28.0f;
    target->DrawLine(D2D1::Point2F(x, y - 9), D2D1::Point2F(x, y + 9), brush, 1.6f);
    target->DrawLine(D2D1::Point2F(x - 9, y), D2D1::Point2F(x + 9, y), brush, 1.6f);
    target->DrawLine(D2D1::Point2F(x - 5.5f, y - 5.5f), D2D1::Point2F(x + 5.5f, y + 5.5f), brush, 1.2f);
    target->DrawLine(D2D1::Point2F(x + 5.5f, y - 5.5f), D2D1::Point2F(x - 5.5f, y + 5.5f), brush, 1.2f);
    target->DrawLine(D2D1::Point2F(x - 12, y - 8), D2D1::Point2F(x - 12, y - 3), brush, 1.1f);
    target->DrawLine(D2D1::Point2F(x - 14.5f, y - 5.5f), D2D1::Point2F(x - 9.5f, y - 5.5f), brush, 1.1f);
}

} // namespace

SearchWindow::SearchWindow(HINSTANCE instance) : instance_(instance) {}

SearchWindow::~SearchWindow() {
    l3_.Stop();
    files_.Shutdown();
    RemoveTray();
    if (hwnd_) UnregisterHotKey(hwnd_, kHotkeyId);
    if (editBrush_) DeleteObject(editBrush_);
    if (uiFont_) DeleteObject(uiFont_);
    if (smallFont_) DeleteObject(smallFont_);
}

bool SearchWindow::Create() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance_;
    wc.lpfnWndProc = &SearchWindow::WndProc;
    wc.lpszClassName = L"TuringDesk.Native.SearchWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())))) return false;

    writeFactory_->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                    15.5f, L"zh-CN", titleFormat_.GetAddressOf());
    if (!titleFormat_) {
        writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        15.5f, L"zh-CN", titleFormat_.GetAddressOf());
    }
    writeFactory_->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                    12.5f, L"zh-CN", subtitleFormat_.GetAddressOf());
    if (!subtitleFormat_) {
        writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        12.5f, L"zh-CN", subtitleFormat_.GetAddressOf());
    }
    if (titleFormat_) titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (subtitleFormat_) subtitleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"图灵智能桌面", WS_POPUP,
                            CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kCollapsedHeight,
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    uiFont_ = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    if (!uiFont_) uiFont_ = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    smallFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    editBrush_ = CreateSolidBrush(kEditSurface);

    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            kEditLeft, kEditTop, kEditRight - kEditLeft, kEditHeight, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)), instance_, nullptr);
    if (!edit_) return false;
    SetWindowLongPtrW(edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    oldEditProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit_, GWLP_WNDPROC,
                                                               reinterpret_cast<LONG_PTR>(&SearchWindow::EditProc)));
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"搜索应用、文件或图灵 AI"));

    ApplyWindows11Style();
    ApplyRoundedWindowRegion(hwnd_, false);
    ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    if (!RegisterHotKey(hwnd_, kHotkeyId, MOD_ALT | MOD_NOREPEAT, VK_SPACE)) return false;

    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTray();
    apps_.BuildIndex();
    fileSearchAvailable_ = files_.Available();
    LoadPosition();
    PositionWindow();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    return true;
}

bool SearchWindow::SelfTest() {
    if (apps_.Count() == 0) apps_.BuildIndex();
    std::wstring reply;
    bool secret = false;
    const bool local = l3_.TryHandleLocal(L"/time", reply, secret);
    return apps_.Count() >= 5 && files_.SelfTest() && local && !reply.empty() && !secret && ShellIconSelfTest();
}

void SearchWindow::ApplyWindows11Style() {
    if (!hwnd_) return;
    const BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    const int corner = 2;
    DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &corner, sizeof(corner));
    const COLORREF noBorder = 0xFFFFFFFEu;
    DwmSetWindowAttribute(hwnd_, kDwmBorderColor, &noBorder, sizeof(noBorder));
    const int backdrop = 3;
    DwmSetWindowAttribute(hwnd_, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
    const MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

void SearchWindow::ShowAndFocus() {
    if (!currentQuery_.empty() || !results_.empty()) SetExpanded(true);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hwnd_);
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int SearchWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void SearchWindow::UpdateFocusVisual() {
    editFocused_ = GetFocus() == edit_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchWindow::SetExpanded(bool expanded) {
    if (!hwnd_ || expanded_ == expanded) return;
    expanded_ = expanded;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    const int height = expanded_ ? kExpandedHeight : kCollapsedHeight;
    SetWindowPos(hwnd_, nullptr, rect.left, rect.top, kWindowWidth, height, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyRoundedWindowRegion(hwnd_, expanded_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchWindow::LoadPosition() {
    const fs::path ini = SearchIniPath();
    if (ini.empty()) return;
    const int x = static_cast<int>(GetPrivateProfileIntW(L"Window", L"X", INT_MIN, ini.c_str()));
    const int y = static_cast<int>(GetPrivateProfileIntW(L"Window", L"Y", INT_MIN, ini.c_str()));
    if (x == INT_MIN || y == INT_MIN) return;
    savedX_ = x;
    savedY_ = y;
    positionLoaded_ = true;
}

void SearchWindow::SavePosition() {
    if (!hwnd_) return;
    RECT rect{};
    if (!GetWindowRect(hwnd_, &rect)) return;
    savedX_ = rect.left;
    savedY_ = rect.top;
    positionLoaded_ = true;
    const fs::path ini = SearchIniPath();
    if (ini.empty()) return;
    const std::wstring x = std::to_wstring(savedX_);
    const std::wstring y = std::to_wstring(savedY_);
    WritePrivateProfileStringW(L"Window", L"X", x.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Window", L"Y", y.c_str(), ini.c_str());
}

void SearchWindow::PositionWindow() {
    HMONITOR monitor = nullptr;
    if (positionLoaded_) {
        const POINT center{savedX_ + kWindowWidth / 2, savedY_ + kCollapsedHeight / 2};
        monitor = MonitorFromPoint(center, MONITOR_DEFAULTTONULL);
    }
    if (!monitor) {
        monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        positionLoaded_ = false;
    }
    MONITORINFO info{sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return;
    const int left = info.rcWork.left;
    const int top = info.rcWork.top;
    const int right = info.rcWork.right;
    const int bottom = info.rcWork.bottom;
    int x = positionLoaded_ ? std::clamp(savedX_, left, std::max(left, right - kWindowWidth))
                            : left + (right - left - kWindowWidth) / 2;
    int y = positionLoaded_ ? std::clamp(savedY_, top, std::max(top, bottom - kCollapsedHeight)) : top + 22;
    if (!positionLoaded_) { savedX_ = x; savedY_ = y; }
    SetWindowPos(hwnd_, nullptr, x, y, kWindowWidth, expanded_ ? kExpandedHeight : kCollapsedHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void SearchWindow::AddTray() {
    if (!hwnd_ || trayAdded_) return;
    tray_ = {};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = hwnd_;
    tray_.uID = 1;
    tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray_.uCallbackMessage = kTrayMessage;
    tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray_.szTip, L"图灵智能桌面");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;
}

void SearchWindow::RemoveTray() {
    if (!trayAdded_) return;
    Shell_NotifyIconW(NIM_DELETE, &tray_);
    trayAdded_ = false;
}

void SearchWindow::HandleTray(UINT mouseMessage) {
    if (mouseMessage == WM_LBUTTONUP || mouseMessage == WM_LBUTTONDBLCLK) { ShowAndFocus(); return; }
    if (mouseMessage != WM_RBUTTONUP && mouseMessage != WM_CONTEXTMENU) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kTrayShow, L"显示搜索");
    AppendMenuW(menu, MF_STRING, kTraySettings, L"设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, L"退出图灵智能桌面");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(hwnd_);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == kTrayShow) ShowAndFocus();
    else if (command == kTraySettings) OpenSettingsCenter();
    else if (command == kTrayExit) ExitApplication();
}

void SearchWindow::OpenSettingsCenter() {
    if (l3_.Busy()) l3_.Stop();
    if (!ShowSettingsCenterWindow(instance_, hwnd_, l3_))
        SetStatus(L"设置启动失败", L"无法创建图灵智能桌面设置窗口。");
}

void SearchWindow::StartWindowsVoiceTyping() {
    if (!edit_) return;
    SetForegroundWindow(hwnd_);
    SetFocus(edit_);
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'H';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'H';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_LWIN;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT));
}

bool SearchWindow::HitVoiceButton(POINT point) const {
    return point.x >= kVoiceCenterX - 21 && point.x <= kVoiceCenterX + 21 &&
           point.y >= kBarTop && point.y <= kBarTop + kBarHeight;
}

bool SearchWindow::HitAiButton(POINT point) const {
    return point.x >= kAiCenterX - 19 && point.x <= kWindowWidth &&
           point.y >= kBarTop && point.y <= kBarTop + kBarHeight;
}

void SearchWindow::ExitApplication() {
    if (exiting_) return;
    exiting_ = true;
    if (const HWND wallpaper = FindWindowW(L"TuringDesk.Native.WallpaperControl", nullptr)) PostMessageW(wallpaper, WM_CLOSE, 0, 0);
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

LRESULT CALLBACK SearchWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SearchWindow* self = reinterpret_cast<SearchWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SearchWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SearchWindow::EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SearchWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) self->UpdateFocusVisual();
    if (message == WM_KEYDOWN) {
        if (wParam == VK_DOWN && !self->results_.empty()) {
            self->SetExpanded(true);
            self->selected_ = std::min<int>(self->selected_ + 1, static_cast<int>(self->results_.size()) - 1);
            InvalidateRect(self->hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_UP && !self->results_.empty()) {
            self->selected_ = std::max(0, self->selected_ - 1);
            InvalidateRect(self->hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_RETURN) { self->ExecuteSelected((GetKeyState(VK_CONTROL) & 0x8000) != 0); return 0; }
        if (wParam == VK_ESCAPE) { self->SetExpanded(false); return 0; }
    }
    return CallWindowProcW(self->oldEditProc_, hwnd, message, wParam, lParam);
}

LRESULT SearchWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated_ != 0 && message == taskbarCreated_) { trayAdded_ = false; AddTray(); return 0; }
    switch (message) {
    case WM_HOTKEY:
        if (wParam == kHotkeyId) { ShowAndFocus(); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && expanded_) SetExpanded(false);
        return 0;
    case WM_SETCURSOR: {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);
        if (HitVoiceButton(point) || HitAiButton(point)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (HitVoiceButton(point)) { StartWindowsVoiceTyping(); return 0; }
        if (HitAiButton(point)) { StartL3(ReadText(edit_)); return 0; }
        break;
    }
    case WM_EXITSIZEMOVE:
        SavePosition(); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kSearchEditId && HIWORD(wParam) == EN_CHANGE) { OnQueryChanged(); return 0; }
        break;
    case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kEditSurface);
        return reinterpret_cast<LRESULT>(editBrush_);
    }
    case kTrayMessage:
        HandleTray(static_cast<UINT>(lParam)); return 0;
    case WM_COPYDATA: {
        std::vector<SearchResult> received;
        if (files_.HandleCopyData(reinterpret_cast<COPYDATASTRUCT*>(lParam), received)) {
            fileSearchAvailable_ = true; fileSearchQueryFailed_ = false; fileResults_ = std::move(received); MergeResults(); return TRUE;
        }
        break;
    }
    case WM_DISPLAYCHANGE:
        PositionWindow(); SavePosition(); return 0;
    case WM_SIZE:
        ResizeRenderTarget(LOWORD(lParam), HIWORD(lParam));
        if (edit_) MoveWindow(edit_, kEditLeft, kEditTop, kEditRight - kEditLeft, kEditHeight, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(hwnd_, &ps); Draw(); EndPaint(hwnd_, &ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE:
        if (exiting_) DestroyWindow(hwnd_); else SetExpanded(false); return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        if (wParam) { exiting_ = true; DestroyWindow(hwnd_); } return 0;
    case WM_DESTROY:
        l3_.Stop(); files_.Shutdown(); RemoveTray(); UnregisterHotKey(hwnd_, kHotkeyId); hwnd_ = nullptr; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void SearchWindow::OnQueryChanged() {
    const auto query = ReadText(edit_);
    currentQuery_ = query;
    appResults_.clear(); fileResults_.clear(); results_.clear(); selected_ = -1; fileSearchQueryFailed_ = false;
    if (query.empty()) {
        fileSearchAvailable_ = files_.Available(); SetExpanded(false); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    SetExpanded(true);
    if (query.front() == L'/') {
        results_.push_back({ResultKind::Status, L"图灵智能桌面命令", L"按 Enter 执行 · /help 查看可用命令", L"", 0});
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    appResults_ = apps_.Query(query, 5);
    fileSearchAvailable_ = files_.Available();
    MergeResults();
    if (fileSearchAvailable_ && !files_.Query(hwnd_, query, 8)) { fileSearchQueryFailed_ = true; MergeResults(); }
}

void SearchWindow::MergeResults() {
    results_.clear();
    for (const auto& result : appResults_) results_.push_back(result);
    for (const auto& result : fileResults_) { if (results_.size() >= 9) break; results_.push_back(result); }
    if (!fileSearchAvailable_) results_.push_back({ResultKind::Status, L"文件搜索正在启动", L"文件索引服务暂不可用。", L"", -1000});
    else if (fileSearchQueryFailed_) results_.push_back({ResultKind::Status, L"文件查询失败", L"文件索引服务已连接，但本次查询没有成功。", L"", -1000});
    selected_ = -1;
    for (std::size_t i = 0; i < results_.size(); ++i) if (IsLaunchable(results_[i].kind)) { selected_ = static_cast<int>(i); break; }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchWindow::ExecuteSelected(bool forceL3) {
    const auto query = ReadText(edit_);
    if (query.empty()) return;
    if (!forceL3 && selected_ >= 0 && selected_ < static_cast<int>(results_.size())) {
        const auto& result = results_[selected_];
        if (IsLaunchable(result.kind) && !result.target.empty()) {
            const auto code = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", result.target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            if (code <= 32) SetStatus(L"打开失败", result.target);
            else { SetWindowTextW(edit_, L""); SetExpanded(false); }
            return;
        }
    }
    StartL3(query);
}

void SearchWindow::StartL3(const std::wstring& prompt) {
    if (l3_.Busy()) l3_.Stop();
    SetExpanded(false);
    if (!ShowL3CliWindow(instance_, hwnd_, l3_, prompt)) { SetStatus(L"图灵 AI 启动失败", L"请检查模型配置后重试。"); return; }
    SetWindowTextW(edit_, L"");
    SetExpanded(false);
}

void SearchWindow::SetStatus(std::wstring title, std::wstring subtitle) {
    results_.clear();
    results_.push_back({ResultKind::Status, std::move(title), std::move(subtitle), L"", 0});
    selected_ = -1;
    SetExpanded(true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchWindow::ResizeRenderTarget(UINT width, UINT height) {
    if (renderTarget_) renderTarget_->Resize(D2D1::SizeU(width, height));
}

void SearchWindow::Draw() {
    if (!renderTarget_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const auto props = D2D1::RenderTargetProperties();
        const auto hwndProps = D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
        if (FAILED(d2dFactory_->CreateHwndRenderTarget(props, hwndProps, renderTarget_.GetAddressOf()))) return;
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xF6F8FB, 0.94f), glassBrush_.GetAddressOf());
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x262B34, 0.94f), textBrush_.GetAddressOf());
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x6B7280, 0.92f), secondaryBrush_.GetAddressOf());
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x5B9CFF, 0.12f), selectionBrush_.GetAddressOf());
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 0.78f), borderBrush_.GetAddressOf());
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x4285F4, 0.68f), accentBrush_.GetAddressOf());
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x4B5563, 0.16f), dividerBrush_.GetAddressOf());
    }

    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(0xF3F6FA));
    const float width = renderTarget_->GetSize().width;
    const float height = renderTarget_->GetSize().height;

    if (expanded_) {
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, width, height), 28.0f, 28.0f), glassBrush_.Get());
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f), 28.0f, 28.0f),
            borderBrush_.Get(), 1.0f);
    }

    const D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, width, 56.0f), 28.0f, 28.0f);
    renderTarget_->FillRoundedRectangle(bar, glassBrush_.Get());
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, width - 0.5f, 55.5f), 27.5f, 27.5f),
        editFocused_ ? accentBrush_.Get() : borderBrush_.Get(), editFocused_ ? 1.35f : 1.0f);
    if (editFocused_) {
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(1.5f, 1.5f, width - 1.5f, 54.5f), 26.5f, 26.5f),
            accentBrush_.Get(), 0.65f);
    }

    DrawSearchGlyph(renderTarget_.Get(), secondaryBrush_.Get());
    DrawMicrophoneGlyph(renderTarget_.Get(), secondaryBrush_.Get());
    renderTarget_->DrawLine(D2D1::Point2F(static_cast<float>(kDividerX), 15.0f),
                            D2D1::Point2F(static_cast<float>(kDividerX), 41.0f), dividerBrush_.Get(), 1.0f);
    DrawSparkleGlyph(renderTarget_.Get(), secondaryBrush_.Get());

    float y = 70.0f;
    if (expanded_ && results_.empty()) {
        const std::wstring title = currentQuery_.empty() ? L"开始搜索" : L"没有本地结果";
        const std::wstring hint = currentQuery_.empty()
            ? L"搜索应用、文件，或点击右侧星光进入图灵 AI"
            : L"按 Enter 交给图灵 AI · Ctrl + Enter 强制进入图灵 AI";
        renderTarget_->DrawText(title.c_str(), static_cast<UINT32>(title.size()), titleFormat_.Get(),
                                D2D1::RectF(22, y + 8, width - 22, y + 34), textBrush_.Get());
        renderTarget_->DrawText(hint.c_str(), static_cast<UINT32>(hint.size()), subtitleFormat_.Get(),
                                D2D1::RectF(22, y + 36, width - 22, y + 60), secondaryBrush_.Get());
    }

    if (expanded_) {
        for (std::size_t i = 0; i < results_.size(); ++i) {
            const auto& result = results_[i];
            const float rowHeight = result.kind == ResultKind::Status ? 64.0f : 56.0f;
            if (static_cast<int>(i) == selected_) {
                renderTarget_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(12, y - 2, width - 12, y + rowHeight - 4), 14, 14),
                    selectionBrush_.Get());
            }
            const float textLeft = IsLaunchable(result.kind) ? 62.0f : 22.0f;
            std::wstring title = result.title;
            if (title.size() > 100) title.resize(100);
            renderTarget_->DrawText(title.c_str(), static_cast<UINT32>(title.size()), titleFormat_.Get(),
                                    D2D1::RectF(textLeft, y + 4, width - 26, y + 28), textBrush_.Get(),
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            std::wstring subtitle = KindLabel(result.kind);
            if (!result.subtitle.empty()) subtitle += L"  ·  " + result.subtitle;
            if (subtitle.size() > 150) subtitle.resize(150);
            renderTarget_->DrawText(subtitle.c_str(), static_cast<UINT32>(subtitle.size()), subtitleFormat_.Get(),
                                    D2D1::RectF(textLeft, y + 31, width - 26, y + rowHeight), secondaryBrush_.Get(),
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            y += rowHeight;
            if (y > height - 24) break;
        }
    }

    const HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        renderTarget_.Reset(); glassBrush_.Reset(); textBrush_.Reset(); secondaryBrush_.Reset();
        selectionBrush_.Reset(); borderBrush_.Reset(); accentBrush_.Reset(); dividerBrush_.Reset();
    } else if (SUCCEEDED(hr) && expanded_) {
        HDC dc = GetDC(hwnd_);
        if (dc) {
            float iconY = 70.0f;
            for (const auto& result : results_) {
                const float rowHeight = result.kind == ResultKind::Status ? 64.0f : 56.0f;
                if (IsLaunchable(result.kind)) {
                    if (HICON icon = ResolveShellIcon(result)) {
                        DrawIconEx(dc, 22, static_cast<int>(iconY + 9.0f), icon, 28, 28, 0, nullptr, DI_NORMAL);
                        DestroyIcon(icon);
                    }
                }
                iconY += rowHeight;
                if (iconY > height - 24) break;
            }
            ReleaseDC(hwnd_, dc);
        }
    }
}

} // namespace turingdesk
