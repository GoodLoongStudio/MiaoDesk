import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");
const header = path.join(root, "src/include/miaodesk/LibraryFilterChipLayout.h");
const library = fs.readFileSync(path.join(root, "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp"), "utf8");

// The library's filter chip row ran off the right edge. At the window's own minimum
// tracked width (S(820) client, S(208) sidebar) the available span is 588 logical px,
// while eight wallpaper chips need 590 and eight widget chips need 610 -- so the last chip
// was clipped out of the window at every DPI, and the ninth chip the library still owes
// (收藏) would have pushed the need to 666. Both sides scale together, so this is a
// logical-pixel bug, not a DPI one.
//
// The geometry is not asserted about textually here: the real header is compiled and
// EXECUTED, and the invariants are checked against its output. That is the only
// verification that would have caught the two mistakes I made while writing it (a
// kMaxFilterChips-wide fill that left the widget row's wide last chip at the wrong index,
// and a wrap that could push the only chip on a row past the span).

const chipWidths = (count, lastWide) => {
  const w = new Array(count).fill(68);
  w[0] = 58;
  if (lastWide && count > 1) w[count - 1] = 88;
  return w;
};

const program = `
#include "LibraryFilterChipLayout.h"
#include <cstdio>
#include <vector>
using namespace miaodesk::library_ui;

static void dump(const char* name, std::int32_t count, bool lastWide, std::int32_t gap,
                 std::int32_t span) {
    std::vector<std::int32_t> w;
    for (std::int32_t i = 0; i < count; ++i) w.push_back(i == 0 ? 58 : 68);
    if (lastWide && count > 1) w[count - 1] = 88;
    const auto l = ResolveFilterChipLayout(w.data(), count, gap, span);
    std::printf("%s rows=%d used=%d chips=", name, l.rows, l.usedWidth);
    for (std::int32_t i = 0; i < count; ++i)
        std::printf("%d:%d,%d ", i, l.column[i], l.row[i]);
    std::printf("band=%d\\n", FilterChipBandHeight(l, 30, gap, 9));
}

int main() {
    // The exact spans the library can produce, from its own layout arithmetic:
    // width - sidebarW(208) - margin*2, margin = max(12, (width-208)*16/1000).
    struct Span { const char* name; std::int32_t span; };
    const Span spans[] = {
        // minimum tracked window (820), then the widths where the old row fitted
        {"min820", 588}, {"w900", 668}, {"w1000", 768}, {"w1100", 864},
        // a hostile case: half the chips
        {"half", 330},
        // narrower than one chip
        {"tiny", 40},
    };
    for (const auto& s : spans) {
        for (const bool lastWide : {false, true}) {
            char label[64];
            std::snprintf(label, sizeof(label), "%s/wide=%d", s.name, lastWide ? 1 : 0);
            dump(label, 8, lastWide, 8, s.span);
        }
    }
    // the two new chips the library still owes
    dump("nine/wide=1", 9, true, 8, 588);
    dump("nine/wide=0", 9, false, 8, 588);
    dump("ten", 10, true, 8, 588);
    // gap and chip-count edge cases
    dump("one-chip", 1, false, 8, 588);
    dump("zero-span", 8, false, 8, 0);
    dump("negative-count", -3, false, 8, 588);
    dump("zero-width-chip", 8, false, 0, 588);
    return 0;
}
`;

const work = fs.mkdtempSync("/tmp/miaodesk-chips-");
const src = path.join(work, "chips.cpp");
fs.writeFileSync(src, program);
const bin = path.join(work, "chips");
execFileSync("clang++", ["-std=c++23", "-O1", "-I", path.dirname(header), src, "-o", bin], {
  stdio: ["ignore", "pipe", "pipe"],
});
const out = execFileSync(bin, [], { encoding: "utf8" });

const parse = (line) => {
  // A zero-chip case prints "chips=" with nothing between it and " band=", which the
  // trailing \s* below absorbs. Being permissive here matters: a strict parser makes the
  // degenerate case look like a harness failure instead of an input case.
  const m = line.match(/^(\S+) rows=(-?\d+) used=(-?\d+) chips=(.*?)\s*band=(-?\d+)$/);
  assert.ok(m, `unparseable harness output: ${line}`);
  const chips = m[4]
    .trim()
    .split(" ")
    .filter(Boolean)
    .map((c) => {
      const [i, rest] = c.split(":");
      const [col, row] = rest.split(",").map(Number);
      return { i: Number(i), col, row };
    });
  return { name: m[1], rows: Number(m[2]), usedWidth: Number(m[3]), chips, band: Number(m[5]) };
};

