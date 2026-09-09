#pragma once

#include <d2d1.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace miaodesk::wallpaper::scenes {

struct ScenePaintContext {
    ID2D1RenderTarget* target{};
    ID2D1SolidColorBrush* brush{};
    float time{};
};

constexpr float kTau = 6.28318530718f;

inline float Hash01(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

inline float Wrap01(float value) {
    value = static_cast<float>(std::fmod(value, 1.0f));
    return value < 0.0f ? value + 1.0f : value;
}

inline float Pulse(float time, float speed, float phase = 0.0f) {
    return 0.5f + 0.5f * static_cast<float>(std::sin(time * speed + phase));
}

inline void FillRect(const ScenePaintContext& ctx, const D2D1_SIZE_F& size, const D2D1_COLOR_F& color) {
    if (!ctx.target || !ctx.brush) return;
    ctx.brush->SetColor(color);
    ctx.target->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height), ctx.brush);
}

inline void FillVerticalGradient(const ScenePaintContext& ctx, const D2D1_SIZE_F& size,
                                 const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom, int bands = 48) {
    if (!ctx.target || !ctx.brush || bands <= 1) return;
    const float bandHeight = size.height / static_cast<float>(bands);
    for (int band = 0; band < bands; ++band) {
        const float t = static_cast<float>(band) / static_cast<float>(bands - 1);
        const D2D1_COLOR_F color{
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

inline void GlowEllipse(const ScenePaintContext& ctx, D2D1_POINT_2F center, float rx, float ry,
                        D2D1_COLOR_F color, int layers = 5, float spread = 0.42f) {
    if (!ctx.target || !ctx.brush) return;
    for (int layer = layers; layer >= 1; --layer) {
        const float p = static_cast<float>(layer) / static_cast<float>(layers);
        D2D1_COLOR_F glow = color;
        glow.a *= (1.0f - p * 0.72f) * 0.42f;
        ctx.brush->SetColor(glow);
        const float scale = 1.0f + p * spread;
        ctx.target->FillEllipse(D2D1::Ellipse(center, rx * scale, ry * scale), ctx.brush);
    }
    ctx.brush->SetColor(color);
    ctx.target->FillEllipse(D2D1::Ellipse(center, rx, ry), ctx.brush);
}

inline void Spark(const ScenePaintContext& ctx, float x, float y, float radius, D2D1_COLOR_F color) {
    if (!ctx.target || !ctx.brush) return;
    GlowEllipse(ctx, D2D1::Point2F(x, y), radius * 0.62f, radius * 0.62f, color, 3, 1.4f);
    ctx.brush->SetColor(color);
    ctx.target->DrawLine(D2D1::Point2F(x - radius * 2.2f, y), D2D1::Point2F(x + radius * 2.2f, y), ctx.brush,
                         std::max(0.7f, radius * 0.35f));
    ctx.target->DrawLine(D2D1::Point2F(x, y - radius * 2.2f), D2D1::Point2F(x, y + radius * 2.2f), ctx.brush,
                         std::max(0.7f, radius * 0.35f));
}

inline void CloudCluster(const ScenePaintContext& ctx, float cx, float cy, float scale,
                         D2D1_COLOR_F color, float alpha) {
    if (!ctx.target || !ctx.brush) return;
    const std::array<D2D1_POINT_2F, 7> offsets{{
        {-1.10f, 0.18f}, {-0.62f, -0.12f}, {-0.18f, -0.32f}, {0.32f, -0.24f},
        {0.78f, -0.04f}, {1.12f, 0.20f}, {0.08f, 0.12f},
    }};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        auto c = color;
        c.a = alpha * (0.76f + 0.04f * static_cast<float>(i));
        ctx.brush->SetColor(c);
        const float rx = scale * (0.55f + static_cast<float>(i % 3) * 0.10f);
        const float ry = scale * (0.34f + static_cast<float>((i + 1) % 3) * 0.06f);
        ctx.target->FillEllipse(D2D1::Ellipse(
            D2D1::Point2F(cx + offsets[i].x * scale, cy + offsets[i].y * scale), rx, ry), ctx.brush);
    }
}

inline void PaintMiaoCloud(const ScenePaintContext& ctx, const D2D1_SIZE_F& size) {
    FillVerticalGradient(ctx, size,
        D2D1::ColorF(0.22f, 0.30f, 0.76f, 1.0f),
        D2D1::ColorF(0.98f, 0.58f, 0.76f, 1.0f), 64);

    const float w = size.width;
    const float h = size.height;
    const float slow = ctx.time * 0.025f;

    const D2D1_POINT_2F sun = D2D1::Point2F(w * 0.17f, h * 0.39f);
    for (int ring = 8; ring >= 1; --ring) {
        const float p = static_cast<float>(ring) / 8.0f;
        ctx.brush->SetColor(D2D1::ColorF(1.0f, 0.82f, 0.70f, 0.018f + (1.0f - p) * 0.035f));
        ctx.target->FillEllipse(D2D1::Ellipse(sun, w * (0.035f + p * 0.12f), h * (0.055f + p * 0.18f)), ctx.brush);
    }
    GlowEllipse(ctx, sun, w * 0.017f, h * 0.027f, D2D1::ColorF(1.0f, 0.95f, 0.84f, 0.96f), 4, 0.7f);

    for (int layer = 0; layer < 3; ++layer) {
        const float speed = 0.010f + layer * 0.006f;
        const float y = h * (0.46f + layer * 0.17f);
        const float scale = w * (0.065f + layer * 0.018f);
        for (int i = 0; i < 7; ++i) {
            const float seed = Hash01(static_cast<std::uint32_t>(layer * 31 + i * 17 + 5));
            const float xNorm = Wrap01(seed + ctx.time * speed + static_cast<float>(i) * 0.16f);
            const float x = (xNorm * 1.35f - 0.18f) * w;
            const float bob = static_cast<float>(std::sin(ctx.time * (0.18f + layer * 0.04f) + i)) * h * 0.008f;
            const auto color = layer == 0 ? D2D1::ColorF(0.91f, 0.82f, 1.0f, 1.0f)
                             : layer == 1 ? D2D1::ColorF(1.0f, 0.80f, 0.91f, 1.0f)
                                          : D2D1::ColorF(1.0f, 0.88f, 0.95f, 1.0f);
            CloudCluster(ctx, x, y + bob, scale, color, 0.28f + layer * 0.12f);
        }
    }

    for (int ribbon = 0; ribbon < 3; ++ribbon) {
        const D2D1_COLOR_F color = ribbon == 0 ? D2D1::ColorF(1.0f, 0.72f, 0.95f, 0.35f)
                                  : ribbon == 1 ? D2D1::ColorF(0.64f, 0.82f, 1.0f, 0.28f)
                                                : D2D1::ColorF(1.0f, 0.92f, 0.72f, 0.22f);
        ctx.brush->SetColor(color);
        constexpr int segments = 72;
        for (int s = 0; s < segments; ++s) {
            const float a = static_cast<float>(s) / segments;
            const float b = static_cast<float>(s + 1) / segments;
            const float x1 = w * (0.08f + a * 0.88f);
            const float x2 = w * (0.08f + b * 0.88f);
            const float base = h * (0.30f + ribbon * 0.12f);
            const float y1 = base + std::sin(a * kTau * 1.35f + ctx.time * (0.16f + ribbon * 0.03f) + ribbon) * h * 0.055f;
            const float y2 = base + std::sin(b * kTau * 1.35f + ctx.time * (0.16f + ribbon * 0.03f) + ribbon) * h * 0.055f;
            ctx.target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), ctx.brush, 1.2f + ribbon * 0.3f);
        }
    }

    for (int i = 0; i < 72; ++i) {
        const float x = Hash01(static_cast<std::uint32_t>(i * 79 + 13)) * w;
        const float y = Hash01(static_cast<std::uint32_t>(i * 101 + 29)) * h * 0.72f;
        const float twinkle = 0.20f + Pulse(ctx.time, 1.0f + (i % 5) * 0.11f, i * 0.61f) * 0.58f;
        const float r = 0.7f + static_cast<float>(i % 4) * 0.45f;
        Spark(ctx, x, y, r, D2D1::ColorF(1.0f, 0.92f, 0.78f, twinkle));
    }

    for (int i = 0; i < 28; ++i) {
        const float p = Wrap01(Hash01(static_cast<std::uint32_t>(i * 37 + 9)) + ctx.time * (0.018f + (i % 5) * 0.003f));
        const float drift = static_cast<float>(std::sin(ctx.time * 0.38f + i * 1.41f)) * w * 0.035f;
        const float x = Hash01(static_cast<std::uint32_t>(i * 53 + 21)) * w + drift;
        const float y = -h * 0.08f + p * h * 1.18f;
        const float rx = w * (0.0025f + (i % 3) * 0.0011f);
        const float ry = h * (0.006f + (i % 4) * 0.0014f);
        ctx.brush->SetColor(D2D1::ColorF(1.0f, 0.68f, 0.83f, 0.22f + (1.0f - p) * 0.40f));
        ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), ctx.brush);
    }

    // Foreground cloud pedestal and a soft mascot-like cat silhouette.
    CloudCluster(ctx, w * 0.73f, h * 0.73f, w * 0.105f, D2D1::ColorF(1.0f, 0.78f, 0.90f, 1.0f), 0.82f);
    const float catX = w * 0.72f;
    const float catY = h * 0.58f;
    const float breathe = 1.0f + Pulse(ctx.time, 0.8f) * 0.012f;
    ctx.brush->SetColor(D2D1::ColorF(0.98f, 0.97f, 1.0f, 0.96f));
    ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(catX, catY + h * 0.065f), w * 0.047f * breathe, h * 0.088f * breathe), ctx.brush);
    ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(catX, catY - h * 0.035f), w * 0.040f, h * 0.062f), ctx.brush);
    ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(catX - w * 0.026f, catY - h * 0.087f), w * 0.015f, h * 0.026f), ctx.brush);
    ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(catX + w * 0.026f, catY - h * 0.087f), w * 0.015f, h * 0.026f), ctx.brush);
    const float blink = Pulse(ctx.time, 0.42f, 2.4f) > 0.985f ? 0.25f : 1.0f;
    ctx.brush->SetColor(D2D1::ColorF(0.18f, 0.33f, 0.58f, 0.96f));
    ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(catX - w * 0.014f, catY - h * 0.041f), w * 0.006f, h * 0.010f * blink), ctx.brush);
    ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(catX + w * 0.014f, catY - h * 0.041f), w * 0.006f, h * 0.010f * blink), ctx.brush);
    ctx.brush->SetColor(D2D1::ColorF(1.0f, 0.55f, 0.66f, 0.92f));
    ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(catX, catY - h * 0.020f), w * 0.004f, h * 0.004f), ctx.brush);
    ctx.brush->SetColor(D2D1::ColorF(0.97f, 0.95f, 1.0f, 0.92f));
    constexpr int tailSegments = 18;
    for (int i = 0; i < tailSegments; ++i) {
        const float a = static_cast<float>(i) / tailSegments;
        const float b = static_cast<float>(i + 1) / tailSegments;
        const float sway = std::sin(ctx.time * 0.75f) * w * 0.008f;
        const D2D1_POINT_2F p1{catX + w * (0.040f + a * 0.050f) + sway * a, catY + h * (0.055f - std::sin(a * 2.2f) * 0.045f)};
        const D2D1_POINT_2F p2{catX + w * (0.040f + b * 0.050f) + sway * b, catY + h * (0.055f - std::sin(b * 2.2f) * 0.045f)};
        ctx.target->DrawLine(p1, p2, ctx.brush, w * 0.009f);
    }

    (void)slow;
}

