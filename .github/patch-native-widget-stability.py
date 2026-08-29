from pathlib import Path
import re

ROOT = Path('.')

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

def write(path, text):
    (ROOT / path).write_text(text, encoding='utf-8')

def replace_once(text, old, new, label):
    if old not in text:
        raise SystemExit(f'missing anchor: {label}')
    return text.replace(old, new, 1)

# 1) Native Direct2D painter: allow previews to reuse the exact painter without clearing the GDI card background.
p = 'src/native/include/turingdesk/NativeWidgetPainter.h'
s = read(p)
s = replace_once(s,
'''    SYSTEMTIME localTime{};\n    bool hasTime{};\n};''',
'''    SYSTEMTIME localTime{};\n    bool hasTime{};\n    // Desktop surfaces clear to transparent. Management thumbnails render over\n    // an existing GDI card and therefore keep the destination background.\n    bool clearBackground{true};\n};''',
'painter context clear flag')
s = replace_once(s,
'''    ctx.target->Clear(D2D1::ColorF(0, 0, 0, 0));\n    const D2D1_RECT_F card{1.0f, 1.0f, ctx.width - 1.0f, ctx.height - 1.0f};''',
'''    if (ctx.clearBackground) ctx.target->Clear(D2D1::ColorF(0, 0, 0, 0));\n    const D2D1_RECT_F card{1.0f, 1.0f, ctx.width - 1.0f, ctx.height - 1.0f};''',
'painter conditional clear')
write(p, s)

# 2) Native host: clip the HWND itself to the same rounded silhouette so transparent Direct2D corners never reveal a black rectangular child surface.
p = 'src/native/src/desktop/widgets/NativeWidgetHost.cpp'
s = read(p)
anchor = '''struct NativeWidgetHostApp {\n    HINSTANCE instance{};'''
insert = r'''float NativeCornerRadiusDip(NativeWidgetPreset preset, float widthDip) {
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
    HINSTANCE instance{};'''
s = replace_once(s, anchor, insert, 'native host rounded helper')
s = replace_once(s,
'''        case WM_SIZE:\n            if (slot->target) slot->target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));\n            slot->owner->ResizeDragHandle(*slot);\n            return 0;''',
'''        case WM_SIZE:\n            if (slot->target) slot->target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));\n            ApplyRoundedWindowRegion(*slot);\n            slot->owner->ResizeDragHandle(*slot);\n            return 0;''',
'native host size region')
s = replace_once(s,
'''        slot.hwnd = hwnd;\n        slot.region = mappedRegion;\n        slot.desktopRegion = desktopRegion;''',
'''        slot.hwnd = hwnd;\n        slot.region = mappedRegion;\n        slot.desktopRegion = desktopRegion;\n        ApplyRoundedWindowRegion(slot);''',
'native host create region')
write(p, s)

# 3) Store: serialize every reader/writer across processes and replace the INI atomically.
p = 'src/native/src/desktop/widgets/DesktopWidgetStore.cpp'
s = read(p)
mutex_anchor = '''bool LooksLikeClockWidget(const fs::path& source) {\n    std::ifstream stream(source, std::ios::binary);\n    if (!stream) return false;\n    std::string html((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());\n    if (html.size() > 256 * 1024) html.resize(256 * 1024);\n    return html.find("toLocaleTimeString") != std::string::npos &&\n           html.find("id=\\\"time\\\"") != std::string::npos;\n}\n\n} // namespace'''
mutex_insert = '''bool LooksLikeClockWidget(const fs::path& source) {\n    std::ifstream stream(source, std::ios::binary);\n    if (!stream) return false;\n    std::string html((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());\n    if (html.size() > 256 * 1024) html.resize(256 * 1024);\n    return html.find("toLocaleTimeString") != std::string::npos &&\n           html.find("id=\\\"time\\\"") != std::string::npos;\n}\n\nconstexpr wchar_t kWidgetStoreMutexName[] = L"Local\\\\TuringDesk.DesktopWidgetStore.v1";\n\nclass WidgetStoreMutexGuard {\npublic:\n    WidgetStoreMutexGuard() {\n        handle_ = CreateMutexW(nullptr, FALSE, kWidgetStoreMutexName);\n        if (!handle_) return;\n        const DWORD wait = WaitForSingleObject(handle_, 5000);\n        acquired_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;\n    }\n    ~WidgetStoreMutexGuard() {\n        if (acquired_) ReleaseMutex(handle_);\n        if (handle_) CloseHandle(handle_);\n    }\n    bool Acquired() const noexcept { return acquired_; }\nprivate:\n    HANDLE handle_{};\n    bool acquired_{};\n};\n\nbool EnsureStoreLock(const WidgetStoreMutexGuard& guard, std::wstring* error) {\n    if (guard.Acquired()) return true;\n    if (error) *error = L"Desktop widget storage is busy; please retry.";\n    return false;\n}\n\nstd::wstring NativeSingletonKey(const DesktopWidget& widget) {\n    if (widget.kind != DesktopWidgetKind::Native || !IsNativePresetSource(widget.source.wstring())) return {};\n    std::wstring key = widget.source.wstring();\n    key += L"|";\n    key += widget.monitorId.empty() ? L"<primary>" : widget.monitorId;\n    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });\n    return key;\n}\n\n} // namespace'''
s = replace_once(s, mutex_anchor, mutex_insert, 'store mutex helpers')

