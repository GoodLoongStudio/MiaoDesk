#include "turingdesk/WebDesktopSurfaceChild.h"
#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/WidgetService.h"

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kSurfaceClass[] = L"TuringDesk.Native.WebWallpaperHost";
constexpr wchar_t kWidgetDragClass[] = L"TuringDesk.Native.WidgetDragHandle";
constexpr wchar_t kLocalVirtualHost[] = L"turingdesk-surface.local";
constexpr UINT kPauseMessage = WM_APP + 901;
constexpr UINT kResumeMessage = WM_APP + 902;
constexpr UINT kShutdownMessage = WM_APP + 903;

struct LaunchOptions {
    HWND parent{};
    RECT region{};
    std::wstring source;
    std::wstring token;
    std::wstring itemId;
    bool muted{true};
    bool widget{};
};

std::wstring Trim(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && std::iswspace(value.back())) value.pop_back();
    return value;
}

bool StartsWithInsensitive(std::wstring_view value, std::wstring_view prefix) noexcept {
    if (value.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(value[i]) != std::towlower(prefix[i])) return false;
    }
    return true;
}

bool IsHtmlFile(const fs::path& path) noexcept {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension == L".html" || extension == L".htm";
}

std::wstring HttpsOrigin(std::wstring_view input) {
    std::wstring value = Trim(std::wstring(input));
    if (!StartsWithInsensitive(value, L"https://")) return {};
    const std::size_t authorityStart = 8;
    const std::size_t end = value.find_first_of(L"/?#", authorityStart);
    std::wstring origin = end == std::wstring::npos ? value : value.substr(0, end);
    if (origin.size() <= authorityStart) return {};
    std::transform(origin.begin(), origin.end(), origin.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return origin;
}

std::wstring EncodeUrlSegment(std::wstring_view input) {
    if (input.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           input.data(), static_cast<int>(input.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string utf8(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            input.data(), static_cast<int>(input.size()),
                            utf8.data(), needed, nullptr, nullptr) <= 0) return {};
    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(utf8.size() * 3);
    for (unsigned char ch : utf8) {
        const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            result.push_back(static_cast<wchar_t>(ch));
        } else {
            result.push_back(L'%');
            result.push_back(hex[(ch >> 4) & 0x0F]);
            result.push_back(hex[ch & 0x0F]);
        }
    }
    return result;
}

std::wstring SafeToken(std::wstring value) {
    if (value.empty()) value = L"surface";
    for (auto& ch : value) {
        if (!(std::iswalnum(ch) || ch == L'-' || ch == L'_')) ch = L'_';
    }
    if (value.size() > 96) value.resize(96);
    return value;
}

fs::path UserDataDirectory(std::wstring_view token) {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path base = (length > 0 && length < std::size(local)) ? fs::path(local) : fs::temp_directory_path();
    fs::path directory = base / L"TuringDesk" / L"WebView2" / L"DesktopSurface" / SafeToken(std::wstring(token));
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory;
}

std::vector<std::wstring> ProcessArguments() {
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw || count <= 0) return {};
    std::vector<std::wstring> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) result.emplace_back(raw[i]);
    LocalFree(raw);
    return result;
}

std::optional<std::wstring> ArgValue(const std::vector<std::wstring>& args, std::wstring_view name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (_wcsicmp(args[i].c_str(), std::wstring(name).c_str()) == 0) return args[i + 1];
    }
    return std::nullopt;
}

bool HasArg(const std::vector<std::wstring>& args, std::wstring_view name) {
    return std::any_of(args.begin(), args.end(), [&](const std::wstring& value) {
        return _wcsicmp(value.c_str(), std::wstring(name).c_str()) == 0;
    });
}

bool ParseLong(const std::vector<std::wstring>& args, std::wstring_view name, LONG& output) {
    const auto value = ArgValue(args, name);
    if (!value) return false;
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value->c_str(), &end, 10);
    if (end == value->c_str() || *end != L'\0') return false;
    output = static_cast<LONG>(parsed);
    return true;
}

