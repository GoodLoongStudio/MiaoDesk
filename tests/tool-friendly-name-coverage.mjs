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
// CCA-03 moved the allowlists out of PiRuntime.cpp into PiLaunchProfile.cpp, one per
// mode. Both are still tool sets the agent can call, so both need readable names.
const launchProfiles = read("src/desktop/control/PiLaunchProfile.cpp");
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

// Each allowlist is split across adjacent wide-string literals declared on one array.
// Read them from the declaration to the terminating semicolon: taking only the first
// literal would silently drop every tool on the later lines.
function allowlistNames(source, name, minimum) {
  const decl = source.indexOf(`const wchar_t ${name}[] =`);
  assert.notStrictEqual(decl, -1, `could not find ${name}`);
  const semi = source.indexOf(";", decl);
  assert.notStrictEqual(semi, -1, `${name} declaration must end with a semicolon`);
  const literals = [...source.slice(decl, semi).matchAll(/L"([^"]*)"/g)].map((m) => m[1]);
  assert.ok(literals.length > 0, `could not read ${name}`);
  const tools = literals.join(",").split(",").map((t) => t.trim()).filter(Boolean);
  assert.ok(tools.length >= minimum, `${name} looks wrong: ${tools.join(",")}`);
  return tools;
}

const tools = allowlistNames(launchProfiles, "kPiChatToolAllowlist", 15);
// The creator allowlist is pinned before its tools exist (CCA-03 froze the surface,
// CCA-04 implements it). A friendly name cannot be written for a tool that does not
// exist yet, so those are listed here as an explicit, auditable exception.
// Adding a creator tool to the allowlist therefore cannot pass silently: it has to be
// added to this list (still no name) or mapped in ToolDisplayNames.h. Either way the
// next person has to make the call out loud.
const CREATOR_TOOLS_WITHOUT_A_NAME_YET = new Set([
  "creator_capabilities_get",
  "creator_package_read",
  "creator_package_update",
  "creator_asset_import",
  "creator_image_generate",
  "creator_candidate_submit",
  "creator_preview_evidence",
]);

const creatorTools = allowlistNames(launchProfiles, "kPiCreatorToolAllowlist", 5);
const unmappedCreatorTools = creatorTools
  .filter((t) => !tools.includes(t) && !CREATOR_TOOLS_WITHOUT_A_NAME_YET.has(t));
assert.deepStrictEqual(
  unmappedCreatorTools,
  [],
  "a creator tool was added to the allowlist without a decision: either give it a"
  + " FriendlyToolName entry now, or add it to CREATOR_TOOLS_WITHOUT_A_NAME_YET."
  + ` Unaccounted: ${unmappedCreatorTools.join(", ")}`,
);
// And the exception list must not drift into claiming tools that are no longer there.
const staleExceptions = [...CREATOR_TOOLS_WITHOUT_A_NAME_YET]
  .filter((t) => !creatorTools.includes(t));
assert.deepStrictEqual(
  staleExceptions,
  [],
  `these creator tools have a name now or no longer exist, so drop them from the`
  + ` exception list: ${staleExceptions.join(", ")}`,
);

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
