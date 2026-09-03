#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>

#include "miaodesk/NativeWidgetPreset.h"
#include "miaodesk/NativeWeatherData.h"

namespace miaodesk::wallpaper {

struct NativeWidgetPaintContext {
    ID2D1RenderTarget* target{};
    IDWriteFactory* dwrite{};
    float width{};
    float height{};
    SYSTEMTIME localTime{};
    bool hasTime{};
    const NativeWeatherSnapshot* weather{};
    // Desktop surfaces clear to transparent. Management thumbnails render over
    // an existing GDI card and therefore keep the destination background.
    bool clearBackground{true};
    // Direct HwndRenderTarget children do not carry per-pixel alpha through
    // Explorer's raised desktop. Give those surfaces an opaque base so a
    // successful Direct2D draw cannot collapse to an all-transparent frame.
    bool opaqueSurface{false};
};

// Painters author layout constants against the DIP canvas a default-fraction
// widget occupies on a 1080p monitor. Every font size, margin and radius is
// multiplied by the card's scale relative to that canvas, so a widget grows
// its text proportionally instead of keeping 1080p pixel sizes on 2K/4K
// monitors (where the fraction-based HWND already covers more pixels).
struct NativeWidgetDesignCanvas {
    float width{};
    float height{};
};

inline NativeWidgetDesignCanvas NativeWidgetCanvasFor(NativeWidgetPreset preset) {
    // Landscape presets share the clock/weather 16:9 card; the task card is
    // portrait and scales from its own canvas.
    if (preset == NativeWidgetPreset::TodayTasks) return {430.0f, 520.0f};
    return {560.0f, 315.0f};
}

inline float NativeWidgetCardScale(NativeWidgetPreset preset, float widthDip, float heightDip) {
    const NativeWidgetDesignCanvas canvas = NativeWidgetCanvasFor(preset);
    return std::clamp(std::min(std::max(1.0f, widthDip) / canvas.width,
                               std::max(1.0f, heightDip) / canvas.height),
                      0.55f, 3.0f);
}

inline float NativeWidgetCardRadius(NativeWidgetPreset preset, float widthDip, float heightDip) {
    return 32.0f * NativeWidgetCardScale(preset, widthDip, heightDip);
}

namespace native_widget_paint {
namespace {

using Microsoft::WRL::ComPtr;

ComPtr<ID2D1SolidColorBrush> Brush(ID2D1RenderTarget* target, float r, float g, float b, float a = 1.0f) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (target) target->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), brush.GetAddressOf());
    return brush;
}

ComPtr<ID2D1LinearGradientBrush> LinearBrush(
    ID2D1RenderTarget* target,
    D2D1_POINT_2F start,
    D2D1_POINT_2F end,
    const std::array<D2D1_GRADIENT_STOP, 3>& stops) {
    ComPtr<ID2D1LinearGradientBrush> brush;
    ComPtr<ID2D1GradientStopCollection> collection;
    if (!target) return brush;
    if (FAILED(target->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), collection.GetAddressOf()))) return brush;
    target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(start, end), collection.Get(), brush.GetAddressOf());
    return brush;
}

ComPtr<ID2D1RadialGradientBrush> RadialBrush(
    ID2D1RenderTarget* target,
    D2D1_POINT_2F center,
    float radiusX,
    float radiusY,
    D2D1_COLOR_F inner,
    D2D1_COLOR_F outer) {
    ComPtr<ID2D1RadialGradientBrush> brush;
    ComPtr<ID2D1GradientStopCollection> collection;
    if (!target) return brush;
    const std::array<D2D1_GRADIENT_STOP, 2> stops{{{0.0f, inner}, {1.0f, outer}}};
    if (FAILED(target->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), collection.GetAddressOf()))) return brush;
    target->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(), radiusX, radiusY),
        collection.Get(), brush.GetAddressOf());
    return brush;
}

