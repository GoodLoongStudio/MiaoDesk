from pathlib import Path

ROOT = Path('.')

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

def write(path, text):
    (ROOT / path).write_text(text, encoding='utf-8')

def once(text, old, new, name):
    if old not in text:
        raise SystemExit(f'missing anchor: {name}')
    return text.replace(old, new, 1)

# CMake: native weather belongs only to the isolated wallpaper/widget runtime.
p = 'src/native/CMakeLists.txt'
s = read(p)
s = once(s,
'''    src/desktop/widgets/DesktopWidgetController.cpp\n    src/desktop/widgets/NativeWidgetPreset.cpp\n    src/ui/widgets/DesktopWidgetUiAdapter.cpp''',
'''    src/desktop/widgets/DesktopWidgetController.cpp\n    src/desktop/widgets/NativeWidgetPreset.cpp\n    src/desktop/widgets/NativeWeatherService.cpp\n    src/ui/widgets/DesktopWidgetUiAdapter.cpp''',
'wallpaper weather source')
write(p, s)

# Painter: remove every fake weather value; consume only real/cached native data.
p = 'src/native/include/turingdesk/NativeWidgetPainter.h'
s = read(p)
s = once(s,
'''#include "turingdesk/NativeWidgetPreset.h"''',
'''#include "turingdesk/NativeWidgetPreset.h"\n#include "turingdesk/NativeWeatherData.h"''',
'painter weather include')
s = once(s,
'''    bool hasTime{};\n    // Desktop surfaces clear to transparent.''',
'''    bool hasTime{};\n    const NativeWeatherSnapshot* weather{};\n    // Desktop surfaces clear to transparent.''',
'painter weather context')
start = s.index('void PaintWeatherGlass(const NativeWidgetPaintContext& ctx) {')
end = s.index('void PaintTodayTasks(const NativeWidgetPaintContext& ctx) {', start)
weather = r'''void PaintWeatherGlass(const NativeWidgetPaintContext& ctx) {
    const float radius = std::clamp(ctx.width * 0.078f, 22.0f, 32.0f);
    DrawGlassCardBase(ctx,
        D2D1::ColorF(0.20f, 0.65f, 0.95f, 0.96f),
        D2D1::ColorF(0.16f, 0.53f, 0.90f, 0.95f),
        D2D1::ColorF(0.08f, 0.28f, 0.68f, 0.97f), radius);

    const NativeWeatherSnapshot* weather = ctx.weather && ctx.weather->valid ? ctx.weather : nullptr;
    const int code = weather ? weather->weatherCode : -1;
    const bool clearSky = code == 0 || code == 1;
    const bool rainy = (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
    const bool snowy = (code >= 71 && code <= 77) || code == 85 || code == 86;

    auto sunGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.76f, ctx.height * 0.37f), ctx.width * 0.22f, ctx.height * 0.34f,
                               D2D1::ColorF(1.0f, 0.88f, 0.34f, clearSky ? 0.50f : 0.32f), D2D1::ColorF(1.0f, 0.8f, 0.2f, 0.0f));
    if (sunGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), sunGlow.Get());

    auto white = Brush(ctx.target, 0.98f, 0.995f, 1.0f, 0.98f);
    auto muted = Brush(ctx.target, 0.88f, 0.95f, 1.0f, 0.86f);
    auto sun = Brush(ctx.target, 1.0f, 0.86f, 0.30f, 0.92f);
    auto cloud = Brush(ctx.target, 0.94f, 0.985f, 1.0f, 0.90f);
    DrawCatMark(ctx.target, 24.0f, 24.0f, 0.7f, muted.Get());
    std::wstring header = L"妙喵 · ";
    header += weather && !weather->location.empty() ? weather->location : L"本地";
    header += L"天气";
    Text(ctx.target, ctx.dwrite, header, 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, muted.Get(), 39.0f, 16.0f, ctx.width * 0.56f, 18.0f);

    if (!rainy && !snowy) {
        Circle(ctx.target, sun.Get(), ctx.width * 0.79f, ctx.height * 0.36f, std::clamp(ctx.width * 0.075f, 14.0f, 25.0f));
    }
    const float cloudY = ctx.height * 0.43f;
    if (!clearSky || !weather) {
        Circle(ctx.target, cloud.Get(), ctx.width * 0.72f, cloudY, 18.0f);
        Circle(ctx.target, cloud.Get(), ctx.width * 0.79f, cloudY - 8.0f, 24.0f);
        Circle(ctx.target, cloud.Get(), ctx.width * 0.86f, cloudY, 19.0f);
        RoundRect(ctx.target, cloud.Get(), nullptr, D2D1::RectF(ctx.width * 0.68f, cloudY - 2.0f, ctx.width * 0.90f, cloudY + 18.0f), 10.0f);
        if (rainy) {
            auto rain = Brush(ctx.target, 0.62f, 0.91f, 1.0f, 0.88f);
            for (int i = 0; i < 4; ++i) {
                const float x = ctx.width * (0.72f + i * 0.045f);
                ctx.target->DrawLine(D2D1::Point2F(x, cloudY + 23.0f), D2D1::Point2F(x - 4.0f, cloudY + 34.0f), rain.Get(), 2.0f);
            }
        } else if (snowy) {
            for (int i = 0; i < 4; ++i)
                Circle(ctx.target, white.Get(), ctx.width * (0.72f + i * 0.045f), cloudY + 29.0f + (i % 2) * 5.0f, 2.0f);
        }
    }

    const std::wstring tempText = weather ? std::to_wstring(weather->temperatureC) + L"°" : L"--°";
    std::wstring conditionText;
    if (weather) conditionText = weather->condition;
    else if (ctx.weather && !ctx.weather->status.empty()) conditionText = ctx.weather->status;
    else conditionText = L"正在获取天气";
    const std::wstring rangeText = weather
        ? L"↑ " + std::to_wstring(weather->highC) + L"°   ↓ " + std::to_wstring(weather->lowC) + L"°"
        : L"↑ --°   ↓ --°";

    const float tempSize = std::clamp(ctx.width * 0.20f, 38.0f, 66.0f);
    Text(ctx.target, ctx.dwrite, tempText, tempSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(), 20.0f, 48.0f, ctx.width * 0.48f, tempSize + 10.0f);
    Text(ctx.target, ctx.dwrite, conditionText, 16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(), 23.0f, 48.0f + tempSize, ctx.width * 0.46f, 24.0f);
    Text(ctx.target, ctx.dwrite, rangeText, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(), 23.0f, 73.0f + tempSize, ctx.width * 0.54f, 20.0f);

    const float gap = 6.0f;
    const float totalW = ctx.width - 28.0f;
    const float chipW = (totalW - gap * 3.0f) / 4.0f;
    float x = 14.0f;
    const float chipTop = ctx.height - 58.0f;
    auto chipFill = Brush(ctx.target, 0.05f, 0.18f, 0.40f, 0.30f);
    auto chipBorder = Brush(ctx.target, 0.9f, 0.98f, 1.0f, 0.14f);
    for (std::size_t i = 0; i < 4; ++i) {
        const std::wstring label = weather && !weather->hours[i].label.empty() ? weather->hours[i].label : L"--:--";
        const std::wstring value = weather ? std::to_wstring(weather->hours[i].temperatureC) + L"°" : L"--°";
        const D2D1_RECT_F chip{x, chipTop, x + chipW, ctx.height - 13.0f};
        RoundRect(ctx.target, chipFill.Get(), chipBorder.Get(), chip, 13.0f, 0.8f);
        Text(ctx.target, ctx.dwrite, label, 9.5f, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(), chip.left + 4, chip.top + 5, chipW - 8, 14, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(ctx.target, ctx.dwrite, value, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(), chip.left + 4, chip.top + 21, chipW - 8, 18, DWRITE_TEXT_ALIGNMENT_CENTER);
        x += chipW + gap;
    }
}

'''
s = s[:start] + weather + s[end:]
write(p, s)

