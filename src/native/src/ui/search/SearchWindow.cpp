#include "turingdesk/SearchWindow.h"
#include "turingdesk/L3CliWindow.h"
#include "turingdesk/SettingsCenterWindow.h"
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr int kHotkeyId = 1;
constexpr int kSearchEditId = 100;
constexpr int kCaretTimerId = 2;
constexpr int kWindowWidth = 720;
constexpr int kCollapsedHeight = 56;
constexpr int kExpandedHeight = 408;
constexpr int kBarHeight = 56;
constexpr int kEditLeft = 52;
constexpr int kEditRight = 594;
constexpr int kInputProxyY = 27;
constexpr int kVoiceCenterX = 628;
constexpr int kDividerX = 658;
constexpr int kAiCenterX = 688;
constexpr UINT kTrayMessage = WM_APP + 91;
constexpr UINT kTrayShow = 5101;
constexpr UINT kTraySettings = 5102;
constexpr UINT kTrayExit = 5103;

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

    const DWORD attributes =
        result.kind == ResultKind::Folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    info = {};
    if (SHGetFileInfoW(result.target.c_str(), attributes, &info, sizeof(info),
                       flags | SHGFI_USEFILEATTRIBUTES) != 0 && info.hIcon)
        return info.hIcon;
    return nullptr;
}

bool ShellIconSelfTest() {
    SearchResult synthetic{
        ResultKind::File, L"Shell icon self-test", L"", L"turingdesk-self-test.txt", 0};
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
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) return {};

    const fs::path directory = fs::path(localAppData) / L"TuringDesk";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory / L"search.ini";
}

void DrawSearchGlyph(ID2D1RenderTarget* target, ID2D1Brush* brush) {
    if (!target || !brush) return;
    target->DrawEllipse(
        D2D1::Ellipse(D2D1::Point2F(25.5f, 26.5f), 8.6f, 8.6f), brush, 1.6f);
    target->DrawLine(
        D2D1::Point2F(31.7f, 32.7f), D2D1::Point2F(37.4f, 38.4f), brush, 1.6f);
}

void DrawMicrophoneGlyph(ID2D1RenderTarget* target, ID2D1Brush* brush) {
    if (!target || !brush) return;
    const float x = static_cast<float>(kVoiceCenterX);
    target->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x - 4.1f, 15.6f, x + 4.1f, 31.0f), 4.1f, 4.1f),
        brush, 1.45f);
    target->DrawLine(D2D1::Point2F(x - 7.7f, 27.6f), D2D1::Point2F(x - 7.7f, 30.5f),
                     brush, 1.45f);
    target->DrawLine(D2D1::Point2F(x + 7.7f, 27.6f), D2D1::Point2F(x + 7.7f, 30.5f),
                     brush, 1.45f);
    target->DrawLine(D2D1::Point2F(x - 7.7f, 30.5f), D2D1::Point2F(x - 4.8f, 35.0f),
                     brush, 1.45f);
    target->DrawLine(D2D1::Point2F(x + 7.7f, 30.5f), D2D1::Point2F(x + 4.8f, 35.0f),
                     brush, 1.45f);
    target->DrawLine(D2D1::Point2F(x - 4.8f, 35.0f), D2D1::Point2F(x + 4.8f, 35.0f),
                     brush, 1.45f);
    target->DrawLine(D2D1::Point2F(x, 35.0f), D2D1::Point2F(x, 40.0f), brush, 1.45f);
    target->DrawLine(D2D1::Point2F(x - 4.2f, 40.0f), D2D1::Point2F(x + 4.2f, 40.0f),
                     brush, 1.45f);
}

