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
// Aliases that are the same command, plus the panel's own.
const aliases = new Set(["/new-chat"]);
const panelOnly = new Set(["/retry"]);

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

console.log(`slash command help coverage: PASS (${implemented.length + 1} commands, all listed)`);