# Native host: start weather I/O only when a weather widget actually exists,
# keep all network work off the paint/message thread, and repaint on data arrival.
p = 'src/native/src/desktop/widgets/NativeWidgetHost.cpp'
s = read(p)
s = once(s,
'''#include "turingdesk/NativeWidgetPreset.h"''',
'''#include "turingdesk/NativeWidgetPreset.h"\n#include "turingdesk/NativeWeatherService.h"''',
'host weather include')
s = once(s,
'''constexpr UINT kShutdownMessage = WM_APP + 913;''',
'''constexpr UINT kShutdownMessage = WM_APP + 913;\nconstexpr UINT kWeatherUpdatedMessage = WM_APP + 914;''',
'host weather message')
s = once(s,
'''    bool paused{};\n    ComPtr<ID2D1Factory> d2dFactory;''',
'''    bool paused{};\n    bool weatherStarted{};\n    NativeWeatherService weatherService;\n    ComPtr<ID2D1Factory> d2dFactory;''',
'host weather member')
s = once(s,
'''        if (slot.preset == NativeWidgetPreset::GlassClock) {\n            GetLocalTime(&context.localTime);\n            context.hasTime = true;\n        }\n        slot.target->BeginDraw();''',
'''        NativeWeatherSnapshot weather;\n        if (slot.preset == NativeWidgetPreset::GlassClock) {\n            GetLocalTime(&context.localTime);\n            context.hasTime = true;\n        } else if (slot.preset == NativeWidgetPreset::WeatherGlass) {\n            weather = weatherService.Snapshot();\n            context.weather = &weather;\n        }\n        slot.target->BeginDraw();''',
'host painter weather data')
s = once(s,
'''            NativeWidgetPreset preset{};\n            if (!widget.enabled || widget.kind != DesktopWidgetKind::Native || !ParseNativePreset(widget.source.wstring(), &preset)) continue;\n            const MonitorInfo* monitor''',
'''            NativeWidgetPreset preset{};\n            if (!widget.enabled || widget.kind != DesktopWidgetKind::Native || !ParseNativePreset(widget.source.wstring(), &preset)) continue;\n            if (preset == NativeWidgetPreset::WeatherGlass && !weatherStarted) {\n                weatherService.Start(messageWindow, kWeatherUpdatedMessage);\n                weatherStarted = true;\n            }\n            const MonitorInfo* monitor''',
'host lazy weather start')
s = once(s,
'''    void RepaintDueWidgets() {\n        if (paused) return;''',
'''    void RepaintWeatherWidgets() {\n        if (paused) return;\n        for (const auto& slot : slots) {\n            if (slot && slot->preset == NativeWidgetPreset::WeatherGlass && slot->hwnd && IsWindow(slot->hwnd))\n                PaintSlot(*slot);\n        }\n    }\n\n    void RepaintDueWidgets() {\n        if (paused) return;''',
'host weather repaint')
s = once(s,
'''        KillTimer(messageWindow, kSyncTimerId);\n        KillTimer(messageWindow, kRefreshTimerId);\n        if (messageWindow && IsWindow(messageWindow)) DestroyWindow(messageWindow);''',
'''        KillTimer(messageWindow, kSyncTimerId);\n        KillTimer(messageWindow, kRefreshTimerId);\n        if (weatherStarted) {\n            weatherService.Stop();\n            weatherStarted = false;\n        }\n        if (messageWindow && IsWindow(messageWindow)) DestroyWindow(messageWindow);''',
'host weather stop')
s = once(s,
'''    if (message == WM_TIMER && gNativeHost) {\n        gNativeHost->HandleTimer(static_cast<UINT_PTR>(wParam));\n        return 0;\n    }''',
'''    if (message == kWeatherUpdatedMessage && gNativeHost) {\n        gNativeHost->RepaintWeatherWidgets();\n        return 0;\n    }\n    if (message == WM_TIMER && gNativeHost) {\n        gNativeHost->HandleTimer(static_cast<UINT_PTR>(wParam));\n        return 0;\n    }''',
'host weather dispatch')
s = once(s,
'''           NativePresetSource(NativeWidgetPreset::WeatherGlass) == L"native:weather-glass" &&\n           NativePresetRefreshIntervalMs(NativeWidgetPreset::GlassClock) == 60000 &&''',
'''           NativePresetSource(NativeWidgetPreset::WeatherGlass) == L"native:weather-glass" &&\n           NativeWeatherService::SelfTest() &&\n           NativePresetRefreshIntervalMs(NativeWidgetPreset::GlassClock) == 60000 &&''',
'host weather selftest')
write(p, s)

