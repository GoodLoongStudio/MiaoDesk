import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const library = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");

// Every action this window offers must talk back when it does nothing.
//
// Two entry points dropped clicks in complete silence, and both looked harmless for the
// same reason: something else in the neighbourhood was gated, so the guard looked
// unreachable.
//
//   - ApplySelected's guard, when nothing was selected or the source was missing. The
//     apply button was EnableWindow(FALSE) and the menu entry MF_GRAYED -- but
//     double-clicking the card had no gate.
//   - ImportWeb's empty-URL guard. 添加 Web is shown enabled next to a field that holds
//     nothing but a cue banner, and there is no text yet to disable it on -- so clicking
//     it while empty is the first thing anyone does, and it did nothing at all.
//
// Both were "log and return". Neither wrote a status line, neither beeped. From outside
// the window the click simply vanished.
//
// A guard is only unreachable if every route into its handler checks the same condition.
// This file checks that rather than trusting the reading of whoever wrote the guard,
// which is the thing that was wrong both times.

const bodyOf = (text, signature) => {
  // With no signature, `text` must already begin at the `if (...) {` that opens the branch.
  // Returning the whole slice instead would sweep in every later statement -- which is how
  // a beep two branches down came to satisfy a branch with no beep of its own.
  const open = signature ? text.indexOf("{", text.indexOf(signature)) : text.indexOf("{");
  if (signature) assert.notStrictEqual(text.indexOf(signature), -1, `cannot find ${signature}`);
  assert.notStrictEqual(open, -1, signature ? `no body after ${signature}` : "no branch body");
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === "{") ++depth;
    else if (text[i] === "}") {
      if (--depth === 0) return text.slice(open, i + 1);
    }
  }
  assert.fail(`unbalanced braces after ${signature}`);
};

