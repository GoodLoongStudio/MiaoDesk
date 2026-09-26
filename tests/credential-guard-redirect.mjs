import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");
const guard = read("src/include/miaodesk/ModelCredentialGuard.h");

// The retired shadow credential MiaoDesk/ModelApiKey is redirected, not refused.
//
// Two bugs lived here and both were silent:
//   1. CredWriteGuard rejected writes to the legacy target outright, so
//      L3Agent::SaveApiKey could never succeed -- the /key command reported
//      "保存失败，Windows 错误：5" forever, while reads still worked because
//      CredReadGuard already redirected.
//   2. Nothing redirected CredDeleteW, so /clear-key deleted a name nothing
//      writes, got ERROR_NOT_FOUND, read that as "cleared", and left the real
//      profile credential working.
// The fix redirects every legacy-target operation to the API Configuration
// Center default profile, which the design already made the only owner.

function body(name) {
  const start = guard.indexOf(`inline BOOL ${name}(`);
  assert.notStrictEqual(start, -1, `${name} must exist`);
  let depth = 0;
  for (let i = guard.indexOf("{", start); i < guard.length; i += 1) {
    if (guard[i] === "{") depth += 1;
    else if (guard[i] === "}") {
      depth -= 1;
      if (depth === 0) return guard.slice(start, i + 1);
    }
  }
  throw new Error(`could not brace-match ${name}`);
}

for (const fn of ["CredWriteGuard", "CredDeleteGuard"]) {
  const text = body(fn);

  // Non-legacy targets must still pass straight through: the settings page
  // legitimately writes MiaoDesk/ApiProfile/<id> through this same macro.
  assert.match(text, /SameTarget\(.+?kActiveCredentialTarget\)/,
    `${fn} must only intercept the retired target`);

  // The legacy target must be redirected, never refused.
  assert.doesNotMatch(text, /ERROR_ACCESS_DENIED/,
    `${fn} must not refuse the legacy target -- that is what broke /key`);
  assert.match(text, /ActiveProfileCredentialTarget\(/,
    `${fn} must resolve the profile that owns the secret`);
}

assert.doesNotMatch(guard, /legacy MiaoDesk\/ModelApiKey write rejected/,
  "the old rejection path must be gone");

// Both macros must exist alongside the read one.
assert.match(guard, /#define CredReadW\(\.\.\.\)\s+::miaodesk::model_credential_guard::CredReadGuard\(__VA_ARGS__\)/);
assert.match(guard, /#define CredWriteW\(\.\.\.\)\s+::miaodesk::model_credential_guard::CredWriteGuard\(__VA_ARGS__\)/);
assert.match(guard, /#define CredDeleteW\(\.\.\.\)\s+::miaodesk::model_credential_guard::CredDeleteGuard\(__VA_ARGS__\)/);

// Ordering invariant. ApiRuntimeProfile.h retires the shadow secret itself with
// a CredDeleteW call, and RawCredDelete/RawCredWrite/RawCredRead reach the real
// Win32 API. If the macros were defined before that include, both would expand
// into the guard and re-enter LoadDefault. #pragma once alone does not save
// this: the first expansion has to happen before the macros exist.
const includeAt = guard.indexOf('#include "miaodesk/ApiRuntimeProfile.h"');
const firstMacroAt = Math.min(
  ...[...guard.matchAll(/#define Cred(?:Read|Write|Delete)W\(/g)].map((m) => m.index)
);
assert.ok(includeAt !== -1, "the guard must include ApiRuntimeProfile.h");
assert.ok(includeAt < firstMacroAt,
  "ApiRuntimeProfile.h must be included before the Cred macros are defined, or its own retirement delete recurses");

for (const raw of ["RawCredRead", "RawCredWrite", "RawCredDelete"]) {
  const rawAt = guard.indexOf(`inline BOOL ${raw}(`);
  assert.notStrictEqual(rawAt, -1, `${raw} must exist`);
  assert.ok(rawAt < firstMacroAt,
    `${raw} must be defined before the Cred macros so it calls the real Win32 API`);
}

console.log("credential guard redirect: PASS");
