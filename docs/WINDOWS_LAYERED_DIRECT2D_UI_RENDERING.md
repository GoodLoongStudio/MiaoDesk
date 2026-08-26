# TuringDesk Windows Layered Direct2D UI Rendering Guide

> Status: **Approved implementation baseline**
>
> Scope: Windows native floating UI surfaces such as Search Bar, compact toolbars, glass panels, floating controls, quick-launch surfaces and similar rounded translucent UI.
>
> Reference implementation: `src/native/src/ui/search/SearchWindow.cpp`

## 1. Why this document exists

The Search Bar went through several rendering approaches before reaching an ARM64 real-Windows result with clean rounded edges. The important lesson is that the visual defect was not mainly a corner-radius parameter problem. It was caused by mixing incompatible window-shaping and rendering systems.

The approved implementation baseline is:

```text
WS_EX_LAYERED HWND
    ↓
32-bit top-down DIB section
    ↓
ID2D1DCRenderTarget
    ↓
PREMULTIPLIED alpha
    ↓
Direct2D anti-aliased geometry/text
    ↓
UpdateLayeredWindow(..., ULW_ALPHA)
    ↓
Windows compositor
```

This path should be reused for future TuringDesk floating native UI when pixel-clean transparent rounded edges are required.

## 2. Approved visual result

The Search Bar implementation was visually accepted on ARM64 Windows using this rendering path.

The design target remains:

- `docs/design/search-bar-reference.jpg`
- `docs/SEARCH_BAR_VISUAL_SPEC.md`

The rendering method in this document is independent from the exact Search Bar colors and dimensions. It is a reusable **window composition and edge-quality technique**.

## 3. The failure mode we must not reintroduce

The earlier implementation combined:

```text
Direct2D antialiased rounded rectangle
+
CreateRoundRectRgn / SetWindowRgn
+
HWND render target / DWM window shaping
```

This is a bad combination for a translucent pill-shaped surface.

Direct2D generates partially covered pixels around a curved edge. Those pixels need intermediate alpha values so the compositor can produce a smooth silhouette.

A GDI window region is effectively a coarse binary clipping boundary for this use case. It does not preserve the same sub-pixel coverage model as the Direct2D geometry. When both systems shape the same visible edge, the result can contain:

- stair-step / fuzzy rounded ends;
- clipped anti-alias pixels;
- gray or white side lobes;
- inconsistent border thickness;
- geometry that changes appearance with DPI or scale.

### Forbidden for this rendering class

Do not use the following to define the visible rounded silhouette of a layered Direct2D floating surface:

```cpp
CreateRoundRectRgn(...)
SetWindowRgn(...)
```

Do not reintroduce `ID2D1HwndRenderTarget` as the primary Search Bar surface when the window itself requires per-pixel transparency.

These markers are intentionally guarded by `scripts/verify-search-input-contract.ps1` for the Search Bar implementation.

## 4. Window creation

The outer Win32 window is a borderless popup/tool window with per-pixel alpha composition:

```cpp
CreateWindowExW(
    WS_EX_TOOLWINDOW | WS_EX_LAYERED,
    windowClass,
    title,
    WS_POPUP,
    ...);
```

The important flag is:

```cpp
WS_EX_LAYERED
```

The window silhouette is **not** produced by a region. Transparent pixels in the backing surface define the actual visible shape.

## 5. Backing surface

Create a memory DC and a 32-bit top-down DIB section:

```cpp
layerDc_ = CreateCompatibleDC(nullptr);

BITMAPINFO bitmapInfo{};
bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
bitmapInfo.bmiHeader.biWidth = width;
bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height); // top-down
bitmapInfo.bmiHeader.biPlanes = 1;
bitmapInfo.bmiHeader.biBitCount = 32;
bitmapInfo.bmiHeader.biCompression = BI_RGB;

layerBitmap_ = CreateDIBSection(
    layerDc_,
    &bitmapInfo,
    DIB_RGB_COLORS,
    &layerBits_,
    nullptr,
    0);
```

The negative height is intentional: it makes the DIB top-down and keeps coordinates aligned with normal UI coordinates.

Before every redraw, clear all pixels to fully transparent:

```cpp
std::memset(layerBits_, 0, width * height * 4u);
```

This is important. Old alpha data must never survive from a previous frame.

## 6. Direct2D target

Use `ID2D1DCRenderTarget`, not an HWND render target:

