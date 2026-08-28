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

namespace turingdesk::wallpaper {

struct NativeWidgetPaintContext {
    ID2D1RenderTarget* target{};
    IDWriteFactory* dwrite{};
    float width{};
    float height{};
    SYSTEMTIME localTime{};
    bool hasTime{};
};

namespace native_widget_paint {
namespace {

using Microsoft::WRL::ComPtr;

Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> Brush(ID2D1RenderTarget* target, float r, float g, float b, float a = 1.0f) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (target) target->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), brush.GetAddressOf());
    return brush;
}

Microsoft::WRL::ComPtr<IDWriteTextFormat> Format(IDWriteFactory* dwrite, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
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
    if (FAILED(dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), w, h, layout.GetAddressOf()))) return;
    target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void RoundRect(ID2D1RenderTarget* target, ID2D1Brush* fill, ID2D1Brush* stroke, const D2D1_RECT_F& rect, float radius, float strokeWidth = 0.0f) {
    const auto rounded = D2D1::RoundedRect(rect, radius, radius);
    if (fill) target->FillRoundedRectangle(rounded, fill);
    if (stroke && strokeWidth > 0.0f) target->DrawRoundedRectangle(rounded, stroke, strokeWidth);
}

void PaintGlassClock(const NativeWidgetPaintContext& ctx) {
    const D2D1_RECT_F bounds{0.0f, 0.0f, ctx.width, ctx.height};
    ctx.target->Clear(D2D1::ColorF(0, 0, 0, 0));
    const float radius = std::clamp(ctx.width * 0.11f, 18.0f, 32.0f);
    const D2D1_RECT_F card{bounds.left + 1.0f, bounds.top + 1.0f, bounds.right - 1.0f, bounds.bottom - 1.0f};
    RoundRect(ctx.target, Brush(ctx.target, 0.08f, 0.14f, 0.23f, 0.92f).Get(), Brush(ctx.target, 1.0f, 1.0f, 1.0f, 0.18f).Get(), card, radius, 1.0f);

    const D2D1_RECT_F glow{card.left, card.top, card.right, card.top + (card.bottom - card.top) * 0.55f};
    RoundRect(ctx.target, Brush(ctx.target, 0.24f, 0.98f, 0.82f, 0.10f).Get(), nullptr, glow, radius, 0.0f);

    auto white = Brush(ctx.target, 0.98f, 0.99f, 1.0f, 0.95f);
    auto muted = Brush(ctx.target, 0.82f, 0.88f, 0.94f, 0.82f);
    Text(ctx.target, ctx.dwrite, L"MIAO · DESKTOP", 9.0f, DWRITE_FONT_WEIGHT_BOLD, muted.Get(), card.left + 18.0f, card.top + 16.0f, card.right - card.left - 36.0f, 16.0f);

    wchar_t timeText[16]{L"12:34"};
    wchar_t dateText[96]{L"Friday, August 28"};
    if (ctx.hasTime) {
        swprintf_s(timeText, L"%02u:%02u", ctx.localTime.wHour, ctx.localTime.wMinute);
        swprintf_s(dateText, L"%u月%u日", ctx.localTime.wMonth, ctx.localTime.wDay);
    }
    const float timeSize = std::clamp(ctx.width * 0.19f, 28.0f, 56.0f);
    Text(ctx.target, ctx.dwrite, timeText, timeSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         card.left + 20.0f, card.bottom - timeSize - 42.0f, card.right - card.left - 40.0f, timeSize + 8.0f);
    Text(ctx.target, ctx.dwrite, dateText, std::clamp(ctx.width * 0.055f, 12.0f, 18.0f), DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
         card.left + 22.0f, card.bottom - 30.0f, card.right - card.left - 40.0f, 22.0f);
}

void PaintTodayTasks(const NativeWidgetPaintContext& ctx) {
    ctx.target->Clear(D2D1::ColorF(0, 0, 0, 0));
    const float radius = std::clamp(ctx.width * 0.09f, 16.0f, 26.0f);
    const D2D1_RECT_F card{1.0f, 1.0f, ctx.width - 1.0f, ctx.height - 1.0f};
    RoundRect(ctx.target, Brush(ctx.target, 0.05f, 0.07f, 0.14f, 0.94f).Get(), Brush(ctx.target, 1.0f, 1.0f, 1.0f, 0.14f).Get(), card, radius, 1.0f);

    auto white = Brush(ctx.target, 0.96f, 0.98f, 1.0f, 0.94f);
    auto muted = Brush(ctx.target, 0.72f, 0.80f, 0.88f, 0.82f);
    auto accent = Brush(ctx.target, 0.34f, 0.96f, 0.82f, 0.95f);
    Text(ctx.target, ctx.dwrite, L"MIAO · 今日待办", 10.0f, DWRITE_FONT_WEIGHT_BOLD, muted.Get(), card.left + 16.0f, card.top + 14.0f, 160.0f, 16.0f);
    Text(ctx.target, ctx.dwrite, L"3 项", 10.0f, DWRITE_FONT_WEIGHT_BOLD, accent.Get(), card.right - 56.0f, card.top + 14.0f, 40.0f, 16.0f, DWRITE_TEXT_ALIGNMENT_TRAILING);

    const std::array tasks = {L"整理桌面", L"完成预览", L"提交版本"};
    float y = card.top + 42.0f;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        const D2D1_RECT_F row{card.left + 14.0f, y, card.right - 14.0f, y + 30.0f};
        RoundRect(ctx.target, Brush(ctx.target, 1.0f, 1.0f, 1.0f, 0.06f).Get(), Brush(ctx.target, 1.0f, 1.0f, 1.0f, 0.08f).Get(), row, 12.0f, 0.8f);
        D2D1_ELLIPSE dot{D2D1::Point2F(row.left + 12.0f, (row.top + row.bottom) * 0.5f), 4.0f, 4.0f};
        ctx.target->FillEllipse(dot, accent.Get());
        auto textBrush = i == 2 ? muted : white;
        Text(ctx.target, ctx.dwrite, tasks[i], 13.0f, DWRITE_FONT_WEIGHT_NORMAL, textBrush.Get(), row.left + 24.0f, row.top + 6.0f, row.right - row.left - 30.0f, 18.0f);
        y += 34.0f;
    }
}

