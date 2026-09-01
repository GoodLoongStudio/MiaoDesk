#include "miaodesk/NativeWidgetHost.h"
#include "miaodesk/AppPaths.h"

#include "miaodesk/DesktopShellHost.h"
#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/NativeWidgetPainter.h"
#include "miaodesk/NativeWidgetPreset.h"
#include "miaodesk/NativeWeatherService.h"
#include "miaodesk/WallpaperMonitorLayout.h"
#include "miaodesk/WebDesktopSurfaceChild.h"
#include "miaodesk/WidgetService.h"

#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kNativeHostMode[] = L"--native-widget-host";
constexpr wchar_t kWidgetDragClass[] = L"MiaoDesk.Native.WidgetDragHandle";
constexpr UINT kPauseMessage = WM_APP + 911;
constexpr UINT kResumeMessage = WM_APP + 912;
constexpr UINT kShutdownMessage = WM_APP + 913;
constexpr UINT kWeatherUpdatedMessage = WM_APP + 914;
constexpr UINT_PTR kSyncTimerId = 71;
constexpr UINT_PTR kRefreshTimerId = 72;
constexpr UINT kRefreshSchedulerTickMs = 1000;
constexpr wchar_t kNativeHostMessageClass[] = L"MiaoDesk.Native.WidgetHostMessage";

std::wstring ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

