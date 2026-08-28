#pragma once

#include <d2d1.h>

#include <array>
#include <cmath>

namespace turingdesk::wallpaper::scenes {

struct ScenePaintContext {
    ID2D1RenderTarget* target{};
    ID2D1SolidColorBrush* brush{};
    float time{};
};

inline void FillRect(const ScenePaintContext& ctx, const D2D1_SIZE_F& size, const D2D1_COLOR_F& color) {
    if (!ctx.target || !ctx.brush) return;
    ctx.brush->SetColor(color);
    ctx.target->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height), ctx.brush);
}

inline void FillVerticalGradient(const ScenePaintContext& ctx, const D2D1_SIZE_F& size,
                                const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom, int bands = 28) {
    if (!ctx.target || !ctx.brush || bands <= 0) return;
    const float bandHeight = size.height / static_cast<float>(bands);
    for (int band = 0; band < bands; ++band) {
        const float t = static_cast<float>(band) / static_cast<float>(bands - 1);
        D2D1_COLOR_F color{
            top.r + (bottom.r - top.r) * t,
            top.g + (bottom.g - top.g) * t,
            top.b + (bottom.b - top.b) * t,
            top.a + (bottom.a - top.a) * t,
        };
        ctx.brush->SetColor(color);
        ctx.target->FillRectangle(
            D2D1::RectF(0.0f, bandHeight * band, size.width, bandHeight * (band + 1) + 1.0f), ctx.brush);
    }
}

// Aurora Flow: cold starfield with vertical northern-light curtains.
inline void PaintAurora(const ScenePaintContext& ctx, const D2D1_SIZE_F& size) {
    FillVerticalGradient(
        ctx, size,
        D2D1::ColorF(0.02f, 0.03f, 0.10f, 1.0f),
        D2D1::ColorF(0.01f, 0.08f, 0.12f, 1.0f));

    const float moonPulse = 0.5f + 0.5f * static_cast<float>(std::sin(ctx.time * 0.35f));
    const D2D1_POINT_2F moonCenter = D2D1::Point2F(size.width * 0.78f, size.height * 0.16f);
    for (int ring = 3; ring >= 0; --ring) {
        ctx.brush->SetColor(D2D1::ColorF(0.78f, 0.90f, 1.0f, 0.05f + moonPulse * 0.03f - ring * 0.01f));
        const float scale = 1.0f + ring * 0.28f;
        ctx.target->FillEllipse(
            D2D1::Ellipse(moonCenter, size.width * 0.035f * scale, size.height * 0.055f * scale), ctx.brush);
    }
    ctx.brush->SetColor(D2D1::ColorF(0.92f, 0.96f, 1.0f, 0.82f));
    ctx.target->FillEllipse(D2D1::Ellipse(moonCenter, size.width * 0.028f, size.height * 0.044f), ctx.brush);

    const std::array<D2D1_COLOR_F, 5> curtains = {
        D2D1::ColorF(0.10f, 0.98f, 0.72f, 0.34f), D2D1::ColorF(0.12f, 0.62f, 1.00f, 0.30f),
        D2D1::ColorF(0.58f, 0.24f, 1.00f, 0.28f), D2D1::ColorF(0.96f, 0.20f, 0.78f, 0.24f),
        D2D1::ColorF(0.08f, 0.88f, 0.96f, 0.26f),
    };
    constexpr int segments = 48;
    for (int curtain = 0; curtain < static_cast<int>(curtains.size()); ++curtain) {
        ctx.brush->SetColor(curtains[static_cast<std::size_t>(curtain)]);
        const float anchorX = size.width * (0.12f + curtain * 0.19f);
        const float sway = static_cast<float>(std::sin(ctx.time * (0.22f + curtain * 0.05f) + curtain * 1.37f));
        for (int segment = 0; segment < segments; ++segment) {
            const float t1 = static_cast<float>(segment) / segments;
            const float t2 = static_cast<float>(segment + 1) / segments;
            const float y1 = size.height * (0.08f + t1 * 0.78f);
            const float y2 = size.height * (0.08f + t2 * 0.78f);
            const float wave1 = std::sin(t1 * 8.0f + ctx.time * 0.55f + curtain) * size.width * 0.028f;
            const float wave2 = std::sin(t2 * 8.0f + ctx.time * 0.55f + curtain) * size.width * 0.028f;
            const float x1 = anchorX + sway * size.width * 0.05f + wave1;
            const float x2 = anchorX + sway * size.width * 0.05f + wave2;
            ctx.target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), ctx.brush, 3.2f - curtain * 0.25f);
        }
    }

    for (int i = 0; i < 80; ++i) {
        const float x = size.width * static_cast<float>((i * 37 + 11) % 101) / 100.0f;
        const float y = size.height * static_cast<float>((i * 53 + 7) % 72) / 100.0f;
        const float twinkle = 0.12f + 0.40f * (0.5f + 0.5f * static_cast<float>(std::sin(ctx.time * 1.1f + i * 1.73f)));
        ctx.brush->SetColor(D2D1::ColorF(0.82f, 0.94f, 1.0f, twinkle));
        const float radius = 0.55f + static_cast<float>(i % 4) * 0.35f;
        ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), ctx.brush);
    }
}

