from pathlib import Path

p = Path('src/native/include/turingdesk/NativeWidgetPainter.h')
s = p.read_text(encoding='utf-8')
start = s.index('void PaintWeatherGlass(const NativeWidgetPaintContext& ctx) {')
end = s.index('void PaintTodayTasks(const NativeWidgetPaintContext& ctx) {', start)
new = r'''void PaintWeatherGlass(const NativeWidgetPaintContext& ctx) {
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

    auto sunGlow = RadialBrush(ctx.target, D2D1::Point2F(ctx.width * 0.76f, ctx.height * 0.35f), ctx.width * 0.22f, ctx.height * 0.31f,
                               D2D1::ColorF(1.0f, 0.88f, 0.34f, clearSky ? 0.50f : 0.32f), D2D1::ColorF(1.0f, 0.8f, 0.2f, 0.0f));
    if (sunGlow) ctx.target->FillRectangle(D2D1::RectF(1, 1, ctx.width - 1, ctx.height - 1), sunGlow.Get());

    auto white = Brush(ctx.target, 0.98f, 0.995f, 1.0f, 0.98f);
    auto muted = Brush(ctx.target, 0.88f, 0.95f, 1.0f, 0.86f);
    auto sun = Brush(ctx.target, 1.0f, 0.86f, 0.30f, 0.92f);
    auto cloud = Brush(ctx.target, 0.94f, 0.985f, 1.0f, 0.90f);

    // Header owns a dedicated top strip. Keep long city names away from the
    // main weather content and never allow them to wrap into the temperature.
    DrawCatMark(ctx.target, 24.0f, 24.0f, 0.7f, muted.Get());
    std::wstring location = weather && !weather->location.empty() ? weather->location : L"本地天气";
    if (location.size() > 24) {
        location.resize(23);
        location += L"…";
    }
    const std::wstring header = L"妙喵 · " + location;
    const float headerSize = location.size() > 18 ? 9.5f : 10.5f;
    Text(ctx.target, ctx.dwrite, header, headerSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, muted.Get(),
         39.0f, 15.0f, ctx.width * 0.62f, 20.0f);

    // Right-side illustration has its own visual region and never shares the
    // left-side text boxes. Scale from both width and height for high DPI / compact cards.
    const float iconCenterX = ctx.width * 0.79f;
    const float iconCenterY = ctx.height * 0.38f;
    const float iconScale = std::clamp(std::min(ctx.width / 360.0f, ctx.height / 224.0f), 0.78f, 1.18f);
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
    const float chipTop = std::max(ctx.height * 0.73f, ctx.height - 58.0f);
    const float mainTop = std::max(42.0f, ctx.height * 0.205f);
    const float infoBottom = chipTop - 8.0f;
    const float mainHeight = std::max(88.0f, infoBottom - mainTop);
    const float tempBoxH = mainHeight * 0.61f;
    const float conditionTop = mainTop + tempBoxH;
    const float conditionH = std::max(18.0f, mainHeight * 0.20f);
    const float rangeTop = conditionTop + conditionH;
    const float rangeH = std::max(15.0f, infoBottom - rangeTop);
    const float tempSize = std::clamp(std::min(ctx.width * 0.18f, tempBoxH * 0.78f), 34.0f, 62.0f);
    const float conditionSize = std::clamp(ctx.width * 0.042f, 12.0f, 16.0f);
    const float rangeSize = std::clamp(ctx.width * 0.031f, 9.5f, 11.5f);
    const float left = std::max(18.0f, ctx.width * 0.055f);
    const float textW = ctx.width * 0.48f;

    Text(ctx.target, ctx.dwrite, tempText, tempSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         left, mainTop, textW, tempBoxH);
    Text(ctx.target, ctx.dwrite, conditionText, conditionSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
         left + 1.0f, conditionTop, textW, conditionH);
    Text(ctx.target, ctx.dwrite, rangeText, rangeSize, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
         left + 1.0f, rangeTop, ctx.width * 0.54f, rangeH);

    const float gap = std::clamp(ctx.width * 0.016f, 4.0f, 7.0f);
    const float side = std::max(12.0f, ctx.width * 0.04f);
    const float totalW = ctx.width - side * 2.0f;
    const float chipW = (totalW - gap * 3.0f) / 4.0f;
    const float chipBottom = ctx.height - std::max(10.0f, ctx.height * 0.055f);
    const float chipH = std::max(38.0f, chipBottom - chipTop);
    float x = side;
    auto chipFill = Brush(ctx.target, 0.05f, 0.18f, 0.40f, 0.30f);
    auto chipBorder = Brush(ctx.target, 0.9f, 0.98f, 1.0f, 0.14f);
    for (std::size_t i = 0; i < 4; ++i) {
        const std::wstring label = weather && !weather->hours[i].label.empty() ? weather->hours[i].label : L"--:--";
        const std::wstring value = weather ? std::to_wstring(weather->hours[i].temperatureC) + L"°" : L"--°";
        const D2D1_RECT_F chip{x, chipTop, x + chipW, chipBottom};
        RoundRect(ctx.target, chipFill.Get(), chipBorder.Get(), chip, std::min(13.0f, chipH * 0.30f), 0.8f);
        const float labelSize = std::clamp(chipW * 0.115f, 8.5f, 10.0f);
        const float valueSize = std::clamp(chipW * 0.155f, 11.0f, 13.0f);
        Text(ctx.target, ctx.dwrite, label, labelSize, DWRITE_FONT_WEIGHT_NORMAL, muted.Get(),
             chip.left + 4.0f, chip.top + chipH * 0.16f, chipW - 8.0f, chipH * 0.32f, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(ctx.target, ctx.dwrite, value, valueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD, white.Get(),
             chip.left + 4.0f, chip.top + chipH * 0.49f, chipW - 8.0f, chipH * 0.36f, DWRITE_TEXT_ALIGNMENT_CENTER);
        x += chipW + gap;
    }
}

'''
s = s[:start] + new + s[end:]
p.write_text(s, encoding='utf-8')
print('weather layout patched')
