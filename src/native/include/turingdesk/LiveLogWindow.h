#pragma once

#include "turingdesk/RuntimeLogger.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::log {

inline std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
        wide.data(), required);
    return wide;
}

inline bool CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        wchar_t* ptr = static_cast<wchar_t*>(GlobalLock(hMem));
        if (ptr) {
            std::memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    return true;
}

class LiveLogWindow {
public:
    static LiveLogWindow& Instance() {
        static LiveLogWindow instance;
        return instance;
    }

    void Show(HINSTANCE instance, HWND parent = nullptr) {
        if (window_ && IsWindow(window_)) {
            ShowWindow(window_, SW_SHOWNORMAL);
            SetForegroundWindow(window_);
            return;
        }

        instance_ = instance;
        parent_ = parent;
        CreateWindowInternal();
    }

    HWND Handle() const noexcept { return window_; }

private:
    static constexpr wchar_t kClassName[] = L"TuringDesk.Native.LiveLogConsole";
    static constexpr UINT_PTR kTimerId = 9101;
    static constexpr int kBtnCopyAllId = 9201;
    static constexpr int kBtnClearId = 9202;
    static constexpr int kBtnOpenFileId = 9203;
    static constexpr int kChkAutoScrollId = 9204;
    static constexpr int kEditLogId = 9210;
    static constexpr int kStaticStatusId = 9211;

    HINSTANCE instance_{};
    HWND parent_{};
    HWND window_{};
    HWND editLog_{};
    HWND btnCopyAll_{};
    HWND btnClear_{};
    HWND btnOpenFile_{};
    HWND chkAutoScroll_{};
    HWND statusText_{};
    HFONT logFont_{};
    HFONT uiFont_{};

    std::uintmax_t fileOffset_{};
    bool autoScroll_{true};
    std::wstring accumulatedText_{};

    int S(int px) const {
        const UINT dpi = window_ ? GetDpiForWindow(window_) : USER_DEFAULT_SCREEN_DPI;
        return MulDiv(px, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
    }

    void CreateWindowInternal() {
        INITCOMMONCONTROLSEX common{};
        common.dwSize = sizeof(common);
        common.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&common);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance_;
        wc.lpfnWndProc = &LiveLogWindow::WndProc;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);

