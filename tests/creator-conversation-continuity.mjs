import assert from "node:assert/strict";
import fs from "node:fs";

const creator = fs.readFileSync("src/ui/ai/ContentCreatorDialog.cpp", "utf8");

assert.match(
  creator,
  /CreatorConversationPath\(ContentCreatorKind kind\)[\s\S]*conversation\.txt/,
  "creator transcript must live in the active creator workspace",
);

// Reopening must (a) put the saved transcript in front of the user, and
// (b) mark context for resume only when there is a real conversation to
// resume from. This used to assert the literal `restoredContextPending = true`,
// which the source no longer does -- and the source is the one that's right.
// Setting the flag unconditionally makes the app send the model an empty
// "【最近对话记录】" block while telling the user "已恢复上次对话". So the
// assertion now pins the invariant instead of the old spelling: the visible
// transcript is restored unconditionally, and the resume flag is derived from
// a non-empty user prompt.
assert.match(
  creator,
  /bool RestoreTranscript\(\)[\s\S]*LoadCreatorConversation\(kind\)[\s\S]*SetWindowTextW\(transcript, saved/,
  "reopening the creator must restore the visible transcript",
);
assert.match(
  creator,
  /lastUserPrompt = LastUserPromptFromTranscript\(saved\);\s*const bool hasRealConversation = !lastUserPrompt\.empty\(\);\s*restoredContextPending = hasRealConversation;/,
  "context for resume must be marked only when the saved transcript has a real user prompt",
);

assert.match(
  creator,
  /void InitializeConversation\(\)[\s\S]*RestoreTranscript\(\)[\s\S]*GreetingText/,
  "window creation must restore history before falling back to a new greeting",
);

assert.match(
  creator,
  /PopulateApiProfiles\(\);\s*InitializeConversation\(\);\s*LoadSkill\(\);/,
  "CreateControls must not call ResetSession, because opening a window is not a new conversation",
);

assert.match(
  creator,
  /void ResetSession\(\)[\s\S]*StartNewCreatorWorkspace\(kind\)[\s\S]*UseWorkspace\(workspace\)/,
  "the explicit new-conversation action must preserve old history and create a fresh workspace",
);
assert.match(
  creator,
  /void ShowHistoryMenu\(\)[\s\S]*ListCreatorWorkspaces\(kind\)[\s\S]*UseWorkspace\(visible\[index\]\)/,
  "history must switch back to the selected creator workspace",
);

assert.match(
  creator,
  /case WM_CLOSE:[\s\S]*state->SaveTranscript\(\);[\s\S]*DestroyWindow/,
  "closing the creator must persist transcript before destroying the window",
);

assert.match(
  creator,
  /这是关窗或重启前保存下来的同一段对话/,
  "the first turn after restart must receive the saved conversation context",
);

assert.doesNotMatch(
  creator,
  /void AppendTranscript\(std::wstring_view text\)\s*\{\s*AppendTranscript\(/,
  "AppendTranscript must append to the control, not recurse into itself",
);

console.log("creator conversation continuity contract: ok");