// Neon Flow: synthwave sunset with perspective highway grid.
inline void PaintNeon(const ScenePaintContext& ctx, const D2D1_SIZE_F& size) {
    FillVerticalGradient(
        ctx, size,
        D2D1::ColorF(0.05f, 0.02f, 0.14f, 1.0f),
        D2D1::ColorF(0.22f, 0.04f, 0.18f, 1.0f));

    const D2D1_POINT_2F horizon = D2D1::Point2F(size.width * 0.5f, size.height * 0.58f);
    for (int band = 0; band < 6; ++band) {
        const float t = static_cast<float>(band) / 5.0f;
        ctx.brush->SetColor(D2D1::ColorF(1.0f, 0.28f + t * 0.35f, 0.08f + t * 0.18f, 0.10f + (1.0f - t) * 0.10f));
        ctx.target->FillEllipse(
            D2D1::Ellipse(horizon, size.width * (0.16f + band * 0.05f), size.height * (0.10f + band * 0.03f)),
            ctx.brush);
    }
    ctx.brush->SetColor(D2D1::ColorF(1.0f, 0.72f, 0.18f, 0.92f));
    ctx.target->FillEllipse(D2D1::Ellipse(horizon, size.width * 0.11f, size.height * 0.07f), ctx.brush);

    const float travel = static_cast<float>(std::fmod(ctx.time * 42.0f, 1.0f));
    constexpr int roadLines = 18;
    for (int i = 0; i < roadLines; ++i) {
        const float lane = (static_cast<float>(i) / static_cast<float>(roadLines - 1) - 0.5f) * 1.35f;
        const float xBottom = horizon.x + lane * size.width * 0.92f;
        const float xTop = horizon.x + lane * size.width * 0.04f;
        const bool center = std::fabs(lane) < 0.12f;
        ctx.brush->SetColor(center ? D2D1::ColorF(0.10f, 0.96f, 1.0f, 0.72f)
                                   : D2D1::ColorF(0.98f, 0.08f, 0.86f, 0.46f));
        ctx.target->DrawLine(D2D1::Point2F(xTop, horizon.y), D2D1::Point2F(xBottom, size.height), ctx.brush,
                             center ? 2.4f : 1.3f);
    }

    constexpr int crossLines = 14;
    for (int i = 0; i < crossLines; ++i) {
        const float progress = static_cast<float>(std::fmod(travel + static_cast<float>(i) / crossLines, 1.0f));
        const float y = horizon.y + progress * (size.height - horizon.y);
        const float width = progress * size.width * 0.95f;
        ctx.brush->SetColor(D2D1::ColorF(0.08f, 0.88f, 1.0f, 0.22f + (1.0f - progress) * 0.28f));
        ctx.target->DrawLine(D2D1::Point2F(horizon.x - width * 0.5f, y), D2D1::Point2F(horizon.x + width * 0.5f, y),
                             ctx.brush, 1.1f);
    }

    const float scan = size.height * static_cast<float>(std::fmod(ctx.time * 0.12f, 1.0f));
    ctx.brush->SetColor(D2D1::ColorF(1.0f, 0.42f, 0.86f, 0.08f));
    ctx.target->FillRectangle(D2D1::RectF(0.0f, scan, size.width, scan + size.height * 0.08f), ctx.brush);
}

