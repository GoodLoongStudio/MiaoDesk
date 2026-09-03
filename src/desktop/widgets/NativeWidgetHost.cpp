#include "miaodesk/NativeWidgetHost.h"
#include "miaodesk/AppPaths.h"

#include "miaodesk/DesktopShellHost.h"
#include "miaodesk/DesktopSurfaceTelemetry.h"
#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/NativeWidgetPainter.h"
#include "miaodesk/NativeWidgetPreset.h"
#include "miaodesk/NativeWeatherService.h"
#include "miaodesk/RuntimeLogger.h"
#include "miaodesk/WallpaperMonitorLayout.h"
#include "miaodesk/WebDesktopSurfaceChild.h"
#include "miaodesk/WidgetService.h"

#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
constexpr UINT kPauseMessage = WM_APP + 911;
constexpr UINT kResumeMessage = WM_APP + 912;
constexpr UINT kShutdownMessage = WM_APP + 913;
constexpr UINT kWeatherUpdatedMessage = WM_APP + 914;
constexpr UINT_PTR kSyncTimerId = 71;
constexpr UINT_PTR kRefreshTimerId = 72;
constexpr UINT kRefreshSchedulerTickMs = 1000;
// Direct GDI widget surfaces re-present on this heartbeat. Explorer's raised
// desktop can discard a child's composited content (wallpaper re-attach,
// remote-desktop session, fullscreen transitions) without any WM_PAINT, and
// static presets never repaint on their own, so the last presented frame
// would otherwise stay stale or turn into uninitialized bits forever.
constexpr std::uint32_t kDirectSurfaceRepaintMs = 2000;
constexpr wchar_t kNativeHostMessageClass[] = L"MiaoDesk.Native.WidgetHostMessage";
constexpr wchar_t kWidgetRuntimeReloadMessageName[] = L"MiaoDesk.WidgetRuntimeReload.v1";

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

std::wstring HandleText(HWND value) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase
           << reinterpret_cast<std::uintptr_t>(value);
    return stream.str();
}

std::wstring HexValue(unsigned long long value) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase << value;
    return stream.str();
}

std::wstring RectText(const RECT& value) {
    return L"[" + std::to_wstring(value.left) + L"," + std::to_wstring(value.top) +
           L"," + std::to_wstring(value.right) + L"," + std::to_wstring(value.bottom) + L"]";
}

std::wstring WindowClassText(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return L"<invalid>";
    wchar_t name[160]{};
    if (GetClassNameW(hwnd, name, static_cast<int>(std::size(name))) <= 0) return L"<unknown>";
    return name;
}

void SetReadyProperty(HWND hwnd, const wchar_t* name, bool ready) {
    if (!hwnd || !IsWindow(hwnd)) return;
    if (ready) SetPropW(hwnd, name, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
    else RemovePropW(hwnd, name);
}

void MarkNativeSurfaceRole(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    SetPropW(hwnd, kWebSurfaceRoleProperty, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(2)));
    SetReadyProperty(hwnd, kWebSurfaceEnvironmentReadyProperty, false);
    SetReadyProperty(hwnd, kWebSurfaceControllerReadyProperty, false);
    SetReadyProperty(hwnd, kWebSurfaceNavigationReadyProperty, false);
    SetReadyProperty(hwnd, kNativeWidgetPaintReadyProperty, false);
}

void MarkNativeSurfacePaintReady(HWND hwnd, bool ready) {
    SetReadyProperty(hwnd, kWebSurfaceEnvironmentReadyProperty, ready);
    SetReadyProperty(hwnd, kWebSurfaceControllerReadyProperty, ready);
    SetReadyProperty(hwnd, kWebSurfaceNavigationReadyProperty, ready);
    SetReadyProperty(hwnd, kNativeWidgetPaintReadyProperty, ready);
}

fs::path WallpaperConfigPath() {
    return paths::StateFile(L"wallpaper.ini");
}

void WriteDiagnostics(const std::wstring& value) {
    WritePrivateProfileStringW(L"Diagnostics", L"NativeWidgetHost", value.c_str(), WallpaperConfigPath().c_str());
}

