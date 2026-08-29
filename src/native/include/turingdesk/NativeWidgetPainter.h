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

#include "turingdesk/NativeWidgetPreset.h"
#include "turingdesk/NativeWeatherData.h"

namespace turingdesk::wallpaper {

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
};

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

void DrawGlassCardBase(const NativeWidgetPaintContext& ctx, D2D1_COLOR_F a, D2D1_COLOR_F b, D2D1_COLOR_F c, float radius) {
    if (ctx.clearBackground) ctx.target->Clear(D2D1::ColorF(0, 0, 0, 0));
    const D2D1_RECT_F card{1.0f, 1.0f, ctx.width - 1.0f, ctx.height - 1.0f};
    const std::array<D2D1_GRADIENT_STOP, 3> stops{{{0.0f, a}, {0.55f, b}, {1.0f, c}}};
    auto gradient = LinearBrush(ctx.target, D2D1::Point2F(card.left, card.top), D2D1::Point2F(card.right, card.bottom), stops);
    auto border = Brush(ctx.target, 0.92f, 0.98f, 1.0f, 0.24f);
    RoundRect(ctx.target, gradient.Get(), border.Get(), card, radius, 1.0f);
    auto topGlow = LinearBrush(ctx.target,
        D2D1::Point2F(card.left, card.top), D2D1::Point2F(card.left, card.top + ctx.height * 0.42f),
        {{{0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f)},
          {0.45f, D2D1::ColorF(0.55f, 0.88f, 1.0f, 0.06f)},
          {1.0f, D2D1::ColorF(0.2f, 0.5f, 0.9f, 0.0f)}}});
    RoundRect(ctx.target, topGlow.Get(), nullptr, card, radius, 0.0f);
}

void PaintGlassClock(const NativeWidgetPaintContext& ctx) {
    const float radius = std::clamp(ctx.width * 0.075f, 22.0f, 34.0f);
    DrawGlassCardBase(ctx,
        D2D1::ColorF(0.08f, 0.25f, 0.54f, 0.96f),
        D2D1::ColorF(0.10f, 0.39f, 0.74f, 0.94f),
        D2D1::ColorF(0.03f, 0.14f, 0.40f, 0.97f), radius);

    auto cyanGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.30f, ctx.height * 0.48f), ctx.width * 0.34f, ctx.height * 0.54f,
                                D2D1::ColorF(0.25f, 1.0f, 0.92f, 0.28f), D2D1::ColorF(0.1f, 0.4f, 0.7f, 0.0f));
    if (cyanGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), cyanGlow.Get());
    auto violetGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.78f, ctx.height * 0.28f), ctx.width * 0.34f, ctx.height * 0.52f,
                                  D2D1::ColorF(0.45f, 0.36f, 1.0f, 0.20f), D2D1::ColorF(0.2f, 0.2f, 0.7f, 0.0f));
    if (violetGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), violetGlow.Get());

    auto star = Brush(ctx.target, 0.82f, 0.96f, 1.0f, 0.72f);
    for (int i = 0; i < 14; ++i) {
        const float x = 24.0f + std::fmod(static_cast<float>(i * 67), std::max(40.0f, ctx.width - 48.0f));
        const float y = 44.0f + std::fmod(static_cast<float>(i * 31), std::max(30.0f, ctx.height * 0.52f));
        Circle(ctx.target, star.Get(), x, y, (i % 3 == 0) ? 1.5f : 0.85f);
    }

    auto white = Brush(ctx.target, 0.98f, 0.995f, 1.0f, 0.98f);
    auto muted = Brush(ctx.target, 0.84f, 0.92f, 1.0f, 0.84f);
    DrawCatMark(ctx.target, 25.0f, 25.0f, 0.72f, muted.Get());
    Text(ctx.target, ctx.dwrite, L"妙喵", 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, muted.Get(), 39.0f, 17.0f, 80.0f, 20.0f);

    wchar_t timeText[16]{L"12:34"};
    wchar_t dateText[96]{L"8月29日 · 星期六"};
    if (ctx.hasTime) {
        static constexpr std::array<const wchar_t*, 7> weekdays{L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
        swprintf_s(timeText, L"%02u:%02u", ctx.localTime.wHour, ctx.localTime.wMinute);
        swprintf_s(dateText, L"%u月%u日 · %s", ctx.localTime.wMonth, ctx.localTime.wDay, weekdays[ctx.localTime.wDayOfWeek]);
    }
    const float timeSize = std::clamp(ctx.width * 0.205f, 38.0f, 86.0f);
    Text(ctx.target, ctx.dwrite, timeText, timeSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         22.0f, ctx.height * 0.35f, ctx.width - 44.0f, timeSize + 16.0f, DWRITE_TEXT_ALIGNMENT_CENTER);

    auto divider = Brush(ctx.target, 0.80f, 0.95f, 1.0f, 0.22f);
    ctx.target->DrawLine(D2D1::Point2F(24.0f, ctx.height - 48.0f), D2D1::Point2F(ctx.width - 24.0f, ctx.height - 48.0f), divider.Get(), 1.0f);
    Text(ctx.target, ctx.dwrite, dateText, std::clamp(ctx.width * 0.042f, 12.0f, 17.0f), DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
         22.0f, ctx.height - 37.0f, ctx.width - 44.0f, 22.0f, DWRITE_TEXT_ALIGNMENT_CENTER);

    auto mascot = Brush(ctx.target, 0.86f, 0.96f, 1.0f, 0.34f);
    Circle(ctx.target, mascot.Get(), ctx.width - 42.0f, ctx.height - 39.0f, 21.0f);
    DrawCatMark(ctx.target, ctx.width - 42.0f, ctx.height - 38.0f, 0.85f, white.Get());
}

