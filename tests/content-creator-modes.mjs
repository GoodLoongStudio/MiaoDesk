import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");

const header = read("src/include/miaodesk/ConversationPanel.h");
const panel = read("src/ui/ai/ConversationPanel.cpp");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");
const creatorHeader = read("src/include/miaodesk/ContentCreatorDialog.h");
const search = read("src/ui/search/SearchWindow.cpp");
const bridge = read("src/desktop/control/ContentCreatorBridge.cpp");
const library = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");
const wallpaperSkill = read("skills/wallpaper-content/SKILL.md");
const widgetSkill = read("skills/widget-content/SKILL.md");

assert.ok(header.includes("PiRuntime& SharedConversationPiRuntime() noexcept"));
assert.match(panel, /PiRuntime& SharedConversationPiRuntime\(\) noexcept[\s\S]*return gPiRuntime/);

assert.match(creatorHeader, /ShowContentCreatorDialog/);
assert.match(creator, /MiaoDesk.Native.ContentCreatorDialog/);
assert.match(creator, /AI 制作壁纸/);
assert.match(creator, /AI 制作组件/);
assert.match(creator, /content-package-basics/);
assert.match(creator, /wallpaper-content/);
assert.match(creator, /widget-content/);
assert.match(creator, /content-review/);
assert.ok(creator.includes('ExecuteNativeToolRaw("content_skill_get"'));
assert.ok(creator.includes("SharedConversationPiRuntime()"));
assert.ok(creator.includes("pi->AskAsync("));
assert.ok(creator.includes("InstallContentPackage("));
assert.ok(creator.includes("CreateContentWidget("));
assert.ok(creator.includes("ApplyLibraryItem("));
assert.ok(creator.includes("MiaoContentPackage::Load("));
assert.match(creator, /FindGeneratedPackagePath/);
assert.ok(creator.includes("EnableWindow(preview, TRUE)"));
assert.ok(creator.includes("EnableWindow(library, TRUE)"));
// Applying a widget mints a NEW instance (WidgetService::CreateContent takes no
// duplicate guard), so Apply must not stay live against an already-applied
// candidate -- a second click would add a duplicate widget.
assert.match(creator, /void UpdateApplyAvailability\(\) \{[\s\S]*?EnableWindow\(apply,[\s\S]*?generatedPackage != appliedPackageRoot/,
  "apply must be gated on the package not already having been applied this session");
assert.match(creator, /EnableWindow\(library, TRUE\);\s*\n\s*UpdateApplyAvailability\(\);/,
  "loading a valid package must route apply through the availability guard");
assert.match(creator, /appliedPackageRoot = generatedPackage;\s*\n\s*UpdateApplyAvailability\(\);/,
  "a successful apply must mark the package applied and disable apply");
assert.doesNotMatch(creator, /EnableWindow\(apply, TRUE\)/,
  "apply must never be enabled unconditionally");
assert.match(creator, /previewPane/);
assert.match(creator, /LoadPreviewBitmap/);
assert.match(creator, /MiaoSceneD2DRenderer/);
assert.match(creator, /StartLivePreview/);
assert.match(creator, /DrawLivePreview/);
assert.match(creator, /kPreviewTimerId/);
assert.match(creator, /PreviewSandboxState/);
assert.match(creator, /TogglePreviewPlayback/);
assert.match(creator, /ReloadPreview/);
assert.match(creator, /SetFullscreenPreview/);
assert.match(creator, /VK_ESCAPE/);
assert.match(creator, /VK_SPACE/);
assert.ok(creator.includes('button(L"暂停"') || creator.includes('SetWindowTextW(previewPlayPause, previewPlaying ? L"暂停" : L"播放")'));
assert.match(creator, /FindGeneratedPackageDirectoryCandidate/);
assert.match(creator, /已自动补齐内容包扩展名/);
assert.match(creator, /DrawPrimaryAction/);
assert.match(creator, /CreatorPreset/);
assert.match(creator, /天气组件/);
assert.match(creator, /治愈猫咪/);
assert.match(creator, /Regenerate\(\)/);

// The surface must be cancellable. It used to have no stop at all: SetBusy
// disabled the send button and relabelled it "生成中…", leaving 新对话 and
// closing as the only exits -- both of which reach the shared Pi runtime and
// stop the *conversation* panel's turn too. The send button now doubles as the
// stop button, matching the conversation surface, and stays enabled while busy.
assert.match(creator, /EnableWindow\(send, TRUE\);/,
  "the send button must stay enabled while a turn runs, or there is no way to cancel");
assert.doesNotMatch(creator, /L"生成中…"/,
  "the send button must not relabel itself into a dead disabled control");
assert.doesNotMatch(creator, /EnableWindow\(send, !value\);/,
  "send must not be disabled by SetBusy");
assert.match(creator, /void StopGenerating\(\)[\s\S]*?pi->Stop\(\)/,
  "stopping must actually reach the Pi runtime");
assert.match(creator, /bool stopRequested\{\}/,
  "the stop must be recorded so the done handler can report it");
assert.match(creator, /if \(id == kSendId && HIWORD\(wParam\) == BN_CLICKED\) \{\s*\n\s*if \(state->busy\) state->StopGenerating\(\);/,
  "clicking send while busy must cancel rather than submit");
assert.match(creator, /state->stopRequested = false;[\s\S]*?SetWindowTextW\(state->resultNote/,
  "a cancelled turn must be reported as cancelled, not as 请求结束");
assert.match(creator, /generatedPackageIsCurrentRound = false;\s*\n\s*stopRequested = false;/,
  "starting a new turn must clear the stop flag");

// A refused request must never be reported as a completed generation.
// The creator and the conversation panel share one PiRuntime but each keeps its
// own busy latch, so while the panel is mid-turn the creator's latch is clear,
// SendPrompt reaches AskAsync, and the runtime's refusal arrives here as an
// ordinary done string. This pins both the handling and the string coupling --
// if either side renames the message, the match below fails rather than the user
// quietly being told an unsent request "已完成".
assert.match(creator, /kBusyRejectionMarker\[\] = L"Pi Runtime 正忙"/,
  "the busy-rejection marker must be declared");
assert.match(creator, /if \(done && done->find\(kBusyRejectionMarker\) != std::wstring::npos\)/,
  "a refused request must be detected in kRequestDone");
assert.match(creator, /未发送，当前有任务在进行/,
  "a refused request must say it was not sent");

const runtime = read("src/ai/pi/PiRuntime.cpp");
const emitted = [...runtime.matchAll(/onDone\(L"([^"]*)"\)/g)].map((m) => m[1]);
assert.ok(emitted.includes("Pi Runtime 正忙"),
  "PiRuntime must still emit the busy rejection through onDone");
assert.ok(emitted.every((text) => text !== "本轮生成已完成"),
  "PiRuntime must not emit the creator's completion wording");

// A failed regenerate must not read as "apply the old generation anyway".
// SetGeneratedPackage returns early on a validation failure without clearing
// generatedPackage, so the previous candidate stays loaded with live Apply
// buttons. The result note has to say which round the visible package came from.
assert.match(creator, /bool generatedPackageIsCurrentRound\{\}/,
  "creator must track whether the loaded package came from the current round");
assert.match(creator, /generatedPackageIsCurrentRound = false;[\s\S]*?agent->ReloadConfig\(\)/,
  "a new round must clear the current-round marker before it runs");
assert.match(creator, /generatedPackage = path;\s*\n\s*generatedPackageIsCurrentRound = true;/,
  "resolving a package must mark it as belonging to the current round");
assert.match(creator, /!state->generatedPackageIsCurrentRound[\s\S]*?上一版候选，应用会使用它/,
  "a finished round that produced no package must disclose that the visible candidate is the previous one");

assert.ok(search.includes("creator::ShowContentCreatorDialog(instance_, hwnd_, l3_, kind)"));
assert.doesNotMatch(search, /OpenContentCreator[sS]{0,800}ShowConversationPanel/);

assert.match(library, /✨ AI 制作壁纸/);
assert.match(library, /✨ AI 制作组件/);
assert.ok(library.includes("place(creatorButton, creatorLeft, S(10), creatorW, S(38))"));

for (const skill of ["content-package-basics", "wallpaper-content", "content-review"]) {
  assert.ok(bridge.includes(skill), `wallpaper creator prompt must require ${skill}`);
}
for (const skill of ["content-package-basics", "widget-content", "content-review"]) {
  assert.ok(bridge.includes(skill), `widget creator prompt must require ${skill}`);
}
assert.ok(bridge.includes(".mdwall"), "wallpaper creator must be preview-first .mdwall");
assert.ok(bridge.includes(".mdwidget"), "widget creator must be preview-first .mdwidget");

assert.match(wallpaperSkill, /^name: wallpaper-content$/m);
assert.match(widgetSkill, /^name: widget-content$/m);
assert.match(wallpaperSkill, /.mdwall/);
assert.match(widgetSkill, /.mdwidget/);

console.log("content creator mode contract: PASS");