inline void PaintNeonCity(const ScenePaintContext& ctx, const D2D1_SIZE_F& size) {
    FillVerticalGradient(ctx, size,
        D2D1::ColorF(0.012f, 0.018f, 0.075f, 1.0f),
        D2D1::ColorF(0.075f, 0.018f, 0.12f, 1.0f), 56);

    const float w = size.width;
    const float h = size.height;
    const float horizonY = h * 0.56f;
    const float centerX = w * 0.50f;

    for (int cloud = 0; cloud < 8; ++cloud) {
        const float x = w * Hash01(static_cast<std::uint32_t>(cloud * 41 + 7));
        const float y = h * (0.08f + Hash01(static_cast<std::uint32_t>(cloud * 71 + 3)) * 0.28f);
        const float pulse = Pulse(ctx.time, 0.24f + cloud * 0.02f, cloud);
        GlowEllipse(ctx, D2D1::Point2F(x, y), w * (0.06f + cloud % 3 * 0.02f), h * 0.035f,
                    cloud % 2 ? D2D1::ColorF(0.14f, 0.46f, 1.0f, 0.035f + pulse * 0.04f)
                              : D2D1::ColorF(0.92f, 0.08f, 0.86f, 0.025f + pulse * 0.035f), 3, 0.9f);
    }

    constexpr int buildings = 28;
    for (int i = 0; i < buildings; ++i) {
        const float left = w * static_cast<float>(i) / buildings;
        const float next = w * static_cast<float>(i + 1) / buildings;
        const float gap = w * 0.0025f;
        const float bw = std::max(3.0f, next - left - gap);
        const float bh = h * (0.12f + Hash01(static_cast<std::uint32_t>(i * 97 + 11)) * 0.39f);
        const float top = horizonY - bh;
        const float flicker = Pulse(ctx.time, 0.35f + (i % 5) * 0.09f, i * 0.73f);
        ctx.brush->SetColor(D2D1::ColorF(0.018f + flicker * 0.018f, 0.025f, 0.075f + flicker * 0.025f, 0.98f));
        ctx.target->FillRectangle(D2D1::RectF(left + gap, top, left + gap + bw, horizonY), ctx.brush);

        const bool cyan = (i % 3) != 0;
        const auto edge = cyan ? D2D1::ColorF(0.04f, 0.78f, 1.0f, 0.54f)
                               : D2D1::ColorF(1.0f, 0.08f, 0.78f, 0.56f);
        ctx.brush->SetColor(edge);
        ctx.target->DrawLine(D2D1::Point2F(left + gap + 1.0f, top), D2D1::Point2F(left + gap + 1.0f, horizonY), ctx.brush, 1.2f);
        if ((i % 4) == 0)
            ctx.target->DrawLine(D2D1::Point2F(left + gap + bw - 1.0f, top), D2D1::Point2F(left + gap + bw - 1.0f, horizonY), ctx.brush, 1.0f);

        const int rows = 5 + i % 5;
        const int cols = 2 + i % 3;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const float on = Hash01(static_cast<std::uint32_t>(i * 1009 + row * 73 + col * 19));
                if (on < 0.30f) continue;
                const float wx = left + gap + bw * (0.16f + static_cast<float>(col) / std::max(1, cols - 1) * 0.68f);
                const float wy = top + bh * (0.14f + static_cast<float>(row) / std::max(1, rows - 1) * 0.72f);
                const float sparkle = 0.22f + 0.50f * Pulse(ctx.time, 0.55f + on * 0.3f, on * 7.0f);
                ctx.brush->SetColor(cyan ? D2D1::ColorF(0.10f, 0.86f, 1.0f, sparkle)
                                         : D2D1::ColorF(1.0f, 0.16f, 0.82f, sparkle));
                ctx.target->FillRectangle(D2D1::RectF(wx, wy, wx + std::max(1.0f, bw * 0.07f), wy + h * 0.0032f), ctx.brush);
            }
        }
    }

    ctx.brush->SetColor(D2D1::ColorF(0.008f, 0.010f, 0.025f, 1.0f));
    ctx.target->FillRectangle(D2D1::RectF(0.0f, horizonY, w, h), ctx.brush);

    constexpr int roadRays = 17;
    for (int i = 0; i < roadRays; ++i) {
        const float lane = (static_cast<float>(i) / (roadRays - 1) - 0.5f) * 1.5f;
        const float xBottom = centerX + lane * w * 0.82f;
        const auto color = (i % 2) ? D2D1::ColorF(0.06f, 0.72f, 1.0f, 0.28f)
                                   : D2D1::ColorF(1.0f, 0.04f, 0.76f, 0.28f);
        ctx.brush->SetColor(color);
        ctx.target->DrawLine(D2D1::Point2F(centerX + lane * w * 0.025f, horizonY), D2D1::Point2F(xBottom, h), ctx.brush, 1.1f);
    }

    const float travel = Wrap01(ctx.time * 0.24f);
    constexpr int crossLines = 22;
    for (int i = 0; i < crossLines; ++i) {
        const float p = Wrap01(travel + static_cast<float>(i) / crossLines);
        const float eased = p * p;
        const float y = horizonY + eased * (h - horizonY);
        const float half = eased * w * 0.68f;
        ctx.brush->SetColor(D2D1::ColorF(0.10f, 0.72f, 1.0f, 0.12f + (1.0f - p) * 0.26f));
        ctx.target->DrawLine(D2D1::Point2F(centerX - half, y), D2D1::Point2F(centerX + half, y), ctx.brush, 1.0f + eased * 1.4f);
    }

    // Fast moving light trails and wet-road reflections.
    for (int trail = 0; trail < 12; ++trail) {
        const float lane = (Hash01(static_cast<std::uint32_t>(trail * 43 + 5)) - 0.5f) * 1.25f;
        const float p = Wrap01(ctx.time * (0.22f + (trail % 4) * 0.035f) + Hash01(static_cast<std::uint32_t>(trail * 97 + 2)));
        const float eased = p * p;
        const float y = horizonY + eased * (h - horizonY);
        const float x = centerX + lane * w * eased * 0.62f;
        const bool pink = (trail % 2) == 0;
        const auto c = pink ? D2D1::ColorF(1.0f, 0.04f, 0.80f, 0.88f)
                            : D2D1::ColorF(0.02f, 0.78f, 1.0f, 0.88f);
        ctx.brush->SetColor(c);
        const float len = w * (0.025f + eased * 0.12f);
        ctx.target->DrawLine(D2D1::Point2F(x - len, y), D2D1::Point2F(x + len, y), ctx.brush, 2.0f + eased * 4.0f);
        auto reflection = c;
        reflection.a *= 0.15f;
        ctx.brush->SetColor(reflection);
        ctx.target->FillRectangle(D2D1::RectF(x - len * 0.38f, y, x + len * 0.38f, std::min(h, y + h * 0.16f * eased)), ctx.brush);
    }

    for (int i = 0; i < 90; ++i) {
        const float x = Hash01(static_cast<std::uint32_t>(i * 67 + 13)) * w;
        const float p = Wrap01(ctx.time * (0.72f + (i % 7) * 0.055f) + Hash01(static_cast<std::uint32_t>(i * 37 + 17)));
        const float y = p * h;
        const float length = h * (0.018f + (i % 4) * 0.006f);
        ctx.brush->SetColor(D2D1::ColorF(0.44f, 0.78f, 1.0f, 0.08f + (i % 5) * 0.018f));
        ctx.target->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x - w * 0.006f, y + length), ctx.brush, 0.8f);
    }

    const float fog = Pulse(ctx.time, 0.18f);
    ctx.brush->SetColor(D2D1::ColorF(0.42f, 0.12f, 0.72f, 0.025f + fog * 0.025f));
    ctx.target->FillRectangle(D2D1::RectF(0.0f, horizonY - h * 0.06f, w, horizonY + h * 0.09f), ctx.brush);
}