// Keys are exactly the labels the harness prints. Getting these wrong silently skips
// the case, which is how the first version of this test ended up checking 4 of 16 rows.
const SPAN_FOR = {
  "min820/wide=0": 588, "min820/wide=1": 588,
  "w900/wide=0": 668, "w900/wide=1": 668,
  "w1000/wide=0": 768, "w1000/wide=1": 768,
  "w1100/wide=0": 864, "w1100/wide=1": 864,
  "half/wide=0": 330, "half/wide=1": 330,
  "tiny/wide=0": 40, "tiny/wide=1": 40,
  "nine/wide=1": 588, "nine/wide=0": 588, ten: 588,
  "one-chip": 588, "zero-span": 0, "negative-count": 588, "zero-width-chip": 588,
};

let checked = 0;
for (const line of out.trim().split("\n")) {
  const layout = parse(line);
  const span = SPAN_FOR[layout.name];
  if (span === undefined) continue;
  const GAP = layout.name === "zero-width-chip" ? 0 : 8;

  // 1. Nothing is ever placed past the right edge -- the original bug. A chip wider than
  //    the whole span is the one exception, and it still gets its own row rather than
  //    being dropped or clamped to zero (the library cannot choose a narrower chip).
  if (span > 0 && layout.name !== "zero-span") {
    for (const chip of layout.chips) {
      const w = chipWidths(layout.chips.length, layout.name.endsWith("wide=1"))[chip.i];
      if (w > span) continue;
      assert.ok(chip.col + w <= span,
        `${layout.name}: chip ${chip.i} ends at ${chip.col + w}, past the ${span}px span`);
    }
  }

  // 2. Rows are contiguous and ordered: skipping a row would punch a hole in the band.
  const rowsUsed = [...new Set(layout.chips.map((c) => c.row))].sort((a, b) => a - b);
  assert.deepStrictEqual(rowsUsed, rowsUsed.map((_, i) => i),
    `${layout.name}: rows must be contiguous from 0, got ${rowsUsed.join(",")}`);
  // With no chips at all there is no row to count, and reporting 1 is the safe answer:
  // it keeps every downstream height calculation positive.
  if (layout.chips.length > 0) {
    assert.equal(layout.rows, rowsUsed.length, `${layout.name}: reported rows must match chips`);
  }

  // 3. No two chips on the same row overlap, and they keep the gap.
  for (const row of rowsUsed) {
    const inRow = layout.chips.filter((c) => c.row === row).map((c) => ({
      i: c.i, col: c.col, w: chipWidths(layout.chips.length, layout.name.endsWith("wide=1"))[c.i],
    }));
    inRow.sort((a, b) => a.col - b.col);
    for (let i = 1; i < inRow.length; ++i) {
      assert.ok(inRow[i].col >= inRow[i - 1].col + inRow[i - 1].w + GAP,
        `${layout.name}: chips ${inRow[i - 1].i} and ${inRow[i].i} overlap or lose the gap`);
    }
  }

  // 4. Greedy-first-fit: a chip must never start a new row while it still fit on the
  //    previous one. Otherwise the band grows for no reason.
  for (const chip of layout.chips) {
    if (chip.row === 0) continue;
    const w = chipWidths(layout.chips.length, layout.name.endsWith("wide=1"))[chip.i];
    const previousRow = chipWidths(layout.chips.length, layout.name.endsWith("wide=1"))
      .slice(0, chip.i)
      .filter((_, idx) => layout.chips[idx].row === chip.row - 1);
    // widths of the previous row + the gaps between them + this gap + this chip
    const previousRowFit =
      previousRow.reduce((sum, cw) => sum + cw, 0) + previousRow.length * GAP + w;
    assert.ok(previousRowFit > span || chip.i === 0,
      `${layout.name}: chip ${chip.i} wrapped although it still fit on row ${chip.row - 1}`);
  }

  // 5. The band the layout reports must actually contain the rows it placed, with the
  //    padding, and nothing more -- that height is what pushes the grid down.
  if (span > 0 && layout.name !== "zero-span") {
    const expected = layout.rows * 30 + (layout.rows - 1) * GAP + 9;
    assert.equal(layout.band, expected,
      `${layout.name}: band height ${layout.band} != ${expected} for ${layout.rows} rows`);
  }

  // 6. usedWidth is the width of the widest row, which is what a caller would need to
  //    know how much of the band is used. A type check alone would let it be a constant.
  if (layout.chips.length > 0) {
    const widthsFor = chipWidths(layout.chips.length, layout.name.endsWith("wide=1"));
    const perRow = {};
    for (const chip of layout.chips) {
      const row = chip.row;
      perRow[row] = Math.max(perRow[row] ?? 0, chip.col + widthsFor[chip.i]);
    }
    const widest = Math.max(...Object.values(perRow));
    assert.equal(layout.usedWidth, widest,
      `${layout.name}: usedWidth ${layout.usedWidth} != widest row ${widest}`);
  } else {
    assert.equal(layout.usedWidth, 0, `${layout.name}: with no chips, nothing is used`);
  }

  // 7. Degenerate inputs must not produce nonsense.
  assert.ok(layout.rows >= 1, `${layout.name}: at least one row`);
  assert.ok(Number.isInteger(layout.usedWidth) && layout.usedWidth >= 0,
    `${layout.name}: used width must be a sane integer`);
  ++checked;
}

