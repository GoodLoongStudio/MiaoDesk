import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");

// Two more surfaces where a click was dropped in silence, found by surveying rather than
// by reading the code I had just written.
//
// The library and automation windows are now covered by their own invariants
// (every-library-action-talks-back.mjs, automation-actions-report-empty-selection.mjs).
// The one below is NOT: those two files survey a WM_COMMAND dispatch table of Win32
// controls, and everything here fails that shape --
//
//   - ContentCreatorDialog's send button is painted, not a control. EnableWindow cannot
//     touch it, and SetBusy deliberately leaves it live so it can act as 停止.
//   - the creator's preview pane is SS_OWNERDRAW | SS_NOTIFY, and its 全屏 *button* gating
//     and its pane gating disagree by design slip.
//   - the task editor's three buttons are plain controls in a window with none of them
//     enabled or disabled, and no LVN_ITEMCHANGED handler at all.
//   - the rules window's 删除 is the only guarded return in a file that knows how to say
//     things -- SaveRule writes a status for the same "nothing filled in" case.
//
// So each is pinned on its own terms below, and each assertion names the thing that made
// its guard reachable. A fourth surface (the conversation panel) is deliberately excluded;
// see the note at the bottom for why it is a different kind of decision.

const bodyOf = (text, signature) => {
  const at = text.indexOf(signature);
  assert.notStrictEqual(at, -1, `cannot find ${signature}`);
  // The first "{" is not necessarily the body's. A default argument like
  // `std::wstring_view repairNote = {}` is an empty pair, and matching it returns "{}" --
  // which reads as "the function is empty" rather than as a failure.
  let open = -1;
  for (let i = text.indexOf("{", at); i !== -1 && i < text.length; i = text.indexOf("{", i + 1)) {
    if (text[i + 1] === "}") { ++i; continue; }
    open = i;
    break;
  }
  assert.notStrictEqual(open, -1, `no body after ${signature}`);
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === "{") ++depth;
    else if (text[i] === "}") {
      if (--depth === 0) return text.slice(open, i + 1);
    }
  }
  assert.fail(`unbalanced braces after ${signature}`);
};

// --- 1. the creator's 生成 button with an empty prompt --------------------
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");
const creatorSend = bodyOf(creator, "void SendPrompt() {");

// The premise: nothing disables it, and nothing else in this file can speak.
assert.match(creator, /EnableWindow\(send, TRUE\);\s*\n\s*SetWindowTextW\(send, value \? L"停止" : L"生成"\);/,
  "the send button must stay live while busy -- it doubles as 停止, which is exactly why an"
  + " empty prompt reaches the guard instead of hitting a disabled control");
