import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");

// The API configuration page is a left-sidebar nav tab labelled "API 配置", reached
// from the tray's 设置. Several user-facing strings point somewhere else instead:
// "右上角 AI 设置" (a corner with nothing in it) and "设置 →「妙喵 AI」" (a tab that
// does not exist). The worst of them is the "未配置 API Key" error, which fires at
// exactly the moment a user is blocked and needs correct directions.
//
// Nothing connects a string to the control it names, so this drifts silently. These
// checks are the connection: read the real nav labels out of the library window and
// refuse any string that directs the user to a tab that is not one of them.

const library = fs.readFileSync(path.join(root, "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp"), "utf8");
const navMatch = /const std::array<const wchar_t\*, 3> navLabels\{([^}]*)\}/.exec(library);
assert.ok(navMatch, "could not read the library nav labels");
const navLabels = [...navMatch[1].matchAll(/L"([^"]+)"/g)].map((m) => m[1]);
assert.deepStrictEqual(navLabels.sort(), ["API 配置", "壁纸", "组件"],
  "the nav labels changed -- re-check every settings pointer against the new set");

const walk = (dir) => {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === ".git" || entry.name === "node_modules") continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walk(full));
    else if (/\.(cpp|inc|h)$/.test(entry.name)) out.push(full);
  }
  return out;
};

const ROUTE = "托盘右键 → 设置 → 左侧「API 配置」";
// Wrong destinations: a nonexistent corner entry, and a nonexistent nav tab.
const BAD = [
  [/右上角[^"]{0,16}(AI 设置|设置)/g, "右上角 (a top-right AI settings entry that does not exist)"],
  [/设置[^"]{0,12}「妙喵 AI」/g, "设置 →「妙喵 AI」 (there is no such nav tab)"],
];

const offenders = [];
for (const file of walk(path.join(root, "src"))) {
  const text = fs.readFileSync(file, "utf8");
  for (const [pattern, what] of BAD) {
    for (const m of text.matchAll(pattern)) {
      offenders.push(`${path.relative(root, file)}: ${what} -- ${m[0]}`);
    }
  }
}
assert.deepStrictEqual(offenders, [],
  "user-facing text directing to a settings entry that does not exist:\n  " + offenders.join("\n  "));

// Every place that tells the user how to configure the model must use the real route.
const guidance = [
  "src/ai/agent/L3Agent.cpp",
  "src/desktop/demo/StoreDemoExperience.cpp",
  "src/ui/ai/ConversationPanelImpl.inc",
];
for (const rel of guidance) {
  const text = fs.readFileSync(path.join(root, rel), "utf8");
  if (!/API 配置/.test(text)) continue;
  assert.ok(text.includes(ROUTE),
    `${rel} gives configuration guidance but not the real route (${ROUTE})`);
}

console.log(`settings pointer accuracy: PASS (nav=${navLabels.join("/")}, ${guidance.length} files checked)`);
