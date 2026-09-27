import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");

// Owner-drawn controls are the blind spot of this app's keyboard work.
//
// Every other control repaints itself: an EDIT shows its caret, a listbox highlights its
// selection, a standard button paints a focus frame. An owner-drawn control paints
// nothing at all unless the code that owns the drawing does it -- and the only signal it
// receives is ODS_FOCUS in the DRAWITEMSTRUCT.
//
// This matters more now than it did before: until this round none of these surfaces
// could even be tabbed to, so a missing focus cue was invisible. Now that the dialog
// manager walks them, a keyboard user tabs across a wall of custom-drawn buttons with
// no idea which one Enter is about to press. LAY-02 asks for 焦点可见性, and this is it.

const read = (p) => fs.readFileSync(path.join(root, p), "utf8");

// Each entry: a file, the owner-draw function inside it, and how many painting paths that
// function has. `paths` counts independent code paths that each paint the control, and
// each one needs its own cue -- the preview pane, for instance, draws a live render on
// one branch and a placeholder on another, and only the last branch is reached by falling
// off the end.
const OWNER_DRAWN = [
  // The AI configuration page: 16 controls, all WS_TABSTOP, the buttons and the profile
  // list owner-drawn.
  { file: "src/ui/settings/DesktopAiSettingsPage.cpp", draw: "DrawActionButton" },
  { file: "src/ui/settings/DesktopAiSettingsPage.cpp", draw: "DrawProfileItem" },
  // The creator's send/apply buttons, its five preset chips, and its clickable preview.
  { file: "src/ui/ai/ContentCreatorDialog.cpp", draw: "DrawPrimaryAction" },
  { file: "src/ui/ai/ContentCreatorDialog.cpp", draw: "DrawPresetChip" },
  { file: "src/ui/ai/ContentCreatorDialog.cpp", draw: "DrawPreviewPane", paths: 2 },
  // The settings centre: its primary action, its nav rail and its filter chips.
  { file: "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp", draw: "DrawPrimaryButton" },
  { file: "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp", draw: "DrawNavButton" },
  { file: "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp", draw: "DrawContentFilterButton" },
];

const bodyOf = (text, signature) => {
  const at = text.indexOf(signature);
  assert.notStrictEqual(at, -1, `cannot find ${signature}`);
  // Walk braces from the opening brace of the function.
  const open = text.indexOf("{", at);
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === "{") ++depth;
    else if (text[i] === "}") {
      --depth;
      if (depth === 0) return text.slice(open, i + 1);
    }
  }
  assert.fail(`unbalanced braces after ${signature}`);
};

for (const { file, draw, paths = 1 } of OWNER_DRAWN) {
  const text = read(file);
  const exists = new RegExp(`${draw}\\s*\\(`);
  assert.ok(exists.test(text),
    `${file}: ${draw} must still exist -- if it was renamed, this entry is stale`);

  const body = bodyOf(text, draw);

  // The cue itself. A check that only looks at ODS_FOCUS would pass for a function that
  // reads the flag and does nothing with it, so require the drawing call too -- and one
  // per painting path, since each path returns to Windows on its own.
  const cues = [...body.matchAll(/DrawFocusRect\s*\(/g)].length;
  assert.match(body, /itemState\s*&\s*ODS_FOCUS/,
    `${file}:${draw} must test ODS_FOCUS -- an owner-drawn control paints no focus cue on`
    + ` its own, so a keyboard user cannot see where focus is`);
  assert.ok(cues >= paths,
    `${file}:${draw} has ${paths} painting path(s) but only ${cues} DrawFocusRect call(s).`
    + ` One cue at the end of the function is not enough when another branch returns early;`
    + ` the branch that returns is the one the user is looking at.`);

  // A function that has a mid-body early return needs the cue on that branch too, not
  // merely the same number of cues somewhere in the body.
  const earlyReturn = /\n\s+return\s*;/.test(body);
  if (earlyReturn) {
    assert.equal(cues, paths,
      `${file}:${draw} returns early from one path and paints there too -- it needs exactly`
      + ` its ${paths} cues distributed, not ${cues} of them piled at the end`);
  }
}

// The files that draw must actually reach these paths: a cue in dead code is not a cue.
for (const file of new Set(OWNER_DRAWN.map((entry) => entry.file))) {
  assert.match(read(file), /case WM_DRAWITEM:/,
    `${file} must still dispatch WM_DRAWITEM, or the owner-draw paths above never run`);
}

// And the controls must be tab stops, which is what makes the cue necessary at all.
assert.match(read("src/ui/settings/DesktopAiSettingsPage.cpp"), /WS_TABSTOP/,
  "the AI page's controls must remain tab stops");
assert.match(read("src/ui/ai/ContentCreatorDialog.cpp"), /SS_OWNERDRAW \| SS_NOTIFY/,
  "the creator's preview pane must stay the owner-drawn click target");
assert.match(read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp"),
  /WS_CHILD \| WS_TABSTOP \| BS_PUSHBUTTON/,
  "the library window's button factory must keep adding WS_TABSTOP, so its owner-drawn"
  + " buttons are reachable");

console.log(`owner-drawn tab stops paint a focus cue: PASS (${OWNER_DRAWN.length} paths)`);