s = replace_once(s,
'''bool DesktopWidgetStore::Load(std::wstring* error) {\n    if (error) error->clear();\n    items_.clear();''',
'''bool DesktopWidgetStore::Load(std::wstring* error) {\n    if (error) error->clear();\n    WidgetStoreMutexGuard storeLock;\n    if (!EnsureStoreLock(storeLock, error)) return false;\n    items_.clear();''',
'store load lock')

# Deduplicate old native singleton presets during load. Prefer an enabled earlier item; otherwise replace a disabled one with the enabled duplicate.
load_tail = '''        widget = Normalize(std::move(widget));\n        if (widget.kind == DesktopWidgetKind::Unknown || widget.source.empty()) continue;\n        items_.push_back(std::move(widget));\n    }\n\n    if (legacyAnsi || repairedText) {'''
load_tail_new = '''        widget = Normalize(std::move(widget));\n        if (widget.kind == DesktopWidgetKind::Unknown || widget.source.empty()) continue;\n\n        const std::wstring singletonKey = NativeSingletonKey(widget);\n        if (!singletonKey.empty()) {\n            auto duplicate = std::find_if(items_.begin(), items_.end(), [&](const DesktopWidget& existing) {\n                return NativeSingletonKey(existing) == singletonKey;\n            });\n            if (duplicate != items_.end()) {\n                if (!duplicate->enabled && widget.enabled) *duplicate = std::move(widget);\n                repairedText = true;\n                continue;\n            }\n        }\n        items_.push_back(std::move(widget));\n    }\n\n    if (legacyAnsi || repairedText) {'''
s = replace_once(s, load_tail, load_tail_new, 'store native duplicate repair')

# Replace Save as one atomic temp-file commit protected by the named mutex.
save_start = s.index('bool DesktopWidgetStore::Save(std::wstring* error) const {')
save_end = s.index('std::optional<DesktopWidget> DesktopWidgetStore::Upsert', save_start)
new_save = r'''bool DesktopWidgetStore::Save(std::wstring* error) const {
    if (error) error->clear();
    WidgetStoreMutexGuard storeLock;
    if (!EnsureStoreLock(storeLock, error)) return false;
    std::error_code ec;
    fs::create_directories(PackageDirectory(), ec);
    if (ec) {
        if (error) *error = L"Unable to create desktop widget storage.";
        return false;
    }

    const fs::path manifest = ManifestPath();
    fs::path temporary = manifest;
    temporary += L".tmp";
    DeleteFileW(temporary.c_str());
    if (!CreateUnicodeIni(temporary)) {
        if (error) *error = L"Unable to create temporary Unicode desktop widget manifest.";
        return false;
    }

    std::wstring ids;
    for (const auto& widget : items_) {
        if (!SafeId(widget.id)) continue;
        if (!ids.empty()) ids.push_back(L';');
        ids += widget.id;
    }
    if (!WriteText(temporary, L"Widgets", L"Ids", ids)) {
        DeleteFileW(temporary.c_str());
        if (error) *error = L"Unable to save desktop widget index.";
        return false;
    }

    for (const auto& raw : items_) {
        if (!SafeId(raw.id)) continue;
        const DesktopWidget widget = Normalize(raw);
        const std::wstring section = L"Widget." + widget.id;
        bool ok = true;
        ok = WriteText(temporary, section, L"Kind", KindKey(widget.kind)) && ok;
        ok = WriteText(temporary, section, L"Title", widget.title) && ok;
        ok = WriteText(temporary, section, L"Source", widget.source.wstring()) && ok;
        ok = WriteText(temporary, section, L"MonitorId", widget.monitorId) && ok;
        ok = WriteText(temporary, section, L"X", FloatText(widget.x)) && ok;
        ok = WriteText(temporary, section, L"Y", FloatText(widget.y)) && ok;
        ok = WriteText(temporary, section, L"Width", FloatText(widget.width)) && ok;
        ok = WriteText(temporary, section, L"Height", FloatText(widget.height)) && ok;
        ok = WriteText(temporary, section, L"ZIndex", std::to_wstring(widget.zIndex)) && ok;
        ok = WriteText(temporary, section, L"Enabled", widget.enabled ? L"1" : L"0") && ok;
        ok = WriteText(temporary, section, L"ManagedSource", widget.managedSource ? L"1" : L"0") && ok;
        if (!ok) {
            DeleteFileW(temporary.c_str());
            if (error) *error = L"Unable to save desktop widget: " + widget.id;
            return false;
        }
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    if (!MoveFileExW(temporary.c_str(), manifest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temporary.c_str());
        if (error) *error = L"Unable to atomically replace desktop widget manifest. Win32=" + std::to_wstring(code);
        return false;
    }
    return true;
}

'''
s = s[:save_start] + new_save + s[save_end:]

