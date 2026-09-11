#include "miaodesk/ContentWidgetHost.h"

#include "miaodesk/AppPaths.h"
#include "miaodesk/ContentWidgetInstanceStore.h"
#include "miaodesk/DesktopShellHost.h"
#include "miaodesk/DesktopSurfaceTelemetry.h"
#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/MiaoSceneD2DRenderer.h"
#include "miaodesk/MiaoWidgetContentCatalog.h"
#include "miaodesk/NativeWidgetHost.h"
#include "miaodesk/RuntimeLogger.h"
#include "miaodesk/WallpaperMonitorLayout.h"
#include "miaodesk/WebDesktopSurfaceChild.h"
#include "miaodesk/WidgetService.h"

#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kContentHostMode[] = L"--content-widget-host";
constexpr UINT kPauseMessage = WM_APP + 941;
constexpr UINT kResumeMessage = WM_APP + 942;
constexpr UINT kShutdownMessage = WM_APP + 943;
constexpr UINT_PTR kSyncTimerId = 81;
constexpr UINT_PTR kRefreshTimerId = 82;
constexpr UINT kSyncIntervalMs = 1000;
constexpr UINT kRefreshTickMs = 16;
constexpr std::uint32_t kIdleContentRefreshMs = 1000;
constexpr std::uint32_t kDirectSurfaceHeartbeatMs = 2000;
constexpr std::wstring_view kGlassClockContentSource = L"content:com.goodloong.glass-clock";

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

bool HasArg(const std::vector<std::wstring>& args, std::wstring_view name) {
    return std::any_of(args.begin(), args.end(), [&](const std::wstring& value) {
        return _wcsicmp(value.c_str(), std::wstring(name).c_str()) == 0;
    });
}

std::optional<std::wstring> ArgValue(const std::vector<std::wstring>& args, std::wstring_view name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (_wcsicmp(args[i].c_str(), std::wstring(name).c_str()) == 0) return args[i + 1];
    }
    return std::nullopt;
}

std::wstring HandleText(HWND value) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(value);
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

RECT MapDesktopRectToParent(HWND parent, const RECT& desktopRect) {
    if (!parent) return desktopRect;
    POINT corners[2] = {{desktopRect.left, desktopRect.top}, {desktopRect.right, desktopRect.bottom}};
    SetLastError(ERROR_SUCCESS);
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
    return L"widget-" + widget.id + L"-content-" + std::to_wstring(region.left) + L"-" +
           std::to_wstring(region.top) + L"-" + std::to_wstring(region.right) + L"-" +
           std::to_wstring(region.bottom);
}

fs::path WallpaperConfigPath() {
    return paths::StateFile(L"wallpaper.ini");
}

void WriteDiagnostics(const std::wstring& value) {
    WritePrivateProfileStringW(L"Diagnostics", L"ContentWidgetHost", value.c_str(), WallpaperConfigPath().c_str());
}

std::wstring ReadDiagnostics() {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(L"Diagnostics", L"ContentWidgetHost", L"", buffer.data(),
                             static_cast<DWORD>(buffer.size()), WallpaperConfigPath().c_str());
    return buffer.data();
}

void SetReadyProperty(HWND hwnd, const wchar_t* name, bool ready) {
    if (!hwnd || !IsWindow(hwnd)) return;
    if (ready) SetPropW(hwnd, name, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
    else RemovePropW(hwnd, name);
}

void MarkSurfaceRole(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    SetPropW(hwnd, kWebSurfaceRoleProperty, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(2)));
    SetReadyProperty(hwnd, kWebSurfaceEnvironmentReadyProperty, false);
    SetReadyProperty(hwnd, kWebSurfaceControllerReadyProperty, false);
    SetReadyProperty(hwnd, kWebSurfaceNavigationReadyProperty, false);
    SetReadyProperty(hwnd, kNativeWidgetPaintReadyProperty, false);
}

void MarkPaintReady(HWND hwnd, bool ready) {
    SetReadyProperty(hwnd, kWebSurfaceEnvironmentReadyProperty, ready);
    SetReadyProperty(hwnd, kWebSurfaceControllerReadyProperty, ready);
    SetReadyProperty(hwnd, kWebSurfaceNavigationReadyProperty, ready);
    SetReadyProperty(hwnd, kNativeWidgetPaintReadyProperty, ready);
}

fs::file_time_type PackageStamp(const fs::path& packageRoot) {
    std::error_code ec;
    const auto value = fs::last_write_time(packageRoot / L"manifest.json", ec);
    return ec ? fs::file_time_type{} : value;
}

fs::file_time_type InstanceParameterStamp(std::wstring_view widgetId) {
    const fs::path path = content::ContentWidgetInstanceStore::InstancePath(widgetId);
    if (path.empty()) return {};
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return {};
    const auto value = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type{} : value;
}

struct ResolvedContentRuntime {
    std::wstring source;
    fs::path packageRoot;
    fs::file_time_type packageStamp{};
    content::ContentDefinition definition;
};