std::wstring ReadNativeDiagnostics() {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(L"Diagnostics", L"NativeWidgetHost", L"", buffer.data(),
                             static_cast<DWORD>(buffer.size()), WallpaperConfigPath().c_str());
    return buffer.data();
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
    ComPtr<ID2D1DCRenderTarget> target;
    // Non-owning view of the current paint surface. Ownership lives in the
    // mode-specific members below (DC render target for layered surfaces,
    // device context + swapchain for direct surfaces).
    ID2D1RenderTarget* activeTarget{};
    bool directPresentation{};
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> deviceContext;
    ComPtr<ID2D1Bitmap1> targetBitmap;
    ComPtr<IDWriteFactory> dwrite;
    HDC layerDc{};
    HBITMAP layerBitmap{};
    HGDIOBJ layerOldBitmap{};
    void* layerBits{};
    UINT layerWidth{};
    UINT layerHeight{};
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
    unsigned long long successfulPaints{};
};

void ReleaseLayerSurface(NativeSlot& slot) {
    MarkNativeSurfacePaintReady(slot.hwnd, false);
    slot.activeTarget = nullptr;
    slot.targetBitmap.Reset();
    slot.deviceContext.Reset();
    slot.d2dDevice.Reset();
    slot.swapChain.Reset();
    slot.d3dDevice.Reset();
    slot.directPresentation = false;
    slot.target.Reset();
    if (slot.layerDc && slot.layerOldBitmap) {
        SelectObject(slot.layerDc, slot.layerOldBitmap);
        slot.layerOldBitmap = nullptr;
    }
    if (slot.layerBitmap) {
        DeleteObject(slot.layerBitmap);
        slot.layerBitmap = nullptr;
    }
    if (slot.layerDc) {
        DeleteDC(slot.layerDc);
        slot.layerDc = nullptr;
    }
    slot.layerBits = nullptr;
    slot.layerWidth = 0;
    slot.layerHeight = 0;
}

struct NativeWidgetHostApp {
    HINSTANCE instance{};
    HWND parent{};
    HWND messageWindow{};
    bool paused{};
    bool weatherStarted{};
    NativeWeatherService weatherService;
    ComPtr<ID2D1Factory1> d2dFactory;
    std::vector<std::unique_ptr<NativeSlot>> slots;
    std::wstring lastSurfaceError;
    std::wstring lastLoggedSummary;

