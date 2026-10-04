import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");
const creatorHeader = read("src/include/miaodesk/ContentCreatorDialog.h");
const search = read("src/ui/search/SearchWindow.cpp");

// The AI creator is modeless: it is created by SearchWindow and its messages are
// dispatched by the search window's pump, in another translation unit. Every control
// in it is WS_TABSTOP -- the prompt, the five preset chips, the transcript, the skill
// list, the skill detail and the apply buttons -- so without the dialog manager in
// that pump, Tab does nothing here and a keyboard-only user cannot reach any of them.

// --- the surface must be visible cross-translation-unit --------------------
// The pump cannot reach the file-local DialogState, so the list of open creator
// windows has to be a public function. If it stays in the anonymous namespace the
// whole fix silently does nothing.
assert.match(creatorHeader, /std::vector<HWND> DialogManagedCreatorWindows\(\);/,
  "DialogManagedCreatorWindows() must be declared in the public header -- the pump is"
  + " in a different translation unit and cannot see an anonymous-namespace symbol");
assert.match(creatorHeader, /#include <vector>/, "the declaration needs <vector>");

// --- the registry must be maintained ---------------------------------------
assert.match(creator, /std::vector<HWND> g_openCreatorWindows;/,
  "the open-surface list must exist");
assert.match(creator, /g_openCreatorWindows\.push_back\(window\);/,
  "ShowContentCreatorDialog must register the window after CreateWindowExW succeeds --"
  + " registering before would put a null HWND in the list");
assert.match(creator, /g_openCreatorWindows\.erase\(it\);/,
  "WM_DESTROY must remove the window, or the pump keeps being handed a dead HWND");
// The userdata slot must be cleared before the state is deleted, so a reader can
// never dereference a freed pointer. This used to require the two statements to be
// adjacent; the source now guards the delete with `if (state->windowOwnsLifetime)`,
// which is stricter, not looser. The order is what matters, so pin the order by
// position rather than by adjacency.
{
  const clearAt = creator.indexOf("SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);");
  const deleteAt = creator.indexOf("delete state;");
  assert.ok(clearAt !== -1 && deleteAt !== -1 && clearAt < deleteAt,
    "keep the existing order: the state is deleted only after the userdata slot is cleared");
}
// The old regex required the two statements to be adjacent, which incidentally
// also pinned the `windowOwnsLifetime` guard. Pin the guard on its own now,
// because it is a real invariant: some creator windows do not own their
// DialogState, and deleting it unconditionally would corrupt them.
assert.match(creator, /if \(state->windowOwnsLifetime\) delete state;/,
  "the DialogState must be deleted only when the window owns its lifetime");

// The erase must happen BEFORE the state is deleted, because the reader dereferences
// GWLP_USERDATA.
const destroyBody = creator.slice(
  creator.indexOf("case WM_DESTROY:"),
  creator.indexOf("case WM_DESTROY:", creator.indexOf("case WM_DESTROY:") + 10)
);
const eraseAt = destroyBody.indexOf("g_openCreatorWindows.erase(it);");
const deleteAt = destroyBody.indexOf("delete state;");
assert.ok(eraseAt !== -1 && deleteAt !== -1 && eraseAt < deleteAt,
  "the window must leave the navigation list before its DialogState is deleted --"
  + " otherwise a reader can dereference a userdata slot that is already gone");

// --- the reader's guards ----------------------------------------------------
const reader = creator.slice(
  creator.indexOf("std::vector<HWND> DialogManagedCreatorWindows() {"),
  creator.indexOf("\n}", creator.indexOf("std::vector<HWND> DialogManagedCreatorWindows() {"))
);
assert.match(reader, /if \(!IsWindow\(window\)\) continue;/,
  "a destroyed HWND must be skipped -- the list is only cleaned up on WM_DESTROY, and a"
  + " window can die without it");
assert.match(reader, /reinterpret_cast<DialogState\*>\(GetWindowLongPtrW\(window, GWLP_USERDATA\)\)/,
  "the reader must go through the window's userdata slot, which is how it finds the state");
assert.match(reader, /if \(!state\) continue;/,
  "a window with no state (already in WM_DESTROY) must be skipped, not pushed");
assert.match(reader, /if \(state->previewFullscreenActive\) continue;/,
  "fullscreen preview must be left out: it owns Esc/Space/R, and the dialog manager"
  + " would swallow Esc into a WM_COMMAND/IDCANCEL this window does not handle, so the"
  + " exit would silently stop working");

// --- the pump actually serves it -------------------------------------------
const pump = search.slice(
  search.indexOf("int SearchWindow::RunMessageLoop() {"),
  search.indexOf("\n    }\n", search.indexOf("int SearchWindow::RunMessageLoop() {")) + 6
);
assert.match(pump, /for \(const HWND surface : creator::DialogManagedCreatorWindows\(\)\)/,
  "the pump must loop over the creator surfaces");
assert.match(pump, /if \(IsWindow\(surface\) && IsDialogMessageW\(surface, &msg\)\)/,
  "each surface needs the IsWindow guard before the dialog manager can see the message");
assert.match(pump, /break;/, "once a surface consumes the message the loop must stop");
assert.match(pump, /if \(!handled\)/, "dispatch must stay conditional");
assert.doesNotMatch(pump, /IsDialogMessageW\(hwnd_/,
  "the search window itself must NOT be handed to the dialog manager -- its Enter"
  + " executes the selected result and would be consumed first");

// --- the creator is keyboard-neutral, which is what makes this safe -------
assert.doesNotMatch(creator, /BS_DEFPUSHBUTTON/,
  "no default pushbutton, so Enter in a field is not hijacked by the dialog manager");
assert.doesNotMatch(creator, /VK_TAB/,
  "no self-handled Tab, or it would fight the dialog manager");
assert.doesNotMatch(creator, /WM_GETDLGCODE/,
  "no WM_GETDLGCODE override, so the dialog manager's key routing cannot be contested");
assert.match(creator, /ES_WANTRETURN/,
  "the prompt is a multiline edit that wants Enter -- the dialog manager passes Enter to"
  + " it rather than treating it as 'click the default button'");

// Fullscreen preview still owns its keys on the window's own WM_KEYDOWN path.
assert.match(creator, /if \(wParam == VK_ESCAPE\) \{ state->SetFullscreenPreview\(false\);/,
  "Esc must still exit fullscreen preview -- the window's own WM_KEYDOWN, which only"
  + " runs for windows NOT handed to the dialog manager");

console.log("AI creator surface: dialog-manager keyboard navigation: PASS");