```cpp
const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
    D2D1_RENDER_TARGET_TYPE_DEFAULT,
    D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_PREMULTIPLIED),
    0.0f,
    0.0f,
    D2D1_RENDER_TARGET_USAGE_NONE,
    D2D1_FEATURE_LEVEL_DEFAULT);

d2dFactory_->CreateDCRenderTarget(&props, renderTarget_.GetAddressOf());
renderTarget_->BindDC(layerDc_, &rect);
```

### Alpha mode is non-negotiable

Use:

```cpp
D2D1_ALPHA_MODE_PREMULTIPLIED
```

`UpdateLayeredWindow` with `AC_SRC_ALPHA` expects premultiplied-alpha pixels. Mixing straight alpha and premultiplied alpha is a common source of dark/dirty fringes around translucent edges.

## 7. Direct2D anti-aliasing

For geometry:

```cpp
renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
```

For text on transparent/translucent UI:

```cpp
renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
```

`GRAYSCALE` is preferred over ClearType for these layered transparent surfaces because ClearType is optimized around opaque RGB sub-pixel assumptions and can create colored fringes when composed over changing backgrounds.

## 8. Rounded geometry rules

A floating glass UI surface should have **one authoritative geometry model**.

For the Search Bar:

```text
Visible size: 720 × 56 logical px
Radius:       28 px
```

The fill, outline, highlight and focus treatment must derive from matching rounded geometry. Do not let unrelated window-shaping code define a second rounded boundary.

Recommended pattern:

```cpp
const auto bar = D2D1::RoundedRect(
    D2D1::RectF(0.5f, 0.5f, width - 0.5f, 55.5f),
    27.5f,
    27.5f);

renderTarget_->FillRoundedRectangle(bar, fillBrush);
renderTarget_->DrawRoundedRectangle(bar, outlineBrush, 1.0f);
```

The half-pixel inset is useful for a 1 px outline because it keeps the stroke visually aligned and avoids clipping at the surface boundary.

For more complex UI, prefer constants/helpers that derive every nested geometry from a single base rectangle and radius rather than duplicating magic numbers.

## 9. Presenting the surface

After Direct2D finishes drawing, present the complete surface with `UpdateLayeredWindow`:

```cpp
BLENDFUNCTION blend{};
blend.BlendOp = AC_SRC_OVER;
blend.SourceConstantAlpha = 255;
blend.AlphaFormat = AC_SRC_ALPHA;

UpdateLayeredWindow(
    hwnd,
    nullptr,
    &destination,
    &size,
    layerDc,
    &source,
    0,
    &blend,
    ULW_ALPHA);
```

Key requirements:

- `SourceConstantAlpha = 255` for full per-pixel-alpha control;
- `AlphaFormat = AC_SRC_ALPHA`;
- `ULW_ALPHA`;
- source pixels already premultiplied.

At this point, transparent pixels around the rounded corners remain truly transparent and the Windows compositor blends anti-aliased edge pixels correctly.

## 10. Input must be separated from visual rendering

A major Search Bar lesson is that input infrastructure and visual UI should not be the same thing.

The approved pattern is:

```text
Visible UI
    Direct2D custom rendering

Input infrastructure
    1 × 1 native EDIT child
    keyboard / IME / clipboard / voice typing only
```

The native `EDIT` exists because Windows text input, IME, clipboard behavior and Windows Voice Typing are mature and valuable. However, it must never paint its own rectangle over the custom surface.

The current Search Bar suppresses the input proxy's painting and draws visible text/caret itself with DirectWrite/Direct2D.

This keeps:

- Chinese IME support;
- keyboard editing;
- clipboard behavior;
- `Win + H` voice typing;
- native focus semantics;

without sacrificing pixel-level visual control.

### Important

Do **not** hide the input control by making the child `EDIT` itself a zero-alpha layered window. That previously caused input/focus behavior to become unreliable. Keep it as a normal tiny input proxy and suppress visual painting instead.

## 11. Caret and text layout

Text and caret must share the same DirectWrite layout origin.

Correct model:

```text
one text origin
    ↓
IDWriteTextLayout
    ↓
DrawTextLayout
    ↓
HitTestTextPosition
    ↓
caret position
```

Do not calculate the caret using independent hard-coded offsets. That creates visible drift between the caret and rendered glyphs.

For transparent UI, DirectWrite + grayscale antialiasing gives stable text over arbitrary backgrounds.

## 12. State rendering

State should be visual layers, not geometry mutations.

Recommended model:

```text
Base rounded geometry
    ├─ fill
    ├─ primary outline
    ├─ directional glass highlight
    └─ optional state glow
```

For Search Bar:

- **Default**: subtle light edge, no blue outline;
- **Hover**: slightly brighter fill/white edge;
- **Focused**: thin blue outline, restrained glow;
- **Active**: slightly stronger blue outline/glow.