std::wstring QuoteArg(std::wstring_view value) {
    std::wstring quoted = L"\"";
    quoted.append(value);
    quoted.push_back(L'"');
    return quoted;
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

RECT MapDesktopRectToParent(HWND parent, const RECT& desktopRect) {
    if (!parent) return desktopRect;
    POINT corners[2] = {{desktopRect.left, desktopRect.top}, {desktopRect.right, desktopRect.bottom}};
    if (MapWindowPoints(nullptr, parent, corners, 2) == 0 && GetLastError() != ERROR_SUCCESS) return desktopRect;
    return RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
}

RECT WidgetRegionInDesktop(const MonitorInfo& monitor, const DesktopWidget& widget) {
    const LONG monitorWidth = std::max<LONG>(1, monitor.desktopRect.right - monitor.desktopRect.left);
    const LONG monitorHeight = std::max<LONG>(1, monitor.desktopRect.bottom - monitor.desktopRect.top);
    RECT region{};
    region.left = monitor.desktopRect.left + static_cast<LONG>(std::lround(widget.x * monitorWidth));
    region.top = monitor.desktopRect.top + static_cast<LONG>(std::lround(widget.y * monitorHeight));
    region.right = region.left + static_cast<LONG>(std::lround(widget.width * monitorWidth));
    region.bottom = region.top + static_cast<LONG>(std::lround(widget.height * monitorHeight));
    return region;
}

const MonitorInfo* PrimaryMonitor(const MonitorTopology& topology) {
    for (const auto& monitor : topology.monitors) {
        if (monitor.primary) return &monitor;
    }
    return topology.monitors.empty() ? nullptr : &topology.monitors.front();
}

std::wstring SlotToken(const DesktopWidget& widget, const RECT& region) {
    return L"widget-" + widget.id + L"-0-" + std::to_wstring(region.left) + L"-" + std::to_wstring(region.top) +
           L"-" + std::to_wstring(region.right) + L"-" + std::to_wstring(region.bottom);
}

void SetReadyProperty(HWND hwnd, const wchar_t* name, bool ready) {
    if (!hwnd || !IsWindow(hwnd)) return;
    if (ready) SetPropW(hwnd, name, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
    else RemovePropW(hwnd, name);
}

void MarkNativeSurfaceReady(HWND hwnd) {
    SetReadyProperty(hwnd, kWebSurfaceEnvironmentReadyProperty, true);
    SetReadyProperty(hwnd, kWebSurfaceControllerReadyProperty, true);
    SetReadyProperty(hwnd, kWebSurfaceNavigationReadyProperty, true);
    SetPropW(hwnd, kWebSurfaceRoleProperty, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(2)));
}

fs::path WallpaperConfigPath() {
    return paths::StateFile(L"wallpaper.ini");
}

void WriteDiagnostics(const std::wstring& value) {
    WritePrivateProfileStringW(L"Diagnostics", L"WidgetRuntime", value.c_str(), WallpaperConfigPath().c_str());
}

struct NativeWidgetHostApp;

LRESULT CALLBACK NativeHostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

struct NativeSlot {
    NativeWidgetHostApp* owner{};
    std::wstring widgetId;
    NativeWidgetPreset preset{NativeWidgetPreset::GlassClock};
    RECT region{};
    RECT desktopRegion{};
    HWND hwnd{};
    HWND dragHandle{};
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<IDWriteFactory> dwrite;
    bool dragging{};
    POINT dragStartCursor_{};
    RECT dragStartRegion_{};
    DesktopWidget dragStartWidget_{};
    float dragPreviewX_{};
    float dragPreviewY_{};
    float dragMonitorWidthPx_{};
    float dragMonitorHeightPx_{};
    ULONGLONG geometryGraceUntil{};
    ULONGLONG nextRefreshAt{};
};

float NativeCornerRadiusDip(NativeWidgetPreset preset, float widthDip) {
    switch (preset) {
    case NativeWidgetPreset::GlassClock: return std::clamp(widthDip * 0.075f, 22.0f, 34.0f);
    case NativeWidgetPreset::WeatherGlass: return std::clamp(widthDip * 0.078f, 22.0f, 32.0f);
    case NativeWidgetPreset::TodayTasks: return std::clamp(widthDip * 0.070f, 22.0f, 32.0f);
    }
    return 24.0f;
}

void ApplyRoundedWindowRegion(NativeSlot& slot) {
    if (!slot.hwnd || !IsWindow(slot.hwnd)) return;
    RECT client{};
    if (!GetClientRect(slot.hwnd, &client)) return;
    const int widthPx = std::max<LONG>(1, client.right - client.left);
    const int heightPx = std::max<LONG>(1, client.bottom - client.top);
    const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(slot.hwnd));
    const float widthDip = static_cast<float>(widthPx) * USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi);
    const int radiusPx = std::max(1, MulDiv(static_cast<int>(std::lround(NativeCornerRadiusDip(slot.preset, widthDip))),
                                            static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    HRGN region = CreateRoundRectRgn(0, 0, widthPx + 1, heightPx + 1, radiusPx * 2, radiusPx * 2);
    if (!region) return;
    if (SetWindowRgn(slot.hwnd, region, TRUE) == 0) DeleteObject(region);
    // On success ownership of the region transfers to Windows.
}

struct NativeWidgetHostApp {
    HINSTANCE instance{};
    HWND parent{};
    HWND messageWindow{};
    bool paused{};
    bool weatherStarted{};
    NativeWeatherService weatherService;
    ComPtr<ID2D1Factory> d2dFactory;
    std::vector<std::unique_ptr<NativeSlot>> slots;

    bool EnsureFactories() {
        if (d2dFactory) return true;
        return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf()));
    }

    bool EnsureRenderTarget(NativeSlot& slot) {
        if (slot.target || !slot.hwnd || !d2dFactory) return slot.target != nullptr;
        RECT rc{};
        if (!GetClientRect(slot.hwnd, &rc)) return false;
        const UINT width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
        const auto props = D2D1::HwndRenderTargetProperties(
            slot.hwnd, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
        if (FAILED(d2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), props, slot.target.GetAddressOf()))) return false;
        if (!slot.dwrite) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(slot.dwrite.GetAddressOf()));
        slot.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        return true;
    }

    void ScheduleNextRefresh(NativeSlot& slot) {
        const std::uint32_t interval = NativePresetRefreshIntervalMs(slot.preset);
        if (interval == 0) {
            slot.nextRefreshAt = 0;
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (slot.preset == NativeWidgetPreset::GlassClock && interval >= 60000) {
            SYSTEMTIME local{};
            GetLocalTime(&local);
            const std::uint32_t elapsedInMinute =
                static_cast<std::uint32_t>(local.wSecond) * 1000u + local.wMilliseconds;
            const std::uint32_t delay = std::max<std::uint32_t>(250u, 60000u - elapsedInMinute);
            slot.nextRefreshAt = now + delay;
            return;
        }
        slot.nextRefreshAt = now + interval;
    }

    void PaintSlot(NativeSlot& slot) {
        if (!EnsureRenderTarget(slot)) return;
        NativeWidgetPaintContext context{};
        context.target = slot.target.Get();
        context.dwrite = slot.dwrite.Get();
        const D2D1_SIZE_F dipSize = slot.target->GetSize();
        context.width = std::max(1.0f, dipSize.width);
        context.height = std::max(1.0f, dipSize.height);
        NativeWeatherSnapshot weather;
        if (slot.preset == NativeWidgetPreset::GlassClock) {
            GetLocalTime(&context.localTime);
            context.hasTime = true;
        } else if (slot.preset == NativeWidgetPreset::WeatherGlass) {
            weather = weatherService.Snapshot();
            context.weather = &weather;
        }
        slot.target->BeginDraw();
        PaintNativeWidgetPreset(context, slot.preset);
        const HRESULT drawResult = slot.target->EndDraw();
        if (SUCCEEDED(drawResult)) {
            ScheduleNextRefresh(slot);
        } else if (drawResult == D2DERR_RECREATE_TARGET) {
            slot.target.Reset();
            slot.nextRefreshAt = 0;
        }
    }

    void ResizeDragHandle(NativeSlot& slot) {
        if (!slot.dragHandle || !slot.hwnd) return;
        RECT client{};
        if (!GetClientRect(slot.hwnd, &client)) return;
        SetWindowPos(slot.dragHandle, HWND_TOP, 0, 0,
                     std::max<LONG>(1, client.right - client.left),
                     std::max<LONG>(1, client.bottom - client.top),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    bool BeginDrag(NativeSlot& slot) {
        if (!slot.hwnd || !parent) return false;
        DesktopWidgetStore store;
        std::wstring ignored;
        if (!store.Load(&ignored)) return false;
        const auto found = store.Find(slot.widgetId);
        if (!found || found->width <= 0.001f || found->height <= 0.001f) return false;

        RECT screenRect{};
        if (!GetWindowRect(slot.hwnd, &screenRect)) return false;
        POINT corners[2] = {{screenRect.left, screenRect.top}, {screenRect.right, screenRect.bottom}};
        MapWindowPoints(nullptr, parent, corners, 2);
        slot.dragStartRegion_ = RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
        if (!GetCursorPos(&slot.dragStartCursor_)) return false;
        slot.dragStartWidget_ = *found;
        slot.dragPreviewX_ = found->x;
        slot.dragPreviewY_ = found->y;
        const float widthPx = static_cast<float>(std::max<LONG>(1, slot.dragStartRegion_.right - slot.dragStartRegion_.left));
        const float heightPx = static_cast<float>(std::max<LONG>(1, slot.dragStartRegion_.bottom - slot.dragStartRegion_.top));
        slot.dragMonitorWidthPx_ = widthPx / found->width;
        slot.dragMonitorHeightPx_ = heightPx / found->height;
        slot.dragging = slot.dragMonitorWidthPx_ > 1.0f && slot.dragMonitorHeightPx_ > 1.0f;
        return slot.dragging;
    }

    void UpdateDrag(NativeSlot& slot) {
        if (!slot.dragging || !slot.hwnd) return;
        POINT cursor{};
        if (!GetCursorPos(&cursor)) return;
        const int dx = cursor.x - slot.dragStartCursor_.x;
        const int dy = cursor.y - slot.dragStartCursor_.y;
        const float maxX = std::max(0.0f, 1.0f - slot.dragStartWidget_.width);
        const float maxY = std::max(0.0f, 1.0f - slot.dragStartWidget_.height);
        slot.dragPreviewX_ = std::clamp(slot.dragStartWidget_.x + static_cast<float>(dx) / slot.dragMonitorWidthPx_, 0.0f, maxX);
        slot.dragPreviewY_ = std::clamp(slot.dragStartWidget_.y + static_cast<float>(dy) / slot.dragMonitorHeightPx_, 0.0f, maxY);
        const LONG appliedX = static_cast<LONG>(std::lround((slot.dragPreviewX_ - slot.dragStartWidget_.x) * slot.dragMonitorWidthPx_));
        const LONG appliedY = static_cast<LONG>(std::lround((slot.dragPreviewY_ - slot.dragStartWidget_.y) * slot.dragMonitorHeightPx_));
        const LONG width = slot.dragStartRegion_.right - slot.dragStartRegion_.left;
        const LONG height = slot.dragStartRegion_.bottom - slot.dragStartRegion_.top;
        SetWindowPos(slot.hwnd, nullptr, slot.dragStartRegion_.left + appliedX, slot.dragStartRegion_.top + appliedY,
                     width, height, SWP_NOACTIVATE | SWP_NOZORDER);
    }

    void EndDrag(NativeSlot& slot, bool persist) {
        if (!slot.dragging) return;
        slot.dragging = false;
        if (GetCapture() == slot.dragHandle) ReleaseCapture();
        if (GetCapture() == slot.hwnd) ReleaseCapture();
        if (!persist) {
            const LONG width = slot.dragStartRegion_.right - slot.dragStartRegion_.left;
            const LONG height = slot.dragStartRegion_.bottom - slot.dragStartRegion_.top;
            SetWindowPos(slot.hwnd, nullptr, slot.dragStartRegion_.left, slot.dragStartRegion_.top, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
            return;
        }
        const float maxX = std::max(0.0f, 1.0f - slot.dragStartWidget_.width);
        const float maxY = std::max(0.0f, 1.0f - slot.dragStartWidget_.height);
        desktop::WidgetUpdateRequest request;
        request.id = slot.widgetId;
        request.x = std::clamp(slot.dragPreviewX_, 0.0f, maxX);
        request.y = std::clamp(slot.dragPreviewY_, 0.0f, maxY);
        const desktop::WidgetService service;
        const auto result = service.Update(request);
        if (!result.success) {
            const LONG width = slot.dragStartRegion_.right - slot.dragStartRegion_.left;
            const LONG height = slot.dragStartRegion_.bottom - slot.dragStartRegion_.top;
            SetWindowPos(slot.hwnd, nullptr, slot.dragStartRegion_.left, slot.dragStartRegion_.top, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
            return;
        }
        DesktopWidgetStore store;
        std::wstring ignored;
        if (store.Load(&ignored)) {
            if (const auto updated = store.Find(slot.widgetId)) {
                const DesktopWidget widget = DesktopWidgetStore::Normalize(*updated);
                const MonitorTopology topology = QueryMonitorTopology();
                if (topology.Valid()) {
                    if (const MonitorInfo* monitor = widget.monitorId.empty() ? PrimaryMonitor(topology)
                                                                              : FindMonitorByStableId(topology, widget.monitorId)) {
                        slot.desktopRegion = WidgetRegionInDesktop(*monitor, widget);
                    }
                }
            }
        }
        if (slot.hwnd && IsWindow(slot.hwnd)) {
            RECT screenRect{};
            if (GetWindowRect(slot.hwnd, &screenRect)) {
                POINT corners[2] = {{screenRect.left, screenRect.top}, {screenRect.right, screenRect.bottom}};
                MapWindowPoints(nullptr, parent, corners, 2);
                slot.region = RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
            }
        }
        slot.geometryGraceUntil = GetTickCount64() + 2000;
    }

    static LRESULT CALLBACK DragProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* slot = reinterpret_cast<NativeSlot*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            slot = static_cast<NativeSlot*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(slot));
        }
        if (!slot || !slot->owner) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
        case WM_NCHITTEST: return HTCLIENT;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); return TRUE;
        case WM_LBUTTONDOWN:
            if (slot->owner->BeginDrag(*slot)) SetCapture(window);
            return 0;
        case WM_MOUSEMOVE:
            if (slot->dragging && GetCapture() == window) slot->owner->UpdateDrag(*slot);
            return 0;
        case WM_LBUTTONUP:
            if (slot->dragging) slot->owner->EndDrag(*slot, true);
            return 0;
        case WM_CAPTURECHANGED:
            if (slot->dragging) slot->owner->EndDrag(*slot, true);
            return 0;
        case WM_ERASEBKGND: return 1;
        default: break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK SurfaceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* slot = reinterpret_cast<NativeSlot*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            slot = static_cast<NativeSlot*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(slot));
        }
        if (!slot || !slot->owner) return DefWindowProcW(hwnd, message, wParam, lParam);
        switch (message) {
        case WM_NCHITTEST: return HTCLIENT;
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONDOWN:
            if (slot->owner->BeginDrag(*slot)) SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (slot->dragging && GetCapture() == hwnd) slot->owner->UpdateDrag(*slot);
            return 0;
        case WM_LBUTTONUP:
            if (slot->dragging) slot->owner->EndDrag(*slot, true);
            return 0;
        case WM_CAPTURECHANGED:
            if (slot->dragging && reinterpret_cast<HWND>(lParam) != hwnd) slot->owner->EndDrag(*slot, true);
            return 0;
        case WM_SIZE:
            if (slot->target) slot->target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            ApplyRoundedWindowRegion(*slot);
            slot->owner->ResizeDragHandle(*slot);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            slot->owner->PaintSlot(*slot);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            if (slot->dragHandle && IsWindow(slot->dragHandle)) DestroyWindow(slot->dragHandle);
            slot->hwnd = nullptr;
            slot->dragHandle = nullptr;
            slot->target.Reset();
            return 0;
        default: break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool EnsureClasses() {
        WNDCLASSEXW surface{};
        surface.cbSize = sizeof(surface);
        surface.hInstance = instance;
        surface.lpfnWndProc = &NativeWidgetHostApp::SurfaceProc;
        surface.lpszClassName = kNativeWidgetSurfaceClass;
        surface.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&surface) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        WNDCLASSEXW drag{};
        drag.cbSize = sizeof(drag);
        drag.hInstance = instance;
        drag.lpfnWndProc = &NativeWidgetHostApp::DragProc;
        drag.lpszClassName = kWidgetDragClass;
        drag.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
        return RegisterClassExW(&drag) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    void DestroySlot(NativeSlot& slot) {
        if (slot.hwnd && IsWindow(slot.hwnd)) DestroyWindow(slot.hwnd);
        slot.hwnd = nullptr;
        slot.dragHandle = nullptr;
        slot.target.Reset();
    }

    NativeSlot* FindSlot(std::wstring_view id) {
        for (const auto& slot : slots) {
            if (slot && slot->widgetId == id) return slot.get();
        }
        return nullptr;
    }

    bool AttachSlotSurface(NativeSlot& slot, const RECT& desktopRegion) {
        if (!slot.hwnd || !IsWindow(slot.hwnd)) return false;
        slot.desktopRegion = desktopRegion;
        DesktopShellHost shell;
        std::wstring error;
        const bool visible = !paused && (IsWindowVisible(slot.hwnd) != FALSE);
        if (!shell.EnsureSurface(slot.hwnd, DesktopSurfaceRole::Widget, desktopRegion, visible, &error)) {
            if (!error.empty()) WriteDiagnostics(L"Native widget attach failed: " + error);
            return false;
        }
        return true;
    }

    bool CreateSlotWindow(NativeSlot& slot, const std::wstring& title, const RECT& mappedRegion, const RECT& desktopRegion) {
        const int width = std::max<LONG>(1, mappedRegion.right - mappedRegion.left);
        const int height = std::max<LONG>(1, mappedRegion.bottom - mappedRegion.top);
        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kNativeWidgetSurfaceClass, title.c_str(),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            mappedRegion.left, mappedRegion.top, width, height,
            parent, nullptr, instance, &slot);
        if (!hwnd) return false;
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        MarkNativeSurfaceReady(hwnd);
        slot.hwnd = hwnd;
        slot.region = mappedRegion;
        slot.desktopRegion = desktopRegion;
        ApplyRoundedWindowRegion(slot);
        slot.dragHandle = CreateWindowExW(
            WS_EX_NOACTIVATE, kWidgetDragClass, L"", WS_CHILD | WS_VISIBLE,
            0, 0, width, height, hwnd, nullptr, instance, &slot);
        SetWindowPos(slot.dragHandle, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ResizeDragHandle(slot);
        PaintSlot(slot);
        AttachSlotSurface(slot, desktopRegion);
        if (!paused) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        return true;
    }

    bool CreateSlot(const DesktopWidget& widget, const RECT& desktopRegion, const RECT& mappedRegion, NativeWidgetPreset preset) {
        auto slot = std::make_unique<NativeSlot>();
        slot->owner = this;
        slot->widgetId = widget.id;
        slot->preset = preset;
        if (!CreateSlotWindow(*slot, SlotToken(widget, mappedRegion), mappedRegion, desktopRegion)) return false;
        slots.push_back(std::move(slot));
        return true;
    }

    void SyncFromStore() {
        if (!parent || !IsWindow(parent)) return;
        DesktopWidgetStore store;
        std::wstring ignored;
        if (!store.Load(&ignored)) return;
        const MonitorTopology topology = QueryMonitorTopology();
        if (!topology.Valid()) return;

        std::vector<std::wstring> desiredIds;
        for (const auto& raw : store.Items()) {
            const DesktopWidget widget = DesktopWidgetStore::Normalize(raw);
            NativeWidgetPreset preset{};
            if (!widget.enabled || widget.kind != DesktopWidgetKind::Native || !ParseNativePreset(widget.source.wstring(), &preset)) continue;
            if (preset == NativeWidgetPreset::WeatherGlass && !weatherStarted) {
                weatherService.Start(messageWindow, kWeatherUpdatedMessage);
                weatherStarted = true;
            }
            const MonitorInfo* monitor = widget.monitorId.empty() ? PrimaryMonitor(topology) : FindMonitorByStableId(topology, widget.monitorId);
            if (!monitor) continue;
            const RECT desktopRegion = WidgetRegionInDesktop(*monitor, widget);
            const RECT mappedRegion = MapDesktopRectToParent(parent, desktopRegion);
            if (mappedRegion.right <= mappedRegion.left || mappedRegion.bottom <= mappedRegion.top) continue;

            desiredIds.push_back(widget.id);
            NativeSlot* existing = FindSlot(widget.id);
            if (!existing) {
                CreateSlot(widget, desktopRegion, mappedRegion, preset);
                continue;
            }
            if (existing->dragging) continue;
            if (existing->geometryGraceUntil > GetTickCount64()) continue;
            const bool presetChanged = existing->preset != preset;
            const LONG existingWidth = existing->region.right - existing->region.left;
            const LONG existingHeight = existing->region.bottom - existing->region.top;
            const LONG mappedWidth = mappedRegion.right - mappedRegion.left;
            const LONG mappedHeight = mappedRegion.bottom - mappedRegion.top;
            const bool regionChanged = existing->region.left != mappedRegion.left || existing->region.top != mappedRegion.top ||
                                       existing->region.right != mappedRegion.right ||
                                       existing->region.bottom != mappedRegion.bottom;
            const bool desktopRegionChanged =
                existing->desktopRegion.left != desktopRegion.left || existing->desktopRegion.top != desktopRegion.top ||
                existing->desktopRegion.right != desktopRegion.right || existing->desktopRegion.bottom != desktopRegion.bottom;
            if (presetChanged) {
                DestroySlot(*existing);
                existing->preset = preset;
                if (!CreateSlotWindow(*existing, SlotToken(widget, mappedRegion), mappedRegion, desktopRegion)) continue;
            } else if (regionChanged && existing->hwnd && IsWindow(existing->hwnd) &&
                       existingWidth == mappedWidth && existingHeight == mappedHeight) {
                SetWindowPos(existing->hwnd, nullptr, mappedRegion.left, mappedRegion.top, mappedWidth, mappedHeight,
                             SWP_NOACTIVATE | SWP_NOZORDER);
                existing->region = mappedRegion;
                existing->desktopRegion = desktopRegion;
            } else if (regionChanged) {
                DestroySlot(*existing);
                if (!CreateSlotWindow(*existing, SlotToken(widget, mappedRegion), mappedRegion, desktopRegion)) continue;
            } else if (desktopRegionChanged && existing->hwnd && IsWindow(existing->hwnd)) {
                existing->desktopRegion = desktopRegion;
            }
        }

        slots.erase(std::remove_if(slots.begin(), slots.end(),
                                   [&](const std::unique_ptr<NativeSlot>& slot) {
                                       if (!slot) return true;
                                       const bool keep = std::find(desiredIds.begin(), desiredIds.end(), slot->widgetId) != desiredIds.end();
                                       if (!keep) DestroySlot(*slot);
                                       return !keep;
                                   }),
                    slots.end());

        for (const auto& slot : slots) {
            if (slot && slot->dragHandle && IsWindow(slot->dragHandle)) {
                SetWindowPos(slot->dragHandle, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }
        WriteDiagnostics(L"Native Direct2D Widget host · surfaces=" + std::to_wstring(slots.size()));
    }

    void SetPaused(bool value) {
        if (paused == value) return;
        paused = value;
        if (!value) {
            for (const auto& slot : slots) {
                if (!slot || !slot->hwnd || !IsWindow(slot->hwnd)) continue;
                if (slot->desktopRegion.right <= slot->desktopRegion.left ||
                    slot->desktopRegion.bottom <= slot->desktopRegion.top) continue;
                AttachSlotSurface(*slot, slot->desktopRegion);
            }
        }
        // Keep HWND visible on the desktop. Performance policy must not hide widgets;
        // "paused" only stops periodic repaints (clocks) to save CPU.
    }

    void RepaintWeatherWidgets() {
        if (paused) return;
        for (const auto& slot : slots) {
            if (slot && slot->preset == NativeWidgetPreset::WeatherGlass && slot->hwnd && IsWindow(slot->hwnd))
                PaintSlot(*slot);
        }
    }

    void RepaintDueWidgets() {
        if (paused) return;
        const ULONGLONG now = GetTickCount64();
        for (const auto& slot : slots) {
            if (!slot || !slot->hwnd || !IsWindow(slot->hwnd)) continue;
            if (NativePresetRefreshIntervalMs(slot->preset) == 0) continue;
            if (slot->nextRefreshAt == 0 || now >= slot->nextRefreshAt) PaintSlot(*slot);
        }
    }

    void HandleTimer(UINT_PTR timerId) {
        if (timerId == kSyncTimerId) SyncFromStore();
        if (timerId == kRefreshTimerId) RepaintDueWidgets();
    }

    bool EnsureMessageWindow() {
        if (messageWindow && IsWindow(messageWindow)) return true;
        WNDCLASSEXW hostClass{};
        hostClass.cbSize = sizeof(hostClass);
        hostClass.hInstance = instance;
        hostClass.lpfnWndProc = &NativeHostWindowProc;
        hostClass.lpszClassName = kNativeHostMessageClass;
        if (!RegisterClassExW(&hostClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        messageWindow = CreateWindowExW(
            0, kNativeHostMessageClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
        return messageWindow && IsWindow(messageWindow);
    }

    int Run() {
        if (!EnsureFactories() || !EnsureClasses() || !EnsureMessageWindow() || !parent || !IsWindow(parent)) return 64;
        SetTimer(messageWindow, kSyncTimerId, 1000, nullptr);
        SetTimer(messageWindow, kRefreshTimerId, kRefreshSchedulerTickMs, nullptr);
        SyncFromStore();

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.message == kShutdownMessage) break;
            if (message.message == kPauseMessage) SetPaused(true);
            if (message.message == kResumeMessage) SetPaused(false);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        KillTimer(messageWindow, kSyncTimerId);
        KillTimer(messageWindow, kRefreshTimerId);
        if (weatherStarted) {
            weatherService.Stop();
            weatherStarted = false;
        }
        if (messageWindow && IsWindow(messageWindow)) DestroyWindow(messageWindow);
        messageWindow = nullptr;
        for (auto& slot : slots) {
            if (slot) DestroySlot(*slot);
        }
        slots.clear();
        return 0;
    }
};

NativeWidgetHostApp* gNativeHost{};

LRESULT CALLBACK NativeHostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kShutdownMessage && gNativeHost) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == kPauseMessage && gNativeHost) {
        gNativeHost->SetPaused(true);
        return 0;
    }
    if (message == kResumeMessage && gNativeHost) {
        gNativeHost->SetPaused(false);
        return 0;
    }
    if (message == kWeatherUpdatedMessage && gNativeHost) {
        gNativeHost->RepaintWeatherWidgets();
        return 0;
    }
    if (message == WM_TIMER && gNativeHost) {
        gNativeHost->HandleTimer(static_cast<UINT_PTR>(wParam));
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

NativeWidgetProcessSet::NativeWidgetProcessSet() = default;
NativeWidgetProcessSet::~NativeWidgetProcessSet() { Stop(); }

bool NativeWidgetProcessSet::Start(HWND parentWindow) {
    Stop();
    lastError_.clear();
    if (!parentWindow || !IsWindow(parentWindow)) {
        lastError_ = L"Native widget host parent 无效";
        return false;
    }

    DesktopWidgetStore store;
    std::wstring ignored;
    if (!store.Load(&ignored)) {
        lastError_ = L"无法读取 native widget 配置";
        return false;
    }
    bool hasNative = false;
    for (const auto& item : store.Items()) {
        if (item.enabled && item.kind == DesktopWidgetKind::Native && IsNativePresetSource(item.source.wstring())) {
            hasNative = true;
            break;
        }
    }
    if (!hasNative) return true;

    parent_ = parentWindow;
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job_);
            job_ = nullptr;
        }
    }

    const std::wstring executable = ExecutablePath();
    if (executable.empty()) {
        lastError_ = L"无法定位 MiaoDeskWallpaper.exe";
        return false;
    }
    std::wstring command = QuoteArg(executable);
    command += L" " + std::wstring(kNativeHostMode);
    command += L" --parent-hwnd " + std::to_wstring(reinterpret_cast<uintptr_t>(parentWindow));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        lastError_ = L"启动 native widget host 失败，Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    process_ = process.hProcess;
    thread_ = process.hThread;
    if (job_) AssignProcessToJobObject(job_, process_);
    return true;
}

