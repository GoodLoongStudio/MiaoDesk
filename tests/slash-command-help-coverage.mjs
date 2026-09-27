import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const agent = read("src/ai/agent/L3Agent.cpp");
const panel = read("src/ui/ai/ConversationPanelImpl.inc");

// /help is the only place a user can discover the command surface, and it had rotted:
// it listed 7 commands while TryHandleLocal implements 12 -- omitting every
// credential-related one, including /clear-key, which is how a stored API key gets
// revoked. A user who wanted to remove a key had no way to learn the command existed.
//
// So parse what is actually implemented and require each to appear in the help text.
// This is the same drift class as the doc-citation gates: the list and the
// implementation are two separate things, and nothing else connects them.

// Commands L3Agent answers locally: `if (lower == L"/x")` and `if (lower.starts_with(L"/x "))`.
const local = new Set();
for (const m of agent.matchAll(/if \(lower\s*(?:==|\.starts_with\()\s*L"(\/[a-z-]+)/g)) {
  local.add(m[1]);
}
// Aliases that are the same command.
const aliases = new Set(["/new-chat"]);
// Commands handled by the conversation panel itself, before the agent is consulted.
const panelOnly = new Set(["/retry", "/runtime"]);
for (const m of panel.matchAll(/if \(lower == L"(\/[a-z-]+)"\)/g)) {
  panelOnly.add(m[1]);
}
panelOnly.delete("/help");
panelOnly.delete("/status");

const helpStart = agent.indexOf('if (lower == L"/help") {');
assert.notStrictEqual(helpStart, -1, "/help must exist");
const help = agent.slice(helpStart, agent.indexOf("return true;", helpStart));
const helpText = [...help.matchAll(/L"([^"]*)"/g)].map((m) => m[1]).join("");

const implemented = [...local, ...panelOnly].filter((c) => !aliases.has(c));
assert.ok(implemented.length >= 10,
  `expected the full command set to be parsed, got ${implemented.length}: ${implemented.join(", ")}`);

const missing = implemented.filter((c) => !helpText.includes(c));
assert.deepStrictEqual(missing, [],
  `commands that work but /help never mentions: ${missing.join(", ")}`);

// The reverse matters too: a help line for a command that does not exist is worse
// than a short help, because it sends the user somewhere broken.
const listed = new Set([...helpText.matchAll(/(\/[a-z-]+)(?:\s|$)/g)].map((m) => m[1]));
const known = new Set([...implemented, ...aliases, "/help"]);
const bogus = [...listed].filter((c) => !known.has(c));
assert.deepStrictEqual(bogus, [],
  `/help advertises commands that are not implemented: ${bogus.join(", ")}`);

// The credential commands specifically, since those were the ones that had gone
// missing and they are the security-relevant half of the surface.
for (const command of ["/key", "/clear-key", "/endpoint", "/provider"]) {
  assert.ok(helpText.includes(command), `/help must mention ${command}`);
}

// There must be exactly one help text. The panel used to answer /help with its own
// shorter list, so the agent's correction never reached the panel path and the two
// drifted in opposite directions.
assert.match(panel, /if \(command == L"\/help"\) return raw;/,
  "the panel must pass /help through to the single authoritative text");
assert.doesNotMatch(panel, /command == L"\/help"\s*\n\s*return L"/,
  "the panel must not keep a second help text of its own");

// The route to the settings page must name where it actually is: a left-sidebar nav
// tab labelled "API 配置", reached from the tray's 设置. Two strings pointed at a
// "右上角 AI 设置" entry that does not exist -- one of them the "未配置 API Key"
// error, which fires at exactly the moment the user is blocked and needs correct
// directions. (The panel's "右上角加个待办小组件" is a screen position, correctly
// worded, so the check is for the settings phrasing and not the bare word.)
for (const [source, label] of [[agent, "the agent"], [panel, "the panel"]]) {
  assert.doesNotMatch(source, /右上角[^"]{0,12}(AI 设置|设置)/,
    `${label} must not point at a top-right AI settings entry that does not exist`);
  assert.doesNotMatch(source, /设置[^"]{0,12}右上角/,
    `${label} must not route through a nonexistent top-right entry`);
}
assert.match(agent, /托盘右键 → 设置 → 左侧「API 配置」/,
  "the help must give the real route to the API configuration page");
assert.match(agent, /托盘右键 → 设置 → 左侧「API 配置」里填写/,
  "the missing-API-Key error must give the real route");

console.log(`slash command help coverage: PASS (${implemented.length + 1} commands, all listed)`);