assert.ok(checked >= 10, `expected the harness to cover the cases, got ${checked}`);

// The specific regression that started this: at the library's minimum window width the
// old single row ran off the edge, and must now fit.
for (const name of ["min820/wide=0", "min820/wide=1"]) {
  const layout = parse(out.trim().split("\n").find((l) => l.startsWith(name)));
  const wide = name.endsWith("wide=1");
  const right = Math.max(...layout.chips.map((c) => c.col + chipWidths(8, wide)[c.i]));
  assert.ok(right <= 588,
    `${name}: last chip ends at ${right}, which must be within the 588px span the library`
    + ` actually has at its minimum window width`);
  assert.equal(layout.rows, 2, `${name}: eight chips in 588px must take two rows`);
}
// ...and a ninth chip must not make it worse than wrapping again.
{
  const layout = parse(out.trim().split("\n").find((l) => l.startsWith("nine/wide=1")));
  const right = Math.max(...layout.chips.map((c) => c.col + chipWidths(9, true)[c.i]));
  assert.ok(right <= 588, `nine/wide=1: must still fit, ends at ${right}`);
  assert.equal(layout.rows, 2, `nine/wide=1: two rows`);
}

// The window must use the shared, pure geometry -- not a second copy of the loop.
assert.match(library, /library_ui::ResolveFilterChipLayout\(\s*\n?\s*widths, count,/,
  "the library must call the pure resolver with the row's own chip count. Passing a"
  + " literal there (1, or the first row's count) places only that many chips and leaves"
  + " the rest wherever they were, which is invisible until someone adds a chip.");
for (const row of ["kWallpaperFilterCount", "kWidgetFilterCount"]) {
  assert.ok(library.includes(`Chips.data(), ${row},`),
    `the ${row} row must hand the wrapping helper its own chip count -- a hardcoded`
    + ` count there would place only that many chips and leave the rest where they were`);
}
assert.match(library, /placeChipRow\(wallpaperFilters\.data\(\)/,
  "the wallpaper row must place every chip through the wrapping helper");
assert.match(library, /placeChipRow\(widgetFilters\.data\(\)/,
  "the widget row must place every chip through the wrapping helper");
assert.doesNotMatch(library, /chipX \+= /, "the old non-wrapping accumulation must be gone");
assert.match(library, /const int categoryH = \(installed \|\| widgets\) \? std::max\(S\(48\), chipBandH\) : 0;/,
  "the band height must grow from the chip rows, so nothing below the chips is overlapped");
assert.doesNotMatch(library, /const int categoryH = \(installed \|\| widgets\) \? S\(48\) : 0;/,
  "the band must not be a fixed S(48) any more");

console.log(`library filter chip layout (compiled + executed): PASS (${checked} cases)`);
