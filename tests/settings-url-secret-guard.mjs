import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");
const page = read("src/ui/settings/DesktopAiSettingsPage.cpp");

// A secret pasted into the Base URL field is persisted verbatim: api-profiles.ini
// keeps baseUrl as-is, and it is copied into the plaintext PiAgent\models.json
// and Harness\DshHome\settings.yaml. So the URL fields need the same guard the
// API Key field already has (HeaderSafeSecret).
//
// This is a presence/coverage contract only. UrlCarriesSecret itself is pure
// string logic with no Windows API, so its behaviour can be checked by
// extracting and compiling it -- which found a real bug during the change (the
// authority scan treated a '/' after '@' as disqualifying, so every normal URL
// with a path defeated it). A text assertion cannot do that; do not mistake
// this file for proof the parsing is right.

const start = page.indexOf("bool UrlCarriesSecret(const std::wstring& url) {");
assert.notStrictEqual(start, -1, "UrlCarriesSecret must exist");
const fn = page.slice(start, page.indexOf("\n}", start) + 2);

assert.match(fn, /authority\.find\(L'@', hostStart\)/,
  "the authority scan must look for embedded user:password credentials");
assert.match(fn, /at < pathStart/,
  "the '@' must count only inside the authority, not in the path or query");
assert.match(fn, /kSecretNames/,
  "secret-named query params must be refused");

// Both URL fields the user can type into must be covered.
const draftStart = page.indexOf("bool ValidateDraft(ApiProfile& profile, std::wstring& key) {");
assert.notStrictEqual(draftStart, -1, "ValidateDraft must exist");
const draft = page.slice(draftStart, page.indexOf("\n}", draftStart) + 2);
for (const field of ["baseUrl", "imageBaseUrl"]) {
  assert.match(draft, new RegExp(`UrlCarriesSecret\\(profile\\.${field}\\)`),
    `ValidateDraft must check ${field}`);
}

// The message has to tell the user where the key actually goes, otherwise the
// rejection reads as a broken form.
assert.match(draft, /API Key 字段/,
  "the base-url rejection must point at the API Key field");

console.log("settings url secret guard: PASS");