void NativeWidgetProcessSet::Stop() {
    if (process_) {
        const DWORD processId = GetProcessId(process_);
        for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
            DWORD windowProcessId = 0;
            GetWindowThreadProcessId(window, &windowProcessId);
            if (windowProcessId != processId) continue;
            wchar_t className[160]{};
            if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
                _wcsicmp(className, kNativeHostMessageClass) == 0) {
                PostMessageW(window, kShutdownMessage, 0, 0);
                break;
            }
        }
        if (WaitForSingleObject(process_, 700) == WAIT_TIMEOUT) TerminateProcess(process_, 0);
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (thread_) {
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (job_) {
        CloseHandle(job_);
        job_ = nullptr;
    }
    parent_ = nullptr;
    paused_ = false;
}

void NativeWidgetProcessSet::SetPaused(bool paused) {
    paused_ = paused;
    if (!process_) return;
    const DWORD processId = GetProcessId(process_);
    for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
        DWORD windowProcessId = 0;
        GetWindowThreadProcessId(window, &windowProcessId);
        if (windowProcessId != processId) continue;
        wchar_t className[160]{};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
            _wcsicmp(className, kNativeHostMessageClass) == 0) {
            PostMessageW(window, paused ? kPauseMessage : kResumeMessage, 0, 0);
            break;
        }
    }
}