        window_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            kClassName,
            L"妙喵 · 实时运行调试日志控制台 (持续输出 / 支持选择与复制)",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, S(920), S(600),
            parent_, nullptr, instance_, this);

        if (!window_) return;

        uiFont_ = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        logFont_ = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, L"Consolas");

        auto makeButton = [&](const wchar_t* text, int id) {
            HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            return h;
        };

        btnCopyAll_ = makeButton(L"📋 复制全部日志", kBtnCopyAllId);
        btnClear_ = makeButton(L"🗑 清空面板", kBtnClearId);
        btnOpenFile_ = makeButton(L"📂 打开日志文件", kBtnOpenFileId);

        chkAutoScroll_ = CreateWindowExW(
            0, L"BUTTON", L"自动滚动到底部",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChkAutoScrollId)), instance_, nullptr);
        SendMessageW(chkAutoScroll_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        SendMessageW(chkAutoScroll_, BM_SETCHECK, BST_CHECKED, 0);

        statusText_ = CreateWindowExW(
            0, L"STATIC", L"● 实时监听中 (可鼠标拖选文本或 Ctrl+A / Ctrl+C 复制)",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStaticStatusId)), instance_, nullptr);
        SendMessageW(statusText_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

        editLog_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditLogId)), instance_, nullptr);
        SendMessageW(editLog_, WM_SETFONT, reinterpret_cast<WPARAM>(logFont_), TRUE);
        SendMessageW(editLog_, EM_SETLIMITTEXT, 0, 0); // Unlimited text size

        Layout();
        ShowWindow(window_, SW_SHOWNORMAL);
        UpdateWindow(window_);

        // Load existing logs and start timer
        fileOffset_ = 0;
        accumulatedText_.clear();
        PollNewLogs();

        SetTimer(window_, kTimerId, 150, nullptr);
    }

    void Layout() {
        if (!window_) return;
        RECT rc{};
        GetClientRect(window_, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        const int pad = S(10);
        const int btnH = S(32);
        const int copyW = S(130);
        const int clearW = S(100);
        const int fileW = S(120);
        const int autoW = S(140);
        const int statusH = S(24);

        int x = pad;
        SetWindowPos(btnCopyAll_, nullptr, x, pad, copyW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += copyW + S(8);
        SetWindowPos(btnClear_, nullptr, x, pad, clearW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += clearW + S(8);
        SetWindowPos(btnOpenFile_, nullptr, x, pad, fileW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += fileW + S(16);
        SetWindowPos(chkAutoScroll_, nullptr, x, pad + S(4), autoW, btnH - S(8), SWP_NOZORDER | SWP_NOACTIVATE);

        const int topY = pad + btnH + pad;
        const int editH = std::max(S(100), h - topY - statusH - pad * 2);
        SetWindowPos(editLog_, nullptr, pad, topY, std::max(10, w - pad * 2), editH, SWP_NOZORDER | SWP_NOACTIVATE);

        const int statusY = topY + editH + S(6);
        SetWindowPos(statusText_, nullptr, pad, statusY, std::max(10, w - pad * 2), statusH, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void PollNewLogs() {
        const auto path = turingdesk::RuntimeLogPath(L"desktop-debug.log");
        if (path.empty()) return;

        std::error_code ec;
        const auto fileSize = fs::file_size(path, ec);
        if (ec) return;

        if (fileSize < fileOffset_) {
            // File was truncated or rotated
            fileOffset_ = 0;
            accumulatedText_.clear();
            SetWindowTextW(editLog_, L"");
        }

        if (fileSize == fileOffset_) return;

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return;

        file.seekg(static_cast<std::streamoff>(fileOffset_));
        std::string newBytes;
        newBytes.resize(static_cast<std::size_t>(fileSize - fileOffset_));
        file.read(&newBytes[0], static_cast<std::streamsize>(newBytes.size()));
        fileOffset_ = fileSize;

        if (newBytes.empty()) return;

        std::wstring newWide = Utf8ToWide(newBytes);
        if (newWide.empty()) return;

        accumulatedText_ += newWide;

        // Keep accumulated text within reasonable bounds (e.g. 500,000 characters)
        if (accumulatedText_.size() > 500000) {
            accumulatedText_.erase(0, accumulatedText_.size() - 400000);
            SetWindowTextW(editLog_, accumulatedText_.c_str());
        } else {
            const int len = GetWindowTextLengthW(editLog_);
            SendMessageW(editLog_, EM_SETSEL, len, len);
            SendMessageW(editLog_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(newWide.c_str()));
        }

        if (autoScroll_) {
            const int totalLen = GetWindowTextLengthW(editLog_);
            SendMessageW(editLog_, EM_SETSEL, totalLen, totalLen);
            SendMessageW(editLog_, EM_SCROLLCARET, 0, 0);
        }

        // Count lines
        std::size_t lineCount = 0;
        for (wchar_t ch : accumulatedText_) {
            if (ch == L'\n') ++lineCount;
        }
        std::wstring status = L"● 实时监听中 · 共 " + std::to_wstring(lineCount) + L" 行日志 · 可鼠标拖选或使用下方/上方按钮一键复制";
        SetWindowTextW(statusText_, status.c_str());
    }

    void OnCopyAll() {
        const int len = GetWindowTextLengthW(editLog_);
        if (len <= 0) {
            SetWindowTextW(statusText_, L"⚠️ 当前日志为空，无可复制内容。");
            return;
        }

        std::wstring text(static_cast<std::size_t>(len + 1), L'\0');
        GetWindowTextW(editLog_, text.data(), len + 1);
        text.resize(len);

        if (CopyToClipboard(window_, text)) {
            SetWindowTextW(statusText_, L"✅ 已成功复制全部日志到剪贴板！");
        } else {
            SetWindowTextW(statusText_, L"❌ 复制到剪贴板失败。");
        }
    }

    void OnClear() {
        accumulatedText_.clear();
        SetWindowTextW(editLog_, L"");
        const auto path = turingdesk::RuntimeLogPath(L"desktop-debug.log");
        if (!path.empty()) {
            std::error_code ec;
            std::ofstream trunc(path, std::ios::trunc);
            trunc.close();
            fileOffset_ = 0;
        }
        SetWindowTextW(statusText_, L"● 已清空日志控制台");
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<LiveLogWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<LiveLogWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = hwnd;
            return TRUE;
        }

        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg) {
        case WM_SIZE:
            self->Layout();
            return 0;
        case WM_TIMER:
            if (wParam == kTimerId) {
                self->PollNewLogs();
            }
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kBtnCopyAllId) {
                self->OnCopyAll();
            } else if (id == kBtnClearId) {
                self->OnClear();
            } else if (id == kBtnOpenFileId) {
                OpenDebugLogFile();
            } else if (id == kChkAutoScrollId) {
                self->autoScroll_ = (SendMessageW(self->chkAutoScroll_, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, kTimerId);
            if (self->uiFont_) DeleteObject(self->uiFont_);
            if (self->logFont_) DeleteObject(self->logFont_);
            self->uiFont_ = nullptr;
            self->logFont_ = nullptr;
            self->window_ = nullptr;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

inline void ShowLiveLogConsole(HINSTANCE instance, HWND parent = nullptr) {
    LiveLogWindow::Instance().Show(instance, parent);
}

} // namespace turingdesk::log