ComPtr<IDWriteTextFormat> Format(IDWriteFactory* dwrite, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
    ComPtr<IDWriteTextFormat> format;
    if (!dwrite) return format;
    dwrite->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"zh-CN", format.GetAddressOf());
    if (!format) {
        dwrite->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"zh-CN", format.GetAddressOf());
    }
    if (format) format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    return format;
}

void Text(ID2D1RenderTarget* target, IDWriteFactory* dwrite, const std::wstring& text, float size,
          DWRITE_FONT_WEIGHT weight, ID2D1Brush* brush, float x, float y, float w, float h,
          DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    auto format = Format(dwrite, size, weight);
    if (!format || !target || !brush) return;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), std::max(1.0f, w), std::max(1.0f, h), layout.GetAddressOf()))) return;
    target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void RoundRect(ID2D1RenderTarget* target, ID2D1Brush* fill, ID2D1Brush* stroke, const D2D1_RECT_F& rect, float radius, float strokeWidth = 0.0f) {
    const auto rounded = D2D1::RoundedRect(rect, radius, radius);
    if (fill) target->FillRoundedRectangle(rounded, fill);
    if (stroke && strokeWidth > 0.0f) target->DrawRoundedRectangle(rounded, stroke, strokeWidth);
}

void Circle(ID2D1RenderTarget* target, ID2D1Brush* fill, float x, float y, float radius) {
    if (!target || !fill) return;
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), fill);
}

void DrawCatMark(ID2D1RenderTarget* target, float x, float y, float scale, ID2D1Brush* brush) {
    if (!target || !brush) return;
    const D2D1_ELLIPSE head{D2D1::Point2F(x, y), 9.0f * scale, 7.5f * scale};
    target->DrawEllipse(head, brush, 1.5f * scale);
    target->DrawLine(D2D1::Point2F(x - 6.5f * scale, y - 5.0f * scale), D2D1::Point2F(x - 4.0f * scale, y - 10.0f * scale), brush, 1.5f * scale);
    target->DrawLine(D2D1::Point2F(x - 4.0f * scale, y - 10.0f * scale), D2D1::Point2F(x - 1.5f * scale, y - 6.5f * scale), brush, 1.5f * scale);
    target->DrawLine(D2D1::Point2F(x + 6.5f * scale, y - 5.0f * scale), D2D1::Point2F(x + 4.0f * scale, y - 10.0f * scale), brush, 1.5f * scale);
    target->DrawLine(D2D1::Point2F(x + 4.0f * scale, y - 10.0f * scale), D2D1::Point2F(x + 1.5f * scale, y - 6.5f * scale), brush, 1.5f * scale);
    Circle(target, brush, x - 3.0f * scale, y - 1.0f * scale, 0.9f * scale);
    Circle(target, brush, x + 3.0f * scale, y - 1.0f * scale, 0.9f * scale);
}

void DrawGlassCardBase(const NativeWidgetPaintContext& ctx, D2D1_COLOR_F a, D2D1_COLOR_F b, D2D1_COLOR_F c, float radius, float s) {
    if (ctx.clearBackground) {
        ctx.target->Clear(ctx.opaqueSurface
            ? D2D1::ColorF(0.025f, 0.05f, 0.10f, 1.0f)
            : D2D1::ColorF(0, 0, 0, 0));
    }
    const D2D1_RECT_F card{1.0f * s, 1.0f * s, ctx.width - 1.0f * s, ctx.height - 1.0f * s};
    const std::array<D2D1_GRADIENT_STOP, 3> stops{{{0.0f, a}, {0.55f, b}, {1.0f, c}}};
    auto gradient = LinearBrush(ctx.target, D2D1::Point2F(card.left, card.top), D2D1::Point2F(card.right, card.bottom), stops);
    auto border = Brush(ctx.target, 0.92f, 0.98f, 1.0f, 0.24f);
    RoundRect(ctx.target, gradient.Get(), border.Get(), card, radius, std::max(1.0f, s));
    auto topGlow = LinearBrush(ctx.target,
        D2D1::Point2F(card.left, card.top), D2D1::Point2F(card.left, card.top + ctx.height * 0.42f),
        {{{0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f)},
          {0.45f, D2D1::ColorF(0.55f, 0.88f, 1.0f, 0.06f)},
          {1.0f, D2D1::ColorF(0.2f, 0.5f, 0.9f, 0.0f)}}});
    RoundRect(ctx.target, topGlow.Get(), nullptr, card, radius, 0.0f);
}

