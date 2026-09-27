import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const panel = read("src/ui/ai/ConversationPanelImpl.inc");

// AI-02's "重试不得自动重复提交" has no commit ledger behind it, so the only honest
// protections are the two that do not need one:
//
//   1. A cancelled turn must not stay retryable. /retry re-sends the prompt verbatim
//      into the same Pi session, whose history still holds the earlier turn's tool
//      calls -- so cancel-then-retry replays committed work from scratch.
//   2. A transport failure in a turn that already ran a tool must not invite /retry.
//      That is the sequence that produces a second .pptx: commit, fail to send the
//      final response, offer the retry, re-run the committing tool.
//
// Neither stops a user who insists on typing /retry; removing the invitation and the
// auto-retained seed is the part that can be done without a ledger.

function fn(signature) {
  const start = panel.indexOf(signature);
  assert.notStrictEqual(start, -1, `${signature} must exist`);
  let depth = 0;
  for (let i = panel.indexOf("{", start); i < panel.length; i += 1) {
    if (panel[i] === "{") depth += 1;
    else if (panel[i] === "}") {
      depth -= 1;
      if (depth === 0) return panel.slice(start, i + 1);
    }
  }
  throw new Error(`could not brace-match ${signature}`);
}

// 1. Cancel drops the retry seed.
const stop = fn("void StopTurn(ConversationState& state) {");
assert.match(stop, /state\.lastPrompt\.clear\(\);/,
  "StopTurn must clear the retry seed, or /retry replays a cancelled turn");
assert.match(stop, /state\.pi->Stop\(\)/,
  "StopTurn must still stop the runtime");

// 2. The turn's tool usage is tracked and gates the retry invitation.
assert.match(panel, /bool turnRanTool\{\};/,
  "the state must record whether this turn ran a tool");

// Not brace-matched: StartModelTurn's default argument `attachments = {}` contains
// balanced braces, which ends the match before the body. Check the reset in place.
assert.match(panel, /state\.lastPrompt = actualPrompt;\s*\n\s*state\.turnRanTool = false;/,
  "each turn must start with the tool flag clear");

const toolStart = fn("void UpdateToolStart(ConversationState& state, const std::wstring& tool) {");
assert.match(toolStart, /state\.turnRanTool = true;/,
  "running a tool must mark the turn");

const finish = fn("void FinishTurn(ConversationState& state, const std::wstring& rawDone, bool classifyFailure) {");
assert.match(finish, /if \(state\.turnRanTool\) \{[\s\S]*?没有提供 \/retry/,
  "a turn that ran tools must not be invited to retry");
assert.match(finish, /\} else if \(!state\.lastPrompt\.empty\(\)\) \{\s*\n\s*state\.streaming \+= L"\\r\\n可以输入 \/retry 重试上一请求。";/,
  "a turn that ran no tool must still offer the retry");

// UpdateToolStart must only be reachable during a live turn, otherwise a stale tool
// name could poison the flag for the next turn's retry decision.
const overlay = read("src/ui/ai/ConversationPanelInputOverlay.inc");
const activityHandler = overlay.slice(
  overlay.indexOf("if (message == kConversationPiActivityMessage) {"),
  overlay.indexOf("if (message == kDeltaMessage && state) {")
);
assert.match(activityHandler, /state->busy &&/,
  "activity events must be gated on the turn still running");
assert.match(activityHandler, /ApplySemanticActivity/,
  "activity events must flow through ApplySemanticActivity");

console.log("retry commit safety: PASS");
