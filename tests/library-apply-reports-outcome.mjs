import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const header = read("src/include/miaodesk/WallpaperLibraryWindow.h");
const library = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");
const engine = read("src/desktop/wallpaper/legacy/WallpaperEngine.cpp");

// The product's core action used to report success when it had failed.
//
// `ApplyCallback` returned void. In ApplySelected that left
//     bool applied = true;
//     if (applyCallback) { applyCallback(*selected, targetId); }   // nothing read back
// so on the engine path `applied` was true no matter what happened downstream. The engine,
// on a bail-out, set `libraryError_` and returned -- and libraryError_ is read in exactly
// one place: the advanced settings window's diagnostics text. A different window, on a
// page the user has to navigate to, that they were not looking at when they pressed
// 应用到桌面.
//
// So the outcome was: status line says "已应用到桌面", MarkUsed stamps the wallpaper as
// used, and the reason for the failure sits one window away. The comment that used to sit
// there even said so -- "the status line below is therefore not evidence of success on
// this path -- the engine's own window is" -- as though that were a design rather than the
// defect. docs/AI_GENERATED_DESKTOP_SANDBOX.md calls this shape false success, and notes
// this repo has already fallen into it three times.

const bodyOf = (text, signature) => {
  const at = text.indexOf(signature);
  assert.notStrictEqual(at, -1, `cannot find ${signature}`);
  const open = text.indexOf("{", at);
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === "{") ++depth;
    else if (text[i] === "}") {
      if (--depth === 0) return text.slice(open, i + 1);
    }
  }
  assert.fail(`unbalanced braces after ${signature}`);
};

// --- the signature must carry the outcome -------------------------------
assert.match(header, /using ApplyCallback = std::function<std::wstring\(const WallpaperLibraryItem&, const std::wstring& targetMonitorId\)>;/,
  "ApplyCallback must return the outcome. A void callback cannot report a failure, so the"
  + " caller has no way to distinguish 'applied' from 'bailed out'.");
assert.doesNotMatch(header, /using ApplyCallback = std::function<void/,
  "...which is exactly what it used to be, and exactly what produced the false success");
assert.match(header, /using GlobalApplyCallback = std::function<std::wstring\(const WallpaperLibraryItem&\)>;/,
  "the global overload must report too -- it forwards to the same callback");
// The forwarding lambda must RETURN the inner result rather than call and discard it.
const showOverload = bodyOf(header, "bool Show(HINSTANCE instance, WallpaperLibrary* library, GlobalApplyCallback applyCallback)");
assert.match(showOverload, /return callback \? callback\(item\) : std::wstring\(\);/,
  "the global overload's bridge must forward the inner return, not just call it");

// --- the library window must actually read it ---------------------------
const apply = bodyOf(library, "void ApplySelected() {");
assert.match(apply, /failure = applyCallback\(\*selected, targetId\);/,
  "the callback's return must be captured -- this is the read that was missing");
assert.match(apply, /applied = failure\.empty\(\);/,
  "and `applied` must be derived from it, so a bail-out is a failure on this path");
// `bool applied = true;` is still the initialiser; what matters is that the callback branch
// overwrites it. If it did not, `applied` would stay true and the branch below would be
// dead code on the engine path.
const callbackBranch = bodyOf(apply, "if (applyCallback) {");
assert.doesNotMatch(callbackBranch, /applied\s*=\s*true/,
  "the callback branch may not assert success itself -- that is the bug in one line");

// The stamp still has to be conditional. MarkUsed feeds 最近使用, which was added this
// round; without the guard a failed apply would rank above wallpapers really used.
assert.match(apply, /if \(applied && library\) library->MarkUsed\(selected->id, &ignored\);/,
  "MarkUsed must stay guarded by `applied` -- a failed apply must not enter the recency view");

// And the failure path that now becomes reachable must still exist.
assert.match(apply, /if \(!applied\) \{[\s\S]*?SetStatus\(failure\.empty\(\) \? L"应用壁纸失败。" : failure\);[\s\S]*?MessageBeep\(MB_ICONERROR\)/,
  "the engine-path failure must land on the status line and beep like every other one");

// --- the engine must return something usable on every path --------------
const engineApply = bodyOf(engine, "std::wstring ApplyLibraryItem(const miaodesk::wallpaper::WallpaperLibraryItem& item, const std::wstring& targetMonitorId) {");
// Falling off the end of a non-void function is undefined behaviour, and this function has
// a fall-through tail (MarkUsed + refreshes).
assert.match(engineApply, /return \{\};\s*\n\s*\}$/,
  "the success tail must end in an explicit empty return -- falling off a std::wstring"
  + " function is undefined behaviour, not an implicit empty string");
const bareReturns = [...engineApply.matchAll(/^\s*return;/gm)];
assert.equal(bareReturns.length, 0,
  `every failure path in the engine's ApplyLibraryItem must RETURN the reason; found`
  + ` ${bareReturns.length} bare return(s). A bare return here yields an empty string,`
  + ` which the window reads as success -- the false success again, one layer down.`);
// Four bail-outs: unknown kind, web activation, per-monitor assignment, and the global
// scene/type mapping. Each must produce a message the person who clicked can act on.
assert.ok((engineApply.match(/return /g) || []).length >= 5,
  "expected the four failure paths plus the success tail to all return");
assert.match(engineApply, /return libraryError_\.empty\(\) \? L"Web 壁纸应用失败。" : libraryError_;/,
  "the web bail-out must return its reason");
assert.match(engineApply, /return error\.empty\(\) \? L"显示器分配失败。" : error;/,
  "...as must the per-monitor assignment bail-out");
assert.match(engineApply, /L"”尚没有可用的运行时，未修改当前桌面。"/,
  "...and the Scene bail-out must name the wallpaper and say the desktop was not changed");
assert.match(engineApply, /L"”这个类型当前不可运行，未修改当前桌面。"/,
  "...and ditto for the other kinds");

// --- and the engine's lambda must forward it -----------------------------
const wire = engine.slice(engine.indexOf("libraryWindow_.Show("), engine.indexOf("void ShowAutomation()"));
assert.match(wire, /return ApplyLibraryItem\(item, targetMonitorId\);/,
  "the lambda the library window calls must return the engine's verdict. Discarding it"
  + " here would reopen the hole from the other end.");
assert.doesNotMatch(wire, /^\s*ApplyLibraryItem\(item, targetMonitorId\);\s*$/m,
  "it must not be a bare call");

console.log("library apply reports the engine's real outcome: PASS");
