import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import assert from "node:assert/strict";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");
const read = (p) => fs.readFileSync(path.join(root, p), "utf8");

// A remote server's response body is untrusted text, and it used to be shown as-is.
//
// L3Agent::ProbeModels builds its error message from up to 220 bytes of whatever the
// server sent. That message goes to the settings page's status line, and into
// profile.lastMessage, which TestConnection persists with WriteIni into the plaintext
// profile file. So a key reflected by a gateway -- and some do reflect, because they echo
// the headers they were given back in their error body -- lands on screen and then in a
// plaintext file.
//
// TODO API-03's acceptance criterion is that API keys never enter plaintext config. It was
// met by assuming no server ever reflects the request. The same assumption holds API-02's
// "error display must not leak the key" together.
//
// This test runs the real redaction (compiled and executed, not asserted about) and pins
// the wiring at the one place a body reaches a message.

// --- the redaction itself, executed ---------------------------------------
const work = fs.mkdtempSync("/tmp/miaodesk-secrets-");
try {
  const src = path.join(root, "tests/secret-redaction-harness.cpp");
  const bin = path.join(work, "redaction");
  execFileSync("clang++",
    ["-std=c++23", "-O1", "-Wall", "-Wextra", "-I", path.join(root, "src/include"), src, "-o", bin],
    { stdio: ["ignore", "pipe", "pipe"] });
  const out = execFileSync(bin, [], { encoding: "utf8" });
  assert.match(out, /ALL PASS/,
    "the compiled redaction harness must pass -- it exercises the real scanner, including"
    + " the reflected-Authorization shape that motivated it");
  assert.ok((out.match(/  ok   /g) || []).length >= 25,
    `expected the harness's cases to actually run, saw ${(out.match(/  ok   /g) || []).length}`);
  assert.doesNotMatch(out, /FAIL/,
    "a failing case in the harness means the scanner no longer does what it claims:");
} catch (error) {
  if (error.status !== undefined || error.code === "ENOENT") {
    assert.fail(`could not compile/run the redaction harness: ${error.stderr || error.message}`);
  }
  throw error;
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}

// --- the wiring at the leak site ------------------------------------------
const agent = read("src/ai/agent/L3Agent.cpp");

const probe = agent.slice(
  agent.indexOf("ModelProbeResult L3Agent::ProbeModels("),
  agent.indexOf("bool L3Agent::ApplyModelConfig("));
assert.ok(probe.length > 200, "cannot locate ProbeModels");

assert.match(agent, /#include "miaodesk\/SecretRedaction\.h"/,
  "L3Agent must include the redaction header");

// The body must pass through SummarizeRemoteBody before it reaches a message.
assert.match(probe, /const auto shortBody = Utf8ToWide\(response\.body\.substr\(0, 220\)\);\n\s+const auto summarized = miaodesk::secrets::SummarizeRemoteBody\(shortBody\);\n\s+if \(!summarized\.empty\(\)\) detail \+= L" " \+ summarized;/,
  "the response body must be truncated FIRST and redacted SECOND, and only the redacted"
  + " form may be appended. Redacting a 2 MB body to then throw it away would be work for"
  + " nothing, and appending before redacting is the defect.");
assert.doesNotMatch(probe, /detail \+= L" " \+ shortBody;/,
  "the raw body must not be appended anywhere in this function");

// Every message this function can return must go through the same funnel, so a future
// branch cannot splice a body in beside it.
const messageAssignments = [...probe.matchAll(/result\.message = (.+);/g)].map((m) => m[1]);
assert.ok(messageAssignments.length >= 6,
  `expected the probe's message assignments, found ${messageAssignments.length}`);
for (const rhs of messageAssignments) {
  assert.doesNotMatch(rhs, /response\.body/,
    `a message assigned directly from response.body: \`result.message = ${rhs.slice(0, 60)};\``);
}

// --- no other place may do it ---------------------------------------------
// One leak site is a leak site. This is the survey that keeps it at one.
const responseBodies = [];
const walk = (relDir) => {
  for (const entry of fs.readdirSync(path.join(root, relDir), { withFileTypes: true })) {
    if (entry.name === ".git" || entry.name === "node_modules") continue;
    const rel = path.posix.join(relDir, entry.name);
    if (entry.isDirectory()) walk(rel);
    else if (/\.(cpp|inc|h)$/.test(entry.name)) {
      const text = read(rel);
      for (const line of text.split("\n")) {
        // A body turned into user-visible or persisted text, without the redactor.
        if (/response\.body|\.body\.substr|HttpBody\b/.test(line) &&
            /(message|reply|detail|status|note|error|text)\s*[+]?=/.test(line) &&
            !/Redact|Summarize|secrets::/.test(line)) {
          responseBodies.push(`${rel}: ${line.trim().slice(0, 90)}`);
        }
      }
    }
  }
};
walk("src");
assert.deepStrictEqual(responseBodies, [],
  "an HTTP response body reaching a message without redaction:\n  " + responseBodies.join("\n  "));

console.log("probe error bodies are redacted before they are shown or saved: PASS");
