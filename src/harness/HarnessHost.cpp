#include "miaodesk/HarnessProcessManager.h"
#include "miaodesk/WindowPlacementStore.h"
#include <windows.h>
#include <objbase.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/client.h>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWindowClass[] = L"MiaoDesk.Native.HarnessWindow";
constexpr wchar_t kHarnessPlacementValue[] = L"DeepSeekHarnessWindow";
constexpr wchar_t kUiMutexName[] = L"Local\\MiaoDesk.Native.Harness.Ui.Singleton";
constexpr wchar_t kBackgroundMutexName[] = L"Local\\MiaoDesk.Native.Harness.Background.Singleton";
constexpr wchar_t kBackgroundStopEventName[] = L"Local\\MiaoDesk.Native.Harness.Background.Stop";
constexpr UINT_PTR kReadyTimerId = 1;
constexpr UINT kReadyPollMs = 250;
constexpr DWORD kSmokeTimeoutMs = 120000;

fs::path UserDataDirectory() {
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData,
                                                  static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) return {};
    const fs::path directory = fs::path(localAppData) / L"MiaoDesk" / L"WebView2" / L"Harness";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory;
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::wstring QuoteArg(std::wstring_view value) {
    return L"\"" + std::wstring(value) + L"\"";
}

bool NamedMutexExists(const wchar_t* name) {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

bool LaunchBackgroundHarnessOwner() {
    if (NamedMutexExists(kBackgroundMutexName)) return true;

    const std::wstring executable = ExecutablePath();
    if (executable.empty()) return false;
    std::wstring command = QuoteArg(executable);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr, fs::path(executable).parent_path().c_str(),
                                        &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    const ULONGLONG deadline = GetTickCount64() + 2000;
    do {
        if (NamedMutexExists(kBackgroundMutexName)) return true;
        Sleep(25);
    } while (GetTickCount64() < deadline);
    return NamedMutexExists(kBackgroundMutexName);
}

std::wstring HrText(HRESULT hr) {
    wchar_t text[64]{};
    swprintf_s(text, L"HRESULT 0x%08X", static_cast<unsigned>(hr));
    return text;
}

std::wstring HarnessLogHint() {
    const std::wstring logPath = miaodesk::HarnessProcessManager::LogPath();
    return logPath.empty() ? std::wstring{} : L"\r\n日志：" + logPath;
}

RECT HarnessWorkAreaForCursor() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) cursor = POINT{0, 0};
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) return info.rcWork;

    RECT work{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return work;
    work.right = GetSystemMetrics(SM_CXSCREEN);
    work.bottom = GetSystemMetrics(SM_CYSCREEN);
    return work;
}

RECT InitialHarnessWindowRect() {
    const RECT work = HarnessWorkAreaForCursor();
    const LONG workWidth = work.right - work.left;
    const LONG workHeight = work.bottom - work.top;

    LONG width = workWidth * 9 / 10;
    LONG height = workHeight * 9 / 10;
    if (width > 1100) width = 1100;
    if (height > 760) height = 760;
    if (width <= 0) width = workWidth;
    if (height <= 0) height = workHeight;

    RECT result{};
    result.left = work.left + (workWidth - width) / 2;
    result.top = work.top + (workHeight - height) / 2;
    result.right = result.left + width;
    result.bottom = result.top + height;
    return result;
}

void ApplyHarnessMaximizedWorkArea(HWND hwnd, MINMAXINFO* minMax) {
    if (!hwnd || !minMax) return;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return;

    const RECT& work = info.rcWork;
    const RECT& bounds = info.rcMonitor;
    minMax->ptMaxPosition.x = work.left - bounds.left;
    minMax->ptMaxPosition.y = work.top - bounds.top;
    minMax->ptMaxSize.x = work.right - work.left;
    minMax->ptMaxSize.y = work.bottom - work.top;
    minMax->ptMaxTrackSize = minMax->ptMaxSize;
}

bool HarnessWindowLayoutSelfTest() {
    const RECT work{100, 50, 1380, 730};
    const LONG workWidth = work.right - work.left;
    const LONG workHeight = work.bottom - work.top;
    LONG width = workWidth * 9 / 10;
    LONG height = workHeight * 9 / 10;
    if (width > 1100) width = 1100;
    if (height > 760) height = 760;
    RECT result{
        work.left + (workWidth - width) / 2,
        work.top + (workHeight - height) / 2,
        0,
        0,
    };
    result.right = result.left + width;
    result.bottom = result.top + height;
    return result.left >= work.left && result.top >= work.top &&
           result.right <= work.right && result.bottom <= work.bottom &&
           width == 1100 && height == 612;
}

int RunHarnessSmokeTest() {
    miaodesk::HarnessProcessManager harness;
    if (!harness.Start()) return 6;
    const bool ready = harness.WaitUntilReady(kSmokeTimeoutMs);
    harness.Stop();
    return ready ? 0 : 7;
}

class HarnessHost {
public:
    explicit HarnessHost(HINSTANCE instance) : instance_(instance) {}