const SAYS_SOMETHING = /SetStatus\(|MessageBoxW\(/;

// --- the dispatch table --------------------------------------------------
const dispatched = [...library.matchAll(/id == (k\w+Id) && notification == BN_CLICKED\) self->(\w+)\(/g)]
  .map((m) => ({ control: m[1], handler: m[2] }));
assert.ok(dispatched.length >= 8, `expected the window's action handlers, found ${dispatched.length}`);

// The dispatch table names controls by id; the enablement and visibility gates name them
// by HWND. Without this mapping "is this control gated?" cannot be answered at all -- and
// answering it wrong is what let the empty-URL click look harmless.
const controlOf = new Map([...library.matchAll(/(\w+) = button\(L"[^"]*", (k\w+Id)/g)].map((m) => [m[2], m[1]]));
assert.ok(controlOf.has("kApplyId") && controlOf.has("kWebConfirmId"),
  "the button survey must still map ids to HWNDs");
for (const { control } of dispatched) {
  if (/^kNav/.test(control)) continue;  // handled as a range, one handler
  assert.ok(controlOf.has(control),
    `${control} is dispatched but no button is created for it -- the id-to-HWND survey is`
    + ` blind to it, so nothing can say whether it is gated`);
}

// Two dispatch lines for one control id is dead code: the chain is else-if, so the second
// can never run. Without this the survey below would happily read a handler nothing can
// call, and a silent guard inside it would look covered.
const dupes = dispatched.map((d) => d.control).filter((id, i, all) => all.indexOf(id) !== i);
assert.deepStrictEqual(dupes, [],
  "each control may be dispatched once:\n  " + dupes.join("\n  ")
  + "\nAn else-if chain means the second line for an id can never run -- and a handler"
  + " surveyed here that cannot be reached is one this file cannot protect.");

// --- what counts as gated ------------------------------------------------
// Only conditional gating counts. A plain ShowWindow(webConfirm, SW_SHOW) hides the bar,
// not the button's reachability -- and that is exactly the shape that let the empty-URL
// click through.
const gated = new Set([...library.matchAll(/EnableWindow\((\w+),/g)].map((m) => m[1]));
for (const m of library.matchAll(/ShowWindow\((\w+), [^;]*\? SW_SHOW : SW_HIDE\)/g)) gated.add(m[1]);
assert.ok(gated.has("applyButton"), "the enablement survey must still find the apply button");
assert.ok(gated.has("creatorButton"), "...and the creator button, hidden on the AI page");
assert.ok(!gated.has("webConfirm"),
  "webConfirm must NOT count as gated: it is shown and hidden with the whole bar, so"
  + " nothing tests the state its guard rejects. That is precisely why the empty-URL"
  + " click reached the guard.");

// 应用到桌面 is also gated at its menu entry, on the same SourceMissing condition. That
// is the mechanism whose existence made the guard look unreachable: the button was
// disabled and the menu greyed, and double-click was the route nobody counted.
assert.match(library, /AppendMenuW\(menu, SourceMissing\(\*selected\) \? MF_STRING \| MF_GRAYED : MF_STRING, kMenuApply/,
  "the context-menu apply entry must stay greyed on a missing source");

// --- guards that no user action can produce ------------------------------
// An internal invariant, not user state. Show() installs the library pointer before any
// control exists, so `!library` is not a state a click can land in, and demanding
// feedback for it would only add noise that hides the real cases.
const INTERNAL_INVARIANTS = ["!library", "!widgetController"];

const ungated = [];
for (const { control, handler } of dispatched) {
  let body;
  try {
    body = bodyOf(library, `void ${handler}() {`);
  } catch {
    continue;  // takes arguments, or lives in the WndProc
  }
  const hwnd = controlOf.get(control);
  if (hwnd && gated.has(hwnd)) continue;

  // Both spellings of a bare early exit: `if (c) return;` and `if (c) { return; }`.
  // Matching only the braced form would miss the exact one ImportWeb used.
  const bare = [...body.matchAll(/if \(([^;{}]*)\)\s*(?:\{\s*)?return;(?:\s*\})?/g)]
    .filter((m) => {
      if (INTERNAL_INVARIANTS.some((inv) => m[1].trim() === inv)) return false;
      // A branch that reports is not bare. Look at what follows, inside the branch.
      const after = body.slice(m.index, m.index + m[0].length + 160);
      return !SAYS_SOMETHING.test(after);
    });
  if (bare.length === 0) continue;
  ungated.push(
    `${handler} (${control}): never gated on any condition, so this is reachable, and it`
    + ` drops the click with no status line, no beep and no message box:\n      `
    + bare.map((b) => `if (${b[1].trim().slice(0, 70)}) return;`).join("\n      "));
}

assert.deepStrictEqual(ungated, [],
  "handlers whose control is never gated, whose bare guard therefore can be reached, and"
  + " which still say nothing:\n  " + ungated.join("\n  "));

// --- the two real cases, pinned on their own terms ------------------------
const importWeb = bodyOf(library, "void ImportWeb() {");
assert.match(importWeb, /if \(url\.empty\(\)\) \{[\s\S]*?SetStatus\(/,
  "an empty Web field must be reported, not ignored");
assert.match(importWeb, /if \(url\.empty\(\)\) \{[\s\S]*?MessageBeep\(MB_ICONERROR\)/,
  "...audibly, so the click is not lost");
assert.match(importWeb, /if \(url\.empty\(\)\) \{[\s\S]*?SetFocus\(webUrl\);/,
  "...and must put focus back in the field that needs the text");
assert.match(importWeb, /先输入要添加的 HTTPS 或本地 HTML 地址，再点添加 Web。/,
  "the message must say what to type, and must be honest about what is accepted:"
  + " IsSupportedSource takes remote HTTPS and an existing local HTML file, which the menu"
  + " label (\"添加 HTTPS Web 地址…\") and the cue banner both understate");
assert.doesNotMatch(importWeb, /if \(url\.empty\(\)\) return;/,
  "the guard must not be a bare return again");
// Keep that message true against what the runtime accepts, not against the menu label.
const host = read("src/desktop/wallpaper/web/WebWallpaperHost.cpp");
assert.match(host, /if \(IsRemoteHttpsSource\(source\)\) return true;/,
  "remote HTTPS must stay accepted");
assert.match(host, /return IsHtmlPath\(path\) && fs::exists\(path, ec\) && fs::is_regular_file\(path, ec\);/,
  "...and so must an existing local HTML file -- which is why the status text names both");

// A control gated only by page visibility is still reachable in the state its guard
// rejects: favourite, remove, widget toggle and widget remove are shown whenever their
// page is up, and their guards test the selection. So the enablement has to be tied to
// that same state, or each of those guards is a dropped click on a live button.
for (const gate of [
  /EnableWindow\(favoriteButton, selected \? TRUE : FALSE\);/,
  /EnableWindow\(removeButton, selected && selected->kind != LibraryWallpaperKind::Scene \? TRUE : FALSE\);/,
  /EnableWindow\(widgetToggleButton, widget \? TRUE : FALSE\);/,
  /EnableWindow\(widgetRemoveButton, widget \? TRUE : FALSE\);/,
]) {
  assert.match(library, gate,
    gate.source + ": this button's only other gate is page visibility, so without a"
    + " selection-tied enablement its guard is reachable and silent");
}

// Each apply guard's branch on its own, not a lazy span: `[\s\S]*?` from a guard reaches
// the NEXT branch's feedback and passes a branch that has none.
const apply = bodyOf(library, "void ApplySelected() {");
for (const header of ["if (SourceMissing(*selected)) {", "if (!selected) {"]) {
  const branch = bodyOf(apply.slice(apply.indexOf(header)), "");
  assert.match(branch, /SetStatus\(/,
    `the ${header} branch must report -- double-click is an ungated third entry point`);
  assert.match(branch, /MessageBeep\(MB_ICONERROR\)/,
    `and must be audible within that same branch, not one further down`);
}

console.log(`every library action talks back: PASS (${dispatched.length} handlers surveyed, ${gated.size} gated controls)`);