# Settings page: remove full-window relayouts from ordinary widget refreshes and
# render the grid through an offscreen bitmap to eliminate create/refresh flashes.
p = 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
s = read(p)
s = once(s,
'''#include "turingdesk/NativeWidgetPreset.h"''',
'''#include "turingdesk/NativeWidgetPreset.h"\n#include "turingdesk/NativeWeatherService.h"''',
'ui weather include')
s = once(s,
'''    desktop::DesktopControlService desktopControl;\n    std::wstring selectedWallpaperId;''',
'''    desktop::DesktopControlService desktopControl;\n    NativeWeatherSnapshot widgetWeather;\n    std::wstring selectedWallpaperId;''',
'ui weather cache member')
# UpdateFooter no longer owns layout. Page/web/resize transitions do.
s = once(s,
'''        }\n        Layout();\n    }\n\n    void SetPage(Page next) {''',
'''        }\n    }\n\n    void SetPage(Page next) {''',
'footer layout removal')
s = once(s,
'''        UpdateFooter();\n        InvalidateRect(window, nullptr, FALSE);''',
'''        UpdateFooter();\n        Layout();\n        InvalidateRect(window, nullptr, FALSE);''',
'page explicit layout')
# One grid repaint after list + health/cache refresh, no erase pass.
s = once(s,
'''        UpdateGridScroll(widgetGrid, true);\n        UpdateFooter();\n        InvalidateRect(widgetGrid, nullptr, TRUE);\n    }\n\n    void RefreshWidgetHealth() {\n        widgetHealth = {};\n        widgetController.RuntimeHealth(&widgetHealth);\n        UpdateFooter();\n        InvalidateRect(widgetGrid, nullptr, FALSE);\n    }''',
'''        UpdateGridScroll(widgetGrid, true);\n        UpdateFooter();\n    }\n\n    void RefreshWidgetHealth() {\n        widgetHealth = {};\n        widgetController.RuntimeHealth(&widgetHealth);\n        NativeWeatherSnapshot cachedWeather;\n        if (NativeWeatherService::ReadCachedSnapshot(&cachedWeather)) widgetWeather = std::move(cachedWeather);\n        UpdateFooter();\n        InvalidateRect(widgetGrid, nullptr, FALSE);\n    }''',
'ui single widget repaint')
# Feed cached real weather into exact preview.
s = once(s,
'''        context.height = designH;\n        context.clearBackground = false;\n        if (preset == NativeWidgetPreset::GlassClock) {''',
'''        context.height = designH;\n        context.clearBackground = false;\n        if (preset == NativeWidgetPreset::WeatherGlass && widgetWeather.valid) context.weather = &widgetWeather;\n        if (preset == NativeWidgetPreset::GlassClock) {''',
'ui cached weather preview')
# Double-buffer the custom grid.
paint_start = s.index('    void PaintGrid(HWND grid, bool widgets) {')
paint_end = s.index('    void ScrollGrid(HWND grid, bool widgets, int delta) {', paint_start)
paint = r'''    void PaintGrid(HWND grid, bool widgets) {
        PAINTSTRUCT ps{};
        HDC paintDc = BeginPaint(grid, &ps);
        RECT client{};
        GetClientRect(grid, &client);
        const int width = std::max(1, RectWidth(client));
        const int height = std::max(1, RectHeight(client));
        HDC bufferDc = CreateCompatibleDC(paintDc);
        HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(paintDc, width, height) : nullptr;
        HGDIOBJ oldBitmap = bufferBitmap ? SelectObject(bufferDc, bufferBitmap) : nullptr;
        HDC dc = bufferBitmap ? bufferDc : paintDc;

        FillSolid(dc, client, RGB(255, 255, 255));
        const int count = static_cast<int>(widgets ? visibleWidgets.size() : visibleWallpapers.size());
        for (int i = 0; i < count; ++i) {
            RECT card = CardRect(grid, i, widgets);
            RECT clipped{};
            if (!IntersectRect(&clipped, &card, &client)) continue;
            if (widgets) DrawWidgetCard(dc, i, card);
            else DrawWallpaperCard(dc, i, card);
        }
        if (count == 0) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(115, 118, 128));
            HGDIOBJ old = SelectObject(dc, bodyFont);
            const wchar_t* empty = widgets ? L"还没有小组件。点右下角「新建桌面小组件」选择类型。" : L"桌面库为空。使用右上角“添加”导入壁纸。";
            DrawTextW(dc, empty, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
        }
        if (bufferBitmap) BitBlt(paintDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
        if (oldBitmap) SelectObject(bufferDc, oldBitmap);
        if (bufferBitmap) DeleteObject(bufferBitmap);
        if (bufferDc) DeleteDC(bufferDc);
        EndPaint(grid, &ps);
    }

'''
s = s[:paint_start] + paint + s[paint_end:]
# Grid resize/erase: never request a separate erase pass.
s = once(s,
'''        case WM_PAINT: self->PaintGrid(hwnd, widgets); return 0;\n        case WM_SIZE: self->UpdateGridScroll(hwnd, widgets); InvalidateRect(hwnd, nullptr, TRUE); return 0;''',
'''        case WM_ERASEBKGND: return 1;\n        case WM_PAINT: self->PaintGrid(hwnd, widgets); return 0;\n        case WM_SIZE: self->UpdateGridScroll(hwnd, widgets); InvalidateRect(hwnd, nullptr, FALSE); return 0;''',
'grid no erase')
# Layout is still allowed for actual geometry transitions but must not erase first.
s = once(s,
'''        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);''',
'''        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);''',
'layout no erase')
# Remove redundant health probe after RefreshWidgets.
s = once(s,
'''        selectedWidgetId = created.id;\n        RefreshWidgets();\n        desktop::WidgetRuntimeHealth health;\n        widgetController.RuntimeHealth(&health);\n        SetStatus(L"已创建桌面小组件：" + created.title);''',
'''        selectedWidgetId = created.id;\n        RefreshWidgets();\n        SetStatus(L"已创建桌面小组件：" + created.title);''',
'create redundant health removal')
write(p, s)

# NativeWeatherService uses std::size on raw buffers.
p = 'src/native/src/desktop/widgets/NativeWeatherService.cpp'
s = read(p)
s = once(s,
'''#include <iomanip>\n#include <locale>''',
'''#include <iomanip>\n#include <iterator>\n#include <locale>''',
'weather iterator include')
write(p, s)

print('widget flicker + native real weather patch applied')