    bool Create() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance_;
        wc.lpfnWndProc = &HarnessHost::WndProc;
        wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        RECT initialBounds = InitialHarnessWindowRect();
        miaodesk::window_placement::Load(kHarnessPlacementValue, initialBounds, 640, 480);
        hwnd_ = CreateWindowExW(0, kWindowClass, L"妙喵工作台 · DeepSeek Harness",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                initialBounds.left, initialBounds.top,
                                initialBounds.right - initialBounds.left,
                                initialBounds.bottom - initialBounds.top,
                                nullptr, nullptr, instance_, this);
        if (!hwnd_) return false;

        status_ = CreateWindowExW(0, L"EDIT", L"正在连接妙喵工作台…",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                      ES_MULTILINE | ES_CENTER | ES_READONLY | ES_NOHIDESEL,
                                  24, 24, 1100, 120, hwnd_, nullptr, instance_, nullptr);
        HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (font) {
            statusFont_ = font;
            SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(statusFont_), TRUE);
        }

        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);

        // WebView2 initialization is intentionally overlapped with DSH startup. Navigation only
        // happens after both sides are ready, so the two cold-start costs no longer add serially.
        InitializeWebView();

        if (harness_.ServiceReady()) {
            harnessReady_ = true;
            NavigateIfReady();
            return true;
        }

        if (!LaunchBackgroundHarnessOwner()) {
            SetStatus(L"妙喵工作台后台服务启动失败。" + HarnessLogHint());
            return true;
        }

        startedAt_ = GetTickCount64();
        nextStatusUpdate_ = startedAt_;
        UpdateStartingStatus(startedAt_);
        SetTimer(hwnd_, kReadyTimerId, kReadyPollMs, nullptr);
        return true;
    }

    int Run() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        HarnessHost* self = reinterpret_cast<HarnessHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<HarnessHost*>(create->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->HandleMessage(message, wParam, lParam)
                    : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_TIMER:
            if (wParam == kReadyTimerId) {
                PollHarness();
                return 0;
            }
            break;
        case WM_GETMINMAXINFO:
            ApplyHarnessMaximizedWorkArea(hwnd_, reinterpret_cast<MINMAXINFO*>(lParam));
            return 0;
        case WM_SIZE:
            ResizeWebView();
            ResizeStatus();
            return 0;
        case WM_EXITSIZEMOVE:
            miaodesk::window_placement::Save(hwnd_, kHarnessPlacementValue);
            return 0;
        case WM_SETFOCUS:
            if (webviewController_) webviewController_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            return 0;
        case WM_CLOSE:
            miaodesk::window_placement::Save(hwnd_, kHarnessPlacementValue);
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            miaodesk::window_placement::Save(hwnd_, kHarnessPlacementValue);
            KillTimer(hwnd_, kReadyTimerId);
            webview_.Reset();
            if (webviewController_) webviewController_->Close();
            webviewController_.Reset();
            // The UI never owns the DSH service process. Closing the workbench only closes UI;
            // the background owner stays warm until the MiaoDesk host asks it to stop.
            if (statusFont_) {
                DeleteObject(statusFont_);
                statusFont_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    void PollHarness() {
        if (harness_.ServiceReady()) {
            KillTimer(hwnd_, kReadyTimerId);
            harnessReady_ = true;
            SetStatus(L"妙喵工作台已就绪，正在打开界面…");
            NavigateIfReady();
            return;
        }

        if (!NamedMutexExists(kBackgroundMutexName)) {
            KillTimer(hwnd_, kReadyTimerId);
            SetStatus(L"妙喵工作台后台服务在 Web UI 就绪前退出。" + HarnessLogHint());
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= nextStatusUpdate_) {
            if (GetFocus() != status_) UpdateStartingStatus(now);
            nextStatusUpdate_ = now + 1000;
        }
    }

    void UpdateStartingStatus(ULONGLONG now) {
        const ULONGLONG elapsedSeconds = startedAt_ == 0 ? 0 : (now - startedAt_) / 1000;
        std::wstring text = L"正在连接妙喵工作台… 已等待 " + std::to_wstring(elapsedSeconds) + L" 秒。";
        text += L"\r\nDeepSeek Harness 正在后台预热；关闭此窗口不会停止后台服务。";
        if (elapsedSeconds >= 45) text += L"\r\n启动时间异常偏长，请检查共享 RuntimeCache 和下方日志。";
        text += HarnessLogHint();
        SetStatus(text);
    }

    void InitializeWebView() {
        if (webviewInitializing_ || webview_ || webViewReady_) return;
        webviewInitializing_ = true;

        const fs::path userData = UserDataDirectory();
        const std::wstring userDataText = userData.empty() ? std::wstring{} : userData.wstring();
        const HRESULT start = CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            userDataText.empty() ? nullptr : userDataText.c_str(),
            nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                    if (FAILED(result) || !environment || !IsWindow(hwnd_)) {
                        webviewInitializing_ = false;
                        SetStatus(L"WebView2 Runtime 初始化失败：" + HrText(result));
                        return S_OK;
                    }

                    return environment->CreateCoreWebView2Controller(
                        hwnd_,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                                webviewInitializing_ = false;
                                if (FAILED(controllerResult) || !controller || !IsWindow(hwnd_)) {
                                    SetStatus(L"WebView2 窗口创建失败：" + HrText(controllerResult));
                                    return S_OK;
                                }

                                webviewController_ = controller;
                                HRESULT hr = webviewController_->get_CoreWebView2(webview_.ReleaseAndGetAddressOf());
                                if (FAILED(hr) || !webview_) {
                                    SetStatus(L"WebView2 页面创建失败：" + HrText(hr));
                                    return S_OK;
                                }

                                webviewController_->put_IsVisible(FALSE);
                                ResizeWebView();
                                webViewReady_ = true;
                                NavigateIfReady();
                                return S_OK;
                            }).Get());
                }).Get());

        if (FAILED(start)) {
            webviewInitializing_ = false;
            SetStatus(L"WebView2 Loader 启动失败：" + HrText(start));
        }
    }

    void NavigateIfReady() {
        if (!harnessReady_ || !webViewReady_ || !webview_ || navigated_) return;
        const std::wstring url = miaodesk::HarnessProcessManager::DefaultUrl();
        const HRESULT hr = webview_->Navigate(url.c_str());
        if (FAILED(hr)) {
            SetStatus(L"打开妙喵工作台 Web UI 失败：" + HrText(hr));
            return;
        }
        navigated_ = true;
        webviewController_->put_IsVisible(TRUE);
        ShowWindow(status_, SW_HIDE);
    }

    void ResizeWebView() {
        if (!webviewController_ || !hwnd_) return;
        RECT bounds{};
        if (GetClientRect(hwnd_, &bounds)) webviewController_->put_Bounds(bounds);
    }

    void ResizeStatus() {
        if (!status_ || !hwnd_) return;
        RECT bounds{};
        if (!GetClientRect(hwnd_, &bounds)) return;
        SetWindowPos(status_, nullptr, 24, 24, (bounds.right - bounds.left) - 48, 140,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SetStatus(const std::wstring& text) {
        if (!status_ || !IsWindow(status_)) return;
        ShowWindow(status_, SW_SHOW);
        SetWindowTextW(status_, text.c_str());
        SendMessageW(status_, EM_SETSEL, 0, 0);
        UpdateWindow(status_);
    }

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND status_{};
    HFONT statusFont_{};
    ULONGLONG startedAt_{};
    ULONGLONG nextStatusUpdate_{};
    bool webviewInitializing_{};
    bool webViewReady_{};
    bool harnessReady_{};
    bool navigated_{};
    miaodesk::HarnessProcessManager harness_;
    ComPtr<ICoreWebView2Controller> webviewController_;
    ComPtr<ICoreWebView2> webview_;
};

void ActivateExistingHarnessWindow() {
    const HWND existing = FindWindowW(kWindowClass, nullptr);
    if (!existing) return;
    SetWindowTextW(existing, L"妙喵工作台 · DeepSeek Harness");
    ShowWindow(existing, SW_SHOWNORMAL);
    SetForegroundWindow(existing);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return 3;

    const std::wstring_view args = commandLine ? std::wstring_view(commandLine) : std::wstring_view{};
    if (args.find(L"--harness-smoke-test") != std::wstring_view::npos) {
        const int result = RunHarnessSmokeTest();
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    }
    if (args.find(L"--self-test") != std::wstring_view::npos) {
        const bool healthy = miaodesk::HarnessProcessManager::SelfTest() && HarnessWindowLayoutSelfTest();
        const int result = healthy ? 0 : 5;
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    }

    const bool showUi = args.find(L"--ui") != std::wstring_view::npos;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, showUi ? kUiMutexName : kBackgroundMutexName);
    if (!mutex) {
        if (SUCCEEDED(com)) CoUninitialize();
        return 2;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (showUi) ActivateExistingHarnessWindow();
        CloseHandle(mutex);
        if (SUCCEEDED(com)) CoUninitialize();
        return 0;
    }

    if (!showUi) {
        HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, kBackgroundStopEventName);
        if (!stopEvent) {
            CloseHandle(mutex);
            if (SUCCEEDED(com)) CoUninitialize();
            return 8;
        }

        miaodesk::HarnessProcessManager harness;
        if (!harness.Start()) {
            CloseHandle(stopEvent);
            CloseHandle(mutex);
            if (SUCCEEDED(com)) CoUninitialize();
            return 6;
        }

        while (harness.Running() && WaitForSingleObject(stopEvent, 500) == WAIT_TIMEOUT) {}
        const bool requestedStop = WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0;
        const DWORD exitCode = harness.ExitCode();
        harness.Stop();
        CloseHandle(stopEvent);
        CloseHandle(mutex);
        if (SUCCEEDED(com)) CoUninitialize();
        return requestedStop || exitCode == STILL_ACTIVE ? 0 : static_cast<int>(exitCode);
    }

    HarnessHost host(instance);
    if (!host.Create()) {
        CloseHandle(mutex);
        if (SUCCEEDED(com)) CoUninitialize();
        return 4;
    }

    const int result = host.Run();
    CloseHandle(mutex);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}