Do not change the pill size/radius between states. State transitions should not move the silhouette.

## 13. Performance rules

This approach is efficient enough for compact desktop UI, but keep several rules:

1. **Reuse the DIB/DC/render target while size is unchanged.**
2. Recreate the surface only when dimensions change or the D2D target becomes invalid.
3. Redraw on actual state changes: query, focus, hover, caret timer, result change, resize/display change.
4. Do not run a continuous 60 FPS loop for static UI.
5. Keep expensive effects bounded. A small floating surface can afford gradients and light glows, but avoid unnecessary full-screen blur/effect chains.

The current Search Bar reuses the backing surface through `EnsureLayerSurface()` and destroys/rebuilds it only when necessary.

## 14. DPI considerations

The current Search Bar contract is defined in logical geometry, while layered surfaces eventually render to actual pixels.

Future reusable components should explicitly decide whether they are:

- fixed logical-pixel UI scaled by monitor DPI; or
- fixed physical-pixel demo surfaces.

For production multi-monitor DPI support, all of the following must use the same scale factor:

- window size;
- DIB dimensions;
- rounded rectangle geometry;
- icon geometry;
- text format size;
- hit-test zones;
- shadow/glow spread.

Never scale only the HWND while leaving Direct2D geometry at the previous pixel dimensions.

## 15. When to use this approach

Use this layered Direct2D baseline for:

- floating Search Bars;
- compact desktop toolbars;
- glass command palettes;
- floating Widget chrome;
- notification/assistant bubbles;
- translucent control strips;
- custom frameless overlays requiring perfect rounded alpha edges.

It is especially appropriate when:

- the UI is visually small/medium;
- the visible silhouette is non-rectangular;
- per-pixel transparency matters;
- native Win32 interaction must remain available;
- a full WinUI/XAML migration would be unnecessary overhead.

## 16. When not to use it

Do not automatically use this for every large application window.

For large complex windows with many standard controls, accessibility trees, scrolling layout, complex text editing or large responsive views, WinUI 3/XAML or another retained UI framework may be more appropriate.

This technique is a **high-control native floating-surface renderer**, not a replacement for every UI framework.

## 17. Relationship to Acrylic / backdrop blur

Per-pixel alpha solves the silhouette/edge problem. It does **not by itself capture and blur desktop pixels behind the window**.

Real backdrop blur/Acrylic is a separate material concern.

Do not reintroduce a rectangular system backdrop if doing so destroys the clean per-pixel silhouette. Any future Acrylic/backdrop implementation must preserve this rule:

> **The visible rounded silhouette remains owned by the per-pixel-alpha Direct2D surface.**

If system Acrylic is later added, validate it on real ARM64 Windows before making it the default. The clean edge has higher priority than adding blur.

## 18. Cleanup / resource lifetime

Layered rendering owns native resources that must be released in the correct order:

```text
ID2D1DCRenderTarget reset
    ↓
restore previous HBITMAP into HDC
    ↓
DeleteObject(DIB)
    ↓
DeleteDC(memory DC)
```

Do not delete a bitmap while it is still selected into the DC.

The current implementation centralizes this in `ReleaseLayerSurface()`.

## 19. Reusable implementation checklist

Before accepting any future floating native UI using this technique, verify:

- [ ] top-level HWND uses `WS_EX_LAYERED`;
- [ ] 32-bit DIB is used;
- [ ] DIB is cleared to transparent before redraw;
- [ ] D2D pixel format is `B8G8R8A8_UNORM`;
- [ ] alpha mode is `PREMULTIPLIED`;
- [ ] geometry antialias mode is `PER_PRIMITIVE`;
- [ ] transparent-surface text uses `GRAYSCALE` unless a tested reason says otherwise;
- [ ] `UpdateLayeredWindow` uses `AC_SRC_ALPHA` and `ULW_ALPHA`;
- [ ] no GDI window region clips the visible curved edge;
- [ ] fill/border/highlight share one geometry system;
- [ ] text and caret share one DirectWrite origin/layout;
- [ ] native input proxy does not become visible;
- [ ] surface resources are reused and safely released;
- [ ] real ARM64 Windows screenshot is visually inspected at 1:1 scale.

## 20. Design principle to keep

The main rule learned from the Search Bar work is simple:

> **One visible edge must have one owner.**

For TuringDesk custom floating UI, that owner is Direct2D rendered into a premultiplied-alpha layered surface. Window regions, DWM shaping and other systems must not independently reshape the same visible edge.

This rule is more important than any individual radius, border color or glow value.