bool NativeWidgetProcessSet::Active() const noexcept {
    if (!process_) return false;
    DWORD exitCode = STILL_ACTIVE;
    return GetExitCodeProcess(process_, &exitCode) && exitCode == STILL_ACTIVE;
}

std::wstring NativeWidgetProcessSet::LastErrorText() const { return lastError_; }

std::wstring NativeWidgetProcessSet::DiagnosticsText() const {
    return Active() ? L"Native Direct2D Widget host 运行中" : (lastError_.empty() ? L"Native widget host 未运行" : lastError_);
}

bool NativeWidgetProcessSet::SelfTest() noexcept {
    NativeWidgetPreset preset{};
    return IsNativePresetSource(L"native:glass-clock") &&
           ParseNativePreset(L"native:today-tasks", &preset) && preset == NativeWidgetPreset::TodayTasks &&
           !ParseNativePreset(L"native:invalid", &preset) &&
           NativePresetSource(NativeWidgetPreset::WeatherGlass) == L"native:weather-glass" &&
           NativeWeatherService::SelfTest() &&
           NativePresetRefreshIntervalMs(NativeWidgetPreset::GlassClock) == 60000 &&
           NativePresetRefreshIntervalMs(NativeWidgetPreset::TodayTasks) == 0;
}

int TryRunNativeWidgetHost(HINSTANCE instance) {
    const auto args = ProcessArguments();
    if (!HasArg(args, kNativeHostMode)) return -1;
    const auto parentText = ArgValue(args, L"--parent-hwnd");
    if (!parentText) return 61;
    HWND parent = reinterpret_cast<HWND>(static_cast<uintptr_t>(_wcstoui64(parentText->c_str(), nullptr, 10)));
    if (!parent || !IsWindow(parent)) return 62;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return 63;

    NativeWidgetHostApp app;
    app.instance = instance;
    app.parent = parent;
    gNativeHost = &app;
    const int result = app.Run();
    gNativeHost = nullptr;
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

} // namespace miaodesk::wallpaper