void PaintWeatherGlass(const NativeWidgetPaintContext& ctx) {
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

void PaintTodayTasks(const NativeWidgetPaintContext& ctx) {
    const float radius = std::clamp(ctx.width * 0.07f, 22.0f, 32.0f);
    DrawGlassCardBase(ctx,
        D2D1::ColorF(0.10f, 0.35f, 0.48f, 0.96f),
        D2D1::ColorF(0.12f, 0.50f, 0.55f, 0.94f),
        D2D1::ColorF(0.05f, 0.24f, 0.38f, 0.97f), radius);

    auto aquaGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.78f, ctx.height * 0.16f), ctx.width * 0.30f, ctx.height * 0.32f,
                                D2D1::ColorF(0.35f, 1.0f, 0.88f, 0.30f), D2D1::ColorF(0.1f, 0.7f, 0.6f, 0.0f));
    if (aquaGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), aquaGlow.Get());

    auto white = Brush(ctx.target, 0.98f, 0.995f, 1.0f, 0.97f);
    auto muted = Brush(ctx.target, 0.82f, 0.93f, 0.94f, 0.82f);
    auto teal = Brush(ctx.target, 0.34f, 0.98f, 0.82f, 0.96f);
    auto amber = Brush(ctx.target, 1.0f, 0.67f, 0.28f, 0.96f);
    DrawCatMark(ctx.target, 24.0f, 24.0f, 0.7f, muted.Get());
    Text(ctx.target, ctx.dwrite, L"妙喵", 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, muted.Get(), 39.0f, 16.0f, 80.0f, 18.0f);
    Text(ctx.target, ctx.dwrite, L"今日待办", 19.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(), 18.0f, 42.0f, ctx.width * 0.55f, 28.0f);

    const float countSize = std::clamp(ctx.width * 0.16f, 34.0f, 58.0f);
    Text(ctx.target, ctx.dwrite, L"3", countSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(), 18.0f, 70.0f, 66.0f, countSize + 8.0f);
    Text(ctx.target, ctx.dwrite, L"项待办", 13.0f, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(), 75.0f, 88.0f, 70.0f, 20.0f);

    auto bubble = Brush(ctx.target, 0.88f, 0.99f, 1.0f, 0.24f);
    Circle(ctx.target, bubble.Get(), ctx.width - 50.0f, 70.0f, 32.0f);
    DrawCatMark(ctx.target, ctx.width - 50.0f, 72.0f, 1.2f, white.Get());

    const float progressTop = 126.0f;
    auto track = Brush(ctx.target, 0.86f, 0.98f, 1.0f, 0.18f);
    RoundRect(ctx.target, track.Get(), nullptr, D2D1::RectF(18.0f, progressTop, ctx.width - 18.0f, progressTop + 8.0f), 4.0f);
    RoundRect(ctx.target, teal.Get(), nullptr, D2D1::RectF(18.0f, progressTop, 18.0f + (ctx.width - 36.0f) / 3.0f, progressTop + 8.0f), 4.0f);
    Text(ctx.target, ctx.dwrite, L"1 / 3 完成", 10.0f, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(), 18.0f, progressTop + 12.0f, ctx.width - 36.0f, 16.0f, DWRITE_TEXT_ALIGNMENT_TRAILING);

    struct TaskRow { const wchar_t* text; const wchar_t* time; bool done; };
    const std::array<TaskRow, 3> tasks{{
        {L"完成产品设计方案", L"今天 10:00", false},
        {L"与团队同步项目进度", L"今天 14:00", false},
        {L"回复客户邮件", L"今天 09:30", true},
    }};
    float y = progressTop + 34.0f;
    auto rowFill = Brush(ctx.target, 0.95f, 1.0f, 1.0f, 0.075f);
    auto rowBorder = Brush(ctx.target, 0.95f, 1.0f, 1.0f, 0.11f);
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (y + 48.0f > ctx.height - 12.0f) break;
        const auto& task = tasks[i];
        const D2D1_RECT_F row{14.0f, y, ctx.width - 14.0f, y + 44.0f};
        RoundRect(ctx.target, rowFill.Get(), rowBorder.Get(), row, 13.0f, 0.8f);
        auto status = task.done ? teal : (i == 0 ? amber : muted);
        ctx.target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(row.left + 15.0f, row.top + 17.0f), 6.0f, 6.0f), status.Get(), 1.8f);
        if (task.done) {
            ctx.target->DrawLine(D2D1::Point2F(row.left + 11.5f, row.top + 17.0f), D2D1::Point2F(row.left + 14.0f, row.top + 19.5f), status.Get(), 1.5f);
            ctx.target->DrawLine(D2D1::Point2F(row.left + 14.0f, row.top + 19.5f), D2D1::Point2F(row.left + 19.0f, row.top + 13.5f), status.Get(), 1.5f);
        }
        Text(ctx.target, ctx.dwrite, task.text, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, task.done ? muted.Get() : white.Get(), row.left + 30.0f, row.top + 7.0f, row.right - row.left - 40.0f, 18.0f);
        Text(ctx.target, ctx.dwrite, task.time, 9.0f, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(), row.left + 30.0f, row.top + 25.0f, row.right - row.left - 40.0f, 14.0f);
        y += 49.0f;
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

} // namespace turingdesk::wallpaper