void PaintGlassClock(const NativeWidgetPaintContext& ctx) {
    const float s = NativeWidgetCardScale(NativeWidgetPreset::GlassClock, ctx.width, ctx.height);
    const float radius = NativeWidgetCardRadius(NativeWidgetPreset::GlassClock, ctx.width, ctx.height);
    DrawGlassCardBase(ctx,
        D2D1::ColorF(0.08f, 0.25f, 0.54f, 0.96f),
        D2D1::ColorF(0.10f, 0.39f, 0.74f, 0.94f),
        D2D1::ColorF(0.03f, 0.14f, 0.40f, 0.97f), radius, s);

    auto cyanGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.30f, ctx.height * 0.48f), ctx.width * 0.34f, ctx.height * 0.54f,
                                D2D1::ColorF(0.25f, 1.0f, 0.92f, 0.28f), D2D1::ColorF(0.1f, 0.4f, 0.7f, 0.0f));
    if (cyanGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), cyanGlow.Get());
    auto violetGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.78f, ctx.height * 0.28f), ctx.width * 0.34f, ctx.height * 0.52f,
                                  D2D1::ColorF(0.45f, 0.36f, 1.0f, 0.20f), D2D1::ColorF(0.2f, 0.2f, 0.7f, 0.0f));
    if (violetGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), violetGlow.Get());

    auto star = Brush(ctx.target, 0.82f, 0.96f, 1.0f, 0.72f);
    for (int i = 0; i < 14; ++i) {
        const float x = 24.0f * s + std::fmod(static_cast<float>(i * 67), std::max(40.0f * s, ctx.width - 48.0f * s));
        const float y = 44.0f * s + std::fmod(static_cast<float>(i * 31), std::max(30.0f * s, ctx.height * 0.52f));
        Circle(ctx.target, star.Get(), x, y, ((i % 3 == 0) ? 1.5f : 0.85f) * s);
    }

    auto white = Brush(ctx.target, 0.98f, 0.995f, 1.0f, 0.98f);
    auto muted = Brush(ctx.target, 0.84f, 0.92f, 1.0f, 0.84f);
    DrawCatMark(ctx.target, 25.0f * s, 25.0f * s, 0.72f * s, muted.Get());
    Text(ctx.target, ctx.dwrite, L"妙喵", 11.0f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, muted.Get(),
         39.0f * s, 17.0f * s, 80.0f * s, 20.0f * s);

    wchar_t timeText[16]{L"12:34"};
    wchar_t dateText[96]{L"8月29日 · 星期六"};
    if (ctx.hasTime) {
        static constexpr std::array<const wchar_t*, 7> weekdays{L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
        swprintf_s(timeText, L"%02u:%02u", ctx.localTime.wHour, ctx.localTime.wMinute);
        swprintf_s(dateText, L"%u月%u日 · %s", ctx.localTime.wMonth, ctx.localTime.wDay, weekdays[ctx.localTime.wDayOfWeek]);
    }
    const float timeSize = 86.0f * s;
    Text(ctx.target, ctx.dwrite, timeText, timeSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         22.0f * s, ctx.height * 0.35f, ctx.width - 44.0f * s, timeSize + 16.0f * s, DWRITE_TEXT_ALIGNMENT_CENTER);

    auto divider = Brush(ctx.target, 0.80f, 0.95f, 1.0f, 0.22f);
    ctx.target->DrawLine(D2D1::Point2F(24.0f * s, ctx.height - 48.0f * s),
                         D2D1::Point2F(ctx.width - 24.0f * s, ctx.height - 48.0f * s), divider.Get(), std::max(1.0f, s));
    Text(ctx.target, ctx.dwrite, dateText, 16.0f * s, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
         22.0f * s, ctx.height - 37.0f * s, ctx.width - 44.0f * s, 22.0f * s, DWRITE_TEXT_ALIGNMENT_CENTER);

    auto mascot = Brush(ctx.target, 0.86f, 0.96f, 1.0f, 0.34f);
    Circle(ctx.target, mascot.Get(), ctx.width - 42.0f * s, ctx.height - 39.0f * s, 21.0f * s);
    DrawCatMark(ctx.target, ctx.width - 42.0f * s, ctx.height - 38.0f * s, 0.85f * s, white.Get());
}

