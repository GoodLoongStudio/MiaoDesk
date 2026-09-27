import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const api = read("src/ui/settings/DesktopAiSettingsPage.cpp");

// Tab order is Z-order for sibling child windows, and Z-order is creation order unless
// something calls SetWindowPos with a Z flag. So a page that creates its controls in one
// order and positions them in another gets a Tab key that wanders around the page.
//
// On the AI configuration page that is exactly what happened: 16 tab stops were created
// grouped by kind (all the edits, then all the buttons), and then positioned by Layout().
// The result is that a keyboard user visiting the fields top-to-bottom then jumps back up
// to 新增配置 (created 10th, placed first), reaches the bottom action row, and then jumps
// back up a third time to 眼睛/复制/探测模型 -- three buttons that were created dead last
// but sit inline beside the API key and the model field they belong to.

// --- creation order (Z-order, hence Tab order) -----------------------------
const createPage = api.slice(
  api.indexOf("bool CreatePage("),
  api.indexOf("\n}", api.indexOf("bool CreatePage("))
);
const CREATED = [...createPage.matchAll(/state\.(\w+)\s*=\s*(?:edit\(|button\(|CreateWindowExW\()/g)]
  .map((m) => m[1]);
// The panel is the container, not a tab stop; the brush is not a window.
const created = CREATED.filter((name) => name !== "panel");
assert.ok(created.length >= 16, `expected the page's controls to be parsed, got ${created.length}`);
assert.equal(new Set(created).size, created.length, "a control was created twice");

// --- layout order (the order the author reads the page in) -----------------
const layout = api.slice(
  api.indexOf("void Layout() {"),
  api.indexOf("\n    }", api.indexOf("void Layout() {"))
);
const placed = [...layout.matchAll(/\bplace\((\w+)\s*,/g)].map((m) => m[1]);
assert.ok(placed.length >= 16, `expected the layout to place the page's controls, got ${placed.length}`);

// Every control must be laid out, or it is stranded at its 10x10 creation size.
assert.deepStrictEqual([...new Set(placed)].sort(), [...created].sort(),
  "the set of controls that Layout() places is not the set that CreatePage() creates --"
  + " a control missing from Layout() stays at its 10x10 creation rectangle");

// The order must agree. Tab walks creation order; the page is read in layout order.
assert.deepStrictEqual(created, placed,
  "Tab order does not match the page's layout order. For sibling windows the dialog"
  + " manager walks them in creation (Z) order, so this is literally the order the"
  + " keyboard visits them.\n"
  + "  creation: " + created.join(" -> ") + "\n"
  + "  layout:   " + placed.join(" -> "));

// Every SetWindowPos inside Layout() must leave Z-order alone. That is what makes
// "creation order is Tab order" true across relayouts; a single call that moves a control
// to the front would silently reorder the keyboard behind the layout's back.
const zOrdered = [...layout.matchAll(/SetWindowPos\([^;]*;/g)].map((m) => m[0]);
assert.ok(zOrdered.length >= 2, `expected Layout() to position the panel and its controls, got ${zOrdered.length}`);
const reordered = zOrdered.filter((call) => !call.includes("SWP_NOZORDER"));
assert.deepStrictEqual(reordered, [],
  "Layout() must position controls without touching Z-order -- otherwise Tab order changes"
  + " every time the page relayouts:\n  " + reordered.join("\n  "));

// The page must stay dialog-managed, or none of this is reachable by keyboard at all.
// Checked inside the pump, not merely somewhere in the file: the library window's HWND is
// also passed to the automation window as its focus-restore target, so a file-wide match
// would pass with the page no longer pumped at all.
const engine = read("src/desktop/wallpaper/legacy/WallpaperEngine.cpp");
const run = engine.slice(
  engine.indexOf("    int Run() {"),
  engine.indexOf("\n    }\n", engine.indexOf("    int Run() {")) + 6
);
assert.match(run, /const HWND surfaces\[\] = \{[^}]*libraryWindow_\.Window\(\)/,
  "the pump must still serve the library window that hosts this page");

console.log(`AI configuration page: tab order matches layout order: PASS (${created.length} controls)`);
