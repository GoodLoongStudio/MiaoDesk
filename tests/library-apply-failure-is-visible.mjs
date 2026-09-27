import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const library = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");

// Applying is this window's core action, and its failure used to be silent on one path.
//
// The two entry points that could be disabled were: 应用到桌面 is EnableWindow(FALSE)
// and the context-menu 应用到桌面 is MF_GRAYED, both when SourceMissing(item). So the
// guard that produces the silence looked unreachable. It is not: double-clicking a card
// is a third entry point, gated by nothing, and a card whose source is missing is
// stamped 不可用 on its face -- so clicking it is exactly what a user does next.
//
// What happened: log line, return. No status, no beep. From the outside the click was
// simply dropped, and the card that looked selected stayed that way.

const bodyOf = (text, signature) => {
  const at = text.indexOf(signature);
  assert.notStrictEqual(at, -1, `cannot find ${signature}`);
  const open = text.indexOf("{", at);
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === "{") ++depth;
    else if (text[i] === "}") {
      if (--depth === 0) return text.slice(open, i + 1);
    }
  }
  assert.fail(`unbalanced braces after ${signature}`);
};

const apply = bodyOf(library, "void ApplySelected() {");

// --- the guard must not collapse the two cases back into one -------------
// `!selected || SourceMissing(*selected)` reads as one condition and therefore produces
// one outcome, which is how both ended up with no feedback at all.
assert.doesNotMatch(apply, /if \(!selected \|\| SourceMissing\(\*selected\)\) \{/,
  "the two cases must not share a branch again: nothing-selected and source-missing have"
  + " different fixes, and one branch means one message at best");
const noSelection = bodyOf(apply, "if (!selected) {");
const missingSource = bodyOf(apply, "if (SourceMissing(*selected)) {");

// --- both must tell the user, the way the failure path below already did --
// The sibling path (`applied == false`, ~40 lines further down) is the precedent: it
// sets a status line and beeps. The guard is the only failure in this function that
// did neither.
for (const [name, body] of [["nothing selected", noSelection], ["source missing", missingSource]]) {
  assert.match(body, /SetStatus\(/,
    `the ${name} branch must write a status line -- this is the only apply failure the user`);
  assert.match(body, /MessageBeep\(MB_ICONERROR\)/,
    `the ${name} branch must beep, so a click that did nothing is audible`);
  assert.match(body, /miaodesk::log::(?:Warn|Error)\(/,
    `the ${name} branch must still log the reason`);
}

// The message has to be actionable, not diagnostic. "资源不存在" repeats what the card
// already said; the missing half is that the fix is to remove and re-import.
assert.match(missingSource, /SetStatus\(L"“" \+ selected->title \+ L"”的资源已不存在/,
  "the message must name the wallpaper the user actually clicked");
assert.match(missingSource, /无法应用；移除后重新导入。/,
  "and must say what to do about it -- the stored entry no longer resolves to a file");
assert.match(missingSource, /selected->id/,
  "and must log which id failed, or the log cannot be matched to a card");

// --- and the path really is reachable ------------------------------------
// If the double-click were gated the way the button is, none of this would matter.
const dblClick = bodyOf(library, "case WM_LBUTTONDBLCLK: {");
const wallpaperBranch = dblClick.slice(dblClick.indexOf("} else {"));
assert.match(wallpaperBranch, /self->ApplySelected\(\);/,
  "double-click must still apply the card the user clicked");
assert.doesNotMatch(wallpaperBranch, /SourceMissing|usable/,
  "and must NOT gate on SourceMissing -- that is the whole reachability argument: the"
  + " button and the menu entry are gated, double-click is not");
// Prove the other two entry points really are gated, so the unguarded one is the only
// route in. If someone later disables double-click too, this is what changes first.
const updateFooter = bodyOf(library, "void UpdateFooter() {");
assert.match(updateFooter, /EnableWindow\(applyButton, usable \? TRUE : FALSE\);/,
  "the apply button must still be disabled when the source is missing -- that is what"
  + " makes the silent double-click the only way in");
assert.match(library, /AppendMenuW\(menu, SourceMissing\(\*selected\) \? MF_STRING \| MF_GRAYED : MF_STRING, kMenuApply/,
  "...and the context-menu entry must still be greyed");

// The double-click branch is the one place that changes the selection without refreshing
// the footer (the widget branch immediately above it does call UpdateFooter). Without it
// the status line keeps naming whichever wallpaper the FIRST click of the pair landed on.
assert.match(wallpaperBranch, /self->selectedWallpaperId = self->visibleWallpapers\[[^\]]*\]\.id;\s*\n\s*\/\/[\s\S]*?self->UpdateFooter\(\);\s*\n\s*self->ApplySelected\(\);/,
  "double-click must refresh the footer before applying, like the widget branch does --"
  + " the second click can land on a different card than the first");

console.log("library apply reports why it did nothing: PASS");