// Ocean Flow (scene-grid): bright surface water fading into deep sea with waves and bubbles.
inline void PaintOcean(const ScenePaintContext& ctx, const D2D1_SIZE_F& size) {
    FillVerticalGradient(
        ctx, size,
        D2D1::ColorF(0.10f, 0.58f, 0.82f, 1.0f),
        D2D1::ColorF(0.01f, 0.08f, 0.20f, 1.0f));

    for (int layer = 0; layer < 3; ++layer) {
        const float depth = size.height * (0.18f + layer * 0.12f);
        ctx.brush->SetColor(D2D1::ColorF(0.72f, 0.96f, 1.0f, 0.05f - layer * 0.01f));
        ctx.target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(size.width * (0.35f + layer * 0.18f), depth),
                          size.width * (0.22f - layer * 0.04f), size.height * 0.05f),
            ctx.brush);
    }

    const std::array<D2D1_COLOR_F, 4> waves = {
        D2D1::ColorF(0.62f, 0.94f, 1.0f, 0.42f), D2D1::ColorF(0.34f, 0.78f, 0.98f, 0.34f),
        D2D1::ColorF(0.18f, 0.58f, 0.92f, 0.28f), D2D1::ColorF(0.10f, 0.42f, 0.78f, 0.22f),
    };
    constexpr int segments = 56;
    for (int band = 0; band < static_cast<int>(waves.size()); ++band) {
        ctx.brush->SetColor(waves[static_cast<std::size_t>(band)]);
        const float baseY = size.height * (0.14f + band * 0.16f);
        const float amplitude = size.height * (0.018f + band * 0.006f);
        for (int segment = 0; segment < segments; ++segment) {
            const float x1 = size.width * static_cast<float>(segment) / segments;
            const float x2 = size.width * static_cast<float>(segment + 1) / segments;
            const float p1 = x1 / size.width * 6.28318f;
            const float p2 = x2 / size.width * 6.28318f;
            const float y1 = baseY + static_cast<float>(std::sin(p1 * (1.6f + band * 0.2f) + ctx.time * (0.55f + band * 0.08f))) *
                                         amplitude;
            const float y2 = baseY + static_cast<float>(std::sin(p2 * (1.6f + band * 0.2f) + ctx.time * (0.55f + band * 0.08f))) *
                                         amplitude;
            ctx.target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), ctx.brush, 2.4f - band * 0.25f);
        }
    }

    for (int i = 0; i < 24; ++i) {
        const float progress = static_cast<float>(std::fmod(ctx.time * (0.035f + (i % 5) * 0.006f) + i * 0.11f, 1.0f));
        const float x = size.width * static_cast<float>((i * 41 + 17) % 97) / 100.0f;
        const float y = size.height * (0.92f - progress * 0.82f);
        const float alpha = 0.10f + (1.0f - progress) * 0.22f;
        const float radius = 2.0f + static_cast<float>(i % 4);
        ctx.brush->SetColor(D2D1::ColorF(0.82f, 0.98f, 1.0f, alpha));
        ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), ctx.brush);
    }

    const float shimmer = 0.5f + 0.5f * static_cast<float>(std::sin(ctx.time * 0.9f));
    ctx.brush->SetColor(D2D1::ColorF(0.90f, 1.0f, 1.0f, 0.10f + shimmer * 0.10f));
    ctx.target->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height * 0.06f), ctx.brush);
}

} // namespace turingdesk::wallpaper::scenes