void DrawSparkleGlyph(ID2D1Factory* factory, ID2D1RenderTarget* target, ID2D1Brush* brush) {
    if (!factory || !target || !brush) return;
    const float x = static_cast<float>(kAiCenterX);
    const float y = 27.5f;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(factory->CreatePathGeometry(geometry.GetAddressOf()))) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(sink.GetAddressOf()))) return;

    sink->BeginFigure(D2D1::Point2F(x, y - 9.6f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(x + 1.1f, y - 3.5f),
        D2D1::Point2F(x + 3.5f, y - 1.1f),
        D2D1::Point2F(x + 9.6f, y)));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(x + 3.5f, y + 1.1f),
        D2D1::Point2F(x + 1.1f, y + 3.5f),
        D2D1::Point2F(x, y + 9.6f)));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(x - 1.1f, y + 3.5f),
        D2D1::Point2F(x - 3.5f, y + 1.1f),
        D2D1::Point2F(x - 9.6f, y)));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(x - 3.5f, y - 1.1f),
        D2D1::Point2F(x - 1.1f, y - 3.5f),
        D2D1::Point2F(x, y - 9.6f)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;

    target->DrawGeometry(geometry.Get(), brush, 1.35f);
    target->DrawLine(D2D1::Point2F(x - 14.0f, y + 5.0f),
                     D2D1::Point2F(x - 14.0f, y + 9.6f), brush, 0.95f);
    target->DrawLine(D2D1::Point2F(x - 16.3f, y + 7.3f),
                     D2D1::Point2F(x - 11.7f, y + 7.3f), brush, 0.95f);
}

} // namespace

SearchWindow::SearchWindow(HINSTANCE instance) : instance_(instance) {}

SearchWindow::~SearchWindow() {
    l3_.Stop();
    files_.Shutdown();
    RemoveTray();
    if (hwnd_) {
        KillTimer(hwnd_, kCaretTimerId);
        UnregisterHotKey(hwnd_, kHotkeyId);
    }
    ReleaseLayerSurface();
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

    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()))))
        return false;

    writeFactory_->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN",
        inputFormat_.GetAddressOf());
    if (!inputFormat_) {
        writeFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN",
            inputFormat_.GetAddressOf());
    }

    writeFactory_->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.5f, L"zh-CN",
        titleFormat_.GetAddressOf());
    if (!titleFormat_) {
        writeFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.5f, L"zh-CN",
            titleFormat_.GetAddressOf());
    }

    writeFactory_->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"zh-CN",
        subtitleFormat_.GetAddressOf());
    if (!subtitleFormat_) {
        writeFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"zh-CN",
            subtitleFormat_.GetAddressOf());
    }

    if (inputFormat_) {
        inputFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        inputFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        inputFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (titleFormat_) titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (subtitleFormat_) subtitleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // The SearchWindow itself is per-pixel alpha. Direct2D is the only owner of the visible
    // rounded edge; no GDI region or DWM rounded-corner mask is allowed to touch the pill.
    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"图灵智能桌面", WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kCollapsedHeight,
        nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    uiFont_ = CreateFontW(
        -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    if (!uiFont_) {
        uiFont_ = CreateFontW(
            -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    smallFont_ = CreateFontW(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    // Native EDIT is input infrastructure only. It never participates in visible layout.
    edit_ = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        kEditLeft, kInputProxyY, 1, 1, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)), instance_, nullptr);
    if (!edit_) return false;

    SetWindowLongPtrW(edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    oldEditProc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SearchWindow::EditProc)));
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

    ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    if (!RegisterHotKey(hwnd_, kHotkeyId, MOD_ALT | MOD_NOREPEAT, VK_SPACE)) return false;
    SetTimer(hwnd_, kCaretTimerId, 530, nullptr);

    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTray();
    apps_.BuildIndex();
    fileSearchAvailable_ = files_.Available();
    LoadPosition();
    PositionWindow();

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    Draw();
    return true;
}

bool SearchWindow::SelfTest() {
    if (apps_.Count() == 0) apps_.BuildIndex();

    std::wstring reply;
    bool secret = false;
    const bool local = l3_.TryHandleLocal(L"/time", reply, secret);

    bool inputProxyWorks = false;
    if (edit_) {
        SetWindowTextW(edit_, L"");
        SendMessageW(edit_, WM_CHAR, static_cast<WPARAM>(L'X'), 1);
        inputProxyWorks = ReadText(edit_) == L"X";
        SetWindowTextW(edit_, L"");
    }

    const bool pixelAlphaWindow =
        hwnd_ && ((GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) & WS_EX_LAYERED) != 0);

    return apps_.Count() >= 5 && files_.SelfTest() && local && !reply.empty() && !secret &&
           ShellIconSelfTest() && inputProxyWorks && pixelAlphaWindow;
}

