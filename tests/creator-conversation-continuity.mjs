import assert from "node:assert/strict";
import fs from "node:fs";

const creator = fs.readFileSync("src/ui/ai/ContentCreatorDialog.cpp", "utf8");

assert.match(
  creator,
  /CreatorConversationPath\(ContentCreatorKind kind\)[\s\S]*conversation\.txt/,
  "creator transcript must live in the active creator workspace",
);

assert.match(
  creator,
  /bool RestoreTranscript\(\)[\s\S]*LoadCreatorConversation\(kind\)[\s\S]*SetWindowTextW\(transcript,[\s\S]*restoredContextPending = true/,
  "reopening the creator must restore the visible transcript and mark context for resume",
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
  /void ResetSession\(\)[\s\S]*ClearCreatorConversation\(kind\)[\s\S]*SaveTranscript\(\)/,
  "only the explicit new-conversation action clears persisted history",
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
