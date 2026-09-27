import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");

// Window classes are referenced by literal or by constant in several translation
// units. Cross-process lookups -- FindWindowW / HasWindowClass / FindWindowExW --
// silently return nothing when the name does not match the one registered, so a
// rename in the registering file breaks every caller with no error anywhere: the
// preview hand-off stops arriving, the creator stops reaching the running instance,
// file search stops connecting.
//
// This test resolves every class constant to its value and asserts that nothing
// looks up a class that is not registered.

const files = [];
const walk = (dir) => {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === ".git" || entry.name === "node_modules") continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full);
    else if (/\.(cpp|inc|h)$/.test(entry.name)) files.push(full);
  }
};
walk(path.join(root, "src"));
const sources = new Map(files.map((f) => [f, fs.readFileSync(f, "utf8")]));

// Resolve `constexpr wchar_t kFooClass[] = L"...";` (and the non-constexpr form).
// A shared constant name with two values is refused: nine files each declared
// `kWindowClass` or `kSettingsClass` for their own, unrelated window, so the same
// name meant seven different classes. They are file-local so nothing was broken, but
// a cross-TU lookup written against the name would have been ambiguous, and the
// invariant below was unenforceable while it held.
const values = new Map();
const collisions = [];
for (const [file, text] of sources) {
  for (const m of text.matchAll(/(?:constexpr\s+)?(?:wchar_t|WCHAR)\s+(k[A-Za-z0-9_]*Class)\s*\[\s*\]\s*=\s*L"([^"]+)"/g)) {
    const existing = values.get(m[1]);
    if (existing !== undefined && existing !== m[2]) {
      collisions.push(`${m[1]}: "${existing}" and "${m[2]}"`);
    }
    values.set(m[1], m[2]);
  }
}
assert.deepStrictEqual(collisions, [],
  "one constant name for two different window classes:\n  " + collisions.join("\n  "));
assert.ok(values.size >= 10, `expected the class constants to be resolved, got ${values.size}`);

const resolve = (token) => (token.startsWith("L\"") ? token.slice(2, -1) : values.get(token));
const isOurClass = (name) => typeof name === "string" && name.startsWith("MiaoDesk.");

// Registered: `lpszClassName = <token>;`
const registered = new Set();
for (const [file, text] of sources) {
  for (const m of text.matchAll(/lpszClassName\s*=\s*((?:L"[^"]+")|k[A-Za-z0-9_]*)\s*;/g)) {
    const name = resolve(m[1]);
    if (isOurClass(name)) registered.add(name);
  }
}
assert.ok(registered.size >= 5, `expected registered classes, got ${[...registered].join(", ")}`);

// Looked up: FindWindowW / FindWindowExW / HasWindowClass.
const LOOKUPS = [
  /FindWindowW\(\s*(?:nullptr\s*,\s*)?((?:L"[^"]+")|k[A-Za-z0-9_]*)/g,
  /FindWindowExW\(\s*(?:[^,]+,\s*){2}((?:L"[^"]+")|k[A-Za-z0-9_]*)/g,
  /HasWindowClass\(\s*\w+\s*,\s*((?:L"[^"]+")|k[A-Za-z0-9_]*)/g,
];

const dangling = [];
for (const [file, text] of sources) {
  for (const pattern of LOOKUPS) {
    for (const m of text.matchAll(pattern)) {
      const name = resolve(m[1]);
      if (!isOurClass(name)) continue;
      if (!registered.has(name)) {
        dangling.push(`${path.relative(root, file)}: looks up "${name}", which nothing registers`);
      }
    }
  }
}
assert.deepStrictEqual(dangling, [],
  "lookups targeting window classes that do not exist:\n  " + dangling.join("\n  "));

console.log(`window class lookup targets: PASS (${registered.size} registered, none dangling)`);
