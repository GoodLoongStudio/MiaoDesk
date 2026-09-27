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

// And the callback path must still be honest about what it can and cannot know.
assert.match(apply, /Void callback: the engine owns its own status/,
  "the void-callback path must be marked as unable to report success");

console.log("library apply reports the real result: PASS");