inline void PaintMysticMoon(const ScenePaintContext& ctx, const D2D1_SIZE_F& size) {
    FillVerticalGradient(ctx, size,
        D2D1::ColorF(0.008f, 0.015f, 0.075f, 1.0f),
        D2D1::ColorF(0.055f, 0.035f, 0.16f, 1.0f), 60);

    const float w = size.width;
    const float h = size.height;
    const float lakeTop = h * 0.55f;
    const D2D1_POINT_2F moon{w * 0.52f, h * 0.22f};

    for (int i = 0; i < 100; ++i) {
        const float x = Hash01(static_cast<std::uint32_t>(i * 83 + 11)) * w;
        const float y = Hash01(static_cast<std::uint32_t>(i * 47 + 19)) * h * 0.50f;
        const float twinkle = 0.12f + Pulse(ctx.time, 0.55f + (i % 6) * 0.10f, i) * 0.52f;
        const float r = 0.55f + static_cast<float>(i % 4) * 0.30f;
        ctx.brush->SetColor(D2D1::ColorF(0.72f, 0.84f, 1.0f, twinkle));
        ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), r, r), ctx.brush);
    }

    for (int ring = 10; ring >= 1; --ring) {
        const float p = static_cast<float>(ring) / 10.0f;
        const float pulse = Pulse(ctx.time, 0.22f, ring * 0.4f);
        ctx.brush->SetColor(D2D1::ColorF(0.30f, 0.46f, 1.0f, 0.012f + (1.0f - p) * 0.030f + pulse * 0.008f));
        ctx.target->FillEllipse(D2D1::Ellipse(moon, w * (0.036f + p * 0.12f), h * (0.057f + p * 0.18f)), ctx.brush);
    }
    GlowEllipse(ctx, moon, w * 0.030f, h * 0.048f, D2D1::ColorF(0.88f, 0.93f, 1.0f, 0.96f), 6, 0.75f);

    // Slow rotating cloud/nebula points around the moon.
    for (int i = 0; i < 42; ++i) {
        const float angle = static_cast<float>(i) / 42.0f * kTau + ctx.time * (0.025f + (i % 3) * 0.006f);
        const float radius = w * (0.070f + Hash01(static_cast<std::uint32_t>(i * 31 + 7)) * 0.105f);
        const float x = moon.x + std::cos(angle) * radius;
        const float y = moon.y + std::sin(angle) * radius * 0.42f;
        const float a = 0.018f + Pulse(ctx.time, 0.20f, i) * 0.026f;
        GlowEllipse(ctx, D2D1::Point2F(x, y), w * 0.009f, h * 0.006f,
                    D2D1::ColorF(0.40f, 0.54f, 1.0f, a), 2, 0.8f);
    }

    // Distant mountain outlines.
    ctx.brush->SetColor(D2D1::ColorF(0.07f, 0.08f, 0.18f, 0.95f));
    for (int m = 0; m < 11; ++m) {
        const float baseX = w * (static_cast<float>(m) / 10.0f);
        const float peakX = baseX + w * 0.045f;
        const float peakY = h * (0.32f + Hash01(static_cast<std::uint32_t>(m * 73 + 8)) * 0.10f);
        ctx.target->DrawLine(D2D1::Point2F(baseX - w * 0.08f, lakeTop), D2D1::Point2F(peakX, peakY), ctx.brush, w * 0.012f);
        ctx.target->DrawLine(D2D1::Point2F(peakX, peakY), D2D1::Point2F(baseX + w * 0.10f, lakeTop), ctx.brush, w * 0.012f);
    }

    // Pine silhouettes across the far shore.
    for (int i = 0; i < 34; ++i) {
        const float x = w * (0.02f + static_cast<float>(i) / 34.0f * 0.96f);
        const float treeH = h * (0.055f + Hash01(static_cast<std::uint32_t>(i * 113 + 4)) * 0.105f);
        const float baseY = lakeTop + h * 0.012f;
        ctx.brush->SetColor(D2D1::ColorF(0.018f, 0.040f, 0.085f, 0.97f));
        ctx.target->FillRectangle(D2D1::RectF(x - w * 0.0012f, baseY - treeH, x + w * 0.0012f, baseY), ctx.brush);
        for (int tier = 0; tier < 5; ++tier) {
            const float p = static_cast<float>(tier) / 5.0f;
            ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, baseY - treeH * (0.25f + p * 0.60f)),
                                                  w * (0.008f + p * 0.004f), h * 0.012f), ctx.brush);
        }
    }

    // Lake body.
    ctx.brush->SetColor(D2D1::ColorF(0.015f, 0.045f, 0.13f, 0.98f));
    ctx.target->FillRectangle(D2D1::RectF(0.0f, lakeTop, w, h), ctx.brush);

    for (int strip = 0; strip < 36; ++strip) {
        const float p = static_cast<float>(strip) / 35.0f;
        const float y = lakeTop + p * (h - lakeTop);
        const float width = w * (0.025f + p * 0.22f) * (0.72f + 0.28f * Pulse(ctx.time, 0.38f, strip * 0.63f));
        const float shift = std::sin(ctx.time * 0.30f + strip * 0.47f) * w * 0.018f * p;
        const float alpha = (1.0f - p) * 0.16f + 0.08f;
        ctx.brush->SetColor(D2D1::ColorF(0.50f, 0.64f, 1.0f, alpha));
        ctx.target->DrawLine(D2D1::Point2F(moon.x - width + shift, y), D2D1::Point2F(moon.x + width + shift, y), ctx.brush,
                             1.0f + p * 1.6f);
    }

    constexpr int waveSegments = 64;
    for (int band = 0; band < 7; ++band) {
        const float baseY = lakeTop + h * (0.035f + band * 0.055f);
        ctx.brush->SetColor(D2D1::ColorF(0.24f, 0.46f, 0.88f, 0.10f + band * 0.012f));
        for (int s = 0; s < waveSegments; ++s) {
            const float a = static_cast<float>(s) / waveSegments;
            const float b = static_cast<float>(s + 1) / waveSegments;
            const float x1 = a * w;
            const float x2 = b * w;
            const float y1 = baseY + std::sin(a * kTau * (1.6f + band * 0.12f) + ctx.time * (0.32f + band * 0.03f)) * h * 0.0045f;
            const float y2 = baseY + std::sin(b * kTau * (1.6f + band * 0.12f) + ctx.time * (0.32f + band * 0.03f)) * h * 0.0045f;
            ctx.target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), ctx.brush, 1.0f);
        }
    }

    // Layered drifting fog.
    for (int fog = 0; fog < 12; ++fog) {
        const float p = Wrap01(Hash01(static_cast<std::uint32_t>(fog * 97 + 31)) + ctx.time * (0.007f + fog % 3 * 0.002f));
        const float x = (p * 1.35f - 0.18f) * w;
        const float y = h * (0.47f + Hash01(static_cast<std::uint32_t>(fog * 43 + 6)) * 0.28f);
        ctx.brush->SetColor(D2D1::ColorF(0.34f, 0.38f, 0.78f, 0.025f + (fog % 4) * 0.008f));
        ctx.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), w * (0.08f + fog % 3 * 0.025f), h * 0.018f), ctx.brush);
    }

    // Fireflies and bioluminescent foreground plants.
    for (int i = 0; i < 48; ++i) {
        const float baseX = Hash01(static_cast<std::uint32_t>(i * 61 + 13));
        const float baseY = Hash01(static_cast<std::uint32_t>(i * 89 + 21));
        const float x = (baseX + std::sin(ctx.time * (0.18f + (i % 5) * 0.03f) + i) * 0.018f) * w;
        const float y = h * (0.38f + baseY * 0.48f + std::cos(ctx.time * 0.22f + i * 0.7f) * 0.012f);
        const float glow = 0.25f + Pulse(ctx.time, 0.72f + (i % 7) * 0.07f, i) * 0.70f;
        GlowEllipse(ctx, D2D1::Point2F(x, y), 1.2f + (i % 3) * 0.6f, 1.2f + (i % 3) * 0.6f,
                    D2D1::ColorF(0.76f, 0.66f, 1.0f, glow), 3, 1.8f);
    }

    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < 12; ++i) {
            const float t = static_cast<float>(i) / 11.0f;
            const float x = side == 0 ? w * (0.02f + t * 0.20f) : w * (0.98f - t * 0.20f);
            const float y = h * (0.80f + Hash01(static_cast<std::uint32_t>(side * 100 + i * 17)) * 0.16f);
            const float pulse = 0.45f + Pulse(ctx.time, 0.55f, i) * 0.50f;
            ctx.brush->SetColor(D2D1::ColorF(0.42f, 0.28f, 0.90f, 0.52f));
            ctx.target->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x, y - h * 0.035f), ctx.brush, 1.2f);
            Spark(ctx, x, y - h * 0.038f, 1.5f + (i % 3) * 0.55f, D2D1::ColorF(0.72f, 0.62f, 1.0f, pulse));
        }
    }
}

} // namespace miaodesk::wallpaper::scenes