void PaintWeatherGlass(const NativeWidgetPaintContext& ctx) {
    const float s = NativeWidgetCardScale(NativeWidgetPreset::WeatherGlass, ctx.width, ctx.height);
    const float radius = NativeWidgetCardRadius(NativeWidgetPreset::WeatherGlass, ctx.width, ctx.height);
    DrawGlassCardBase(ctx,
        D2D1::ColorF(0.20f, 0.65f, 0.95f, 0.96f),
        D2D1::ColorF(0.16f, 0.53f, 0.90f, 0.95f),
        D2D1::ColorF(0.08f, 0.28f, 0.68f, 0.97f), radius, s);

    const NativeWeatherSnapshot* weather = ctx.weather && ctx.weather->valid ? ctx.weather : nullptr;
    const int code = weather ? weather->weatherCode : -1;
    const bool clearSky = code == 0 || code == 1;
    const bool rainy = (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
    const bool snowy = (code >= 71 && code <= 77) || code == 85 || code == 86;

    auto sunGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.76f, ctx.height * 0.35f), ctx.width * 0.22f, ctx.height * 0.31f,
                               D2D1::ColorF(1.0f, 0.88f, 0.34f, clearSky ? 0.50f : 0.32f), D2D1::ColorF(1.0f, 0.8f, 0.2f, 0.0f));
    if (sunGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), sunGlow.Get());

    auto white = Brush(ctx.target, 0.98f, 0.995f, 1.0f, 0.98f);
    auto muted = Brush(ctx.target, 0.88f, 0.95f, 1.0f, 0.86f);
    auto sun = Brush(ctx.target, 1.0f, 0.86f, 0.30f, 0.92f);
    auto cloud = Brush(ctx.target, 0.94f, 0.985f, 1.0f, 0.90f);

    // Header owns a dedicated top strip. Keep long city names away from the
    // main weather content and never allow them to wrap into the temperature.
    DrawCatMark(ctx.target, 24.0f * s, 24.0f * s, 0.7f * s, muted.Get());
    std::wstring location = weather && !weather->location.empty() ? weather->location : L"本地天气";
    if (location.size() > 24) {
        location.resize(23);
        location += L"…";
    }
    const std::wstring header = L"妙喵 · " + location;
    const float headerSize = (location.size() > 18 ? 9.5f : 10.5f) * s;
    Text(ctx.target, ctx.dwrite, header, headerSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, muted.Get(),
         39.0f * s, 15.0f * s, ctx.width * 0.62f, 20.0f * s);

    // Right-side illustration has its own visual region and never shares the
    // left-side text boxes. Scale from both width and height for high DPI / compact cards.
    const float iconCenterX = ctx.width * 0.79f;
    const float iconCenterY = ctx.height * 0.38f;
    const float iconScale = s * 1.2f;
    if (!rainy && !snowy) {
        Circle(ctx.target, sun.Get(), iconCenterX, iconCenterY - 10.0f * iconScale, 22.0f * iconScale);
    }
    if (!clearSky || !weather) {
        const float cloudY = iconCenterY + 3.0f * iconScale;
        Circle(ctx.target, cloud.Get(), iconCenterX - 25.0f * iconScale, cloudY, 17.0f * iconScale);
        Circle(ctx.target, cloud.Get(), iconCenterX, cloudY - 8.0f * iconScale, 23.0f * iconScale);
        Circle(ctx.target, cloud.Get(), iconCenterX + 25.0f * iconScale, cloudY, 18.0f * iconScale);
        RoundRect(ctx.target, cloud.Get(), nullptr,
                  D2D1::RectF(iconCenterX - 40.0f * iconScale, cloudY - 2.0f * iconScale,
                              iconCenterX + 41.0f * iconScale, cloudY + 17.0f * iconScale),
                  10.0f * iconScale);
        if (rainy) {
            auto rain = Brush(ctx.target, 0.62f, 0.91f, 1.0f, 0.88f);
            for (int i = 0; i < 4; ++i) {
                const float x = iconCenterX - 24.0f * iconScale + i * 16.0f * iconScale;
                ctx.target->DrawLine(D2D1::Point2F(x, cloudY + 22.0f * iconScale),
                                     D2D1::Point2F(x - 4.0f * iconScale, cloudY + 33.0f * iconScale),
                                     rain.Get(), 2.0f * iconScale);
            }
        } else if (snowy) {
            for (int i = 0; i < 4; ++i)
                Circle(ctx.target, white.Get(), iconCenterX - 24.0f * iconScale + i * 16.0f * iconScale,
                       cloudY + (28.0f + (i % 2) * 5.0f) * iconScale, 2.0f * iconScale);
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

    // Main information is laid out as four non-overlapping vertical regions:
    // header / temperature / condition+range / hourly forecast.
    const float chipTop = std::max(ctx.height * 0.73f, ctx.height - 58.0f * s);
    const float mainTop = std::max(42.0f * s, ctx.height * 0.205f);
    const float infoBottom = chipTop - 8.0f * s;
    const float mainHeight = std::max(88.0f * s, infoBottom - mainTop);
    const float tempBoxH = mainHeight * 0.61f;
    const float conditionTop = mainTop + tempBoxH;
    const float conditionH = std::max(18.0f * s, mainHeight * 0.20f);
    const float rangeTop = conditionTop + conditionH;
    const float rangeH = std::max(15.0f * s, infoBottom - rangeTop);
    const float tempSize = std::min(62.0f * s, tempBoxH * 0.78f);
    const float conditionSize = 15.0f * s;
    const float rangeSize = 11.0f * s;
    const float left = std::max(18.0f * s, ctx.width * 0.055f);
    const float textW = ctx.width * 0.48f;

    Text(ctx.target, ctx.dwrite, tempText, tempSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         left, mainTop, textW, tempBoxH);
    Text(ctx.target, ctx.dwrite, conditionText, conditionSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         left + 1.0f, conditionTop, textW, conditionH);
    Text(ctx.target, ctx.dwrite, rangeText, rangeSize, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
         left + 1.0f, rangeTop, ctx.width * 0.54f, rangeH);

    const float gap = 6.0f * s;
    const float side = std::max(12.0f * s, ctx.width * 0.04f);
    const float totalW = ctx.width - side * 2.0f;
    const float chipW = (totalW - gap * 3.0f) / 4.0f;
    const float chipBottom = ctx.height - std::max(10.0f * s, ctx.height * 0.055f);
    const float chipH = std::max(38.0f * s, chipBottom - chipTop);
    float x = side;
    auto chipFill = Brush(ctx.target, 0.05f, 0.18f, 0.40f, 0.30f);
    auto chipBorder = Brush(ctx.target, 0.9f, 0.98f, 1.0f, 0.14f);
    for (std::size_t i = 0; i < 4; ++i) {
        const std::wstring label = weather && !weather->hours[i].label.empty() ? weather->hours[i].label : L"--:--";
        const std::wstring value = weather ? std::to_wstring(weather->hours[i].temperatureC) + L"°" : L"--°";
        const D2D1_RECT_F chip{x, chipTop, x + chipW, chipBottom};
        RoundRect(ctx.target, chipFill.Get(), chipBorder.Get(), chip, std::min(13.0f * s, chipH * 0.30f), 0.8f * s);
        const float labelSize = 10.0f * s;
        const float valueSize = 13.0f * s;
        Text(ctx.target, ctx.dwrite, label, labelSize, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
             chip.left + 4.0f * s, chip.top + chipH * 0.16f, chipW - 8.0f * s, chipH * 0.32f, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(ctx.target, ctx.dwrite, value, valueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
             chip.left + 4.0f * s, chip.top + chipH * 0.49f, chipW - 8.0f * s, chipH * 0.36f, DWRITE_TEXT_ALIGNMENT_CENTER);
        x += chipW + gap;
    }
}

void PaintTodayTasks(const NativeWidgetPaintContext& ctx) {
    const float s = NativeWidgetCardScale(NativeWidgetPreset::TodayTasks, ctx.width, ctx.height);
    const float radius = NativeWidgetCardRadius(NativeWidgetPreset::TodayTasks, ctx.width, ctx.height);
    DrawGlassCardBase(ctx,
        D2D1::ColorF(0.10f, 0.35f, 0.48f, 0.96f),
        D2D1::ColorF(0.12f, 0.50f, 0.55f, 0.94f),
        D2D1::ColorF(0.05f, 0.24f, 0.38f, 0.97f), radius, s);

    auto aquaGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.78f, ctx.height * 0.16f), ctx.width * 0.30f, ctx.height * 0.32f,
                                D2D1::ColorF(0.35f, 1.0f, 0.88f, 0.30f), D2D1::ColorF(0.1f, 0.7f, 0.6f, 0.0f));
    if (aquaGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), aquaGlow.Get());

    auto white = Brush(ctx.target, 0.98f, 0.995f, 1.0f, 0.97f);
    auto muted = Brush(ctx.target, 0.82f, 0.93f, 0.94f, 0.82f);
    auto teal = Brush(ctx.target, 0.34f, 0.98f, 0.82f, 0.96f);
    auto amber = Brush(ctx.target, 1.0f, 0.67f, 0.28f, 0.96f);
    DrawCatMark(ctx.target, 24.0f * s, 24.0f * s, 0.7f * s, muted.Get());
    Text(ctx.target, ctx.dwrite, L"妙喵", 10.5f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, muted.Get(),
         39.0f * s, 16.0f * s, 80.0f * s, 18.0f * s);
    Text(ctx.target, ctx.dwrite, L"今日待办", 19.0f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         18.0f * s, 42.0f * s, ctx.width * 0.55f, 28.0f * s);

    const float countSize = 58.0f * s;
    Text(ctx.target, ctx.dwrite, L"3", countSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         18.0f * s, 70.0f * s, 66.0f * s, countSize + 8.0f * s);
    Text(ctx.target, ctx.dwrite, L"项待办", 13.0f * s, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
         75.0f * s, 88.0f * s, 70.0f * s, 20.0f * s);

    auto bubble = Brush(ctx.target, 0.88f, 0.99f, 1.0f, 0.24f);
    Circle(ctx.target, bubble.Get(), ctx.width - 50.0f * s, 70.0f * s, 32.0f * s);
    DrawCatMark(ctx.target, ctx.width - 50.0f * s, 72.0f * s, 1.2f * s, white.Get());

    const float progressTop = 126.0f * s;
    auto track = Brush(ctx.target, 0.86f, 0.98f, 1.0f, 0.18f);
    RoundRect(ctx.target, track.Get(), nullptr,
              D2D1::RectF(18.0f * s, progressTop, ctx.width - 18.0f * s, progressTop + 8.0f * s), 4.0f * s);
    RoundRect(ctx.target, teal.Get(), nullptr,
              D2D1::RectF(18.0f * s, progressTop, 18.0f * s + (ctx.width - 36.0f * s) / 3.0f, progressTop + 8.0f * s), 4.0f * s);
    Text(ctx.target, ctx.dwrite, L"1 / 3 完成", 10.0f * s, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
         18.0f * s, progressTop + 12.0f * s, ctx.width - 36.0f * s, 16.0f * s, DWRITE_TEXT_ALIGNMENT_TRAILING);

    struct TaskRow { const wchar_t* text; const wchar_t* time; bool done; };
    const std::array<TaskRow, 3> tasks{{
        {L"完成产品设计方案", L"今天 10:00", false},
        {L"与团队同步项目进度", L"今天 14:00", false},
        {L"回复客户邮件", L"今天 09:30", true},
    }};
    float y = progressTop + 34.0f * s;
    auto rowFill = Brush(ctx.target, 0.95f, 1.0f, 1.0f, 0.075f);
    auto rowBorder = Brush(ctx.target, 0.95f, 1.0f, 1.0f, 0.11f);
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (y + 48.0f * s > ctx.height - 12.0f * s) break;
        const auto& task = tasks[i];
        const D2D1_RECT_F row{14.0f * s, y, ctx.width - 14.0f * s, y + 44.0f * s};
        RoundRect(ctx.target, rowFill.Get(), rowBorder.Get(), row, 13.0f * s, 0.8f * s);
        auto status = task.done ? teal : (i == 0 ? amber : muted);
        ctx.target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(row.left + 15.0f * s, row.top + 17.0f * s), 6.0f * s, 6.0f * s), status.Get(), 1.8f * s);
        if (task.done) {
            ctx.target->DrawLine(D2D1::Point2F(row.left + 11.5f * s, row.top + 17.0f * s), D2D1::Point2F(row.left + 14.0f * s, row.top + 19.5f * s), status.Get(), 1.5f * s);
            ctx.target->DrawLine(D2D1::Point2F(row.left + 14.0f * s, row.top + 19.5f * s), D2D1::Point2F(row.left + 19.0f * s, row.top + 13.5f * s), status.Get(), 1.5f * s);
        }
        Text(ctx.target, ctx.dwrite, task.text, 11.5f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, task.done ? muted.Get() : white.Get(),
             row.left + 30.0f * s, row.top + 7.0f * s, row.right - row.left - 40.0f * s, 18.0f * s);
        Text(ctx.target, ctx.dwrite, task.time, 9.0f * s, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
             row.left + 30.0f * s, row.top + 25.0f * s, row.right - row.left - 40.0f * s, 14.0f * s);
        y += 49.0f * s;
    }
}

using PaintFunction = void (*)(const NativeWidgetPaintContext&);

struct PaintEntry {
    NativeWidgetPreset preset;
    PaintFunction paint;
};

constexpr std::array<PaintEntry, 3> kPainters{{
    {NativeWidgetPreset::GlassClock, &PaintGlassClock},
    {NativeWidgetPreset::TodayTasks, &PaintTodayTasks},
    {NativeWidgetPreset::WeatherGlass, &PaintWeatherGlass},
}};

} // namespace

inline void PaintNativeWidgetPreset(const NativeWidgetPaintContext& context, NativeWidgetPreset preset) {
    if (!context.target || context.width <= 1.0f || context.height <= 1.0f) return;
    for (const auto& entry : kPainters) {
        if (entry.preset == preset) {
            entry.paint(context);
            return;
        }
    }
}

} // namespace native_widget_paint

inline void PaintNativeWidgetPreset(const NativeWidgetPaintContext& context, NativeWidgetPreset preset) {
    native_widget_paint::PaintNativeWidgetPreset(context, preset);
}

} // namespace miaodesk::wallpaper
