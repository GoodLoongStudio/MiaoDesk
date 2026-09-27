import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const bridge = read("src/ui/ai/ConversationPanelPreviewBridge.inc");

// A sandbox preview is created by a native tool inside a turn, and the COPYDATA
// hand-off to the search window had no notion of whether that turn was still
// running. PiRuntime::Stop does not join its worker, so a tool that had already
// reached NotifyMainProcess still delivered its preview after the user cancelled
// and was told "已停止。"; the window came up with a live 应用 button, one click
// from changing the desktop for a request the user had abandoned.

const handler = bridge.slice(
  bridge.indexOf("LRESULT CALLBACK SearchPreviewBridgeProc("),
  bridge.indexOf("LRESULT CALLBACK ConversationPreviewBridgeProc(")
);

assert.match(handler, /!gConversationState->pi->Busy\(\)/,
  "the hand-off must be gated on the shared runtime still having a turn in flight");
assert.match(handler, /return TRUE;/,
  "a preview for a finished turn must be swallowed, not forwarded");

// The signal must be the shared runtime, not this panel's own busy flag: the creator
// dialog drives the same runtime, so keying on the panel would drop previews that a
// creator turn legitimately spawned.
assert.doesNotMatch(handler, /gConversationState->busy\b/,
  "the gate must not use the panel's own busy flag -- the creator shares the runtime");
assert.match(handler, /gConversationState && gConversationState->pi &&/,
  "the gate must pass through safely when the runtime is not wired up yet");

// Confirm the shared-runtime claim on both sides, or the reasoning above is wrong.
const panel = read("src/ui/ai/ConversationPanelImpl.inc");
assert.match(panel, /state->pi = &gPiRuntime;/, "the panel must use the shared runtime");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");
assert.match(creator, /SharedConversationPiRuntime\(\)/,
  "the creator must use the same shared runtime");
assert.match(read("src/include/miaodesk/PiRuntime.h"),
  /bool Busy\(\) const noexcept \{ return busy_\.load\(\); \}/,
  "PiRuntime must expose Busy()");

console.log("preview hand-off turn guard: PASS");