std::optional<LaunchOptions> ParseLaunchOptions() {
    const auto args = ProcessArguments();
    if (!HasArg(args, L"--web-wallpaper-host")) return std::nullopt;

    LaunchOptions options;
    const auto parent = ArgValue(args, L"--parent-hwnd");
    const auto source = ArgValue(args, L"--source");
    const auto token = ArgValue(args, L"--token");
    if (!parent || !source || !token) return LaunchOptions{};

    wchar_t* end = nullptr;
    const unsigned long long parentValue = _wcstoui64(parent->c_str(), &end, 10);
    if (end == parent->c_str() || *end != L'\0' || parentValue == 0) return LaunchOptions{};
    options.parent = reinterpret_cast<HWND>(static_cast<uintptr_t>(parentValue));

    if (!ParseLong(args, L"--left", options.region.left) ||
        !ParseLong(args, L"--top", options.region.top) ||
        !ParseLong(args, L"--right", options.region.right) ||
        !ParseLong(args, L"--bottom", options.region.bottom)) return LaunchOptions{};

    options.source = *source;
    options.token = *token;
    options.itemId = ArgValue(args, L"--item-id").value_or(L"web");
    const auto muted = ArgValue(args, L"--muted");
    options.muted = !muted || *muted != L"0";
    options.widget = options.itemId.rfind(L"widget-", 0) == 0 || options.itemId.rfind(L"widget_", 0) == 0;
    return options;
}

bool SupportedSource(const std::wstring& source) {
    if (!HttpsOrigin(source).empty()) return true;
    if (source.empty()) return false;
    std::error_code ec;
    const fs::path path(source);
    return IsHtmlFile(path) && fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

class WebDesktopSurfaceChild {
public:
    WebDesktopSurfaceChild(HINSTANCE instance, LaunchOptions options)
        : instance_(instance), options_(std::move(options)) {}

    bool Create() {
        if (!options_.parent || !IsWindow(options_.parent) ||
            options_.region.right <= options_.region.left ||
            options_.region.bottom <= options_.region.top ||
            !SupportedSource(options_.source)) {
            exitCode_ = 61;
            return false;
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance_;
        wc.lpfnWndProc = &WebDesktopSurfaceChild::WndProc;
        wc.lpszClassName = kSurfaceClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            exitCode_ = 62;
            return false;
        }

        const LONG width = options_.region.right - options_.region.left;
        const LONG height = options_.region.bottom - options_.region.top;
        const DWORD exStyle = WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOOLWINDOW |
                              (options_.widget ? 0 : WS_EX_TRANSPARENT);
        hwnd_ = CreateWindowExW(
            exStyle,
            kSurfaceClass,
            options_.token.c_str(),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            options_.region.left, options_.region.top, width, height,
            options_.parent, nullptr, instance_, this);
        if (!hwnd_) {
            exitCode_ = 63;
            return false;
        }
        if (!SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA)) {
            exitCode_ = 64;
            DestroyWindow(hwnd_);
            return false;
        }

        SetPropW(hwnd_, kWebSurfaceRoleProperty,
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(options_.widget ? 2 : 1)));
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        if (options_.widget && !CreateWidgetDragHandle()) {
            exitCode_ = 74;
            DestroyWindow(hwnd_);
            return false;
        }
        InitializeWebView();
        return true;
    }

    int Run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return exitCode_ == 0 ? static_cast<int>(message.wParam) : exitCode_;
    }