void PaintWeatherGlass(const NativeWidgetPaintContext& ctx) {
    ctx.target->Clear(D2D1::ColorF(0, 0, 0, 0));
    const float radius = std::clamp(ctx.width * 0.10f, 16.0f, 28.0f);
    const D2D1_RECT_F card{1.0f, 1.0f, ctx.width - 1.0f, ctx.height - 1.0f};
    RoundRect(ctx.target, Brush(ctx.target, 0.07f, 0.16f, 0.29f, 0.93f).Get(), Brush(ctx.target, 1.0f, 1.0f, 1.0f, 0.16f).Get(), card, radius, 1.0f);
    RoundRect(ctx.target, Brush(ctx.target, 0.28f, 0.73f, 1.0f, 0.12f).Get(), nullptr,
              D2D1::RectF(card.left, card.top, card.right, card.top + (card.bottom - card.top) * 0.45f), radius, 0.0f);

    auto white = Brush(ctx.target, 0.97f, 0.99f, 1.0f, 0.95f);
    auto muted = Brush(ctx.target, 0.80f, 0.88f, 0.96f, 0.84f);
    Text(ctx.target, ctx.dwrite, L"MIAO · 本地天气", 10.0f, DWRITE_FONT_WEIGHT_BOLD, muted.Get(), card.left + 16.0f, card.top + 14.0f, card.right - card.left - 32.0f, 16.0f);
    const float tempSize = std::clamp(ctx.width * 0.22f, 30.0f, 52.0f);
    Text(ctx.target, ctx.dwrite, L"22°", tempSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(), card.left + 16.0f, card.top + 44.0f, card.right * 0.45f, tempSize + 6.0f);
    Text(ctx.target, ctx.dwrite, L"晴朗\n体感 24°", 14.0f, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(), card.right - 110.0f, card.top + 52.0f, 94.0f, 40.0f, DWRITE_TEXT_ALIGNMENT_TRAILING);

    const std::array days = {std::pair{L"今天", L"22°"}, std::pair{L"明天", L"20°"}, std::pair{L"后天", L"18°"}};
    const float chipW = (card.right - card.left - 44.0f) / 3.0f;
    float x = card.left + 14.0f;
    const float chipTop = card.bottom - 52.0f;
    for (const auto& [label, value] : days) {
        const D2D1_RECT_F chip{x, chipTop, x + chipW, chipTop + 36.0f};
        RoundRect(ctx.target, Brush(ctx.target, 0.02f, 0.04f, 0.09f, 0.55f).Get(), Brush(ctx.target, 1.0f, 1.0f, 1.0f, 0.10f).Get(), chip, 10.0f, 0.8f);
        Text(ctx.target, ctx.dwrite, label, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(), chip.left + 4.0f, chip.top + 4.0f, chipW - 8.0f, 14.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(ctx.target, ctx.dwrite, value, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(), chip.left + 4.0f, chip.top + 16.0f, chipW - 8.0f, 16.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
        x += chipW + 6.0f;
    }
}

} // namespace

inline void PaintNativeWidgetPreset(const NativeWidgetPaintContext& context, NativeWidgetPreset preset) {
    if (!context.target || context.width <= 1.0f || context.height <= 1.0f) return;
    switch (preset) {
    case NativeWidgetPreset::GlassClock: PaintGlassClock(context); break;
    case NativeWidgetPreset::TodayTasks: PaintTodayTasks(context); break;
    case NativeWidgetPreset::WeatherGlass: PaintWeatherGlass(context); break;
    }
}

} // namespace native_widget_paint

inline void PaintNativeWidgetPreset(const NativeWidgetPaintContext& context, NativeWidgetPreset preset) {
    native_widget_paint::PaintNativeWidgetPreset(context, preset);
}

} // namespace turingdesk::wallpaper
