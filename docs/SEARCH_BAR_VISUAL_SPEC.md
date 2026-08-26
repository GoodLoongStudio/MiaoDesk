# TuringDesk Search Bar Visual Contract

This document is the authoritative visual contract for the TuringDesk Search Bar demo surface.

## Authoritative reference

![Approved Search Bar reference](design/search-bar-reference.jpg)

The image above is the visual acceptance source. When implementation values and the rendered result disagree, the rendered result must be adjusted until the ARM64 real-Windows screenshot is visually aligned with this reference.

## Geometry

- Visible pill: **720 × 56 logical px**.
- Corner radius: **28 px** (full pill).
- Search icon: **20 px** visual footprint.
- Left content inset: **20 px**.
- Search icon to text gap: **16 px**.
- Divider: **1 px**.
- Right icon group follows the reference spacing and must remain visually centered vertically.

## Material and color

- Target material: light macOS-like frosted glass, not opaque gray and not dark acrylic.
- Design reference background intent: `rgba(255,255,255,0.45)` over a blurred backdrop of approximately `20 px`.
- Border intent: `1 px rgba(255,255,255,0.60)` plus a subtle directional glass-edge highlight.
- Icon color: `#6B7280`.
- Primary typed text: darker than placeholder while remaining soft, approximately `#374151`.
- Placeholder: clearly readable at rest; it must never fade into the glass.
- Divider: `rgba(0,0,0,0.08)`.
- Shadow intent: approximately `0 10px 30px rgba(0,0,0,0.08)`.
- Focus glow intent: approximately `rgba(66,133,244,0.25)`.

## State contract

### Default

- Light, clean glass.
- Very subtle white edge.
- No blue outline.
- Placeholder and icons remain clearly readable.

### Hover

- Glass lifts slightly brighter.
- White edge highlight is a little stronger.
- Still no blue outline.

### Focused

- Thin clean blue outline.
- Very restrained blue halo; never a thick neon ring.
- Caret aligns to the same DirectWrite layout origin as the rendered text.

### Active

- Blue outline is slightly brighter than Focused.
- A soft, broader blue glow may appear outside/inside the outline without changing the pill geometry.

## Edge-quality acceptance

The following are release-blocking visual defects for this Search Bar:

- gray or white side lobes at either rounded end;
- fuzzy GDI-style stair-step edges;
- clipped antialias pixels along the 28 px radius;
- a second hidden/native text-box rectangle becoming visible;
- a placeholder that is difficult to read in the default state;
- caret origin or baseline not matching the rendered query text;
- Default/Hover/Focused/Active using the same outline treatment.

The fill, outline and edge highlight must use matching rounded geometry and pixel-aligned insets. The visible antialiased pill edge must not be drawn directly against a coarse `SetWindowRgn` clipping boundary.

## Interaction contract

- `Alt + Space` focuses the Search Bar.
- Native text input remains available for keyboard, Chinese IME, clipboard and Windows voice typing.
- Microphone invokes Windows voice typing.
- AI sparkle invokes the existing Turing AI path.
- Settings are not shown on the Search Bar; settings remain available through the system tray.

## ARM64 visual acceptance

A real ARM64 Windows screenshot is the final visual gate. Compare the screenshot against `docs/design/search-bar-reference.jpg` at 1:1 logical geometry and verify silhouette, readability, state outline, icon alignment and caret alignment before further UI expansion.
