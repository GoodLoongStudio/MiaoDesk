#pragma once

#include <array>
#include <cstdint>

// Pure geometry for the library's filter-chip row, shared by the layout and by tests.
//
// The row used to be laid out as `chipX += chipW + gap` with no wrap, and it does not
// fit: at the library window's own minimum tracked width (S(820) client, S(208) sidebar)
// the available span is 588 logical px, while eight wallpaper chips need 590 and eight
// widget chips need 610. So the last chip ran off the right edge of the window at every
// DPI -- both sides scale together, so this is a logical-pixel problem, not a DPI one --
// and the ninth chip the library still owes (收藏) would have made it 78 px worse.
//
// The layout keeps a single visual row whenever it fits and grows the band by exactly one
// row per wrap, because shrinking ten owner-drawn chips with Chinese labels into a
// clipped row is not a fix. The band height is derived from the row count, so nothing
// below the chips is ever overlapped.
//
// Coordinates are logical pixels with the caller's scaling already applied: this
// function takes a span and the chip widths, never a device pixel, so it can be compiled
// and executed directly by a test.

namespace miaodesk::library_ui {

constexpr std::size_t kMaxFilterChips = 16;

struct FilterChipLayout {
    std::array<std::int32_t, kMaxFilterChips> column{};
    std::array<std::int32_t, kMaxFilterChips> row{};
    std::int32_t rows{1};
    // Widest chip in each row; the caller uses it to know how much of the row is used.
    std::array<std::int32_t, kMaxFilterChips> rowWidth{};
    std::int32_t usedWidth{};
};

// `widths[i]` is chip i's width, `count` how many there are, `gap` the space between
// chips (also used as the space between rows), `span` the width available from the first
// chip's left edge to the row's right edge. A chip wider than the span still gets its own
// row rather than being dropped or clipped to zero.
inline FilterChipLayout ResolveFilterChipLayout(const std::int32_t* widths,
                                                std::int32_t count,
                                                std::int32_t gap,
                                                std::int32_t span) {
    FilterChipLayout layout;
    if (count <= 0 || !widths) return layout;
    if (count > static_cast<std::int32_t>(kMaxFilterChips)) count = static_cast<std::int32_t>(kMaxFilterChips);

    std::int32_t x = 0;
    std::int32_t row = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::int32_t width = widths[i] > 0 ? widths[i] : 0;
        // Wrap only when something is already on this row: the first chip always gets
        // placed, however wide, or a too-narrow span would hide the whole row.
        if (i > 0 && x + gap + width > span) {
            ++row;
            x = 0;
        } else if (i > 0) {
            x += gap;
        }
        layout.column[static_cast<std::size_t>(i)] = x;
        layout.row[static_cast<std::size_t>(i)] = row;
        x += width;
        if (width > layout.rowWidth[static_cast<std::size_t>(row)])
            layout.rowWidth[static_cast<std::size_t>(row)] = width;
        if (x > layout.usedWidth) layout.usedWidth = x;
    }
    layout.rows = row + 1;
    return layout;
}

// Height a chip band needs for a layout: every row's chips plus the gaps between rows,
// plus the padding the layout keeps under the last row.
inline std::int32_t FilterChipBandHeight(const FilterChipLayout& layout,
                                         std::int32_t chipHeight,
                                         std::int32_t gap,
                                         std::int32_t bottomPadding) {
    const std::int32_t rows = layout.rows > 0 ? layout.rows : 1;
    return rows * chipHeight + (rows - 1) * gap + bottomPadding;
}

} // namespace miaodesk::library_ui
