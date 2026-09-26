import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const search = read("src/ui/search/SearchWindow.cpp");
const settings = read("src/ui/wallpaper/ContentWidgetSettingsDialog.cpp");

// Two input-surface contracts that were broken in opposite directions: Search
// forced a geometry the IME contract forbids, while the settings dialog failed to
// provide the one input the dialog manager requires.

// 1. Search's WM_SIZE must not re-impose the 1x1 proxy.
// WINDOWS_CUSTOM_INPUT_IME.md section 2 forbids it by name. It was also
// self-defeating: MoveWindow posts WM_WINDOWPOSCHANGED, and the WH_CALLWNDPROCRET
// hook answers that with EnsureSearchImeGeometry -- so the real rect came back
// anyway, after a window in which the composition and candidate windows had
// anchored to a 1x1 rect. Re-anchoring through the shared helper removes the
// conflict and the per-resize churn, and keeps one source of truth for the rect.
// Strip comments before matching, so a comment that quotes the forbidden pattern
// (which the fix itself does, to explain what it replaced) is not read as code.
const stripComments = (text) => text.replace(/\/\/[^\n]*/g, "").replace(/\/\*[\s\S]*?\*\//g, "");

const sizeBranch = stripComments(search.slice(
  search.indexOf("case WM_SIZE:"),
  search.indexOf("case WM_PAINT:", search.indexOf("case WM_SIZE:"))
));
assert.doesNotMatch(sizeBranch, /MoveWindow\([^)]*,\s*1\s*,\s*1\s*,\s*FALSE\)/,
  "WM_SIZE must not force a 1x1 EDIT proxy");
assert.match(sizeBranch, /input_ime_detail::EnsureSearchImeGeometry\(edit_\)/,
  "WM_SIZE must re-anchor through the shared Search geometry helper");

// The helper itself must be the one that sets the real rect, not a stub.
const anchor = read("src/include/miaodesk/InputImeAnchor.h");
assert.match(anchor, /SetWindowPos\(\s*edit,\s*nullptr,\s*kSearchEditLeft,\s*kSearchEditTop/,
  "EnsureSearchImeGeometry must place the real input rectangle");

// 2. The settings dialog must accept Esc.
// Its pump calls IsDialogMessageW, which routes VK_ESCAPE to a control whose id is
// IDCANCEL and beeps when there is none. 关闭 was the only way out besides
// Alt+F4, even though every other dialog in the product closes on Esc.
assert.match(settings, /case IDCANCEL:\s*DestroyWindow\(hwnd\);/,
  "the widget settings dialog must close on Esc (IDCANCEL)");
assert.match(settings, /IsDialogMessageW/,
  "the dialog must keep pumping IsDialogMessageW, which is what makes IDCANCEL matter");

// 关闭 must still work as a button, not only via the Esc mapping.
assert.match(settings, /case kCloseId:\s*DestroyWindow\(hwnd\);/,
  "the 关闭 button must still close the dialog");

console.log("input surface contract: PASS");
