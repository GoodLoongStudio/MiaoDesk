import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const engine = read("src/desktop/wallpaper/legacy/WallpaperEngine.cpp");
const automation = read("src/ui/automation/WallpaperAutomationWindow.cpp");
const rules = read("src/ui/automation/WallpaperApplicationRulesWindow.cpp");
const automationHeader = read("src/include/miaodesk/WallpaperAutomationWindow.h");
const rulesHeader = read("src/include/miaodesk/WallpaperApplicationRulesWindow.h");
const helper = read("src/include/miaodesk/SurfaceKeyboardFocus.h");

// The automation window and its application-rules window are whole settings
// surfaces -- profiles, playlists, schedules, seven day checkboxes, an EXE field
// and rule priority -- built out of WS_TABSTOP combos, edits, checkboxes and
// buttons, and until this change a keyboard user could not reach a single field in
// either of them. Closing them also dropped the keyboard: both close by hiding, and
// the window holding focus (the 关闭 button that was just clicked) is the one that
// disappears, so Windows hands focus to whatever comes next in the Z order.

// --- the pump -------------------------------------------------------------
// IsDialogMessageW is the dialog manager: it walks tab stops, gives a combo box its
// arrow keys and routes Esc. Without it in the pump, Tab in these windows does
// nothing at all.
const run = engine.slice(
  engine.indexOf("    int Run() {"),
  engine.indexOf("\n    }\n", engine.indexOf("    int Run() {")) + 6
);

const surfaces = [...run.matchAll(/IsDialogMessageW\(surface, &msg\)/g)];
assert.equal(surfaces.length, 1,
  "the pump must call IsDialogMessageW once, over each surface in turn -- a second"
  + " per-message call would consume a message twice");
assert.match(run, /const HWND surfaces\[\] = \{[^}]*libraryWindow_\.Window\(\)[^}]*automationWindow_\.Window\(\),[^}]*automationWindow_\.RulesWindow\(\)/,
  "the pump must serve the library window and BOTH automation surfaces -- the"
  + " automation window and its rules window are the same shape and were equally"
  + " unreachable by keyboard");
assert.match(run, /for \(const HWND surface : surfaces\)/,
  "the loop must be written over the array, not unrolled by copy-paste -- three"
  + " copies of the same call is how one of them ends up without the guard");
assert.match(run, /if \(IsWindow\(surface\) && IsDialogMessageW\(surface, &msg\)\)/,
  "every surface needs the IsWindow guard: all three Window() accessors return a raw"
  + " HWND that is null before creation and after destruction, and a null handle must"
  + " never reach the dialog manager");
assert.match(run, /break;/,
  "once a surface consumes the message the loop must stop; the same message must not"
  + " be offered to the remaining surfaces");

// --- keyboard close, identical to the 关闭 button ------------------------
for (const [name, text] of [["automation", automation], ["rules", rules]]) {
  assert.match(text, /if \(message == WM_KEYDOWN && wParam == VK_ESCAPE\) \{/,
    `${name}: Esc must be handled explicitly -- this whole surface is tab stops, and`);

  // Esc and the 关闭 button must take the same path, or they drift apart.
  const hides = [...text.matchAll(/surface_focus::HideSurface\(hwnd, self->restoreFocus\)/g)];
  assert.equal(hides.length, 4,
    `${name}: all four hide routes -- Esc (WM_KEYDOWN), the 关闭 button, the`
    + ` dialog manager's IDCANCEL command, and WM_CLOSE -- must go through HideSurface,`
    + ` so none of them can hide the window while it holds focus. Expected 4 call sites,`
    + ` found ${hides.length}`);

  assert.doesNotMatch(text, /ShowWindow\(hwnd, SW_HIDE\)/,
    `${name}: no route may still hide the window directly -- that is the bug (hiding a`
    + ` window that owns the keyboard focus drops focus into the Z order's next window,`);

  assert.match(text, /id == IDCANCEL/,
    `${name}: WM_COMMAND must carry IDCANCEL as well, because the dialog manager posts`);

  assert.match(text, /#include "miaodesk\/SurfaceKeyboardFocus\.h"/,
    `${name}: must use the shared helper rather than a private copy, so the two surfaces`);

  assert.match(text, /HWND restoreFocus\{\};/,
    `${name}: Impl must record which surface opened it (restoreFocus), default-initialised`
    + ` to null so Show() being called without one is survivable`);

  assert.doesNotMatch(text, /BS_DEFPUSHBUTTON/,
    `${name}: no default pushbutton, so Enter in an EDIT is not hijacked by the dialog manager`);
}

// --- the helper itself ---------------------------------------------------
// The whole point is the conditional: restoring focus that was never ours would move
// the keyboard away from a control the user was actually using.
assert.match(helper, /const bool heldKeyboard = GetFocus\(\) == surface;/,
  "the helper must remember whether the keyboard was ours BEFORE hiding");
assert.match(helper, /ShowWindow\(surface, SW_HIDE\);\s*\n\s*if \(heldKeyboard\)/,
  "the restore must be conditional on the window having held focus");
assert.match(helper, /if \(!IsWindow\(target\) \|\| !IsWindowVisible\(target\)\) return;/,
  "a stale or hidden owner must not receive focus -- restore targets are raw HWNDs");
assert.match(helper, /WS_TABSTOP\) && \(style & WS_VISIBLE\) && IsWindowEnabled\(control\)/,
  "the walk must stop on the first tab stop that is visible and enabled, not the first"
  + " child (which is usually a static label) and not a disabled control");

// --- the restore target is actually threaded through ---------------------
assert.match(automationHeader, /HWND restoreFocus = nullptr/,
  "automation Show(): the caller must be able to name the surface to return keyboard to");
assert.match(rulesHeader, /HWND restoreFocus = nullptr/,
  "rules Show(): same");
assert.match(automation, /impl_->restoreFocus = restoreFocus;/,
  "automation Show(): the restore target must be recorded, not just accepted as a parameter");
assert.match(rules, /impl_->restoreFocus = restoreFocus;/,
  "rules Show(): the restore target must be recorded");
assert.match(automation, /self->applicationRulesWindow\.Show\(self->instance, hwnd\);/,
  "the rules window is opened from the automation window, so the automation window is"
  + " its restore target");
assert.match(engine, /libraryWindow_\.Window\(\)\);\s*\n\s*\}/,
  "ShowAutomation must pass the library window, which is what opened it -- a nullptr"
  + " here would leave the pump's focus work undone on the most-used path");

console.log("automation surfaces: keyboard navigation + focus on close: PASS");
