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

assert.match(handler, /!gConversationState->pi->TurnActive\(\)/,
  "the hand-off must be gated on the shared runtime still having a turn in flight");
// The swallow must be the statement *inside* that guard, not just any `return TRUE`
// in the handler -- there is a second one on the forward path, and a bare
// /return TRUE;/ passes even after the guard has been gutted. Adjacency is the
// contract here, so it is what gets asserted.
const swallow = handler.match(
  /!gConversationState->pi->TurnActive\(\)\)\s*\{\s*return TRUE;\s*\}/);
assert.ok(swallow,
  "a preview for a finished turn must be swallowed inside the guard, not forwarded");
assert.match(handler, /return TRUE;/,
  "the handler must still return TRUE on its own paths (the swallow is not the only one)");

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
// PiRuntime must still expose the accessor the bridge above calls. Assert the
// *invariant*, not a spelling: AI-03 replaced the two-state `busy_` bool with a
// three-state turn phase (Idle / Running / Stopping), and the body of these
// accessors is an implementation detail. What this gate actually depends on is:
//   * TurnActive() exists, answers from the phase, and is never a constant;
//   * Stopping is NOT active -- a cancel the worker has not yet honoured must
//     still swallow the preview, because the user was already told "已停止。".
// Pinning an old body here would fail for the wrong reason: it went red on a
// deliberate improvement rather than on a defect.
const pi = read("src/include/miaodesk/PiRuntime.h");
const activeDecl = pi.match(/bool TurnActive\(\) const noexcept \{([^}]*)\}/);
assert.ok(activeDecl, "PiRuntime must expose TurnActive() const noexcept with a body");
const activeBody = activeDecl[1];
assert.match(activeBody, /turnPhase_/,
  "TurnActive() must be derived from the turn's phase -- that is the signal this gate guards");
assert.doesNotMatch(activeBody, /return (true|false)\s*;/,
  "TurnActive() must answer from state, not a literal -- a constant would make this " +
  "guard pass forever while swallowing every preview");
assert.match(activeBody, /==\s*turn_lifecycle::TurnPhase::Running/,
  "TurnActive() must be true only for Running -- Stopping is a cancel the user has " +
  "already been told about, and a preview from it must not raise the window");
assert.doesNotMatch(activeBody, /!=\s*turn_lifecycle::TurnPhase::Idle/,
  "TurnActive() must not be the same predicate as Busy() -- that would forward " +
  "previews for turns the user already cancelled");

// Busy() must stay a separate question ("may I start another turn"), because the
// other 14 readers of it need "cancel requested" to count as busy.
assert.match(pi, /bool Busy\(\) const noexcept \{[^}]*!=\s*turn_lifecycle::TurnPhase::Idle/,
  "Busy() must still count Stopping as busy -- that is the AI-03 invariant");

console.log("preview hand-off turn guard: PASS");
