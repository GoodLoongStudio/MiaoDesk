import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const lib = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");

// Applying a wallpaper is the product's core action, and ApplySelected logged the
// result and then ignored it: it always ended in SetStatus("已应用到桌面：…") whether
// or not the apply succeeded, so a failure looked exactly like a success and left
// nothing in the status line to act on. It also called MarkUsed unconditionally,
// which advances the usage order that RecentlyUsed ranks by -- recording a wallpaper
// the user failed to apply.
//
// The sibling ToggleWallpaper already used the result message, so this was the
// outlier rather than the house style.

const apply = lib.slice(
  lib.indexOf("void ApplySelected() {"),
  lib.indexOf("\n    }\n", lib.indexOf("void ApplySelected() {")) + 6
);

assert.match(apply, /applied = result\.success;/,
  "ApplySelected must record whether the apply actually succeeded");
assert.match(apply, /if \(!applied\) \{[\s\S]*?SetStatus\(failure\.empty\(\) \? L"应用壁纸失败。" : failure\);/,
  "a failed apply must report the real failure, not a fixed success string");
assert.match(apply, /if \(!applied\) \{[\s\S]*?MessageBeep\(MB_ICONERROR\)/,
  "a failed apply must be audible, not silent");
assert.match(apply, /if \(applied && library\) library->MarkUsed/,
  "the usage stamp must not advance for an apply that failed");
assert.match(apply, /return;/,
  "the success status must not be reached after a failure");

// The fixed success string must now be guarded, not unconditional.
assert.doesNotMatch(apply, /RefreshWallpapers\(\);\s*\n\s*SetStatus\(L"已应用到桌面/,
  "the success status must come after the failure branch, not straight after the refresh");

// The callback path used to be a hole here, and it was documented as one: the comment
// said the void callback meant the status line was "not evidence of success on this
// path -- the engine's own window is". That was true, and it was the defect. On a
// bail-out the engine wrote its reason into libraryError_, which only the advanced
// settings window reads -- a different page the user has to navigate to -- so the click
// got "已应用到桌面" and a MarkUsed stamp while nothing had been applied.
//
// The callback now returns the outcome. See library-apply-reports-outcome.mjs for the
// contract itself and the engine's side; this file covers that ApplySelected reads it.
assert.doesNotMatch(apply, /Void callback/,
  "the void-callback path must be gone -- a void callback is exactly what made the"
  + " status line a false success on the engine path");
assert.match(apply, /failure = applyCallback\(\*selected, targetId\);/,
  "the callback's return must be captured");
assert.match(apply, /applied = failure\.empty\(\);/,
  "and `applied` must follow from it, so a bail-out is a failure here too");
// Both paths must land on the same reporting, or one of them is the silent one again.
// The initialiser is excluded: `bool applied = true;` on the declaration line is not an
// assignment to a result, and counting it would make this test pass for the wrong reason.
const assignments = [...apply.matchAll(/(?<!bool )applied = ([^;]+);/g)].map((m) => m[1].trim());
assert.equal(assignments.length, 2,
  `both the callback and the DesktopControl path must set \`applied\` from a result;`
  + ` found ${assignments.length}: ${assignments.join(", ")}`);
for (const rhs of assignments) {
  assert.notEqual(rhs, "true",
    "`applied` may never be assigned the constant true -- that is the false success in one"
    + " token, and it is what both paths used to do");
}
assert.match(apply, /applied = result\.success;/,
  "the DesktopControl fallback path must still read its own result");

console.log("library apply reports the real result: PASS");
