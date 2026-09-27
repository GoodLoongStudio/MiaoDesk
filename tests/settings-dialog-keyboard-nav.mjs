import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const engine = read("src/desktop/wallpaper/legacy/WallpaperEngine.cpp");
const search = read("src/ui/search/SearchWindow.cpp");
const library = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");

// The settings centre is the wallpaper library window. Every control in it is
// WS_TABSTOP -- the wallpaper search box, the nav and filter buttons, the target
// combo, and the whole API page -- but the only pump serving it had no
// IsDialogMessageW, so a keyboard-only user could not reach a single one of those
// fields. That fails LAY-02's "键盘可完成搜索、配置、预览和应用" outright.

const run = engine.slice(
  engine.indexOf("    int Run() {"),
  engine.indexOf("\n    }\n", engine.indexOf("    int Run() {")) + 6
);

assert.match(run, /IsDialogMessageW\(surface, &msg\)/,
  "the pump must give the library window dialog-manager keyboard navigation");
assert.match(run, /const HWND surfaces\[\] = \{/,
  "the pump must serve its surfaces from one list, so adding a surface later cannot"
  + " silently skip the keyboard path");
assert.match(run, /libraryWindow_\.Window\(\)/,
  "the library / settings window must still be in that list -- it was the first"
  + " surface that needed the dialog manager");
assert.match(run, /if \(IsWindow\(surface\) && IsDialogMessageW\(surface, &msg\)\)/,
  "the guard must check the window exists -- WallpaperLibraryWindow::Window() returns a"
  + " raw HWND that is null before creation and after destruction, and a null dialog"
  + " handle must never reach IsDialogMessageW");
assert.match(run, /TranslateMessage\(&msg\);\s*\n\s*DispatchMessageW\(&msg\);/,
  "messages the dialog does not consume must still reach the normal dispatch");
assert.match(run, /if \(!handled\)/,
  "dispatch must be conditional on the dialog not having consumed the message");

// The library window must be keyboard-neutral, which is what makes this safe: no
// custom key handling to override and no default button to hijack Enter with.
assert.doesNotMatch(library, /VK_RETURN|VK_TAB|WM_GETDLGCODE/,
  "the library window must stay free of its own key handling, or IsDialogMessageW"
  + " would start fighting it");
assert.doesNotMatch(library, /BS_DEFPUSHBUTTON/,
  "the library window must have no default pushbutton, so Enter still reaches the"
  + " control it was typed into");
assert.match(library, /WS_TABSTOP/,
  "there must be tab stops for the dialog manager to walk");

// The search box itself deliberately does NOT get the dialog manager. Its EDIT has its
// own Enter handling that executes the selected result, and IsDialogMessageW would
// take precedence; that surface needs its own decision, recorded in LAY-02. The pump
// does hand the dialog manager to the AI creator surface that dispatches through it --
// see creator-window-keyboard-conformance.mjs -- so the boundary to hold here is
// "never the search window itself", not "never IsDialogMessageW in this file".
const searchPump = search.slice(
  search.indexOf("int SearchWindow::RunMessageLoop() {"),
  search.indexOf("\n    }\n", search.indexOf("int SearchWindow::RunMessageLoop() {")) + 6
);
assert.doesNotMatch(searchPump, /IsDialogMessageW\(hwnd_/,
  "the search window's own HWND must never be handed to the dialog manager -- its"
  + " Enter executes the selected result and would be consumed first");
assert.doesNotMatch(searchPump, /IsDialogMessageW\(edit_/,
  "nor its search EDIT: the IME anchor and its Enter handling both live there");
assert.match(searchPump, /creator::DialogManagedCreatorWindows\(\)/,
  "the creator surface that shares this pump must be served by the dialog manager");
assert.match(search, /WM_CHAR|VK_RETURN/,
  "the search box does have its own input handling, which is why it is excluded");

console.log("settings centre dialog keyboard navigation: PASS");