bool ResolveContentRuntime(
    const DesktopWidget& widget,
    ResolvedContentRuntime* runtime,
    std::wstring* error) {
    if (error) error->clear();
    if (!runtime) {
        if (error) *error = L"Content runtime output is null.";
        return false;
    }
    *runtime = {};
    if (widget.kind != DesktopWidgetKind::Content || !widget.enabled) {
        if (error) *error = L"Widget is not an enabled Content instance.";
        return false;
    }

    content::ResolvedWidgetContent resolved;
    if (!content::MiaoWidgetContentCatalog::Resolve(widget.source.wstring(), &resolved, error)) return false;
    if (resolved.definition.kind != content::ContentKind::Widget) {
        if (error) *error = L"Content definition is not a widget: " + resolved.definition.id;
        return false;
    }
    if (resolved.definition.runtime != content::ContentRuntimeKind::Scene) {
        if (error) *error = L"Content widget runtime is not Scene: " + resolved.definition.id;
        return false;
    }

    runtime->source = widget.source.wstring();
    runtime->packageRoot = resolved.packageRoot;
    runtime->packageStamp = PackageStamp(resolved.packageRoot);
    runtime->definition = std::move(resolved.definition);
    return true;
}

struct ContentWidgetHostApp;
LRESULT CALLBACK ContentHostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

struct ContentSlot {
    ContentWidgetHostApp* owner{};
    std::wstring widgetId;
    std::wstring source;
    fs::path packageRoot;
    fs::file_time_type packageStamp{};
    content::ContentDefinition definition;
    fs::file_time_type instanceParameterStamp{};
    RECT region{};
    RECT desktopRegion{};
    HWND hwnd{};

    ID2D1RenderTarget* activeTarget{};
    bool directPresentation{};
    ComPtr<ID2D1DCRenderTarget> dcTarget;
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> deviceContext;
    ComPtr<ID2D1Bitmap1> targetBitmap;
    HDC layerDc{};
    HBITMAP layerBitmap{};
    HGDIOBJ layerOldBitmap{};
    void* layerBits{};
    UINT layerWidth{};
    UINT layerHeight{};

    std::unique_ptr<content::MiaoSceneD2DRenderer> renderer;
    ID2D1RenderTarget* rendererTarget{};

    bool dragging{};
    POINT dragStartCursor{};
    RECT dragStartRegion{};
    DesktopWidget dragStartWidget{};
    float dragPreviewX{};
    float dragPreviewY{};
    float dragMonitorWidthPx{};
    float dragMonitorHeightPx{};
    ULONGLONG geometryGraceUntil{};
    ULONGLONG nextRefreshAt{};
    unsigned long long successfulPaints{};
};

void ResetRenderer(ContentSlot& slot) {
    if (slot.renderer) slot.renderer->Reset();
    slot.rendererTarget = nullptr;
}