void SearchWindow::ShowAndFocus() {
    if (!currentQuery_.empty() || !results_.empty()) SetExpanded(true);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hwnd_);
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    caretVisible_ = true;
    Draw();
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
    caretVisible_ = true;
    Draw();
}

void SearchWindow::SetHoverVisual(bool hovered) {
    if (hovered_ == hovered) return;
    hovered_ = hovered;
    Draw();
}

void SearchWindow::SetExpanded(bool expanded) {
    if (!hwnd_ || expanded_ == expanded) return;
    expanded_ = expanded;

    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    const int height = expanded_ ? kExpandedHeight : kCollapsedHeight;
    SetWindowPos(hwnd_, nullptr, rect.left, rect.top, kWindowWidth, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    Draw();
}

void SearchWindow::LoadPosition() {
    const fs::path ini = SearchIniPath();
    if (ini.empty()) return;
    const int x = static_cast<int>(
        GetPrivateProfileIntW(L"Window", L"X", INT_MIN, ini.c_str()));
    const int y = static_cast<int>(
        GetPrivateProfileIntW(L"Window", L"Y", INT_MIN, ini.c_str()));
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
        const POINT center{
            savedX_ + kWindowWidth / 2, savedY_ + kCollapsedHeight / 2};
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

    int x = positionLoaded_
        ? std::clamp(savedX_, left, std::max(left, right - kWindowWidth))
        : left + (right - left - kWindowWidth) / 2;
    int y = positionLoaded_
        ? std::clamp(savedY_, top, std::max(top, bottom - kCollapsedHeight))
        : top + 22;

    if (!positionLoaded_) {
        savedX_ = x;
        savedY_ = y;
    }

    SetWindowPos(hwnd_, nullptr, x, y, kWindowWidth,
                 expanded_ ? kExpandedHeight : kCollapsedHeight,
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
    if (mouseMessage == WM_LBUTTONUP || mouseMessage == WM_LBUTTONDBLCLK) {
        ShowAndFocus();
        return;
    }
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
    const int command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
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
           point.y >= 0 && point.y <= kBarHeight;
}

bool SearchWindow::HitAiButton(POINT point) const {
    return point.x >= kAiCenterX - 19 && point.x <= kWindowWidth &&
           point.y >= 0 && point.y <= kBarHeight;
}

void SearchWindow::ExitApplication() {
    if (exiting_) return;
    exiting_ = true;
    if (const HWND wallpaper = FindWindowW(L"TuringDesk.Native.WallpaperControl", nullptr))
        PostMessageW(wallpaper, WM_CLOSE, 0, 0);
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

LRESULT CALLBACK SearchWindow::WndProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SearchWindow* self =
        reinterpret_cast<SearchWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SearchWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SearchWindow::EditProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self =
        reinterpret_cast<SearchWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);

    if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
        self->UpdateFocusVisual();

    if (message == WM_PAINT) {
        ValidateRect(hwnd, nullptr);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;

    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        self->SetHoverVisual(true);
    } else if (message == WM_MOUSELEAVE) {
        self->SetHoverVisual(false);
    }

    if (message == WM_KEYDOWN) {
        if (wParam == VK_DOWN && !self->results_.empty()) {
            self->SetExpanded(true);
            self->selected_ = std::min<int>(
                self->selected_ + 1, static_cast<int>(self->results_.size()) - 1);
            self->Draw();
            return 0;
        }
        if (wParam == VK_UP && !self->results_.empty()) {
            self->selected_ = std::max(0, self->selected_ - 1);
            self->Draw();
            return 0;
        }
        if (wParam == VK_RETURN) {
            self->ExecuteSelected((GetKeyState(VK_CONTROL) & 0x8000) != 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            self->SetExpanded(false);
            return 0;
        }
    }

    return CallWindowProcW(self->oldEditProc_, hwnd, message, wParam, lParam);
}

LRESULT SearchWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated_ != 0 && message == taskbarCreated_) {
        trayAdded_ = false;
        AddTray();
        return 0;
    }

    switch (message) {
    case WM_HOTKEY:
        if (wParam == kHotkeyId) {
            ShowAndFocus();
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && expanded_) SetExpanded(false);
        return 0;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        SetHoverVisual(true);
        return 0;
    }

    case WM_MOUSELEAVE:
        SetHoverVisual(false);
        return 0;

    case WM_SETCURSOR: {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);

        if (HitVoiceButton(point) || HitAiButton(point)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        if (point.x >= kEditLeft && point.x < kEditRight &&
            point.y >= 0 && point.y <= kBarHeight) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x >= kEditLeft && point.x < kEditRight &&
            point.y >= 0 && point.y <= kBarHeight) {
            SetForegroundWindow(hwnd_);
            SetFocus(edit_);
            SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(-1),
                         static_cast<LPARAM>(-1));
            caretVisible_ = true;
            Draw();
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (HitVoiceButton(point)) {
            StartWindowsVoiceTyping();
            return 0;
        }
        if (HitAiButton(point)) {
            StartL3(ReadText(edit_));
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == kCaretTimerId && editFocused_) {
            caretVisible_ = !caretVisible_;
            Draw();
            return 0;
        }
        break;

    case WM_EXITSIZEMOVE:
        SavePosition();
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == kSearchEditId && HIWORD(wParam) == EN_CHANGE) {
            OnQueryChanged();
            return 0;
        }
        break;

    case kTrayMessage:
        HandleTray(static_cast<UINT>(lParam));
        return 0;

    case WM_COPYDATA: {
        std::vector<SearchResult> received;
        if (files_.HandleCopyData(
                reinterpret_cast<COPYDATASTRUCT*>(lParam), received)) {
            fileSearchAvailable_ = true;
            fileSearchQueryFailed_ = false;
            fileResults_ = std::move(received);
            MergeResults();
            return TRUE;
        }
        break;
    }

    case WM_DISPLAYCHANGE:
        PositionWindow();
        SavePosition();
        Draw();
        return 0;

    case WM_SIZE:
        ResizeRenderTarget(LOWORD(lParam), HIWORD(lParam));
        if (edit_) MoveWindow(edit_, kEditLeft, kInputProxyY, 1, 1, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd_, &ps);
        Draw();
        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        if (exiting_) DestroyWindow(hwnd_);
        else SetExpanded(false);
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            exiting_ = true;
            DestroyWindow(hwnd_);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd_, kCaretTimerId);
        l3_.Stop();
        files_.Shutdown();
        RemoveTray();
        UnregisterHotKey(hwnd_, kHotkeyId);
        ReleaseLayerSurface();
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void SearchWindow::OnQueryChanged() {
    const auto query = ReadText(edit_);
    currentQuery_ = query;
    caretVisible_ = true;

    appResults_.clear();
    fileResults_.clear();
    results_.clear();
    selected_ = -1;
    fileSearchQueryFailed_ = false;

    if (query.empty()) {
        fileSearchAvailable_ = files_.Available();
        SetExpanded(false);
        Draw();
        return;
    }

    SetExpanded(true);

    if (query.front() == L'/') {
        results_.push_back({
            ResultKind::Status, L"图灵智能桌面命令",
            L"按 Enter 执行 · /help 查看可用命令", L"", 0});
        Draw();
        return;
    }

    appResults_ = apps_.Query(query, 5);
    fileSearchAvailable_ = files_.Available();
    MergeResults();

    if (fileSearchAvailable_ && !files_.Query(hwnd_, query, 8)) {
        fileSearchQueryFailed_ = true;
        MergeResults();
    }
}

