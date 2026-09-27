import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");
const runtime = read("src/include/miaodesk/PiRuntime.h");

// Two gaps this round closes, both of which cost the user a turn:
//
//  1. PROGRESS. The creator receives the same PiActivityEvent stream the conversation
//     panel renders as an activity card, and used it for exactly one thing -- looking for
//     a content-package path in the tool result text. So during a multi-minute turn the
//     only signals were the button reading 停止 and a static "AI 正在生成内容包…"
//     placeholder. PI_AGENT_ACTIVITY_FEEDBACK.md §1: the user must never have to wonder
//     whether Pi is frozen, thinking, waiting, executing, retrying, or finished.
//
//  2. THE VALIDATION VERDICT NEVER REACHED THE MODEL. SetGeneratedPackage dropped the
//     validator's message on the floor (`if (!inspected.success ...) return;`), the user
//     got a generic line, and the model got nothing at all -- while PiRuntime's own system
//     prompt tells it that scene .mdwall and .mdwidget packages are host-validated, i.e.
//     it must not expect to hear back. So a model that produced a package with one bad
//     parameter had no way to learn which one.

// --- 1. progress ----------------------------------------------------------
assert.match(creator, /void ShowActivity\(const PiActivityEvent& event\)/,
  "the creator must render the Pi activity stream, not just consume it");
assert.match(creator, /activityText = L"正在理解你的需求…";\s*\n\s*busyStartedAt = GetTickCount64\(\);\s*\n\s*if \(window\) SetTimer\(window, kActivityTimerId, kActivityTickMs, nullptr\);/,
  "starting a turn must start the elapsed-seconds tick; without it the line is static");