# Refresh from disk under the same recursive named mutex before every mutation so a stale process never overwrites newly-created widgets.
s = replace_once(s,
'''std::optional<DesktopWidget> DesktopWidgetStore::Upsert(DesktopWidget widget, std::wstring* error) {\n    if (error) error->clear();\n    if (widget.id.empty()) widget.id = MakeId();''',
'''std::optional<DesktopWidget> DesktopWidgetStore::Upsert(DesktopWidget widget, std::wstring* error) {\n    if (error) error->clear();\n    WidgetStoreMutexGuard storeLock;\n    if (!EnsureStoreLock(storeLock, error)) return std::nullopt;\n    std::wstring refreshError;\n    if (!Load(&refreshError)) {\n        if (error) *error = refreshError;\n        return std::nullopt;\n    }\n    if (widget.id.empty()) widget.id = MakeId();''',
'store upsert refresh')
s = replace_once(s,
'''bool DesktopWidgetStore::UpdateManagedHtml(std::wstring_view id, std::string_view htmlUtf8, std::wstring* error) {\n    if (error) error->clear();\n    const auto index = FindIndex(id);''',
'''bool DesktopWidgetStore::UpdateManagedHtml(std::wstring_view id, std::string_view htmlUtf8, std::wstring* error) {\n    if (error) error->clear();\n    WidgetStoreMutexGuard storeLock;\n    if (!EnsureStoreLock(storeLock, error)) return false;\n    std::wstring refreshError;\n    if (!Load(&refreshError)) { if (error) *error = refreshError; return false; }\n    const auto index = FindIndex(id);''',
'store html refresh')
s = replace_once(s,
'''bool DesktopWidgetStore::Remove(std::wstring_view id, bool deleteManagedSource, std::wstring* error) {\n    if (error) error->clear();\n    const auto index = FindIndex(id);''',
'''bool DesktopWidgetStore::Remove(std::wstring_view id, bool deleteManagedSource, std::wstring* error) {\n    if (error) error->clear();\n    WidgetStoreMutexGuard storeLock;\n    if (!EnsureStoreLock(storeLock, error)) return false;\n    std::wstring refreshError;\n    if (!Load(&refreshError)) { if (error) *error = refreshError; return false; }\n    const auto index = FindIndex(id);''',
'store remove refresh')
write(p, s)