const creatorStatus = (creator.match(/SetStatus\(/g) || []).length +
                      (creator.match(/MessageBeep\(/g) || []).length;
assert.equal(creatorStatus, 0,
  `this window has no status line and no beep of its own (found ${creatorStatus});`
  + ` its only speaking surface is resultNote, which is what the guard must use`);
assert.match(creatorSend, /if \(text\.empty\(\)\) \{[\s\S]*?SetWindowTextW\(resultNote,/,
  "an empty prompt must be written to the result note -- the one surface this window has");
assert.match(creatorSend, /if \(text\.empty\(\)\) \{[\s\S]*?SetFocus\(prompt\);/,
  "...and must return focus to the prompt that needs the text");
assert.doesNotMatch(creatorSend, /if \(text\.empty\(\)\) return;/,
  "the guard must not be a bare return again");

// --- 2. the creator's preview pane with nothing to show -------------------
// The 全屏 button is gated on (previewLive || previewBitmap); the pane is gated on
// generatedPackage being non-empty. A package can publish with 预览加载失败 or 无预览资源,
// which satisfies the second and not the first.
const pane = bodyOf(creator, "void ToggleFullscreenPreview() {");
assert.match(pane, /if \(!previewLive && !previewBitmap\) \{[\s\S]*?SetWindowTextW\(resultNote,/,
  "clicking the pane with no preview must say why nothing happened");
assert.match(pane, /previewRenderError/,
  "...and must show the render failure when there is one, rather than a generic line");

// The two gates really do disagree, which is what makes the pane's guard reachable.
assert.match(creator, /EnableWindow\(previewFullscreen,\n\s+\(previewLive \|\| previewBitmap\) \? TRUE : FALSE\);/,
  "the 全屏 BUTTON is gated on the preview actually existing");
assert.doesNotMatch(creator, /EnableWindow\(previewPane,/,
  "the pane is an SS_OWNERDRAW|SS_NOTIFY static, so it cannot be disabled the way the"
  + " button is -- which is why its guard is the reachable one");

// --- 2b. 重新生成 must not be force-enabled mid-turn ------------------------
// UpdatePreviewChrome computes (!lastUserPrompt.empty() && !busy) for this button, and
// SetGeneratedPackage used to override it with a bare TRUE immediately afterwards --
// while SetGeneratedPackage itself can run from an activity event, i.e. during a turn.
// Result: 重新生成 live while busy, and a click hitting Regenerate()'s `if (busy)` in
// silence. SetBusy(false) re-runs the gate at the end of the turn, so the override bought
// nothing.
const generated = bodyOf(creator, "void SetGeneratedPackage(");
assert.doesNotMatch(generated, /EnableWindow\(preview, TRUE\);/,
  "SetGeneratedPackage must not force 重新生成 live: UpdatePreviewChrome has just set its"
  + " real condition, and this function can run mid-turn");
assert.match(generated, /UpdatePreviewChrome\(\);/,
  "it must still go through UpdatePreviewChrome, which owns the button's state");
assert.match(creator, /void SetBusy\(bool value\) \{[\s\S]*?UpdatePreviewChrome\(\);/,
  "SetBusy must keep re-running the gate, or removing the override leaves the button dead");

// --- 3. the task editor's three buttons with nothing selected -------------
// None are EnableWindow'd and there is no LVN_ITEMCHANGED handler, so clicking blank space
// beneath the rows deselects, and the live buttons then drop the click.
const editor = read("src/ui/wallpaper/TodayTaskEditorDialog.cpp");
// Scoped to the WM_NOTIFY switch, not the whole file: this file now mentions the code in
// its own comments, and a whole-file check would fail on prose.
const notifyAt = editor.indexOf("case WM_NOTIFY:");
assert.notStrictEqual(notifyAt, -1, "cannot find the editor's WM_NOTIFY handling");
const notify = editor.slice(notifyAt, editor.indexOf("case WM_", notifyAt + 10));
assert.doesNotMatch(notify, /LVN_ITEMCHANGED/,
  "there must still be no selection-change handler -- that is why the buttons stay live in"
  + " a state their guards reject, and why the click is dropped rather than absorbed");
const enabledOnActions = [...editor.matchAll(/EnableWindow\((\w+),/g)]
  .map((m) => m[1])
  .filter((name) => !/owner/.test(name));
assert.deepStrictEqual(enabledOnActions, [],
  "no action button in this window may be EnableWindow'd -- there are none to disable, so"
  + " every guard here is reachable and must report:\n  " + enabledOnActions.join("\n  "));
for (const [handler, label] of [["EditSelected", "编辑"],
                                ["ToggleSelected", "完成 / 恢复"],
                                ["DeleteSelected", "删除"]]) {
  const body = bodyOf(editor, `void ${handler}(EditorState& state) {`);
  assert.match(body, /if \(index < 0[\s\S]*?\) \{[\s\S]*?SetStatus\(state,/,
    `${handler} must report an empty selection`);
  assert.match(body, /MessageBeep\(MB_ICONERROR\)/,
    `...and beep with it`);
  assert.match(body, new RegExp(`L"${label.replace(/[.*+?^${}()|[\]\\/]/g, "\\$&")}"`),
    `${handler} must name the button it is refusing to press`);
}
assert.doesNotMatch(editor, /kindLabel/,
  "the placeholder used while writing this must not survive into the build");

// --- 4. the rules window's 删除 ------------------------------------------
const rules = read("src/ui/automation/WallpaperApplicationRulesWindow.cpp");
const deleteRule = bodyOf(rules, "void DeleteRule() {");
assert.match(deleteRule, /if \(selectedRuleId\.empty\(\)\) \{[\s\S]*?SetStatus\(/,
  "删除 with no rule selected must report -- NewRule() clears selectedRuleId as its first"
  + " statement, and opening the window on a machine with no rules lands here too");
assert.match(deleteRule, /MessageBeep\(MB_ICONERROR\)/, "...and beep with it");
const rulesEnabled = [...rules.matchAll(/EnableWindow\((\w+),/g)].map((m) => m[1]);
assert.ok(rulesEnabled.every((name) => /owner/.test(name)),
  "no control in the rules window is enabled or disabled either -- every EnableWindow here"
  + " targets the owner window, so the 删除 guard is reachable");
// The same file knows how to say this; SaveRule is the precedent two hundred lines up.
assert.match(rules, /SetStatus\(L"请填写或选择一个 EXE。 "\);/,
  "SaveRule's empty-field status line must stay -- it is why 删除's silence was an outlier"
  + " rather than the house style");

// --- 5. the automation window's two playlist siblings ---------------------
// Found by the same survey, and only after the branch extractor in that file was fixed: it
// had been reading 200 characters past the guard, so a SetStatus belonging to a LATER
// branch satisfied a guard that said nothing. ActivatePlaylist and NextPlaylist have all
// their feedback below them, so they passed.
const automation = read("src/ui/automation/WallpaperAutomationWindow.cpp");
for (const handler of ["ActivatePlaylist", "NextPlaylist"]) {
  const body = bodyOf(automation, `void ${handler}() {`);
  assert.match(body, /if \(!id\) \{[\s\S]*?SetStatus\(/,
    `${handler} must report an empty Playlist combo`);
  assert.match(body, /if \(!id\) \{[\s\S]*?MessageBeep\(MB_ICONERROR\)/,
    `...and beep, like the eight siblings fixed in the same window`);
}
assert.match(automation, /没有选中的 Playlist；先新建或选中一个播放列表。/,
  "both must name the Playlist list specifically -- this window has three combos");

console.log("creator, task editor, rules window and playlist actions all report: PASS");