void SearchWindow::MergeResults() {
    results_.clear();
    for (const auto& result : appResults_) results_.push_back(result);
    for (const auto& result : fileResults_) {
        if (results_.size() >= 9) break;
        results_.push_back(result);
    }

    if (!fileSearchAvailable_) {
        results_.push_back({
            ResultKind::Status, L"文件搜索正在启动",
            L"文件索引服务暂不可用。", L"", -1000});
    } else if (fileSearchQueryFailed_) {
        results_.push_back({
            ResultKind::Status, L"文件查询失败",
            L"文件索引服务已连接，但本次查询没有成功。", L"", -1000});
    }

    selected_ = -1;
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (IsLaunchable(results_[i].kind)) {
            selected_ = static_cast<int>(i);
            break;
        }
    }

    Draw();
}

void SearchWindow::ExecuteSelected(bool forceL3) {
    const auto query = ReadText(edit_);
    if (query.empty()) return;

    if (!forceL3 && selected_ >= 0 &&
        selected_ < static_cast<int>(results_.size())) {
        const auto& result = results_[selected_];
        if (IsLaunchable(result.kind) && !result.target.empty()) {
            const auto code = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwnd_, L"open", result.target.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL));
            if (code <= 32) {
                SetStatus(L"打开失败", result.target);
            } else {
                SetWindowTextW(edit_, L"");
                SetExpanded(false);
            }
            return;
        }
    }

    StartL3(query);
}

