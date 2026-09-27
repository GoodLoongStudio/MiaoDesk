import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const file = read("src/ui/wallpaper/TodayTaskEditorDialog.cpp");

// Two nested pumps live in this file, and both call IsDialogMessageW. Both are full of
// WS_TABSTOP controls, so Tab works in them -- that part was already fine.
//
// What was not fine: the item dialog (新增/编辑待办) has a 取消 button whose id IS
// IDCANCEL, so Esc works there. The editor window that hosts it -- list, 新增, 编辑,
// 完成/恢复, 删除, 关闭 -- had no IDCANCEL at all, so Esc did nothing in it: the dialog
// manager looks for a control with that id, finds none, and the key goes nowhere. The
// same file, one level apart, answered Esc in one place and not the other.

// EditorProc holds the WM_COMMAND switch under test.
const editorProc = file.slice(
  file.indexOf("LRESULT CALLBACK EditorProc("),
  file.indexOf("\n} // namespace\n")
);
const itemProc = file.slice(
  file.indexOf("LRESULT CALLBACK ItemProc("),
  file.indexOf("bool EditTaskItem(")
);
// The two nested pumps. Both must keep handing messages to the dialog manager -- that is
// what makes Esc and Tab work at all here, and what makes the missing IDCANCEL visible
// rather than silent.
const editorLoop = file.slice(
  file.indexOf("bool ShowTodayTaskEditorDialog("),
  file.indexOf("\n} // namespace miaodesk::wallpaper")
);
const itemLoop = file.slice(
  file.indexOf("bool EditTaskItem("),
  file.indexOf("struct EditorState {")
);

for (const [name, text] of [["editor", editorLoop], ["item", itemLoop]]) {
  assert.match(text, /IsDialogMessageW\(state\.window, &message\)/,
    `${name}: its own pump must keep handing messages to the dialog manager`);

  // The nested loop must re-post WM_QUIT. GetMessageW returns 0 for a quit, and this loop
  // is nested inside the caller's pump: swallowing it here would leave the outer loop
  // running after the process was asked to exit, so the quit has to be put back.
  assert.match(text, /if \(result <= 0\) \{\s*\n\s*if \(result == 0\) PostQuitMessage\(static_cast<int>\(message\.wParam\)\);/,
    `${name}: the nested pump must re-post WM_QUIT so the outer loop still terminates`);
  assert.match(text, /if \(!IsDialogMessageW\(state\.window, &message\)\) \{\s*\n\s*TranslateMessage\(&message\);\s*\n\s*DispatchMessageW\(&message\);/,
    `${name}: messages the dialog manager does not consume must still be dispatched`);

  // The owner is disabled for the duration, and must always be given back.
  assert.match(text, /EnableWindow\(owner, FALSE\);/,
    `${name}: the owner is disabled while this window is up`);
  assert.match(text, /if \(owner && IsWindow\(owner\)\) \{\s*\n\s*EnableWindow\(owner, TRUE\);/,
    `${name}: the owner must be re-enabled on every exit path, including the quit path`);
}

// The fix: Esc reaches the editor through WM_COMMAND/IDCANCEL.
assert.match(editorProc, /case IDCANCEL: DestroyWindow\(hwnd\); return 0;/,
  "the editor must answer IDCANCEL the same way it answers its own 关闭 button -- without"
  + " it the dialog manager's Esc goes nowhere and the key is simply dead");
assert.match(editorProc, /case kCloseId: DestroyWindow\(hwnd\); return 0;/,
  "the 关闭 button path must stay; IDCANCEL is added alongside it, not instead of it");
assert.match(editorProc, /case WM_CLOSE:\s*\n\s*DestroyWindow\(hwnd\);/,
  "the window's own WM_CLOSE must stay");

// Esc must be the identical action, not a new one that can drift.
const closeActions = [...editorProc.matchAll(/case (?:kCloseId|IDCANCEL): DestroyWindow\(hwnd\); return 0;/g)];
assert.equal(closeActions.length, 2,
  `Esc and 关闭 must be the same action (destroy the window); found ${closeActions.length}`);

// There must be tab stops for the dialog manager to walk in both windows. The editor's
// buttons all come from one factory lambda, so a single WS_TABSTOP there covers 新增,
// 编辑, 完成/恢复, 删除 and 关闭.
assert.ok((editorLoop.match(/WS_TABSTOP/g) || []).length >= 2,
  "the editor needs tab stops: the list, and the button factory that makes all five");
assert.match(editorLoop, /makeButton\(L"(?:新增|编辑|完成 \/ 恢复|删除|关闭)"/,
  "the editor must actually use that factory to build its buttons");
assert.ok((itemLoop.match(/WS_TABSTOP/g) || []).length >= 4,
  "the item dialog needs tab stops (title, detail, 取消, 确定)");
assert.match(itemLoop, /BS_DEFPUSHBUTTON/,
  "the item dialog's 确定 is the default button, so Enter submits it");
assert.match(file, /kItemCancelId = IDCANCEL/,
  "the item dialog's cancel id is IDCANCEL, which is why Esc already worked there");

console.log("today-task editor: Esc closes the window, matching its 关闭 button: PASS");