void ReleaseSurface(ContentSlot& slot) {
    MarkPaintReady(slot.hwnd, false);
    ResetRenderer(slot);
    slot.activeTarget = nullptr;
    slot.targetBitmap.Reset();
    slot.deviceContext.Reset();
    slot.d2dDevice.Reset();
    slot.swapChain.Reset();
    slot.d3dDevice.Reset();
    slot.directPresentation = false;
    slot.dcTarget.Reset();
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

struct ContentWidgetHostApp {
    HINSTANCE instance{};
    HWND parent{};
    HWND messageWindow{};
    bool paused{};
    ComPtr<ID2D1Factory1> d2dFactory;
    std::vector<std::unique_ptr<ContentSlot>> slots;
    std::wstring lastError;
    std::wstring lastLoggedSummary;

    std::wstring SurfaceState(const ContentSlot& slot) const {
        RECT screen{};
        const bool screenValid = slot.hwnd && GetWindowRect(slot.hwnd, &screen) != FALSE;
        const HWND actualParent = slot.hwnd ? GetParent(slot.hwnd) : nullptr;
        const LONG_PTR exStyle = slot.hwnd ? GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE) : 0;
        const bool paintReady = slot.hwnd && GetPropW(slot.hwnd, kNativeWidgetPaintReadyProperty) != nullptr;
        const auto zOrder = InspectDesktopSurfaceZOrder(slot.hwnd, DesktopSurfaceTelemetryRole::Widget);
        return L"id=" + slot.widgetId +
               L" source=\"" + slot.source + L"\"" +
               L" hwnd=" + HandleText(slot.hwnd) +
               L" parent=" + HandleText(actualParent) +
               L" parentClass=" + WindowClassText(actualParent) +
               L" mode=" + ((exStyle & WS_EX_LAYERED) != 0 ? std::wstring(L"layered-dc") : std::wstring(L"direct-swapchain")) +
               L" visible=" + std::wstring(slot.hwnd && IsWindowVisible(slot.hwnd) ? L"true" : L"false") +
               L" paintReady=" + std::wstring(paintReady ? L"true" : L"false") +
               L" zOrderValid=" + std::wstring(zOrder.valid ? L"true" : L"false") +
               L" paints=" + std::to_wstring(slot.successfulPaints) +
               L" screen=" + (screenValid ? RectText(screen) : std::wstring(L"<invalid>")) +
               L" desktop=" + RectText(slot.desktopRegion);
    }

    void LogSlot(miaodesk::log::Level level, std::wstring_view event,
                 const ContentSlot& slot, std::wstring_view detail = {}) const {
        std::wstring text(event);
        text += L": " + SurfaceState(slot);
        if (!detail.empty()) text += L" " + std::wstring(detail);
        miaodesk::log::Write(level, L"ContentWidgetHost", text);
    }

    void ReportFailure(ContentSlot* slot, std::wstring message) {
        const bool changed = lastError != message;
        lastError = std::move(message);
        WriteDiagnostics(lastError);
        if (slot) MarkPaintReady(slot->hwnd, false);
        if (!changed) return;
        if (slot) LogSlot(miaodesk::log::Level::Error, L"Content Surface 失败", *slot, lastError);
        else miaodesk::log::Error(L"ContentWidgetHost", lastError);
    }

    bool EnsureFactory() {
        if (d2dFactory) return true;
        D2D1_FACTORY_OPTIONS options{};
        const HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                             &options, reinterpret_cast<void**>(d2dFactory.GetAddressOf()));
        if (FAILED(hr)) {
            ReportFailure(nullptr, L"D2D1CreateFactory failed HRESULT=" +
                                   HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(hr))));
            return false;
        }
        return true;
    }

    void ApplyWindowRegion(ContentSlot& slot) {
        if (!slot.hwnd || !IsWindow(slot.hwnd)) return;
        if (slot.source != kGlassClockContentSource) {
            SetWindowRgn(slot.hwnd, nullptr, FALSE);
            return;
        }
        RECT rc{};
        if (!GetClientRect(slot.hwnd, &rc) || rc.right <= rc.left || rc.bottom <= rc.top) return;
        const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(slot.hwnd));
        const float scale = static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
        const int inset = std::max(1, static_cast<int>(std::lround(scale)));
        const int radius = std::max(1, static_cast<int>(std::lround(32.0f * scale)));
        HRGN region = CreateRoundRectRgn(inset, inset, rc.right - inset + 1, rc.bottom - inset + 1,
                                         radius * 2, radius * 2);
        if (region && !SetWindowRgn(slot.hwnd, region, FALSE)) DeleteObject(region);
    }

    bool EnsureDirectTarget(ContentSlot& slot, UINT width, UINT height, UINT dpi) {
        ComPtr<ID3D11DeviceContext> d3dContext;
        D3D_FEATURE_LEVEL featureLevel{};
        constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       nullptr, 0, D3D11_SDK_VERSION, slot.d3dDevice.GetAddressOf(),
                                       &featureLevel, d3dContext.GetAddressOf());
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                   nullptr, 0, D3D11_SDK_VERSION, slot.d3dDevice.GetAddressOf(),
                                   &featureLevel, d3dContext.GetAddressOf());
        }
        if (FAILED(hr)) {
            ReportFailure(&slot, L"D3D11CreateDevice failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(hr))));
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(slot.d3dDevice.As(&dxgiDevice)) ||
            FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) ||
            FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())))) {
            ReportFailure(&slot, L"DXGI device chain unavailable");
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
        hr = factory->CreateSwapChainForHwnd(slot.d3dDevice.Get(), slot.hwnd, &descriptor,
                                             nullptr, nullptr, slot.swapChain.GetAddressOf());
        if (FAILED(hr)) {
            descriptor.BufferCount = 1;
            descriptor.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = factory->CreateSwapChainForHwnd(slot.d3dDevice.Get(), slot.hwnd, &descriptor,
                                                 nullptr, nullptr, slot.swapChain.GetAddressOf());
        }
        if (FAILED(hr)) {
            ReportFailure(&slot, L"CreateSwapChainForHwnd failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(hr))));
            return false;
        }
        factory->MakeWindowAssociation(slot.hwnd, DXGI_MWA_NO_WINDOW_CHANGES);

        if (FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), slot.d2dDevice.GetAddressOf())) ||
            FAILED(slot.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                       slot.deviceContext.GetAddressOf()))) {
            ReportFailure(&slot, L"D2D device context creation failed");
            return false;
        }

        ComPtr<IDXGISurface> backBuffer;
        if (FAILED(slot.swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) {
            ReportFailure(&slot, L"Swapchain back buffer unavailable");
            return false;
        }
        const auto props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi), static_cast<float>(dpi));
        if (FAILED(slot.deviceContext->CreateBitmapFromDxgiSurface(backBuffer.Get(), &props,
                                                                   slot.targetBitmap.GetAddressOf()))) {
            ReportFailure(&slot, L"D2D swapchain target bitmap creation failed");
            return false;
        }
        slot.deviceContext->SetTarget(slot.targetBitmap.Get());
        slot.deviceContext->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        slot.activeTarget = slot.deviceContext.Get();
        slot.directPresentation = true;
        slot.layerWidth = width;
        slot.layerHeight = height;
        ApplyWindowRegion(slot);
        return true;
    }

    bool EnsureRenderTarget(ContentSlot& slot) {
        if (!slot.hwnd || !IsWindow(slot.hwnd) || !d2dFactory) return false;
        RECT rc{};
        if (!GetClientRect(slot.hwnd, &rc)) return false;
        const UINT width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
        const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(slot.hwnd));
        const bool layered = (GetWindowLongPtrW(slot.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;

        if (slot.activeTarget && slot.directPresentation == !layered &&
            slot.layerWidth == width && slot.layerHeight == height) {
            if (!layered) return true;
            if (slot.dcTarget && slot.layerDc && slot.layerBitmap && slot.layerBits) {
                RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
                if (SUCCEEDED(slot.dcTarget->BindDC(slot.layerDc, &bind))) {
                    slot.dcTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
                    return true;
                }
            }
        }

        ReleaseSurface(slot);
        if (!layered) return EnsureDirectTarget(slot, width, height, dpi);

        slot.layerDc = CreateCompatibleDC(nullptr);
        if (!slot.layerDc) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        slot.layerBitmap = CreateDIBSection(slot.layerDc, &info, DIB_RGB_COLORS, &slot.layerBits, nullptr, 0);
        if (!slot.layerBitmap || !slot.layerBits) {
            ReleaseSurface(slot);
            return false;
        }
        slot.layerOldBitmap = SelectObject(slot.layerDc, slot.layerBitmap);
        if (!slot.layerOldBitmap || slot.layerOldBitmap == HGDI_ERROR) {
            slot.layerOldBitmap = nullptr;
            ReleaseSurface(slot);
            return false;
        }
        std::memset(slot.layerBits, 0, static_cast<std::size_t>(width) * height * 4);

        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi), static_cast<float>(dpi));
        if (FAILED(d2dFactory->CreateDCRenderTarget(&props, slot.dcTarget.GetAddressOf()))) {
            ReleaseSurface(slot);
            return false;
        }
        RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        if (FAILED(slot.dcTarget->BindDC(slot.layerDc, &bind))) {
            ReleaseSurface(slot);
            return false;
        }
        slot.dcTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        slot.activeTarget = slot.dcTarget.Get();
        slot.directPresentation = false;
        slot.layerWidth = width;
        slot.layerHeight = height;
        SetWindowRgn(slot.hwnd, nullptr, FALSE);
        return true;
    }

    bool ApplyInstanceParameters(ContentSlot& slot) {
        if (!slot.renderer || !slot.renderer->Loaded()) return false;
        content::ContentParameterValues overrides;
        std::wstring error;
        if (!content::ContentWidgetInstanceStore::LoadOverrides(
                slot.widgetId, slot.definition, &overrides, &error)) {
            ReportFailure(&slot, L"Content parameter load failed: " + error);
            return false;
        }
        for (const auto& [key, value] : overrides) {
            const auto* parameter = content::MiaoContentModel::FindParameter(slot.definition, key);
            if (!parameter) continue;
            if (!slot.renderer->SetParameter(parameter->runtimeId, value, &error)) {
                ReportFailure(&slot, L"Content parameter apply failed: " + key + L": " + error);
                return false;
            }
        }
        return true;
    }

    bool EnsureRenderer(ContentSlot& slot) {
        if (!slot.activeTarget || slot.packageRoot.empty()) return false;
        if (!slot.renderer) slot.renderer = std::make_unique<content::MiaoSceneD2DRenderer>();
        if (slot.rendererTarget == slot.activeTarget && slot.renderer->Loaded()) return true;
        slot.renderer->Reset();
        std::wstring error;
        if (!slot.renderer->Load(slot.packageRoot, slot.activeTarget, &error)) {
            ReportFailure(&slot, L"Content Scene load failed: " + error);
            return false;
        }
        if (!ApplyInstanceParameters(slot)) {
            slot.renderer->Reset();
            return false;
        }
        slot.rendererTarget = slot.activeTarget;
        return true;
    }

    bool Present(ContentSlot& slot) {
        if (slot.directPresentation) {
            if (!slot.swapChain) return false;
            const HRESULT hr = slot.swapChain->Present(1, 0);
            if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) return false;
            MarkPaintReady(slot.hwnd, true);
            return true;
        }
        if (!slot.layerDc) return false;
        POINT source{0, 0};
        SIZE size{static_cast<LONG>(slot.layerWidth), static_cast<LONG>(slot.layerHeight)};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        if (!UpdateLayeredWindow(slot.hwnd, nullptr, nullptr, &size, slot.layerDc, &source,
                                 0, &blend, ULW_ALPHA)) return false;
        MarkPaintReady(slot.hwnd, true);
        return true;
    }

    void ScheduleNextRefresh(ContentSlot& slot, double timeSeconds) {
        const ULONGLONG now = GetTickCount64();
        std::uint32_t interval = kIdleContentRefreshMs;
        if (slot.renderer && slot.renderer->Loaded()) {
            content::MiaoSceneFrameDemand demand;
            std::wstring ignored;
            if (slot.renderer->PrepareFrame(timeSeconds, &demand,
                                            content::MiaoSceneFrameScheduler::kDefaultAnimationFps,
                                            &ignored) && demand.intervalMs > 0) {
                interval = std::max<std::uint32_t>(kRefreshTickMs, demand.intervalMs);
            }
        }
        if (slot.directPresentation) interval = std::min(interval, kDirectSurfaceHeartbeatMs);
        slot.nextRefreshAt = now + interval;
    }

    void PaintSlot(ContentSlot& slot) {
        const bool wasReady = GetPropW(slot.hwnd, kNativeWidgetPaintReadyProperty) != nullptr;
        if (!EnsureRenderTarget(slot) || !EnsureRenderer(slot)) {
            if (lastError.empty()) ReportFailure(&slot, L"Content render target/renderer unavailable");
            return;
        }

        const D2D1_SIZE_F size = slot.activeTarget->GetSize();
        const double timeSeconds = static_cast<double>(GetTickCount64()) / 1000.0;
        slot.activeTarget->BeginDraw();
        slot.activeTarget->Clear(slot.directPresentation
            ? D2D1::ColorF(0.035f, 0.06f, 0.11f, 1.0f)
            : D2D1::ColorF(0, 0, 0, 0));
        std::wstring renderError;
        const bool rendered = slot.renderer->Draw(static_cast<float>(timeSeconds), size, &renderError);
        const HRESULT drawResult = slot.activeTarget->EndDraw();

        if (!rendered) {
            ReportFailure(&slot, L"Content Scene draw failed: " + renderError);
            return;
        }
        if (drawResult == D2DERR_RECREATE_TARGET) {
            ReleaseSurface(slot);
            slot.nextRefreshAt = 0;
            return;
        }
        if (FAILED(drawResult)) {
            ReportFailure(&slot, L"Content Direct2D EndDraw failed HRESULT=" +
                                 HexValue(static_cast<unsigned long long>(static_cast<std::uint32_t>(drawResult))));
            return;
        }
        if (!Present(slot)) {
            ReportFailure(&slot, L"Content surface presentation failed");
            return;
        }

        const bool firstPaint = slot.successfulPaints == 0;
        ++slot.successfulPaints;
        const bool recovered = !lastError.empty() || !wasReady;
        lastError.clear();
        ScheduleNextRefresh(slot, timeSeconds);
        if (firstPaint || recovered) {
            LogSlot(miaodesk::log::Level::Info,
                    firstPaint ? L"Content 组件首次绘制成功" : L"Content 组件绘制恢复成功",
                    slot);
        }
    }

    bool BeginDrag(ContentSlot& slot) {
        DesktopWidgetStore store;
        std::wstring ignored;
        if (!store.Load(&ignored)) return false;
        const auto found = store.Find(slot.widgetId);
        if (!found || found->width <= 0.001f || found->height <= 0.001f) return false;
        RECT screen{};
        if (!GetWindowRect(slot.hwnd, &screen) || !GetCursorPos(&slot.dragStartCursor)) return false;
        POINT corners[2] = {{screen.left, screen.top}, {screen.right, screen.bottom}};
        MapWindowPoints(nullptr, parent, corners, 2);
        slot.dragStartRegion = RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
        slot.dragStartWidget = *found;
        slot.dragPreviewX = found->x;
        slot.dragPreviewY = found->y;
        const float widthPx = static_cast<float>(std::max<LONG>(1, slot.dragStartRegion.right - slot.dragStartRegion.left));
        const float heightPx = static_cast<float>(std::max<LONG>(1, slot.dragStartRegion.bottom - slot.dragStartRegion.top));
        slot.dragMonitorWidthPx = widthPx / found->width;
        slot.dragMonitorHeightPx = heightPx / found->height;
        slot.dragging = slot.dragMonitorWidthPx > 1.0f && slot.dragMonitorHeightPx > 1.0f;
        return slot.dragging;
    }

    void UpdateDrag(ContentSlot& slot) {
        if (!slot.dragging) return;
        POINT cursor{};
        if (!GetCursorPos(&cursor)) return;
        const int dx = cursor.x - slot.dragStartCursor.x;
        const int dy = cursor.y - slot.dragStartCursor.y;
        const float maxX = std::max(0.0f, 1.0f - slot.dragStartWidget.width);
        const float maxY = std::max(0.0f, 1.0f - slot.dragStartWidget.height);
        slot.dragPreviewX = std::clamp(slot.dragStartWidget.x + dx / slot.dragMonitorWidthPx, 0.0f, maxX);
        slot.dragPreviewY = std::clamp(slot.dragStartWidget.y + dy / slot.dragMonitorHeightPx, 0.0f, maxY);
        const LONG appliedX = static_cast<LONG>(std::lround((slot.dragPreviewX - slot.dragStartWidget.x) * slot.dragMonitorWidthPx));
        const LONG appliedY = static_cast<LONG>(std::lround((slot.dragPreviewY - slot.dragStartWidget.y) * slot.dragMonitorHeightPx));
        SetWindowPos(slot.hwnd, nullptr, slot.dragStartRegion.left + appliedX, slot.dragStartRegion.top + appliedY,
                     slot.dragStartRegion.right - slot.dragStartRegion.left,
                     slot.dragStartRegion.bottom - slot.dragStartRegion.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
    }

    void EndDrag(ContentSlot& slot, bool persist) {
        if (!slot.dragging) return;
        slot.dragging = false;
        if (GetCapture() == slot.hwnd) ReleaseCapture();
        if (!persist) return;
        desktop::WidgetUpdateRequest request;
        request.id = slot.widgetId;
        request.x = slot.dragPreviewX;
        request.y = slot.dragPreviewY;
        const desktop::WidgetService service;
        if (!service.Update(request).success) return;
        slot.geometryGraceUntil = GetTickCount64() + 2000;
    }

    static LRESULT CALLBACK SurfaceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* slot = reinterpret_cast<ContentSlot*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            slot = static_cast<ContentSlot*>(create->lpCreateParams);
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
        case WM_DPICHANGED:
            ReleaseSurface(*slot);
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
            ReleaseSurface(*slot);
            slot->hwnd = nullptr;
            return 0;
        default: break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool EnsureWindows() {
        WNDCLASSEXW surface{};
        surface.cbSize = sizeof(surface);
        surface.hInstance = instance;
        surface.lpfnWndProc = &ContentWidgetHostApp::SurfaceProc;
        surface.lpszClassName = kNativeWidgetSurfaceClass;
        surface.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&surface) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        WNDCLASSEXW message{};
        message.cbSize = sizeof(message);
        message.hInstance = instance;
        message.lpfnWndProc = &ContentHostWindowProc;
        message.lpszClassName = kContentWidgetHostMessageClass;
        if (!RegisterClassExW(&message) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        messageWindow = CreateWindowExW(0, kContentWidgetHostMessageClass, L"", 0,
                                        0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
        return messageWindow && IsWindow(messageWindow);
    }

    void DestroySlot(ContentSlot& slot) {
        if (slot.hwnd && IsWindow(slot.hwnd)) LogSlot(miaodesk::log::Level::Info, L"销毁 Content Surface", slot);
        ReleaseSurface(slot);
        if (slot.hwnd && IsWindow(slot.hwnd)) DestroyWindow(slot.hwnd);
        slot.hwnd = nullptr;
    }

    ContentSlot* FindSlot(std::wstring_view id) {
        for (const auto& slot : slots) if (slot && slot->widgetId == id) return slot.get();
        return nullptr;
    }

    bool Attach(ContentSlot& slot, const RECT& desktopRegion) {
        slot.desktopRegion = desktopRegion;
        DesktopShellHost shell;
        std::wstring error;
        if (!shell.EnsureSurface(slot.hwnd, DesktopSurfaceRole::Widget, desktopRegion, !paused, &error)) {
            ReportFailure(&slot, L"Content Widget attach failed: " + error);
            return false;
        }
        return true;
    }

    void ConfigureRuntime(ContentSlot& slot, const ResolvedContentRuntime& runtime) {
        slot.source = runtime.source;
        slot.packageRoot = runtime.packageRoot;
        slot.packageStamp = runtime.packageStamp;
        slot.definition = runtime.definition;
        slot.instanceParameterStamp = InstanceParameterStamp(slot.widgetId);
        ResetRenderer(slot);
    }

    bool CreateSlotWindow(ContentSlot& slot, const DesktopWidget& widget,
                          const RECT& mappedRegion, const RECT& desktopRegion) {
        const int width = std::max<LONG>(1, desktopRegion.right - desktopRegion.left);
        const int height = std::max<LONG>(1, desktopRegion.bottom - desktopRegion.top);
        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kNativeWidgetSurfaceClass, SlotToken(widget, mappedRegion).c_str(),
            WS_POPUP | WS_CLIPSIBLINGS,
            desktopRegion.left, desktopRegion.top, width, height,
            nullptr, nullptr, instance, &slot);
        if (!hwnd) {
            ReportFailure(&slot, L"Content Widget CreateWindowEx failed Win32=" + std::to_wstring(GetLastError()));
            return false;
        }
        slot.hwnd = hwnd;
        slot.region = mappedRegion;
        slot.desktopRegion = desktopRegion;
        MarkSurfaceRole(hwnd);
        if (!Attach(slot, desktopRegion)) {
            DestroyWindow(hwnd);
            slot.hwnd = nullptr;
            return false;
        }
        ReleaseSurface(slot);
        PaintSlot(slot);
        if (!paused) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        return IsWindow(hwnd) != FALSE;
    }

    bool CreateSlot(const DesktopWidget& widget, const ResolvedContentRuntime& runtime,
                    const RECT& desktopRegion, const RECT& mappedRegion) {
        auto slot = std::make_unique<ContentSlot>();
        slot->owner = this;
        slot->widgetId = widget.id;
        ConfigureRuntime(*slot, runtime);
        if (!CreateSlotWindow(*slot, widget, mappedRegion, desktopRegion)) return false;
        slots.push_back(std::move(slot));
        return true;
    }

    void SyncFromStore() {
        DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) {
            ReportFailure(nullptr, L"ContentWidgetHost store load failed: " + error);
            return;
        }
        const MonitorTopology topology = QueryMonitorTopology();
        if (!topology.Valid()) {
            ReportFailure(nullptr, L"ContentWidgetHost monitor topology unavailable");
            return;
        }

        std::vector<std::wstring> desiredIds;
        std::wstring rejectedDetail;
        for (const auto& raw : store.Items()) {
            const DesktopWidget widget = DesktopWidgetStore::Normalize(raw);
            if (!widget.enabled || widget.kind != DesktopWidgetKind::Content) continue;

            ResolvedContentRuntime runtime;
            std::wstring resolveError;
            if (!ResolveContentRuntime(widget, &runtime, &resolveError)) {
                if (rejectedDetail.empty()) rejectedDetail = widget.id + L": " + resolveError;
                continue;
            }
            const MonitorInfo* monitor = widget.monitorId.empty()
                ? PrimaryMonitor(topology) : FindMonitorByStableId(topology, widget.monitorId);
            if (!monitor) continue;
            const RECT desktopRegion = WidgetRegionInDesktop(*monitor, widget);
            const RECT mappedRegion = MapDesktopRectToParent(parent, desktopRegion);
            if (mappedRegion.right <= mappedRegion.left || mappedRegion.bottom <= mappedRegion.top) continue;

            desiredIds.push_back(widget.id);
            ContentSlot* existing = FindSlot(widget.id);
            if (!existing) {
                CreateSlot(widget, runtime, desktopRegion, mappedRegion);
                continue;
            }
            if (existing->dragging || existing->geometryGraceUntil > GetTickCount64()) continue;

            const bool runtimeChanged = existing->source != runtime.source ||
                                        existing->packageRoot != runtime.packageRoot ||
                                        existing->packageStamp != runtime.packageStamp;
            const auto parameterStamp = InstanceParameterStamp(widget.id);
            const bool parametersChanged = existing->instanceParameterStamp != parameterStamp;
            if (!runtimeChanged && parametersChanged) {
                existing->instanceParameterStamp = parameterStamp;
                ResetRenderer(*existing);
            }

            const LONG oldW = existing->region.right - existing->region.left;
            const LONG oldH = existing->region.bottom - existing->region.top;
            const LONG newW = mappedRegion.right - mappedRegion.left;
            const LONG newH = mappedRegion.bottom - mappedRegion.top;
            const bool regionChanged = existing->region.left != mappedRegion.left ||
                                       existing->region.top != mappedRegion.top ||
                                       existing->region.right != mappedRegion.right ||
                                       existing->region.bottom != mappedRegion.bottom;
            if (runtimeChanged) {
                DestroySlot(*existing);
                ConfigureRuntime(*existing, runtime);
                CreateSlotWindow(*existing, widget, mappedRegion, desktopRegion);
            } else if (regionChanged && oldW == newW && oldH == newH && existing->hwnd && IsWindow(existing->hwnd)) {
                SetWindowPos(existing->hwnd, nullptr, mappedRegion.left, mappedRegion.top, newW, newH,
                             SWP_NOACTIVATE | SWP_NOZORDER);
                existing->region = mappedRegion;
                existing->desktopRegion = desktopRegion;
            } else if (regionChanged) {
                DestroySlot(*existing);
                CreateSlotWindow(*existing, widget, mappedRegion, desktopRegion);
            }
            if (existing->hwnd && IsWindow(existing->hwnd) &&
                (parametersChanged || GetPropW(existing->hwnd, kNativeWidgetPaintReadyProperty) == nullptr)) {
                PaintSlot(*existing);
            }
        }

        slots.erase(std::remove_if(slots.begin(), slots.end(), [&](const auto& slot) {
            if (!slot) return true;
            const bool keep = std::find(desiredIds.begin(), desiredIds.end(), slot->widgetId) != desiredIds.end();
            if (!keep) DestroySlot(*slot);
            return !keep;
        }), slots.end());

        const auto visible = std::count_if(slots.begin(), slots.end(), [](const auto& slot) {
            return slot && slot->hwnd && IsWindow(slot->hwnd) && IsWindowVisible(slot->hwnd);
        });
        const auto ready = std::count_if(slots.begin(), slots.end(), [](const auto& slot) {
            return slot && slot->hwnd && IsWindow(slot->hwnd) &&
                   GetPropW(slot->hwnd, kNativeWidgetPaintReadyProperty) != nullptr;
        });
        std::wstring summary = L"Content Direct2D Widget host desired=" + std::to_wstring(desiredIds.size()) +
                               L" surfaces=" + std::to_wstring(slots.size()) +
                               L" visible=" + std::to_wstring(visible) +
                               L" paintReady=" + std::to_wstring(ready);
        if (!rejectedDetail.empty()) summary += L" rejected=\"" + rejectedDetail + L"\"";
        if (!lastError.empty()) summary += L" error=\"" + lastError + L"\"";
        WriteDiagnostics(summary);
        if (summary != lastLoggedSummary) {
            miaodesk::log::Info(L"ContentWidgetHost", summary);
            lastLoggedSummary = summary;
        }

        DesktopShellHost shell;
        std::wstring shellError;
        if (shell.EnsureCurrent(&shellError)) shell.RepairKnownMiaoDeskSurfaces();
    }

    void RepaintDue() {
        if (paused) return;
        const ULONGLONG now = GetTickCount64();
        for (const auto& slot : slots) {
            if (!slot || !slot->hwnd || !IsWindow(slot->hwnd)) continue;
            if (slot->nextRefreshAt == 0 || now >= slot->nextRefreshAt) PaintSlot(*slot);
        }
    }

    void SetPaused(bool value) {
        if (paused == value) return;
        paused = value;
        if (!paused) {
            for (const auto& slot : slots) {
                if (slot && slot->hwnd && IsWindow(slot->hwnd)) PaintSlot(*slot);
            }
        }
    }

    int Run() {
        if (!parent || !IsWindow(parent) || !EnsureFactory() || !EnsureWindows()) return 74;
        WriteDiagnostics(L"Content Direct2D Widget host started parent=" + HandleText(parent));
        miaodesk::log::Info(L"ContentWidgetHost", L"Content 组件宿主启动: parent=" + HandleText(parent));
        SetTimer(messageWindow, kSyncTimerId, kSyncIntervalMs, nullptr);
        SetTimer(messageWindow, kRefreshTimerId, kRefreshTickMs, nullptr);
        SyncFromStore();

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        KillTimer(messageWindow, kSyncTimerId);
        KillTimer(messageWindow, kRefreshTimerId);
        for (auto& slot : slots) if (slot) DestroySlot(*slot);
        slots.clear();
        if (messageWindow && IsWindow(messageWindow)) DestroyWindow(messageWindow);
        messageWindow = nullptr;
        WriteDiagnostics(L"Content Direct2D Widget host stopped");
        return 0;
    }
};