void SearchWindow::StartL3(const std::wstring& prompt) {
    if (l3_.Busy()) l3_.Stop();
    SetExpanded(false);

    if (!ShowL3CliWindow(instance_, hwnd_, l3_, prompt)) {
        SetStatus(L"图灵 AI 启动失败", L"请检查模型配置后重试。");
        return;
    }

    SetWindowTextW(edit_, L"");
    SetExpanded(false);
}

void SearchWindow::SetStatus(std::wstring title, std::wstring subtitle) {
    results_.clear();
    results_.push_back({
        ResultKind::Status, std::move(title), std::move(subtitle), L"", 0});
    selected_ = -1;
    SetExpanded(true);
    Draw();
}

bool SearchWindow::EnsureLayerSurface(UINT width, UINT height) {
    if (width == 0 || height == 0) return false;

    if (renderTarget_ && layerDc_ && layerBitmap_ && layerBits_ &&
        layerWidth_ == width && layerHeight_ == height) {
        RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        return SUCCEEDED(renderTarget_->BindDC(layerDc_, &rect));
    }

    ReleaseLayerSurface();

    layerDc_ = CreateCompatibleDC(nullptr);
    if (!layerDc_) return false;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    layerBitmap_ = CreateDIBSection(
        layerDc_, &bitmapInfo, DIB_RGB_COLORS, &layerBits_, nullptr, 0);
    if (!layerBitmap_ || !layerBits_) {
        ReleaseLayerSurface();
        return false;
    }

    layerOldBitmap_ = SelectObject(layerDc_, layerBitmap_);
    if (!layerOldBitmap_ || layerOldBitmap_ == HGDI_ERROR) {
        layerOldBitmap_ = nullptr;
        ReleaseLayerSurface();
        return false;
    }

    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    if (FAILED(d2dFactory_->CreateDCRenderTarget(
            &props, renderTarget_.GetAddressOf()))) {
        ReleaseLayerSurface();
        return false;
    }

    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (FAILED(renderTarget_->BindDC(layerDc_, &rect))) {
        ReleaseLayerSurface();
        return false;
    }

    layerWidth_ = width;
    layerHeight_ = height;
    return true;
}

void SearchWindow::ReleaseLayerSurface() {
    renderTarget_.Reset();

    if (layerDc_ && layerOldBitmap_) {
        SelectObject(layerDc_, layerOldBitmap_);
        layerOldBitmap_ = nullptr;
    }
    if (layerBitmap_) {
        DeleteObject(layerBitmap_);
        layerBitmap_ = nullptr;
    }
    if (layerDc_) {
        DeleteDC(layerDc_);
        layerDc_ = nullptr;
    }

    layerBits_ = nullptr;
    layerWidth_ = 0;
    layerHeight_ = 0;
}