private:
    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<WebDesktopSurfaceChild*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<WebDesktopSurfaceChild*>(create->lpCreateParams);
            if (self) {
                self->hwnd_ = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
        }
        return self ? self->HandleMessage(message, wParam, lParam)
                    : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM, LPARAM lParam) {
        switch (message) {
        case WM_NCHITTEST:
            return options_.widget ? HTCLIENT : HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            ResizeController();
            ResizeWidgetDragHandle();
            return 0;
        case kPauseMessage:
            Pause();
            return 0;
        case kResumeMessage:
            Resume();
            return 0;
        case kShutdownMessage:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            ClearReadyProperties();
            webview_.Reset();
            if (controller_) controller_->Close();
            controller_.Reset();
            PostQuitMessage(exitCode_);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd_, message, 0, lParam);
    }

    static LRESULT CALLBACK WidgetDragProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<WebDesktopSurfaceChild*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<WebDesktopSurfaceChild*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
        case WM_NCHITTEST: return HTCLIENT;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        case WM_LBUTTONDOWN:
            if (self->BeginWidgetDrag()) SetCapture(window);
            return 0;
        case WM_MOUSEMOVE:
            if (self->dragging_ && GetCapture() == window) self->UpdateWidgetDrag();
            return 0;
        case WM_LBUTTONUP:
            if (self->dragging_) self->EndWidgetDrag(true);
            return 0;
        case WM_CAPTURECHANGED:
            if (self->dragging_) self->EndWidgetDrag(true);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT rect{};
            GetClientRect(window, &rect);
            const LONG gripW = std::clamp<LONG>(rect.right - rect.left - 12, 24L, 48L);
            const LONG center = (rect.left + rect.right) / 2;
            RECT grip{center - gripW / 2, 7, center + gripW / 2, 11};
            HBRUSH brush = CreateSolidBrush(RGB(196, 202, 214));
            HPEN pen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            RoundRect(dc, grip.left, grip.top, grip.right, grip.bottom, 4, 4);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
            DeleteObject(brush);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_NCDESTROY:
            if (self->dragHandle_ == window) self->dragHandle_ = nullptr;
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    bool CreateWidgetDragHandle() {
        if (!options_.widget || !hwnd_) return true;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance_;
        wc.lpfnWndProc = &WebDesktopSurfaceChild::WidgetDragProc;
        wc.lpszClassName = kWidgetDragClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        dragHandle_ = CreateWindowExW(
            WS_EX_NOACTIVATE,
            kWidgetDragClass, L"", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, hwnd_, nullptr, instance_, this);
        if (!dragHandle_) return false;
        ResizeWidgetDragHandle();
        return true;
    }

    void RaiseWidgetDragHandle() {
        if (!dragHandle_ || !IsWindow(dragHandle_)) return;
        SetWindowPos(dragHandle_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void ResizeWidgetDragHandle() {
        if (!dragHandle_ || !IsWindow(dragHandle_) || !hwnd_) return;
        RECT client{};
        if (!GetClientRect(hwnd_, &client)) return;
        SetWindowPos(dragHandle_, HWND_TOP, 0, 0,
                     std::max<LONG>(1, client.right - client.left),
                     std::max<LONG>(1, client.bottom - client.top),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RaiseWidgetDragHandle();
        InvalidateRect(dragHandle_, nullptr, FALSE);
    }

    std::wstring WidgetId() const {
        if (!options_.widget) return {};
        std::wstring id = options_.itemId;
        if (id.rfind(L"widget-", 0) == 0 || id.rfind(L"widget_", 0) == 0) id.erase(0, 7);
        return id;
    }

    bool BeginWidgetDrag() {
        const std::wstring id = WidgetId();
        if (id.empty() || !hwnd_ || !options_.parent) return false;
        DesktopWidgetStore store;
        std::wstring ignored;
        if (!store.Load(&ignored)) return false;
        const auto found = store.Find(id);
        if (!found || found->width <= 0.001f || found->height <= 0.001f) return false;

        RECT screenRect{};
        if (!GetWindowRect(hwnd_, &screenRect)) return false;
        POINT corners[2] = {{screenRect.left, screenRect.top}, {screenRect.right, screenRect.bottom}};
        MapWindowPoints(nullptr, options_.parent, corners, 2);
        dragStartRegion_ = RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
        if (!GetCursorPos(&dragStartCursor_)) return false;

        dragStartWidget_ = *found;
        dragPreviewX_ = found->x;
        dragPreviewY_ = found->y;
        const float widthPx = static_cast<float>(std::max<LONG>(1, dragStartRegion_.right - dragStartRegion_.left));
        const float heightPx = static_cast<float>(std::max<LONG>(1, dragStartRegion_.bottom - dragStartRegion_.top));
        dragMonitorWidthPx_ = widthPx / found->width;
        dragMonitorHeightPx_ = heightPx / found->height;
        dragging_ = dragMonitorWidthPx_ > 1.0f && dragMonitorHeightPx_ > 1.0f;
        return dragging_;
    }

    void UpdateWidgetDrag() {
        if (!dragging_ || !hwnd_) return;
        POINT cursor{};
        if (!GetCursorPos(&cursor)) return;
        const int dx = cursor.x - dragStartCursor_.x;
        const int dy = cursor.y - dragStartCursor_.y;
        const float maxX = std::max(0.0f, 1.0f - dragStartWidget_.width);
        const float maxY = std::max(0.0f, 1.0f - dragStartWidget_.height);
        dragPreviewX_ = std::clamp(dragStartWidget_.x + static_cast<float>(dx) / dragMonitorWidthPx_, 0.0f, maxX);
        dragPreviewY_ = std::clamp(dragStartWidget_.y + static_cast<float>(dy) / dragMonitorHeightPx_, 0.0f, maxY);
        const LONG appliedX = static_cast<LONG>(std::lround((dragPreviewX_ - dragStartWidget_.x) * dragMonitorWidthPx_));
        const LONG appliedY = static_cast<LONG>(std::lround((dragPreviewY_ - dragStartWidget_.y) * dragMonitorHeightPx_));
        const LONG width = dragStartRegion_.right - dragStartRegion_.left;
        const LONG height = dragStartRegion_.bottom - dragStartRegion_.top;
        SetWindowPos(hwnd_, nullptr, dragStartRegion_.left + appliedX, dragStartRegion_.top + appliedY,
                     width, height, SWP_NOACTIVATE | SWP_NOZORDER);
        options_.region = RECT{dragStartRegion_.left + appliedX, dragStartRegion_.top + appliedY,
                               dragStartRegion_.left + appliedX + width, dragStartRegion_.top + appliedY + height};
    }

    void EndWidgetDrag(bool persist) {
        if (!dragging_) return;
        dragging_ = false;
        if (GetCapture() == dragHandle_) ReleaseCapture();
        if (!persist) {
            const LONG width = dragStartRegion_.right - dragStartRegion_.left;
            const LONG height = dragStartRegion_.bottom - dragStartRegion_.top;
            SetWindowPos(hwnd_, nullptr, dragStartRegion_.left, dragStartRegion_.top, width, height,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            options_.region = dragStartRegion_;
            return;
        }
        const std::wstring id = WidgetId();
        if (id.empty()) return;
        desktop::WidgetUpdateRequest request;
        request.id = id;
        request.x = dragPreviewX_;
        request.y = dragPreviewY_;
        const desktop::WidgetService service;
        const auto result = service.Update(request);
        if (!result.success) {
            const LONG width = dragStartRegion_.right - dragStartRegion_.left;
            const LONG height = dragStartRegion_.bottom - dragStartRegion_.top;
            SetWindowPos(hwnd_, nullptr, dragStartRegion_.left, dragStartRegion_.top, width, height,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            options_.region = dragStartRegion_;
        }
    }

    void SetReadyProperty(const wchar_t* name, bool ready) {
        if (!hwnd_ || !IsWindow(hwnd_)) return;
        if (ready) SetPropW(hwnd_, name, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
        else RemovePropW(hwnd_, name);
    }

    void ClearReadyProperties() {
        if (!hwnd_) return;
        RemovePropW(hwnd_, kWebSurfaceEnvironmentReadyProperty);
        RemovePropW(hwnd_, kWebSurfaceControllerReadyProperty);
        RemovePropW(hwnd_, kWebSurfaceNavigationReadyProperty);
        RemovePropW(hwnd_, kWebSurfaceRoleProperty);
    }

    void Fail(int code) {
        if (exitCode_ == 0) exitCode_ = code;
        if (hwnd_ && IsWindow(hwnd_)) PostMessageW(hwnd_, kShutdownMessage, 0, 0);
    }

    bool AllowedNavigation(std::wstring_view uri) const {
        if (uri.empty() || StartsWithInsensitive(uri, L"about:blank")) return true;
        if (allowedOrigin_.empty() || !StartsWithInsensitive(uri, allowedOrigin_)) return false;
        if (uri.size() == allowedOrigin_.size()) return true;
        const wchar_t separator = uri[allowedOrigin_.size()];
        return separator == L'/' || separator == L'?' || separator == L'#';
    }

    std::wstring ResolveNavigationTarget(ComPtr<ICoreWebView2_3>& webview3) {
        const std::wstring httpsOrigin = HttpsOrigin(options_.source);
        if (!httpsOrigin.empty()) {
            allowedOrigin_ = httpsOrigin;
            return options_.source;
        }

        std::error_code ec;
        fs::path source = fs::absolute(fs::path(options_.source), ec);
        if (ec) source = fs::path(options_.source);
        source = source.lexically_normal();
        if (!webview3 || !fs::exists(source, ec) || !fs::is_regular_file(source, ec) || !IsHtmlFile(source)) return {};
        if (FAILED(webview3->SetVirtualHostNameToFolderMapping(
                kLocalVirtualHost, source.parent_path().c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) return {};
        allowedOrigin_ = L"https://" + std::wstring(kLocalVirtualHost);
        const std::wstring encodedName = EncodeUrlSegment(source.filename().wstring());
        return encodedName.empty() ? std::wstring{} : allowedOrigin_ + L"/" + encodedName;
    }

    void ConfigureSecurity() {
        if (!webview_) return;
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(webview_->get_Settings(settings.GetAddressOf())) && settings) {
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
        }

        EventRegistrationToken navigationStarting{};
        webview_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    if (args && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                        const std::wstring value(uri);
                        CoTaskMemFree(uri);
                        if (!AllowedNavigation(value)) args->put_Cancel(TRUE);
                    }
                    return S_OK;
                }).Get(), &navigationStarting);

        EventRegistrationToken navigationCompleted{};
        webview_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    BOOL success = FALSE;
                    if (!args || FAILED(args->get_IsSuccess(&success)) || !success) {
                        Fail(70);
                        return S_OK;
                    }
                    navigationReady_ = true;
                    SetReadyProperty(kWebSurfaceNavigationReadyProperty, true);
                    if (controller_) controller_->put_IsVisible(TRUE);
                    return S_OK;
                }).Get(), &navigationCompleted);

        EventRegistrationToken newWindow{};
        webview_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    if (args) args->put_Handled(TRUE);
                    return S_OK;
                }).Get(), &newWindow);

        EventRegistrationToken permission{};
        webview_->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                    if (args) args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                    return S_OK;
                }).Get(), &permission);

        EventRegistrationToken processFailed{};
        webview_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                    if (!paused_) Fail(71);
                    return S_OK;
                }).Get(), &processFailed);

        ComPtr<ICoreWebView2_4> webview4;
        if (SUCCEEDED(webview_.As(&webview4)) && webview4) {
            EventRegistrationToken download{};
            webview4->add_DownloadStarting(
                Callback<ICoreWebView2DownloadStartingEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                        if (args) {
                            args->put_Cancel(TRUE);
                            args->put_Handled(TRUE);
                        }
                        return S_OK;
                    }).Get(), &download);
        }

        ComPtr<ICoreWebView2_8> webview8;
        if (SUCCEEDED(webview_.As(&webview8)) && webview8)
            webview8->put_IsMuted(options_.muted ? TRUE : FALSE);
    }

    void InitializeWebView() {
        const fs::path userData = UserDataDirectory(options_.token);
        const std::wstring userDataText = userData.wstring();
        const HRESULT start = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, userDataText.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                    if (FAILED(result) || !environment || !hwnd_ || !IsWindow(hwnd_)) {
                        Fail(65);
                        return S_OK;
                    }
                    SetReadyProperty(kWebSurfaceEnvironmentReadyProperty, true);
                    return environment->CreateCoreWebView2Controller(
                        hwnd_,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(controllerResult) || !controller || !hwnd_ || !IsWindow(hwnd_)) {
                                    Fail(66);
                                    return S_OK;
                                }
                                controller_ = controller;
                                if (FAILED(controller_->get_CoreWebView2(webview_.ReleaseAndGetAddressOf())) || !webview_) {
                                    Fail(67);
                                    return S_OK;
                                }
                                SetReadyProperty(kWebSurfaceControllerReadyProperty, true);
                                ComPtr<ICoreWebView2_3> webview3;
                                webview_.As(&webview3);
                                const std::wstring target = ResolveNavigationTarget(webview3);
                                if (target.empty()) {
                                    Fail(68);
                                    return S_OK;
                                }
                                ConfigureSecurity();
                                ResizeController();
                                controller_->put_IsVisible(FALSE);
                                if (FAILED(webview_->Navigate(target.c_str()))) {
                                    Fail(69);
                                    return S_OK;
                                }
                                return S_OK;
                            }).Get());
                }).Get());
        if (FAILED(start)) Fail(72);
    }

    void ResizeController() {
        if (!controller_ || !hwnd_) return;
        RECT bounds{};
        if (GetClientRect(hwnd_, &bounds)) controller_->put_Bounds(bounds);
        ResizeWidgetDragHandle();
        RaiseWidgetDragHandle();
    }

    void Pause() {
        paused_ = true;
        if (webview_) {
            ComPtr<ICoreWebView2_3> webview3;
            if (SUCCEEDED(webview_.As(&webview3)) && webview3) {
                webview3->TrySuspend(
                    Callback<ICoreWebView2TrySuspendCompletedHandler>(
                        [](HRESULT, BOOL) -> HRESULT { return S_OK; }).Get());
            }
        }
        if (controller_) controller_->put_IsVisible(FALSE);
    }

    void Resume() {
        paused_ = false;
        if (webview_) {
            ComPtr<ICoreWebView2_3> webview3;
            if (SUCCEEDED(webview_.As(&webview3)) && webview3) webview3->Resume();
        }
        if (controller_ && navigationReady_) controller_->put_IsVisible(TRUE);
    }

    HINSTANCE instance_{};
    LaunchOptions options_;
    HWND hwnd_{};
    HWND dragHandle_{};
    bool dragging_{};
    POINT dragStartCursor_{};
    RECT dragStartRegion_{};
    DesktopWidget dragStartWidget_{};
    float dragPreviewX_{};
    float dragPreviewY_{};
    float dragMonitorWidthPx_{};
    float dragMonitorHeightPx_{};
    int exitCode_{};
    bool paused_{};
    bool navigationReady_{};
    std::wstring allowedOrigin_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
};

} // namespace

int TryRunWebDesktopSurfaceChild(HINSTANCE instance) {
    const auto parsed = ParseLaunchOptions();
    if (!parsed) return -1;
    if (!parsed->parent || parsed->source.empty() || parsed->token.empty()) return 60;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return 73;

    WebDesktopSurfaceChild child(instance, *parsed);
    int result = 61;
    if (child.Create()) result = child.Run();
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

} // namespace turingdesk::wallpaper
