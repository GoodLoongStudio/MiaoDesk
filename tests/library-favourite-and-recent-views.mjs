import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const library = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");
const store = read("src/include/miaodesk/WallpaperLibrary.h");

// Two library views had their data maintained on every apply and no way to read it:
//
//   - the star. SetFavorite has been reachable since the library grew one (title-bar
//     button plus a context-menu entry), and WallpaperLibraryItem carries `favorite`, but
//     nothing filtered on it -- so a user could mark wallpapers and then had no way to
//     see just those;
//   - recency. lastUsedUnixSeconds is stamped by every apply path (WallpaperService's
//     ApplyLibraryItem / AssignLibraryItemToMonitor, fixed earlier this round, plus the
//     engine's own path), and RecentlyUsed() exists with a self-test, but no UI called it.
//
// Both were waiting on one prerequisite: the filter row did not wrap, so it could not
// take two more chips. That is fixed separately (LibraryFilterChipLayout.h).

// --- the two chips exist ------------------------------------------------
assert.match(library, /constexpr int kWallpaperFilterCount = 10;/,
  "the wallpaper filter row must have grown to ten chips");
const labelBlock = library.slice(
  library.indexOf("constexpr std::array<const wchar_t*, kWallpaperFilterCount> kWallpaperFilterLabels{{"),
  library.indexOf("}};", library.indexOf("kWallpaperFilterLabels{{"))
);
const chipLabels = [...labelBlock.matchAll(/L"([^"]+)"/g)].map((m) => m[1]);
assert.equal(chipLabels.length, 10, `ten chips, got ${chipLabels.length}: ${chipLabels.join(",")}`);
assert.ok(chipLabels.includes("收藏"), "the star needs a chip that reads it back");
assert.ok(chipLabels.includes("最近使用"), "recency needs a chip");
// The order matters: these are filter indices, and index 0 is 全部.
assert.equal(chipLabels[0], "全部", "the first chip must stay 全部 (index 0 means no filter)");
assert.equal(chipLabels[8], "收藏", "收藏 must be index 8, which is what MatchesWallpaperFilter switches on");
assert.equal(chipLabels[9], "最近使用", "最近使用 must be index 9");

// --- the predicates read the data ---------------------------------------
assert.match(library, /case 8: \/\/ 收藏\s*\n\s*return item\.favorite;/,
  "the favourite chip must filter on the flag the star writes");
assert.match(library, /case 9: \/\/ 最近使用\s*\n\s*return item\.lastUsedUnixSeconds > 0;/,
  "the recency chip must filter on the timestamp every apply stamps");
assert.doesNotMatch(library, /case 8:[^}]*ContainsAny/,
  "the favourite chip must not be a text-match filter -- favouriting is explicit, and a"
  + " keyword heuristic would silently include wallpapers the user never starred");

// --- the write paths are intact -----------------------------------------
// A read-only chip on a flag nothing writes would be a dead view.
// Both entry points must survive: the title-bar button and the context-menu entry. A
// read view over a flag nothing writes is a permanently empty view.
assert.match(library, /id == kFavoriteId && notification == BN_CLICKED\) self->ToggleFavorite\(\);/,
  "the star button must still route to ToggleFavorite");
assert.match(library, /id == kMenuFavorite\) self->ToggleFavorite\(\);/,
  "the context-menu favourite entry must still route to ToggleFavorite");
assert.match(library, /->SetFavorite\(/,
  "and ToggleFavorite must actually write the flag through the library's SetFavorite");
assert.match(store, /bool SetFavorite\(std::wstring_view id, bool favorite/,
  "WallpaperLibrary must keep offering SetFavorite");
assert.match(store, /bool MarkUsed\(std::wstring_view id/, "...and MarkUsed");
assert.match(library, /kMenuFavorite/, "the context menu's favourite entry must stay");

// --- recency is ordered, and only there --------------------------------
assert.match(library, /if \(wallpaperFilterIndex == 9\) \{[\s\S]*?std::stable_sort\(/,
  "the recency view must order by recency -- in library order it is not a recency view");
assert.match(library, /a\.lastUsedUnixSeconds > b\.lastUsedUnixSeconds/,
  "...newest first");
assert.match(library, /std::stable_sort\(/,
  "the sort must be stable so wallpapers used equally keep their library order");

// The default view must NOT be reordered: that would move every wallpaper under users who
// never asked for a recency view.
const refresh = library.slice(
  library.indexOf("void RefreshWallpapers() {"),
  library.indexOf("\n    }", library.indexOf("void RefreshWallpapers() {"))
);
const sortAt = refresh.indexOf("std::stable_sort");
const guardAt = refresh.indexOf("if (wallpaperFilterIndex == 9)");
assert.ok(guardAt !== -1 && sortAt > guardAt,
  "the sort must be guarded by the recency chip -- an unguarded sort changes 全部 too");
// The paint path walks the visible list in order and has no second ordering, so whatever
// RefreshWallpapers produced is what the user sees.
assert.match(library, /else DrawWallpaperCard\(dc, i, card\);/,
  "the card loop must paint in the list's order, with no second sort at paint time");

// --- an empty result must not claim the library is empty -----------------
// 收藏 is empty for a brand-new user, so a wrong "桌面库为空" here sends them off to
// import wallpapers they already have.
const emptyBlock = library.slice(
  library.indexOf("if (count == 0) {"),
  library.indexOf("SelectObject(dc, old);", library.indexOf("if (count == 0) {"))
);
assert.match(emptyBlock, /library->Items\(\)\.empty\(\)/,
  "the empty message must check whether the LIBRARY is empty, not just the grid");
assert.match(emptyBlock, /当前筛选下没有壁纸/,
  "when only the filter matched nothing, it must say so and point back at 全部");
assert.match(emptyBlock, /没有匹配「/,
  "and when the search matched nothing, quote the query -- the user cannot fix what they"
  + " are not told was searched for");
assert.doesNotMatch(emptyBlock, /^const wchar_t\* empty = L"桌面库为空/,
  "the message must no longer be a single unconditional string");
// The search case must beat the filter case: with a query in the box, the mismatch is the
// query's fault and the filter chip is not what the user needs to look at.
const searchGuard = emptyBlock.indexOf("WindowText(search).empty()");
const filterMessage = emptyBlock.indexOf("当前筛选下没有壁纸");
assert.ok(searchGuard !== -1 && searchGuard < filterMessage,
  "the empty text must branch on the search box FIRST, then the filter -- otherwise an"
  + " empty search tells the user to press 全部, which cannot help them");

console.log("library favourite + recency views read back what was written: PASS");
