import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");

// Every tool the Pi agent is allowed to call must have a human-readable name.
//
// FriendlyToolName falls back to the raw tool name when a tool is missing from
// its map, so a tool added to the allowlist without a mapping degrades silently
// to English snake_case in the activity box -- e.g. desktop_preview_wallpaper
// was missing for the headline wallpaper flow and rendered as
// "正在执行：desktop_preview_wallpaper". Nothing failed; it just looked broken.

const runtime = read("src/ai/pi/PiRuntime.cpp");
const shared = read("src/include/miaodesk/ToolDisplayNames.h");
const overlay = read("src/ui/ai/ConversationPanelImpl.inc");

// The map has exactly one home. It used to live inside the .inc, which made it
// unreachable from any other translation unit -- fine while only the conversation panel
// needed it. The AI creator needs the same names now, and a second private copy is how
// the two surfaces end up describing the same tool differently.
assert.doesNotMatch(overlay, /std::wstring FriendlyToolName\(/,
  "the map must not be re-declared in ConversationPanelImpl.inc -- it lives in"
  + " ToolDisplayNames.h so the creator can use the same names");
assert.match(overlay, /using miaodesk::ai::FriendlyToolName;/,
  "the conversation panel must import the shared map, not keep its own");

// The allowlist is split across adjacent wide-string literals in one array.
const allowlistBlock = runtime.slice(
  runtime.indexOf("kAgentToolAllowlist[]"),
  runtime.indexOf(";", runtime.indexOf("kAgentToolAllowlist[]"))
);
const literals = [...allowlistBlock.matchAll(/L"([^"]*)"/g)].map((m) => m[1]);
assert.ok(literals.length > 0, "could not read the Pi tool allowlist");
const tools = literals.join(",").split(",").map((s) => s.trim()).filter(Boolean);
assert.ok(tools.length > 5, `allowlist looks wrong: ${tools.join(",")}`);

const fn = shared.slice(
  shared.indexOf("inline std::wstring FriendlyToolName("),
  shared.indexOf("\n}", shared.indexOf("inline std::wstring FriendlyToolName("))
);
const mapped = new Set(
  [...fn.matchAll(/raw == L"([^"]+)"/g)].map((m) => m[1])
);
// write and edit share a branch, as do find/grep/ls.
assert.ok(mapped.has("write") && mapped.has("edit"), "write/edit both need mappings");

const missing = tools.filter((tool) => !mapped.has(tool));
assert.deepStrictEqual(missing, [],
  `tools callable by the agent but with no user-readable name: ${missing.join(", ")}`);

// The fallback must not be what ships: an unmapped tool has to be a bug, not a
// planned state.
assert.match(fn, /return raw\.empty\(\) \? L"执行操作" : raw;/,
  "the fallback must stay an honest last resort, not become the norm");

console.log(`tool friendly-name coverage: PASS (${tools.length} tools, all mapped)`);
