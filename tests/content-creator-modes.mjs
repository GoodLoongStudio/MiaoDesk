import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");

const header = read("src/include/miaodesk/ConversationPanel.h");
const panel = read("src/ui/ai/ConversationPanel.cpp");
const panelImpl = read("src/ui/ai/ConversationPanelImpl.inc");
const search = read("src/ui/search/SearchWindow.cpp");
const bridge = read("src/desktop/control/ContentCreatorBridge.cpp");
const wallpaperSkill = read("skills/wallpaper-content/SKILL.md");
const widgetSkill = read("skills/widget-content/SKILL.md");

assert.match(header, /enum class ConversationPanelMode[\s\S]*WallpaperCreator[\s\S]*WidgetCreator/);
assert.match(header, /ConversationPanelMode mode = ConversationPanelMode::General/);

assert.match(panel, /妙喵 · 壁纸 AI/);
assert.match(panel, /妙喵 · 组件 AI/);
assert.match(panel, /描述你想制作的壁纸/);
assert.match(panel, /描述你想制作的桌面组件/);
assert.match(panel, /state\.mode != mode/);
assert.match(panel, /ClearConversation\(state\)/);

assert.match(panelImpl, /ConversationWelcome\(ConversationPanelMode mode/);
assert.match(panelImpl, /content-package-basics → wallpaper-content → content-review/);
assert.match(panelImpl, /content-package-basics → widget-content → content-review/);
assert.match(panelImpl, /state\.headerTitle/);

assert.match(search, /ContentCreatorKind::Widget[\s\S]*ConversationPanelMode::WidgetCreator/);
assert.match(search, /ConversationPanelMode::WallpaperCreator/);
assert.match(search, /ShowConversationPanel\(instance_, hwnd_, l3_, prompt, mode\)/);

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
assert.match(wallpaperSkill, /\.mdwall/);
assert.match(widgetSkill, /\.mdwidget/);

console.log("content creator mode contract: PASS");
