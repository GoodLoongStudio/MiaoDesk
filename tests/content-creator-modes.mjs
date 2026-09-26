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
