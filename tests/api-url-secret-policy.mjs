import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import assert from "node:assert/strict";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");
const read = (p) => fs.readFileSync(path.join(root, p), "utf8");

// The URL secret guard is logic, so it gets executed rather than described.
//
// UrlCarriesSecret used to sit in an anonymous namespace inside
// DesktopAiSettingsPage.cpp. It is why a Base URL with a pasted credential is refused at
// save time -- and baseUrl lands verbatim in api-profiles.ini, in the plaintext
// PiAgent\models.json and in the Harness settings.yaml. TODO API-03's criterion is that a
// key enters no plaintext config, log or test evidence.
//
// The round that wrote it compiled and ran the real bytes against 17 cases and then left
// no test, noting the gap itself: "回归只由 settings-url-secret-guard.mjs 做存在性与覆盖
// 断言，它证明不了解析正确." So it was logic with no net, and it moved again here.

// --- run the real function -----------------------------------------------
const work = fs.mkdtempSync("/tmp/miaodesk-url-policy-");
try {
  const bin = path.join(work, "policy");
  execFileSync("clang++",
    ["-std=c++23", "-O1", "-Wall", "-Wextra", "-I", path.join(root, "src/include"),
     path.join(root, "tests/api-url-secret-policy-harness.cpp"), "-o", bin],
    { stdio: ["ignore", "pipe", "pipe"] });
  const out = execFileSync(bin, [], { encoding: "utf8" });
  assert.match(out, /ALL PASS/, "the URL secret guard must pass its own cases:\n" + out);
  assert.doesNotMatch(out, /FAIL/, out);
  assert.ok((out.match(/  ok   /g) || []).length >= 30,
    `expected the harness's cases to run, saw ${(out.match(/  ok   /g) || []).length}`);
} catch (error) {
  if (error.status !== undefined || error.code === "ENOENT") {
    assert.fail(`could not compile/run the URL policy harness: ${error.stderr || error.message}`);
  }
  throw error;
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}

// --- the guard must live somewhere testable ------------------------------
const policy = read("src/include/miaodesk/ApiUrlSecretPolicy.h");
assert.match(policy, /inline bool UrlCarriesSecret\(const std::wstring& url\)/,
  "the guard must stay a free inline function in its own header, where a harness can"
  + " compile it without the whole settings page");
assert.doesNotMatch(policy, /windows\.h|WritePrivateProfile|CreateFileW/,
  "the header must stay pure -- one Windows include and it can no longer be run here");

const page = read("src/ui/settings/DesktopAiSettingsPage.cpp");
assert.doesNotMatch(page, /bool UrlCarriesSecret\(const std::wstring& url\) \{/,
  "the local copy must not come back: a definition inside an anonymous namespace in the"
  + " settings page is a definition nothing can execute");
assert.match(page, /using miaodesk::api_url::UrlCarriesSecret;/,
  "the settings page must use the shared one");
assert.match(page, /#include "miaodesk\/ApiUrlSecretPolicy\.h"/,
  "...and include its header -- without this the page does not compile, which is the one"
  + " thing the include assertions are for");

// --- and every URL field must still be guarded ---------------------------
// Two fields take a Base URL, and the guard is only worth what its coverage is.
//
// Presence alone is not enough: `if (false && UrlCarriesSecret(profile.baseUrl))` keeps the
// call in the text and removes the behaviour, which is how a guard dies without anyone
// editing it. So each call is also checked against neutralising conditions.
for (const field of ["baseUrl", "imageBaseUrl"]) {
  const call = new RegExp(`UrlCarriesSecret\\(profile\\.${field}\\)`);
  assert.match(page, call, `${field} must still be refused when it carries a secret`);
  const neutralised = [...page.matchAll(/if \(([^;\n]*)\)/g)]
    .filter((m) => call.test(m[1]) && /\bfalse\b|\b0\s*&&|\|\|\s*true\b/.test(m[1]))
    .map((m) => m[0].trim());
  assert.deepStrictEqual(neutralised, [],
    `the ${field} guard is present but dead:\n  ` + neutralised.join("\n  "));
}

// The refusal must be a refusal, not a rewrite: the two consumers disagree about the
// query (direct model drops it, the harness keeps it), so silently editing it would
// change what goes on the wire in one of them.
assert.match(page, /SetStatus\(L"Base URL 里含疑似密钥参数[\s\S]*?不要写在地址里。", false\);/,
  "a refusing message must tell the user WHY and which field to use instead");

// --- the path rule, which is the new half --------------------------------
// The query rules cannot see a credential pasted into the path, and one there reaches
// three plaintext files plus the chat error's "Endpoint=<path>".
assert.match(policy, /inline bool LooksLikePastedCredential\(std::wstring_view segment\)/,
  "a path-embedded credential needs its own predicate -- the query scan does not cover it");
assert.match(policy, /startsWith\(L"sk-"\) && !startsWith\(L"pk-"\) && !startsWith\(L"rk-"\)/,
  "the predicate must be narrow: it turns on rejection, and a heuristic for 'opaque and"
  + " long' would refuse ordinary deployment names and webhook paths");
// The query name table must cover the spellings providers actually use. `api_token` was
// missing until the executed test caught it.
for (const name of ["key", "api_key", "apikey", "x-api-key", "secret_key", "token",
                    "api_token", "access_token", "access-token", "refresh_token",
                    "secret", "client_secret", "auth", "authorization", "password",
                    "pwd", "credential", "credentials"]) {
  assert.ok(policy.includes(`L"${name}"`), `the query name table must still include ${name}`);
}

// --- the chat error path must not echo an unguarded endpoint -------------
// Endpoint=<path> is printed verbatim; with a credential in the path that is the secret
// in the transcript and in l3-runtime.log.
const agent = read("src/ai/agent/L3Agent.cpp");
assert.match(agent, /L" · Endpoint=" \+ miaodesk::secrets::RedactSecrets\(path\)/,
  "the chat failure line must redact the endpoint it echoes -- a credential allowed into"
  + " the path would otherwise be printed by name");

console.log("URL secret guard executed and wired: PASS");