# 4) Controller: source/preset identity is authoritative; no overlapping fallback and no duplicate singleton presets per monitor.
p = 'src/native/src/desktop/widgets/DesktopWidgetController.cpp'
s = read(p)
s = replace_once(s, '#include <algorithm>\n#include <cmath>', '#include <algorithm>\n#include <array>\n#include <cmath>\n#include <optional>', 'controller includes')
start = s.index('std::pair<float, float> AutomaticPlacement(')
end = s.index('void AppendControllerErrorLog', start)
new_placement = r'''std::optional<std::pair<float, float>> AutomaticPlacement(
    const std::vector<wallpaper::DesktopWidget>& widgets,
    std::wstring_view monitorId,
    float width,
    float height) {
    const float maxX = std::max(kPlacementMargin, 1.0f - kPlacementMargin - width);
    const float maxY = std::max(kPlacementMargin, 1.0f - kPlacementMargin - height);
    const int xSteps = std::max(0, static_cast<int>(std::ceil((maxX - kPlacementMargin) / kPlacementGap)));
    const int ySteps = std::max(0, static_cast<int>(std::ceil((maxY - kPlacementMargin) / kPlacementGap)));

    for (int xStep = 0; xStep <= xSteps; ++xStep) {
        const float x = std::max(kPlacementMargin, maxX - static_cast<float>(xStep) * kPlacementGap);
        for (int yStep = 0; yStep <= ySteps; ++yStep) {
            const float y = std::min(maxY, kPlacementMargin + static_cast<float>(yStep) * kPlacementGap);
            const NormalizedRect candidate{x, y, x + width, y + height};
            if (PlacementFree(widgets, monitorId, candidate)) return std::pair{x, y};
        }
    }
    return std::nullopt;
}

std::wstring CanonicalMonitorId(std::wstring monitorId) {
    if (monitorId.empty()) return monitorId;
    const auto topology = wallpaper::QueryMonitorTopology();
    if (!topology.Valid()) return monitorId;
    const wallpaper::MonitorInfo* primary = nullptr;
    for (const auto& monitor : topology.monitors) {
        if (monitor.primary) { primary = &monitor; break; }
    }
    if (!primary && !topology.monitors.empty()) primary = &topology.monitors.front();
    if (!primary) return monitorId;
    const auto key = wallpaper::StableMonitorKey(*primary);
    if (_wcsicmp(key.c_str(), monitorId.c_str()) == 0 ||
        _wcsicmp(primary->deviceName.c_str(), monitorId.c_str()) == 0) return {};
    return monitorId;
}

bool MatchesNativePreset(const wallpaper::DesktopWidget& widget,
                         wallpaper::NativeWidgetPreset preset,
                         std::wstring_view monitorId) {
    if (widget.kind != wallpaper::DesktopWidgetKind::Native || !SameMonitor(widget, monitorId)) return false;
    wallpaper::NativeWidgetPreset existing{};
    return wallpaper::ParseNativePreset(widget.source.wstring(), &existing) && existing == preset;
}

'''
s = s[:start] + new_placement + s[end:]

start = s.index('DesktopControlResult DesktopWidgetController::CreateClock(')
end = s.index('DesktopControlResult DesktopWidgetController::SetEnabled', start)
new_create = r'''DesktopControlResult DesktopWidgetController::CreateClock(
    std::wstring monitorId,
    wallpaper::DesktopWidget* created) const {
    monitorId = CanonicalMonitorId(std::move(monitorId));
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;

    constexpr std::array<WidgetFixedPreset, 3> order{
        WidgetFixedPreset::GlassClock,
        WidgetFixedPreset::TodayTasks,
        WidgetFixedPreset::WeatherGlass,
    };

    // The generic "new widget" action fills the built-in showcase exactly once
    // per display. Identity comes from native: source, never from localized title.
    for (const auto preset : order) {
        const bool exists = std::any_of(existing.begin(), existing.end(), [&](const auto& widget) {
            return MatchesNativePreset(widget, preset, monitorId);
        });
        if (!exists) return CreatePreset(preset, monitorId, created);
    }
    for (const auto preset : order) {
        auto disabled = std::find_if(existing.begin(), existing.end(), [&](const auto& widget) {
            return MatchesNativePreset(widget, preset, monitorId) && !widget.enabled;
        });
        if (disabled != existing.end()) return CreatePreset(preset, monitorId, created);
    }
    return {false, L"该显示器的玻璃时钟、今日待办和玻璃天气均已存在；请拖动、停用或删除现有组件。"};
}

DesktopControlResult DesktopWidgetController::CreatePreset(
    WidgetFixedPreset preset,
    std::wstring monitorId,
    wallpaper::DesktopWidget* created) const {
    monitorId = CanonicalMonitorId(std::move(monitorId));
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;

    const auto* definition = wallpaper::NativePresetDefinition(preset);
    if (!definition) return {false, L"未知的原生小组件模板。"};

    auto duplicate = std::find_if(existing.begin(), existing.end(), [&](const auto& widget) {
        return MatchesNativePreset(widget, preset, monitorId);
    });
    if (duplicate != existing.end()) {
        if (created) *created = *duplicate;
        if (!duplicate->enabled) {
            const auto enabled = SetEnabled(duplicate->id, true);
            if (enabled.success && created) created->enabled = true;
            return enabled.success
                ? DesktopControlResult{true, L"已重新启用现有「" + std::wstring(definition->title) + L"」。"}
                : enabled;
        }
        return {false, L"该显示器已经存在「" + std::wstring(definition->title) + L"」，不会重复创建重叠副本。"};
    }

    const auto placement = AutomaticPlacement(existing, monitorId, definition->defaultWidth, definition->defaultHeight);
    if (!placement) {
        return {false, L"当前显示器没有足够的空闲区域放置「" + std::wstring(definition->title) + L"」；请先移动或删除现有小组件。"};
    }

    NativeWidgetCreateRequest request;
    request.preset = preset;
    request.title = std::wstring(definition->title);
    request.monitorId = std::move(monitorId);
    request.x = placement->first;
    request.y = placement->second;
    request.width = definition->defaultWidth;
    request.height = definition->defaultHeight;
    return service_.CreateNativeWidget(request, created);
}

'''
s = s[:start] + new_create + s[end:]
write(p, s)

