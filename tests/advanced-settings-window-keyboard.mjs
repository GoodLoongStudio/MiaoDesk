import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const engine = read("src/desktop/wallpaper/legacy/WallpaperEngine.cpp");

// The advanced settings window (壁纸 / 显示器 / 性能) was the fourth surface WallpaperEngine's
// pump forgot. It is created inline in this translation unit, has ~20 WS_TABSTOP controls
// (scene, layout, scale, focal X/Y, fps, fullscreen/maximized action, loop, mute, volume,
// rate and the seek buttons) and is opened from the library nav, so a keyboard user could
// not reach a single one of them: Tab did nothing at all. It also had no Esc.
//
// The generic invariant in esc-answers-every-dialog-surface.mjs covers the set of surfaces
// and their Esc handlers. This file covers the two things it cannot see: that the pump's
// loop actually still names this window, and that Esc does the same thing the window's own
// close path does rather than merely returning.

// --- the pump must still serve it ----------------------------------------
const run = engine.slice(
  engine.indexOf("int Run() {"),
  engine.indexOf("\n    }\n", engine.indexOf("int Run() {"))
);
assert.match(run, /const HWND surfaces\[\] = \{[^}]*settings_/,
  "Run()'s pump must serve the advanced settings window. It is declared in this same"
  + " translation unit, which is exactly why it was missed: the other three surfaces are"
  + " objects with a Window() accessor, and this one is a bare HWND member.");
assert.match(run, /for \(const HWND surface : surfaces\)/,
  "the loop must walk the array, so a surface cannot be served by a call that was never made");

// SettingsProc's body: from its signature to the start of the next member at the same
// indentation. Taking the first closing brace would stop inside the WM_NCCREATE block.
const settingsStart = engine.indexOf("static LRESULT CALLBACK SettingsProc(");
const settingsEnd = engine.indexOf("\n    void ", settingsStart);
assert.ok(settingsStart !== -1 && settingsEnd > settingsStart, "cannot locate SettingsProc");
const settingsText = engine.slice(settingsStart, settingsEnd);
assert.match(settingsText, /case IDCANCEL:/,
  "the settings window must answer IDCANCEL, which is what the dialog manager posts for Esc");
// This window HIDES on close -- both its 关闭 button and WM_CLOSE. So Esc must hide too. My
// first attempt made it DestroyWindow, which would have been a second, different action on
// the same intent: it throws away the control state the window is kept for, and does it
// only when the user pressed Esc.
const hideCalls = [...settingsText.matchAll(/miaodesk::surface_focus::HideSurface\(/g)];
assert.equal(hideCalls.length, 3,
  `all three close routes (Esc, the 关闭 button and WM_CLOSE) must take the same hide path;`
  + ` found ${hideCalls.length}. Divergence here is exactly how two surfaces in one file end`
  + ` up answering Esc differently.`);
assert.doesNotMatch(settingsText, /case IDCANCEL:[\s\S]*?DestroyWindow\(hwnd\);/,
  "Esc must NOT destroy this window -- it keeps its control state by hiding");
assert.doesNotMatch(settingsText, /case kCloseButtonId:[\s\S]*?DestroyWindow\(hwnd\);/,
  "nor may the 关闭 button");
// The helper exists to hand the keyboard back; hiding a window that holds focus would
// otherwise drop it into the Z order's next window, frequently the desktop.
assert.match(settingsText, /HideSurface\(hwnd, self->libraryWindow_\.Window\(\)\)/,
  "each hide must name the surface to hand the keyboard back to (the library window is the"
  + " only one that opens this)");
assert.match(settingsText, /message == WM_DESTROY\) \{[\s\S]*?self->settings_ = nullptr;/,
  "the destroy path must still clear the HWND for the case where something else destroys"
  + " the window -- otherwise the pump keeps serving a dead HWND");
assert.match(engine, /if \(settings_ && IsWindow\(settings_\)\) \{\s*\n\s*ShowWindow\(settings_, SW_RESTORE\);/,
  "reopening must reuse a live window and show a destroyed one again");

// The dialog manager must be safe here: nothing in this WndProc may compete for the keys it
// routes. A custom VK_RETURN would make Enter ambiguous; a custom VK_TAB would fight it.
assert.doesNotMatch(settingsText, /VK_RETURN|VK_TAB|WM_GETDLGCODE/,
  "the settings window must stay keyboard-neutral -- Enter, Tab and the arrow keys belong"
  + " to the focused control, exactly as they do in a standard dialog");
assert.doesNotMatch(settingsText, /BS_DEFPUSHBUTTON/,
  "no default pushbutton, so Enter in a combo box or checkbox is not hijacked");

// There must be tab stops worth reaching, or none of this matters. They are created in
// CreateUi, which is a different member from SettingsProc.
// The window is created inline inside ShowAdvancedSettings(), so the tab stops live there
// rather than in a member of their own.
const showStart = engine.indexOf("void ShowAdvancedSettings() {");
assert.ok(showStart !== -1, "cannot find ShowAdvancedSettings");
const createEnd = engine.indexOf("\n    }\n\n", showStart);
const createText = engine.slice(showStart, createEnd);
const tabStops = (createText.match(/WS_TABSTOP/g) || []).length;
assert.ok(tabStops >= 15,
  `expected the settings window's ~20 tab stops, found ${tabStops}`);

// The window must not be a duplicate of the library window's class.
assert.match(engine, /lpszClassName = kWallpaperSettingsClass;/, "its own class registration");

console.log("advanced settings window: reachable by keyboard, and Esc closes it: PASS");
