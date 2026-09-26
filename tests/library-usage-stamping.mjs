import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");
const service = read("src/desktop/wallpaper/WallpaperService.cpp");

// Applies that go through WallpaperService never updated the library's usage
// order. WallpaperEngine owns a WallpaperLibrary and calls MarkUsed on its own
// apply paths, but the AI creator's "应用到桌面", the content manager and the
// per-monitor assignment all route through here -- so lastUsedUnixSeconds stayed
// frozen at migration values for everything applied on those paths, and
// RecentlyUsed() would rank by a timestamp that ignores most real usage.

function fn(signature) {
  const start = service.indexOf(signature);
  assert.notStrictEqual(start, -1, `${signature} must exist`);
  let depth = 0;
  for (let i = service.indexOf("{", start); i < service.length; i += 1) {
    if (service[i] === "{") depth += 1;
    else if (service[i] === "}") {
      depth -= 1;
      if (depth === 0) return service.slice(start, i + 1);
    }
  }
  throw new Error(`could not brace-match ${signature}`);
}

// The helper must be bookkeeping: a failure to stamp usage can never fail an
// apply that already succeeded.
const helper = fn("void MarkLibraryItemUsed(std::wstring_view id) {");
assert.match(helper, /if \(id\.empty\(\)\) return;/,
  "MarkLibraryItemUsed must cheap out on an empty id");
assert.doesNotMatch(helper, /return \{false/,
  "MarkLibraryItemUsed must not be able to fail the apply");
assert.match(helper, /library\.MarkUsed\(id, &error\)/,
  "MarkLibraryItemUsed must call WallpaperLibrary::MarkUsed");

// Single exit: every success has to flow through the tail, or a new branch can
// add an early `return` and silently skip the stamping again.
const apply = fn("WallpaperServiceResult WallpaperService::ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const {");
assert.match(apply, /if \(result\.success\) MarkLibraryItemUsed\(item\.id\);/,
  "ApplyLibraryItem must stamp usage once, on success, after the switch");
const successReturns = apply.match(/return WallpaperServiceResult\{true,/g) ?? [];
assert.strictEqual(successReturns.length, 0,
  "no success may return directly from inside the switch -- it would bypass marking");
const switchAt = apply.indexOf("switch (item.kind) {");
const tailAt = apply.indexOf("if (result.success) MarkLibraryItemUsed(item.id);");
assert.ok(tailAt > switchAt, "the marking must come after the switch");

// Per-monitor assignment is a use too.
const assign = fn("WallpaperServiceResult WallpaperService::AssignLibraryItemToMonitor(");
assert.match(assign, /MarkLibraryItemUsed\(item\.id\);/,
  "AssignLibraryItemToMonitor must stamp usage as well");

console.log("wallpaper library usage stamping: PASS");