# 5) Library thumbnails: render native widgets through the exact same Direct2D painter, fitted to their actual monitor-relative aspect ratio.
p = 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
s = read(p)
s = replace_once(s,
'''#include "turingdesk/LiveLogWindow.h"\n\n#include <commctrl.h>''',
'''#include "turingdesk/LiveLogWindow.h"\n#include "turingdesk/NativeWidgetPainter.h"\n#include "turingdesk/NativeWidgetPreset.h"\n\n#include <commctrl.h>''',
'library native painter include')
s = replace_once(s,
'''#include <shellapi.h>\n#include <windowsx.h>''',
'''#include <shellapi.h>\n#include <windowsx.h>\n#include <d2d1.h>\n#include <dwrite.h>\n#include <wrl/client.h>''',
'library d2d includes')

member_anchor = '''    HFONT cardTitleFont{};\n    HFONT cardSmallFont{};'''
member_new = '''    HFONT cardTitleFont{};\n    HFONT cardSmallFont{};\n\n    Microsoft::WRL::ComPtr<ID2D1Factory> widgetPreviewFactory;\n    Microsoft::WRL::ComPtr<IDWriteFactory> widgetPreviewDWrite;\n    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> widgetPreviewTarget;'''
s = replace_once(s, member_anchor, member_new, 'library preview members')

insert_before = '''    void DrawWidgetCard(HDC dc, int index, const RECT& card) {'''
helper = r'''    bool EnsureNativeWidgetPreviewRenderer() {
        if (!widgetPreviewFactory) {
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, widgetPreviewFactory.GetAddressOf()))) return false;
        }
        if (!widgetPreviewDWrite) {
            if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                           reinterpret_cast<IUnknown**>(widgetPreviewDWrite.GetAddressOf())))) return false;
        }
        if (!widgetPreviewTarget) {
            const auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96.0f, 96.0f);
            if (FAILED(widgetPreviewFactory->CreateDCRenderTarget(&properties, widgetPreviewTarget.GetAddressOf()))) return false;
        }
        return true;
    }

    bool DrawNativeWidgetPreview(HDC dc, const RECT& bounds, const DesktopWidget& widget,
                                 NativeWidgetPreset preset) {
        if (!EnsureNativeWidgetPreviewRenderer()) return false;
        RECT render = bounds;
        InflateRect(&render, -S(6), -S(6));
        const int availableW = std::max(1, RectWidth(render));
        const int availableH = std::max(1, RectHeight(render));
        const float screenW = static_cast<float>(std::max(1, GetSystemMetrics(SM_CXSCREEN)));
        const float screenH = static_cast<float>(std::max(1, GetSystemMetrics(SM_CYSCREEN)));
        const float aspect = std::clamp((widget.width * screenW) / std::max(1.0f, widget.height * screenH), 0.35f, 4.0f);
        if (static_cast<float>(availableW) / availableH > aspect) {
            const int fittedW = std::max(1, static_cast<int>(std::lround(availableH * aspect)));
            render.left += (availableW - fittedW) / 2;
            render.right = render.left + fittedW;
        } else {
            const int fittedH = std::max(1, static_cast<int>(std::lround(availableW / aspect)));
            render.top += (availableH - fittedH) / 2;
            render.bottom = render.top + fittedH;
        }

        FillSolid(dc, bounds, RGB(248, 250, 253));
        if (FAILED(widgetPreviewTarget->BindDC(dc, &render))) return false;
        widgetPreviewTarget->SetDpi(96.0f, 96.0f);
        NativeWidgetPaintContext context{};
        context.target = widgetPreviewTarget.Get();
        context.dwrite = widgetPreviewDWrite.Get();
        context.width = static_cast<float>(std::max(1, RectWidth(render)));
        context.height = static_cast<float>(std::max(1, RectHeight(render)));
        context.clearBackground = false;
        if (preset == NativeWidgetPreset::GlassClock) {
            GetLocalTime(&context.localTime);
            context.hasTime = true;
        }
        widgetPreviewTarget->BeginDraw();
        PaintNativeWidgetPreset(context, preset);
        return SUCCEEDED(widgetPreviewTarget->EndDraw());
    }

    void DrawWidgetCard(HDC dc, int index, const RECT& card) {'''