ContentWidgetHostApp* gContentHost{};

LRESULT CALLBACK ContentHostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kShutdownMessage && gContentHost) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == kPauseMessage && gContentHost) {
        gContentHost->SetPaused(true);
        return 0;
    }
    if (message == kResumeMessage && gContentHost) {
        gContentHost->SetPaused(false);
        return 0;
    }
    if (message == WM_TIMER && gContentHost) {
        if (wParam == kSyncTimerId) gContentHost->SyncFromStore();
        if (wParam == kRefreshTimerId) gContentHost->RepaintDue();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool HasRunnableContentWidget(std::wstring* error) {
    DesktopWidgetStore store;
    std::wstring loadError;
    if (!store.Load(&loadError)) {
        if (error) *error = loadError;
        return false;
    }
    bool foundConfiguredContent = false;
    std::wstring firstError;
    for (const auto& raw : store.Items()) {
        const DesktopWidget widget = DesktopWidgetStore::Normalize(raw);
        if (!widget.enabled || widget.kind != DesktopWidgetKind::Content) continue;
        foundConfiguredContent = true;
        ResolvedContentRuntime runtime;
        std::wstring resolveError;
        if (ResolveContentRuntime(widget, &runtime, &resolveError)) return true;
        if (firstError.empty()) firstError = resolveError;
    }
    if (foundConfiguredContent && error) *error = firstError.empty() ? L"No runnable Scene Content widget." : firstError;
    return false;
}

} // namespace