assert.match(creator, /case kActivityEvent: \{[\s\S]*?state->ShowActivity\(\*event\);/,
  "the activity handler must call ShowActivity -- it used to only hunt for a package path");
assert.match(creator, /if \(wParam == kActivityTimerId\) \{[\s\S]*?RefreshActivityLine\(\);/,
  "the activity timer must be handled in WM_TIMER or it fires forever with no effect");

// Every semantic state the runtime can emit must have copy. PiActivityKind's cases are
// an exhaustive list; a state that falls through silently is a state the user cannot see.
const enumBlock = runtime.slice(
  runtime.indexOf("enum class PiActivityKind {"),
  runtime.indexOf("};", runtime.indexOf("enum class PiActivityKind {"))
);
const kinds = [...enumBlock.matchAll(/^\s*(\w+),/gm)].map((m) => m[1]);
assert.ok(kinds.length === 8, `expected the runtime's eight activity kinds, got ${kinds.join(", ")}`);
const showActivity = creator.slice(
  creator.indexOf("void ShowActivity(const PiActivityEvent& event) {"),
  creator.indexOf("\n    }", creator.indexOf("void ShowActivity(const PiActivityEvent& event) {"))
);
for (const kind of kinds) {
  assert.ok(showActivity.includes(`PiActivityKind::${kind}`),
    `ShowActivity has no branch for PiActivityKind::${kind} -- that state would be invisible`);
}

// Tool names must be the shared map, never a second table and never the raw id.
const sharedNameUses = [...creator.matchAll(/ai::FriendlyToolName\(event\.toolName\)/g)];
assert.ok(sharedNameUses.length >= 3,
  `the creator must use the shared tool-name map for every tool label it shows`
  + ` (tool started, tool finished, tool failed); found ${sharedNameUses.length} uses`);
assert.doesNotMatch(creator, /std::wstring FriendlyToolName\(/,
  "the creator must not carry its own copy of the tool-name table -- the two surfaces"
  + " would drift the moment a tool is added");
assert.match(read("src/include/miaodesk/ToolDisplayNames.h"), /inline std::wstring FriendlyToolName\(/,
  "ToolDisplayNames.h must own the map for both surfaces");

// The idle text must come back. The line is a static set once at creation, so overwriting
// it without a restore leaves the header claiming work is in progress after the turn ended.
assert.match(creator, /idleNoteText = initial;/,
  "the header note's idle text must be captured at creation");
assert.match(creator, /const std::wstring text = activityVisible\s*\n\s*\? activityText \+ ElapsedSuffix\(\)\s*\n\s*: idleNoteText;/,
  "when idle the line must restore the idle text, not the last activity text");
assert.match(creator, /activityVisible = false;\s*\n\s*busyStartedAt = 0;\s*\n\s*if \(window\) KillTimer\(window, kActivityTimerId\);/,
  "ending a turn must stop the tick and clear the visible flag");

// No fabricated progress. The contract forbids it, and the only honest determinate
// number this pipeline has is elapsed time.
assert.doesNotMatch(creator, /PBM_SETPOS|PBM_SETSTEP|百分比|进度：|progress %|L"\\d+%"/,
  "no invented percentage or progress-bar position: nothing here knows how many tool calls"
  + " a description needs. (DPI percentages in unrelated comments are not the claim here.)");
assert.match(creator, /L" · 已等待 " \+ std::to_wstring\(seconds\) \+ L" 秒"/,
  "the determinate part must be elapsed seconds, which is factual");

// --- 2. validation verdict ------------------------------------------------
assert.match(creator, /std::wstring lastValidationError;/,
  "the validator's message must be kept in state");
assert.match(creator, /if \(!inspected\.success \|\| info\.kind != ExpectedKind\(\)\) \{[\s\S]*?\n\s+SetValidationFailure\(path,/,
  "a refused package must record WHY, not just return -- this used to be a bare return."
  + " The record has to be an unconditional statement inside that branch: guarding it with"
  + " a condition keeps the text present while dropping every invocation.");

// The user must see the reason.
assert.match(creator, /void AppendRuntimeFailureNote\(/,
  "the refusal must be written into the transcript where the round's outcome is read");
assert.match(creator, /内容包未通过校验/, "the transcript block must be labelled so the user knows this is a refusal, not chatter");
assert.match(creator, /校验未通过：.*lastValidationError/,
  "the final status line must carry the validator's reason instead of only the generic"
  + " '未检测到有效内容包路径'");

// And so must the model, on the next turn.
assert.match(creator, /if \(!lastValidationError\.empty\(\)\) \{[\s\S]*?request \+= lastValidationError;/,
  "BuildPrompt must feed the previous validation error back to the model");
assert.match(creator, /上轮生成的包未通过校验，必须先修掉这一条/,
  "the feedback must be unmistakable, or the model treats it as optional advice");
assert.match(creator, /校验器原文：/,
  "the validator's own words must be quoted verbatim -- a paraphrase loses the field name");
assert.match(creator, /不要改变用户需求，也不要另起一个不同的主题/,
  "the retry must be scoped to fixing the package, not reinterpreting the request");

// A new session must not inherit a stale failure.
assert.match(creator, /primed = false;[\s\S]{0,400}?ClearValidationFailure\(\);/,
  "ResetSession must clear the stored error: the package it described no longer exists");

// --- 3. auto-install to the library --------------------------------------
assert.match(creator, /installedToLibrary = InstallToLibrary\(\);/,
  "a validated package must be installed to the library automatically");
assert.match(creator, /if \(generatedPackageIsCurrentRound\) \{[\s\S]*?installedToLibrary = InstallToLibrary\(\);/,
  "auto-install must be gated on the package being from THIS round -- reloading an older"
  + " candidate must not silently reinstall it");
assert.match(creator, / · 已自动入库/,
  "the status must say the package was installed automatically");
assert.match(creator, /bool InstallToLibrary\(fs::path package, content::ContentKind kind,\s*\n\s*content::ContentPackageInstallResult\* out\)/,
  "the library install must be one shared function used by both the automatic path and"
  + " the explicit button, so the two cannot drift");

// The desktop stays a deliberate click. AI_GENERATED_DESKTOP_SANDBOX.md §1.
const sandbox = read("docs/AI_GENERATED_DESKTOP_SANDBOX.md");
assert.match(sandbox, /Only the explicit Apply command may cross the commit boundary/,
  "the documented commit boundary must stay put");
assert.doesNotMatch(creator, /InstallToLibrary\([^)]*\);\s*\n\s*InstallGeneratedPackage\(true\)/,
  "auto-install must not also apply to the desktop");

// Applying a widget mints a new instance; that guard must be untouched.
assert.match(creator, /generatedPackage != appliedPackageRoot/,
  "apply must stay gated on the package not already applied this session");

console.log("creator progress line, validation-error feedback loop and auto-install: PASS");