s = replace_once(s, insert_before, helper, 'library preview helper')

# Replace only the old fake preview section inside DrawWidgetCard, keeping text/status rendering unchanged.
old_preview = '''        const bool clock = widget.title.find(L"时钟") != std::wstring::npos;\n        const bool tasks = widget.title.find(L"待办") != std::wstring::npos;\n        const bool weather = widget.title.find(L"天气") != std::wstring::npos;\n        COLORREF previewBase = RGB(72, 87, 132);\n        COLORREF previewAccent = RGB(153, 190, 255);\n        if (clock) {\n            previewBase = RGB(38, 73, 112);\n            previewAccent = RGB(104, 230, 218);\n        } else if (weather) {\n            previewBase = RGB(73, 151, 204);\n            previewAccent = RGB(178, 229, 255);\n        } else if (tasks) {\n            previewBase = RGB(35, 91, 79);\n            previewAccent = RGB(111, 229, 190);\n        }\n\n        RECT preview = card;\n        preview.bottom -= S(50);\n        FillSolid(dc, preview, previewBase);\n        HBRUSH accentBrush = CreateSolidBrush(previewAccent);\n        HGDIOBJ oldBrush = SelectObject(dc, accentBrush);\n        HPEN accentPen = CreatePen(PS_NULL, 0, previewAccent);\n        HGDIOBJ oldPen = SelectObject(dc, accentPen);\n        const int glow = std::max(S(68), RectHeight(preview));\n        Ellipse(dc, preview.right - glow, preview.top - glow / 3,\n                preview.right + glow / 3, preview.top + glow);\n        SelectObject(dc, oldPen);\n        SelectObject(dc, oldBrush);\n        DeleteObject(accentPen);\n        DeleteObject(accentBrush);\n\n        SetBkMode(dc, TRANSPARENT);\n        HGDIOBJ old = SelectObject(dc, titleFont);\n        SetTextColor(dc, RGB(250, 252, 255));\n        const wchar_t* previewLabel = clock ? L"12:34" : tasks ? L"待办" : weather ? L"22°" : L"组件";\n        DrawTextW(dc, previewLabel, -1, &preview, DT_CENTER | DT_VCENTER | DT_SINGLELINE);'''
new_preview = '''        RECT preview = card;\n        preview.bottom -= S(50);\n        NativeWidgetPreset nativePreset{};\n        const bool exactNativePreview = widget.kind == DesktopWidgetKind::Native &&\n            ParseNativePreset(widget.source.wstring(), &nativePreset) &&\n            DrawNativeWidgetPreview(dc, preview, widget, nativePreset);\n        if (!exactNativePreview) {\n            FillSolid(dc, preview, RGB(72, 87, 132));\n            SetBkMode(dc, TRANSPARENT);\n            HGDIOBJ previewOld = SelectObject(dc, titleFont);\n            SetTextColor(dc, RGB(250, 252, 255));\n            DrawTextW(dc, L"组件", -1, &preview, DT_CENTER | DT_VCENTER | DT_SINGLELINE);\n            SelectObject(dc, previewOld);\n        }\n\n        SetBkMode(dc, TRANSPARENT);\n        HGDIOBJ old = SelectObject(dc, titleFont);'''
s = replace_once(s, old_preview, new_preview, 'library exact widget preview')
write(p, s)

print('native widget stability patch applied')