    std::wstring SurfaceState(const NativeSlot& slot) const {
        RECT client{};
        RECT screen{};
        const bool clientValid = slot.hwnd && GetClientRect(slot.hwnd, &client) != FALSE;
        const bool screenValid = slot.hwnd && GetWindowRect(slot.hwnd, &screen) != FALSE;
        const HWND actualParent = slot.hwnd ? GetParent(slot.hwnd) : nullptr;
        const LONG_PTR style = slot.hwnd ? GetWindowLongPtrW(slot.hwnd, GWL_STYLE) : 0;
        const LONG_PTR exStyle = slot.hwnd ? GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE) : 0;
        const LONG_PTR parentExStyle = actualParent ? GetWindowLongPtrW(actualParent, GWL_EXSTYLE) : 0;
        const bool paintReady = slot.hwnd && GetPropW(slot.hwnd, kNativeWidgetPaintReadyProperty) != nullptr;
        const auto zOrder = InspectDesktopSurfaceZOrder(
            slot.hwnd, DesktopSurfaceTelemetryRole::Widget);
        return L"id=" + slot.widgetId +
               L" preset=\"" + NativePresetTitle(slot.preset) + L"\"" +
               L" hwnd=" + HandleText(slot.hwnd) +
               L" parent=" + HandleText(actualParent) +
               L" parentClass=" + WindowClassText(actualParent) +
               L" parentNoRedirection=" + std::wstring((parentExStyle & WS_EX_NOREDIRECTIONBITMAP) != 0 ? L"true" : L"false") +
               L" mode=" + ((exStyle & WS_EX_LAYERED) != 0 ? std::wstring(L"layered-dc") : std::wstring(L"direct-gdi")) +
               L" visible=" + std::wstring(slot.hwnd && IsWindowVisible(slot.hwnd) ? L"true" : L"false") +
               L" paintReady=" + std::wstring(paintReady ? L"true" : L"false") +
               L" zOrderReported=" + std::wstring(zOrder.reported ? L"true" : L"false") +
               L" zOrderValid=" + std::wstring(zOrder.valid ? L"true" : L"false") +
               L" zOrderDetail=\"" + zOrder.detail + L"\"" +
               L" paints=" + std::to_wstring(slot.successfulPaints) +
               L" dpi=" + std::to_wstring(slot.hwnd ? GetDpiForWindow(slot.hwnd) : 0) +
               L" style=" + HexValue(static_cast<unsigned long long>(style)) +
               L" exStyle=" + HexValue(static_cast<unsigned long long>(exStyle)) +
               L" parentExStyle=" + HexValue(static_cast<unsigned long long>(parentExStyle)) +
               L" client=" + (clientValid ? RectText(client) : std::wstring(L"<invalid>")) +
               L" screen=" + (screenValid ? RectText(screen) : std::wstring(L"<invalid>")) +
               L" desktop=" + RectText(slot.desktopRegion) +
               L" mapped=" + RectText(slot.region);
    }

    void LogSlot(miaodesk::log::Level level, std::wstring_view event,
                 const NativeSlot& slot, std::wstring_view detail = {}) const {
        std::wstring message(event);
        message += L": ";
        message += SurfaceState(slot);
        if (!detail.empty()) {
            message += L" ";
            message.append(detail);
        }
        miaodesk::log::Write(level, L"WidgetHost", message);
    }

    void ReportFailure(NativeSlot* slot, const std::wstring& message) {
        const bool changed = lastSurfaceError != message;
        lastSurfaceError = message;
        WriteDiagnostics(lastSurfaceError);
        if (slot) MarkNativeSurfacePaintReady(slot->hwnd, false);
        if (!changed) return;
        if (slot) LogSlot(miaodesk::log::Level::Error, L"组件 Surface 失败", *slot, message);
        else miaodesk::log::Error(L"WidgetHost", message);
    }

    bool EnsureFactories() {
        if (d2dFactory) return true;
        D2D1_FACTORY_OPTIONS factoryOptions{};
        const HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                                 &factoryOptions, reinterpret_cast<void**>(d2dFactory.GetAddressOf()));
        if (FAILED(result)) {
            ReportFailure(nullptr, L"D2D1CreateFactory failed HRESULT=" +
                                   HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(result))));
            return false;
        }
        return true;
    }

    // Direct swapchain surfaces are opaque: the compositor ignores the frame's
    // alpha, so the painter's rounded corners left the dark clear color visible
    // as black wedges in all four corners. Clip the child HWND to the card's
    // rounded rect instead; the parent's wallpaper then shows through outside
    // the card. Layered surfaces keep per-pixel alpha and must not carry a
    // region (it would hard-clip their anti-aliased edges).
    void ApplyCardWindowRegion(NativeSlot& slot) {
        if (!slot.hwnd || !IsWindow(slot.hwnd)) return;
        RECT rc{};
        const bool clientValid = GetClientRect(slot.hwnd, &rc) != FALSE &&
                                 rc.right > rc.left && rc.bottom > rc.top;
        if (!clientValid) {
            SetWindowRgn(slot.hwnd, nullptr, FALSE);
            return;
        }
        const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(slot.hwnd));
        const float dipScale = static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
        const float widthDip = static_cast<float>(rc.right - rc.left) / dipScale;
        const float heightDip = static_cast<float>(rc.bottom - rc.top) / dipScale;
        const int radiusPx = std::max(1, static_cast<int>(std::lround(
            NativeWidgetCardRadius(slot.preset, widthDip, heightDip) * dipScale)));
        HRGN region = CreateRoundRectRgn(0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1, radiusPx, radiusPx);
        if (!region) return;
        if (!SetWindowRgn(slot.hwnd, region, FALSE)) DeleteObject(region);
    }

    bool EnsureRenderTarget(NativeSlot& slot) {
        if (!slot.hwnd || !IsWindow(slot.hwnd) || !d2dFactory) {
            ReportFailure(&slot, L"Render target prerequisites are invalid");
            return false;
        }
        RECT rc{};
        if (!GetClientRect(slot.hwnd, &rc)) {
            ReportFailure(&slot, L"GetClientRect failed Win32=" + std::to_wstring(GetLastError()));
            return false;
        }
        const UINT width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
        const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(slot.hwnd));

        const bool layered = (GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;

        if (!slot.dwrite) {
            const HRESULT writeResult = DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(slot.dwrite.GetAddressOf()));
            if (FAILED(writeResult)) {
                ReportFailure(&slot, L"DWriteCreateFactory failed HRESULT=" +
                                     HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(writeResult))));
                return false;
            }
        }

        if (slot.activeTarget && slot.directPresentation == !layered &&
            slot.layerWidth == width && slot.layerHeight == height) {
            if (!layered) return true;
            if (slot.target && slot.layerDc && slot.layerBitmap && slot.layerBits) {
                RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
                if (SUCCEEDED(slot.target->BindDC(slot.layerDc, &bind))) {
                    slot.target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
                    return true;
                }
            }
        }

        ReleaseLayerSurface(slot);

        if (!layered) return EnsureDirectSwapChainTarget(slot, width, height, dpi);

        // Layered surfaces keep the DC-render-target + premultiplied DIB +
        // UpdateLayeredWindow contract for legacy WorkerW desktop generations.
        slot.layerDc = CreateCompatibleDC(nullptr);
        if (!slot.layerDc) {
            ReportFailure(&slot, L"CreateCompatibleDC failed Win32=" + std::to_wstring(GetLastError()));
            return false;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
        bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        slot.layerBitmap = CreateDIBSection(
            slot.layerDc, &bitmapInfo, DIB_RGB_COLORS, &slot.layerBits, nullptr, 0);
        if (!slot.layerBitmap || !slot.layerBits) {
            ReportFailure(&slot, L"CreateDIBSection failed Win32=" + std::to_wstring(GetLastError()));
            ReleaseLayerSurface(slot);
            return false;
        }
        slot.layerOldBitmap = SelectObject(slot.layerDc, slot.layerBitmap);
        if (!slot.layerOldBitmap || slot.layerOldBitmap == HGDI_ERROR) {
            slot.layerOldBitmap = nullptr;
            ReportFailure(&slot, L"SelectObject for Widget DIB failed Win32=" + std::to_wstring(GetLastError()));
            ReleaseLayerSurface(slot);
            return false;
        }
        // CreateDIBSection does not guarantee zeroed bits. A frame that never
        // reaches its full-rect Clear must never present recycled GDI memory
        // (which can look like fragments of an older desktop image).
        if (slot.layerBits) {
            memset(slot.layerBits, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        }

        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi), static_cast<float>(dpi),
            D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        const HRESULT dcTargetResult = d2dFactory->CreateDCRenderTarget(&props, slot.target.GetAddressOf());
        if (FAILED(dcTargetResult)) {
            ReportFailure(&slot, L"CreateDCRenderTarget failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(dcTargetResult))));
            ReleaseLayerSurface(slot);
            return false;
        }
        RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        const HRESULT bindResult = slot.target->BindDC(slot.layerDc, &bind);
        if (FAILED(bindResult)) {
            ReportFailure(&slot, L"BindDC failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(bindResult))));
            ReleaseLayerSurface(slot);
            return false;
        }
        slot.target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        slot.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        slot.activeTarget = slot.target.Get();
        slot.directPresentation = false;
        slot.layerWidth = width;
        slot.layerHeight = height;
        SetWindowRgn(slot.hwnd, nullptr, FALSE);
        LogSlot(miaodesk::log::Level::Info, L"Direct2D 渲染目标已创建", slot,
                L"target=layered-dc size=" + std::to_wstring(width) + L"x" +
                std::to_wstring(height) + L" dpi=" + std::to_wstring(dpi));
        return true;
    }

    // Direct surfaces present through a DXGI swapchain. Explorer's raised
    // Progman is a WS_EX_NOREDIRECTIONBITMAP window owned by another process;
    // on several Windows 11 generations GDI writes into such a child report
    // success while the compositor keeps showing the previous band content.
    // A swapchain presentation is the same route Wallpaper-class engines use
    // and does not depend on the parent's redirection state at all.
    bool EnsureDirectSwapChainTarget(NativeSlot& slot, UINT width, UINT height, UINT dpi) {
        ComPtr<ID3D11Device> d3dDevice;
        ComPtr<ID3D11DeviceContext> d3dContext;
        D3D_FEATURE_LEVEL featureLevel{};
        constexpr UINT kDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT deviceResult = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, kDeviceFlags,
                                                 nullptr, 0, D3D11_SDK_VERSION, d3dDevice.GetAddressOf(),
                                                 &featureLevel, d3dContext.GetAddressOf());
        if (FAILED(deviceResult)) {
            deviceResult = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, kDeviceFlags,
                                             nullptr, 0, D3D11_SDK_VERSION, d3dDevice.GetAddressOf(),
                                             &featureLevel, d3dContext.GetAddressOf());
        }
        if (FAILED(deviceResult)) {
            ReportFailure(&slot, L"D3D11CreateDevice failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(deviceResult))));
            ReleaseLayerSurface(slot);
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> dxgiFactory;
        if (FAILED(d3dDevice.As(&dxgiDevice)) ||
            FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) ||
            FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf())))) {
            ReportFailure(&slot, L"DXGI device chain unavailable");
            ReleaseLayerSurface(slot);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 descriptor{};
        descriptor.Width = width;
        descriptor.Height = height;
        descriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        descriptor.SampleDesc.Count = 1;
        descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        descriptor.BufferCount = 2;
        descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        descriptor.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        HRESULT swapResult = dxgiFactory->CreateSwapChainForHwnd(
            d3dDevice.Get(), slot.hwnd, &descriptor, nullptr, nullptr, slot.swapChain.GetAddressOf());
        if (FAILED(swapResult)) {
            // Some child-HWND compositions reject the flip model; the classic
            // DXGI_SWAP_EFFECT_DISCARD bitblt model presents through the same
            // DWM surface contract.
            descriptor.BufferCount = 1;
            descriptor.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            swapResult = dxgiFactory->CreateSwapChainForHwnd(
                d3dDevice.Get(), slot.hwnd, &descriptor, nullptr, nullptr, slot.swapChain.GetAddressOf());
        }
        if (FAILED(swapResult)) {
            ReportFailure(&slot, L"CreateSwapChainForHwnd failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(swapResult))));
            ReleaseLayerSurface(slot);
            return false;
        }
        dxgiFactory->MakeWindowAssociation(slot.hwnd, DXGI_MWA_NO_WINDOW_CHANGES);

        if (FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), slot.d2dDevice.GetAddressOf())) ||
            FAILED(slot.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                       slot.deviceContext.GetAddressOf()))) {
            ReportFailure(&slot, L"D2D device context creation failed");
            ReleaseLayerSurface(slot);
            return false;
        }

        ComPtr<IDXGISurface> backBuffer;
        if (FAILED(slot.swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf())))) {
            ReportFailure(&slot, L"Swapchain back buffer unavailable");
            ReleaseLayerSurface(slot);
            return false;
        }
        const D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi), static_cast<float>(dpi));
        if (FAILED(slot.deviceContext->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bitmapProperties,
                                                                   slot.targetBitmap.GetAddressOf()))) {
            ReportFailure(&slot, L"D2D swapchain target bitmap creation failed");
            ReleaseLayerSurface(slot);
            return false;
        }
        slot.deviceContext->SetTarget(slot.targetBitmap.Get());
        slot.deviceContext->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        slot.activeTarget = slot.deviceContext.Get();
        slot.directPresentation = true;
        slot.layerWidth = width;
        slot.layerHeight = height;
        ApplyCardWindowRegion(slot);
        LogSlot(miaodesk::log::Level::Info, L"Direct2D 渲染目标已创建", slot,
                L"target=direct-swapchain size=" + std::to_wstring(width) + L"x" +
                std::to_wstring(height) + L" dpi=" + std::to_wstring(dpi));
        return true;
    }

    bool PresentLayerSurface(NativeSlot& slot) {
        if (!slot.hwnd || !IsWindow(slot.hwnd) || slot.layerWidth == 0 || slot.layerHeight == 0) return false;
        if ((GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0) {
            // Direct surfaces present the swapchain straight to the compositor.
            if (!slot.swapChain) {
                ReportFailure(&slot, L"Direct presentation has no swapchain");
                return false;
            }
            const HRESULT present = slot.swapChain->Present(1, 0);
            if (FAILED(present) && present != DXGI_STATUS_OCCLUDED) {
                ReportFailure(&slot, L"Swapchain Present failed HRESULT=" +
                                     HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(present))));
                return false;
            }
            MarkNativeSurfacePaintReady(slot.hwnd, true);
            return true;
        }
        if (!slot.layerDc) return false;
        POINT source{0, 0};
        SIZE size{static_cast<LONG>(slot.layerWidth), static_cast<LONG>(slot.layerHeight)};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        const auto present = [&] {
            SetLastError(ERROR_SUCCESS);
            return UpdateLayeredWindow(
                slot.hwnd, nullptr, nullptr, &size, slot.layerDc, &source, 0, &blend, ULW_ALPHA) != FALSE;
        };

        if (!present()) {
            const DWORD firstError = GetLastError();
            // Re-parenting a layered popup into Explorer changes it into a child
            // surface. Some real Windows 11 raised-desktop generations keep the
            // old layered state cached and reject the first presentation. Reset
            // the style once and retry without destroying the HWND/slot.
            const LONG_PTR exStyle = GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE, exStyle & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
            SetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            SetWindowPos(slot.hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            if (!present()) {
                ReportFailure(&slot, L"UpdateLayeredWindow failed first=" +
                                     std::to_wstring(firstError) + L" retry=" +
                                     std::to_wstring(GetLastError()));
                return false;
            }
        }
        MarkNativeSurfacePaintReady(slot.hwnd, true);
        return true;
    }

    void ScheduleNextRefresh(NativeSlot& slot) {
        const bool directSurface = slot.hwnd && IsWindow(slot.hwnd) &&
            (GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0;
        if (directSurface) {
            slot.nextRefreshAt = GetTickCount64() + kDirectSurfaceRepaintMs;
            return;
        }

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
        const bool wasReady = GetPropW(slot.hwnd, kNativeWidgetPaintReadyProperty) != nullptr;
        if (!EnsureRenderTarget(slot)) {
            if (lastSurfaceError.empty()) ReportFailure(&slot, L"Native Widget render target unavailable");
            return;
        }
        NativeWidgetPaintContext context{};
        context.target = slot.activeTarget;
        context.dwrite = slot.dwrite.Get();
        context.opaqueSurface =
            (GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0;
        if (!context.target) {
            ReportFailure(&slot, L"Native Widget render target unavailable after initialization");
            return;
        }
        const D2D1_SIZE_F dipSize = context.target->GetSize();
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
        context.target->BeginDraw();
        PaintNativeWidgetPreset(context, slot.preset);
        const HRESULT drawResult = context.target->EndDraw();
        if (SUCCEEDED(drawResult) && PresentLayerSurface(slot)) {
            const bool firstPaint = slot.successfulPaints == 0;
            ++slot.successfulPaints;
            const bool recovered = !lastSurfaceError.empty() || !wasReady;
            lastSurfaceError.clear();
            ScheduleNextRefresh(slot);
            if (firstPaint || recovered) {
                LogSlot(miaodesk::log::Level::Info,
                        firstPaint ? L"组件首次绘制成功" : L"组件绘制恢复成功", slot,
                        L"EndDraw=" + HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(drawResult))));
            }
        } else if (drawResult == D2DERR_RECREATE_TARGET) {
            LogSlot(miaodesk::log::Level::Warn, L"Direct2D 请求重建渲染目标", slot,
                    L"EndDraw=" + HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(drawResult))));
            ReleaseLayerSurface(slot);
            slot.nextRefreshAt = 0;
        } else if (FAILED(drawResult)) {
            ReportFailure(&slot, L"Direct2D EndDraw failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(drawResult))));
        } else {
            ReportFailure(&slot, L"Widget presentation failed after successful EndDraw");
        }
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
            ReleaseLayerSurface(*slot);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
            ReleaseLayerSurface(*slot);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            slot->owner->PaintSlot(*slot);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            ReleaseLayerSurface(*slot);
            slot->hwnd = nullptr;
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
        surface.hbrBackground = nullptr;
        return RegisterClassExW(&surface) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    void DestroySlot(NativeSlot& slot) {
        if (slot.hwnd && IsWindow(slot.hwnd)) LogSlot(miaodesk::log::Level::Info, L"销毁组件 Surface", slot);
        ReleaseLayerSurface(slot);
        if (slot.hwnd && IsWindow(slot.hwnd)) DestroyWindow(slot.hwnd);
        slot.hwnd = nullptr;
    }

    NativeSlot* FindSlot(std::wstring_view id) {
        for (const auto& slot : slots) {
            if (slot && slot->widgetId == id) return slot.get();
        }
        return nullptr;
    }

    bool AttachSlotSurface(NativeSlot& slot, const RECT& desktopRegion) {
        if (!slot.hwnd || !IsWindow(slot.hwnd)) {
            ReportFailure(&slot, L"Native Widget attach rejected: HWND invalid");
            return false;
        }
        slot.desktopRegion = desktopRegion;
        DesktopShellHost shell;
        std::wstring error;
        const bool visible = !paused;
        if (!shell.EnsureSurface(slot.hwnd, DesktopSurfaceRole::Widget, desktopRegion, visible, &error)) {
            ReportFailure(&slot, L"Native Widget attach failed: " +
                                 (error.empty() ? std::wstring(L"no detail") : error));
            return false;
        }
        const auto& snapshot = shell.Snapshot();
        LogSlot(miaodesk::log::Level::Info, L"组件挂载桌面成功", slot,
                L"shellMode=" + std::wstring(DesktopShellHost::ModeKey(snapshot.mode)) +
                L" progman=" + HandleText(snapshot.progman) +
                L" defView=" + HandleText(snapshot.shellDefView) +
                L" workerW=" + HandleText(snapshot.workerW) +
                L" explorerPid=" + std::to_wstring(snapshot.explorerPid));
        return true;
    }

    bool CreateSlotWindow(NativeSlot& slot, const std::wstring& title, const RECT& mappedRegion, const RECT& desktopRegion) {
        const int width = std::max<LONG>(1, desktopRegion.right - desktopRegion.left);
        const int height = std::max<LONG>(1, desktopRegion.bottom - desktopRegion.top);
        // Create top-level first, then let DesktopShellHost perform the only
        // cross-process SetParent/WS_CHILD transaction. Raised-desktop native
        // Widgets switch to a direct HWND target after the final parent is known;
        // legacy WorkerW generations retain layered UpdateLayeredWindow output.
        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kNativeWidgetSurfaceClass, title.c_str(),
            WS_POPUP | WS_CLIPSIBLINGS,
            desktopRegion.left, desktopRegion.top, width, height,
            nullptr, nullptr, instance, &slot);
        if (!hwnd) {
            ReportFailure(&slot, L"Native Widget CreateWindowEx failed Win32=" + std::to_wstring(GetLastError()));
            return false;
        }
        slot.hwnd = hwnd;
        slot.region = mappedRegion;
        slot.desktopRegion = desktopRegion;
        MarkNativeSurfaceRole(hwnd);
        LogSlot(miaodesk::log::Level::Info, L"组件 Surface 已创建", slot,
                L"requestedDesktop=" + RectText(desktopRegion));
        if (!AttachSlotSurface(slot, desktopRegion)) {
            DestroyWindow(hwnd);
            slot.hwnd = nullptr;
            ReleaseLayerSurface(slot);
            return false;
        }
        ReleaseLayerSurface(slot);
        PaintSlot(slot);
        if (!paused) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        LogSlot(miaodesk::log::Level::Info, L"组件 Surface 初始化完成", slot);
        // Surface creation and first paint are separate recovery domains. Keep
        // the attached HWND alive when the first layered presentation is not
        // ready yet; SyncFromStore will retry instead of freeing the NativeSlot
        // while the HWND still retains its pointer in GWLP_USERDATA.
        return IsWindow(hwnd) != FALSE;
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
        if (!parent || !IsWindow(parent)) {
            ReportFailure(nullptr, L"NativeWidgetHost sync skipped: invalid parent");
            return;
        }
        DesktopWidgetStore store;
        std::wstring storeError;
        if (!store.Load(&storeError)) {
            ReportFailure(nullptr, L"NativeWidgetHost store load failed: " + storeError);
            return;
        }
        const MonitorTopology topology = QueryMonitorTopology();
        if (!topology.Valid()) {
            ReportFailure(nullptr, L"NativeWidgetHost sync skipped: invalid monitor topology");
            return;
        }

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
                if (!CreateSlot(widget, desktopRegion, mappedRegion, preset)) {
                    if (lastSurfaceError.empty()) lastSurfaceError = L"Native widget surface create/attach failed: " + widget.id;
                }
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
                                       existing->region.right != mappedRegion.right || existing->region.bottom != mappedRegion.bottom;
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
            if (existing->hwnd && IsWindow(existing->hwnd) &&
                GetPropW(existing->hwnd, kNativeWidgetPaintReadyProperty) == nullptr) {
                PaintSlot(*existing);
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

        const auto visibleCount = std::count_if(slots.begin(), slots.end(), [](const auto& slot) {
            return slot && slot->hwnd && IsWindow(slot->hwnd) && IsWindowVisible(slot->hwnd);
        });
        const auto paintReadyCount = std::count_if(slots.begin(), slots.end(), [](const auto& slot) {
            return slot && slot->hwnd && IsWindow(slot->hwnd) && GetPropW(slot->hwnd, kNativeWidgetPaintReadyProperty) != nullptr;
        });
        const auto directCount = std::count_if(slots.begin(), slots.end(), [](const auto& slot) {
            return slot && slot->hwnd && IsWindow(slot->hwnd) &&
                   (GetWindowLongPtrW(slot->hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0;
        });
        const bool wallpaperEnabled = GetPrivateProfileIntW(
            L"Wallpaper", L"Enabled", 1, WallpaperConfigPath().c_str()) != 0;
        std::wstring summary = L"wallpaper.enabled=" + std::wstring(wallpaperEnabled ? L"true" : L"false") +
                               L" Native Direct2D Widget host configured=" +
                               std::to_wstring(store.Items().size()) +
                               L" desired=" + std::to_wstring(desiredIds.size()) +
                               L" surfaces=" + std::to_wstring(slots.size()) +
                               L" visible=" + std::to_wstring(visibleCount) +
                               L" paintReady=" + std::to_wstring(paintReadyCount) +
                               L" directHwnd=" + std::to_wstring(directCount) +
                               L" layered=" + std::to_wstring(slots.size() - directCount);
        if (!lastSurfaceError.empty()) summary += L" error=" + lastSurfaceError;
        WriteDiagnostics(summary);
        if (summary != lastLoggedSummary) {
            miaodesk::log::Info(L"WidgetHost", L"组件运行时状态变化: " + summary);
            for (const auto& slot : slots) {
                if (slot) LogSlot(miaodesk::log::Level::Info, L"组件 Surface 状态", *slot);
            }
            lastLoggedSummary = summary;
        }

        // Explorer and ShowWindow can both disturb sibling order. Reassert the
        // product contract after every state sync: wallpaper < icons < widgets.
        DesktopShellHost shell;
        std::wstring shellError;
        if (shell.EnsureCurrent(&shellError)) shell.RepairKnownMiaoDeskSurfaces();
        else if (!shellError.empty()) {
            ReportFailure(nullptr, L"NativeWidgetHost z-order repair failed: " + shellError);
        }
    }

    void SetPaused(bool value) {
        if (paused == value) return;
        paused = value;
        miaodesk::log::Info(L"WidgetHost", value ? L"组件刷新已暂停，Surface 保持显示"
                                                  : L"组件刷新已恢复");
        if (!value) {
            for (const auto& slot : slots) {
                if (!slot || !slot->hwnd || !IsWindow(slot->hwnd)) continue;
                if (slot->desktopRegion.right <= slot->desktopRegion.left ||
                    slot->desktopRegion.bottom <= slot->desktopRegion.top) continue;
                if (AttachSlotSurface(*slot, slot->desktopRegion)) PaintSlot(*slot);
            }
        }
        // Keep HWND visible on the desktop. Performance policy pauses periodic
        // work only; the last presented layered bitmap remains visible.
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
            // nextRefreshAt == 0 means a static layered preset: its cached
            // UpdateLayeredWindow bitmap survives DWM resets by itself.
            if (slot->nextRefreshAt == 0) continue;
            if (now >= slot->nextRefreshAt) PaintSlot(*slot);
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
        if (!EnsureFactories() || !EnsureClasses() || !EnsureMessageWindow() || !parent || !IsWindow(parent)) {
            ReportFailure(nullptr, L"NativeWidgetHost init failed Win32=" + std::to_wstring(GetLastError()) +
                                   L" parent=" + HandleText(parent));
            return 64;
        }
        WriteDiagnostics(L"Native Direct2D Widget host started parent=" +
                         std::to_wstring(reinterpret_cast<std::uintptr_t>(parent)));
        miaodesk::log::Info(L"WidgetHost", L"原生组件宿主启动: parent=" + HandleText(parent) +
                            L" parentClass=" + WindowClassText(parent) +
                            L" parentExStyle=" + HexValue(static_cast<unsigned long long>(GetWindowLongPtrW(parent, GWL_EXSTYLE))));
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
        miaodesk::log::Info(L"WidgetHost", L"原生组件宿主已退出");
        return 0;
    }
};

NativeWidgetHostApp* gNativeHost{};

LRESULT CALLBACK NativeHostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static const UINT reloadMessage = RegisterWindowMessageW(kWidgetRuntimeReloadMessageName);
    if (reloadMessage != 0 && message == reloadMessage && gNativeHost) {
        gNativeHost->SyncFromStore();
        return 0;
    }
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
        lastError_ = L"Native widget host parent invalid";
        return false;
    }

    DesktopWidgetStore store;
    std::wstring ignored;
    if (!store.Load(&ignored)) {
        lastError_ = L"Cannot read native widget configuration";
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
        lastError_ = L"Cannot locate MiaoDeskWallpaper.exe";
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
        lastError_ = L"Start native widget host failed Win32=" + std::to_wstring(GetLastError());
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
    if (!Active()) return lastError_.empty() ? L"Native Direct2D Widget host stopped" : lastError_;
    const std::wstring detail = ReadNativeDiagnostics();
    return detail.empty() ? L"Native Direct2D Widget host running" : detail;
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