ContentWidgetProcessSet::ContentWidgetProcessSet() = default;
ContentWidgetProcessSet::~ContentWidgetProcessSet() { Stop(); }

bool ContentWidgetProcessSet::Start(HWND parentWindow) {
    Stop();
    lastError_.clear();
    if (!parentWindow || !IsWindow(parentWindow)) {
        lastError_ = L"Content widget host parent invalid";
        return false;
    }

    std::wstring contentError;
    if (!HasRunnableContentWidget(&contentError)) {
        if (!contentError.empty()) {
            lastError_ = L"Content widget configuration is not runnable: " + contentError;
            return false;
        }
        return true;
    }

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
    std::wstring command = QuoteArg(executable) + L" " + kContentHostMode + L" --parent-hwnd " +
                           std::to_wstring(reinterpret_cast<std::uintptr_t>(parentWindow));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        lastError_ = L"Start Content widget host failed Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    process_ = process.hProcess;
    thread_ = process.hThread;
    if (job_) AssignProcessToJobObject(job_, process_);
    return true;
}

void ContentWidgetProcessSet::Stop() {
    if (process_) {
        const DWORD pid = GetProcessId(process_);
        for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner != pid) continue;
            wchar_t className[160]{};
            if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
                _wcsicmp(className, kContentWidgetHostMessageClass) == 0) {
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

void ContentWidgetProcessSet::SetPaused(bool paused) {
    paused_ = paused;
    if (!process_) return;
    const DWORD pid = GetProcessId(process_);
    for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != pid) continue;
        wchar_t className[160]{};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
            _wcsicmp(className, kContentWidgetHostMessageClass) == 0) {
            PostMessageW(window, paused ? kPauseMessage : kResumeMessage, 0, 0);
            break;
        }
    }
}

