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

assert.match(header, /PiRuntime& SharedConversationPiRuntime() noexcept/);
assert.match(panel, /PiRuntime& SharedConversationPiRuntime() noexcept[sS]*return gPiRuntime/);

assert.match(creatorHeader, /ShowContentCreatorDialog/);
assert.match(creator, /MiaoDesk.Native.ContentCreatorDialog/);
assert.match(creator, /AI 制作壁纸/);
assert.match(creator, /AI 制作组件/);
assert.match(creator, /content-package-basics/);
assert.match(creator, /wallpaper-content/);
assert.match(creator, /widget-content/);
assert.match(creator, /content-review/);
assert.match(creator, /ExecuteNativeToolRaw("content_skill_get"/);
assert.match(creator, /SharedConversationPiRuntime()/);
assert.match(creator, /pi->AskAsync(/);
assert.match(creator, /InstallContentPackage(/);
assert.match(creator, /CreateContentWidget(/);
assert.match(creator, /ApplyLibraryItem(/);
assert.match(creator, /MiaoContentPackage::Load(/);
assert.match(creator, /FindGeneratedPackagePath/);
assert.match(creator, /EnableWindow(preview, TRUE)/);
assert.match(creator, /EnableWindow(library, TRUE)/);
assert.match(creator, /EnableWindow(apply, TRUE)/);
assert.match(creator, /previewPane/);
assert.match(creator, /LoadPreviewBitmap/);
assert.match(creator, /MiaoSceneD2DRenderer/);
assert.match(creator, /StartLivePreview/);
assert.match(creator, /DrawLivePreview/);
assert.match(creator, /kPreviewTimerId/);
assert.match(creator, /FindGeneratedPackageDirectoryCandidate/);
assert.match(creator, /已自动补齐内容包扩展名/);
assert.match(creator, /DrawPrimaryAction/);
assert.match(creator, /CreatorPreset/);
assert.match(creator, /天气组件/);
assert.match(creator, /治愈猫咪/);
assert.match(creator, /Regenerate\(\)/);

assert.match(search, /creator::ShowContentCreatorDialog(instance_, hwnd_, l3_, kind)/);
assert.doesNotMatch(search, /OpenContentCreator[sS]{0,800}ShowConversationPanel/);

assert.match(library, /✨ AI 制作壁纸/);
assert.match(library, /✨ AI 制作组件/);
assert.match(library, /place(creatorButton, creatorLeft, S(11), creatorW, S(36))/);

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