bool SearchWindow::PresentLayerSurface(UINT width, UINT height) {
    if (!hwnd_ || !layerDc_) return false;

    RECT windowRect{};
    if (!GetWindowRect(hwnd_, &windowRect)) return false;

    POINT destination{windowRect.left, windowRect.top};
    POINT source{0, 0};
    SIZE size{static_cast<LONG>(width), static_cast<LONG>(height)};

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    return UpdateLayeredWindow(
               hwnd_, nullptr, &destination, &size, layerDc_, &source,
               0, &blend, ULW_ALPHA) != FALSE;
}

void SearchWindow::ResizeRenderTarget(UINT width, UINT height) {
    if (width == 0 || height == 0) return;
    if (layerWidth_ != width || layerHeight_ != height)
        ReleaseLayerSurface();
}

void SearchWindow::Draw() {
    if (!hwnd_ || !d2dFactory_ || !writeFactory_) return;

    RECT client{};
    if (!GetClientRect(hwnd_, &client)) return;
    const UINT widthPx = static_cast<UINT>(std::max<LONG>(0, client.right - client.left));
    const UINT heightPx = static_cast<UINT>(std::max<LONG>(0, client.bottom - client.top));
    if (!EnsureLayerSurface(widthPx, heightPx)) return;

    std::memset(layerBits_, 0,
                static_cast<std::size_t>(widthPx) *
                static_cast<std::size_t>(heightPx) * 4u);

    RECT bindRect{0, 0, static_cast<LONG>(widthPx), static_cast<LONG>(heightPx)};
    if (FAILED(renderTarget_->BindDC(layerDc_, &bindRect))) return;

    renderTarget_->BeginDraw();
    renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    renderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    const float width = static_cast<float>(widthPx);
    const float height = static_cast<float>(heightPx);
    const bool active = editFocused_ && !currentQuery_.empty();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> defaultFill;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverFill;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> focusFill;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secondaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> defaultOutline;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverOutline;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> focusedOutline;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activeOutline;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> softGlow;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dividerBrush;

    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White, 0.68f), defaultFill.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White, 0.72f), hoverFill.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White, 0.74f), focusFill.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White, 0.78f), panelBrush.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x374151, 0.98f), textBrush.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x6B7280, 0.96f), secondaryBrush.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x5B9CFF, 0.12f), selectionBrush.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White, 0.72f), defaultOutline.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White, 0.88f), hoverOutline.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x73A7FF, 0.82f), focusedOutline.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x5B9CFF, 0.96f), activeOutline.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x4285F4, 0.12f), softGlow.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x4285F4, 0.90f), accentBrush.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0x111827, 0.08f), dividerBrush.GetAddressOf());

    if (!defaultFill || !hoverFill || !focusFill || !panelBrush ||
        !textBrush || !secondaryBrush || !selectionBrush || !defaultOutline ||
        !hoverOutline || !focusedOutline || !activeOutline || !softGlow ||
        !accentBrush || !dividerBrush) {
        renderTarget_->EndDraw();
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> edgeStops;
    const D2D1_GRADIENT_STOP stops[3] = {
        {0.0f, D2D1::ColorF(D2D1::ColorF::White, 0.94f)},
        {0.48f, D2D1::ColorF(D2D1::ColorF::White, 0.56f)},
        {1.0f, D2D1::ColorF(D2D1::ColorF::White, 0.12f)}
    };
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> edgeHighlight;
    if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
            stops, static_cast<UINT32>(std::size(stops)), edgeStops.GetAddressOf())) &&
        edgeStops) {
        const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES edgeProps{
            D2D1::Point2F(0.0f, 0.0f),
            D2D1::Point2F(width, static_cast<float>(kBarHeight))};
        renderTarget_->CreateLinearGradientBrush(
            edgeProps, edgeStops.Get(), edgeHighlight.GetAddressOf());
    }

    if (expanded_) {
        const auto panel = D2D1::RoundedRect(
            D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f),
            27.5f, 27.5f);
        renderTarget_->FillRoundedRectangle(panel, panelBrush.Get());
        renderTarget_->DrawRoundedRectangle(
            panel, defaultOutline.Get(), 1.0f);
    }

    ID2D1Brush* fill = editFocused_
        ? static_cast<ID2D1Brush*>(focusFill.Get())
        : hovered_
            ? static_cast<ID2D1Brush*>(hoverFill.Get())
            : static_cast<ID2D1Brush*>(defaultFill.Get());

    // This is the single authoritative visible edge. It is alpha-antialiased by Direct2D and
    // uploaded with UpdateLayeredWindow; no integer GDI region exists anymore.
    const auto bar = D2D1::RoundedRect(
        D2D1::RectF(0.5f, 0.5f, width - 0.5f, 55.5f),
        27.5f, 27.5f);
    renderTarget_->FillRoundedRectangle(bar, fill);

    if (editFocused_) {
        const float glowInset = active ? 3.0f : 2.75f;
        const float glowWidth = active ? 2.4f : 1.8f;
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    glowInset, glowInset, width - glowInset,
                    static_cast<float>(kBarHeight) - glowInset),
                28.0f - glowInset, 28.0f - glowInset),
            softGlow.Get(), glowWidth);
    }

    ID2D1Brush* outline = defaultOutline.Get();
    float outlineWidth = 1.0f;
    if (active) {
        outline = activeOutline.Get();
        outlineWidth = 1.10f;
    } else if (editFocused_) {
        outline = focusedOutline.Get();
        outlineWidth = 1.05f;
    } else if (hovered_) {
        outline = hoverOutline.Get();
    }
    renderTarget_->DrawRoundedRectangle(bar, outline, outlineWidth);

    if (edgeHighlight) {
        const float inset =
            active ? 1.75f : editFocused_ ? 1.65f : hovered_ ? 1.55f : 1.50f;
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    inset, inset, width - inset,
                    static_cast<float>(kBarHeight) - inset),
                28.0f - inset, 28.0f - inset),
            edgeHighlight.Get(), 0.55f);
    }

    DrawSearchGlyph(renderTarget_.Get(), secondaryBrush.Get());
    DrawMicrophoneGlyph(renderTarget_.Get(), secondaryBrush.Get());
    renderTarget_->DrawLine(
        D2D1::Point2F(static_cast<float>(kDividerX), 15.0f),
        D2D1::Point2F(static_cast<float>(kDividerX), 41.0f),
        dividerBrush.Get(), 1.0f);
    DrawSparkleGlyph(d2dFactory_.Get(), renderTarget_.Get(), secondaryBrush.Get());

    Microsoft::WRL::ComPtr<IDWriteTextLayout> queryLayout;
    if (!currentQuery_.empty()) {
        if (SUCCEEDED(writeFactory_->CreateTextLayout(
                currentQuery_.c_str(), static_cast<UINT32>(currentQuery_.size()),
                inputFormat_.Get(), static_cast<float>(kEditRight - kEditLeft),
                static_cast<float>(kBarHeight), queryLayout.GetAddressOf())) &&
            queryLayout) {
            renderTarget_->DrawTextLayout(
                D2D1::Point2F(static_cast<float>(kEditLeft), 0.0f),
                queryLayout.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    } else {
        static constexpr wchar_t placeholder[] = L"搜索应用、文件或图灵 AI";
        const float placeholderX =
            static_cast<float>(kEditLeft) + (editFocused_ ? 6.0f : 0.0f);
        renderTarget_->DrawText(
            placeholder, static_cast<UINT32>(std::size(placeholder) - 1),
            inputFormat_.Get(),
            D2D1::RectF(
                placeholderX, 0.0f, static_cast<float>(kEditRight),
                static_cast<float>(kBarHeight)),
            secondaryBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    if (editFocused_ && caretVisible_) {
        float caretX = static_cast<float>(kEditLeft);
        float caretTop = 18.0f;
        float caretBottom = 38.0f;

        if (queryLayout) {
            DWORD selectionStart = 0;
            DWORD selectionEnd = 0;
            SendMessageW(
                edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                reinterpret_cast<LPARAM>(&selectionEnd));
            const UINT32 caretIndex = std::min<UINT32>(
                static_cast<UINT32>(selectionEnd),
                static_cast<UINT32>(currentQuery_.size()));

            FLOAT hitX = 0.0f;
            FLOAT hitY = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(queryLayout->HitTestTextPosition(
                    caretIndex, FALSE, &hitX, &hitY, &metrics))) {
                caretX += hitX;
                caretTop = hitY + 1.0f;
                caretBottom = hitY + std::max(18.0f, metrics.height - 1.0f);
            }
        }

        renderTarget_->DrawLine(
            D2D1::Point2F(caretX, caretTop),
            D2D1::Point2F(caretX, caretBottom),
            accentBrush.Get(), 1.15f);
    }

    float y = 70.0f;
    if (expanded_ && results_.empty()) {
        const std::wstring title =
            currentQuery_.empty() ? L"开始搜索" : L"没有本地结果";
        const std::wstring hint = currentQuery_.empty()
            ? L"搜索应用、文件，或点击右侧星光进入图灵 AI"
            : L"按 Enter 交给图灵 AI · Ctrl + Enter 强制进入图灵 AI";

        renderTarget_->DrawText(
            title.c_str(), static_cast<UINT32>(title.size()),
            titleFormat_.Get(),
            D2D1::RectF(22, y + 8, width - 22, y + 34),
            textBrush.Get());
        renderTarget_->DrawText(
            hint.c_str(), static_cast<UINT32>(hint.size()),
            subtitleFormat_.Get(),
            D2D1::RectF(22, y + 36, width - 22, y + 60),
            secondaryBrush.Get());
    }

    if (expanded_) {
        for (std::size_t i = 0; i < results_.size(); ++i) {
            const auto& result = results_[i];
            const float rowHeight =
                result.kind == ResultKind::Status ? 64.0f : 56.0f;

            if (static_cast<int>(i) == selected_) {
                renderTarget_->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(
                            12, y - 2, width - 12, y + rowHeight - 4),
                        14, 14),
                    selectionBrush.Get());
            }

            const float rowTextLeft =
                IsLaunchable(result.kind) ? 62.0f : 22.0f;

            std::wstring title = result.title;
            if (title.size() > 100) title.resize(100);
            renderTarget_->DrawText(
                title.c_str(), static_cast<UINT32>(title.size()),
                titleFormat_.Get(),
                D2D1::RectF(
                    rowTextLeft, y + 4, width - 26, y + 28),
                textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            std::wstring subtitle = KindLabel(result.kind);
            if (!result.subtitle.empty())
                subtitle += L"  ·  " + result.subtitle;
            if (subtitle.size() > 150) subtitle.resize(150);
            renderTarget_->DrawText(
                subtitle.c_str(), static_cast<UINT32>(subtitle.size()),
                subtitleFormat_.Get(),
                D2D1::RectF(
                    rowTextLeft, y + 31, width - 26, y + rowHeight),
                secondaryBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            y += rowHeight;
            if (y > height - 24) break;
        }
    }

    const HRESULT drawResult = renderTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        ReleaseLayerSurface();
        return;
    }
    if (FAILED(drawResult)) return;

    // Keep the existing shell result icons without bringing a second HWND/GDI edge path back.
    if (expanded_) {
        float iconY = 70.0f;
        for (const auto& result : results_) {
            const float rowHeight =
                result.kind == ResultKind::Status ? 64.0f : 56.0f;

            if (IsLaunchable(result.kind)) {
                if (HICON icon = ResolveShellIcon(result)) {
                    DrawIconEx(
                        layerDc_, 22, static_cast<int>(iconY + 9.0f),
                        icon, 28, 28, 0, nullptr, DI_NORMAL);
                    DestroyIcon(icon);
                }
            }

            iconY += rowHeight;
            if (iconY > height - 24) break;
        }
        GdiFlush();
    }

    PresentLayerSurface(widthPx, heightPx);
}

} // namespace turingdesk