bool ContentWidgetProcessSet::Active() const noexcept {
    if (!process_) return false;
    DWORD exitCode = STILL_ACTIVE;
    return GetExitCodeProcess(process_, &exitCode) && exitCode == STILL_ACTIVE;
}

std::wstring ContentWidgetProcessSet::LastErrorText() const { return lastError_; }

std::wstring ContentWidgetProcessSet::DiagnosticsText() const {
    if (!Active()) return lastError_.empty() ? L"Content Direct2D Widget host stopped" : lastError_;
    const std::wstring detail = ReadDiagnostics();
    return detail.empty() ? L"Content Direct2D Widget host running" : detail;
}

bool ContentWidgetProcessSet::SelfTest() noexcept {
    return content::MiaoWidgetContentCatalog::IsContentSource(L"content:com.goodloong.example") &&
           !content::MiaoWidgetContentCatalog::IsContentSource(L"native:glass-clock");
}

int TryRunContentWidgetHost(HINSTANCE instance) {
    const auto args = ProcessArguments();
    if (!HasArg(args, kContentHostMode)) return -1;
    const auto parentText = ArgValue(args, L"--parent-hwnd");
    if (!parentText) return 71;
    HWND parent = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(_wcstoui64(parentText->c_str(), nullptr, 10)));
    if (!parent || !IsWindow(parent)) return 72;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return 73;

    ContentWidgetHostApp app;
    app.instance = instance;
    app.parent = parent;
    gContentHost = &app;
    const int result = app.Run();
    gContentHost = nullptr;
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

} // namespace miaodesk::wallpaper
