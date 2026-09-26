import fs from "node:fs";
import assert from "node:assert/strict";

const source = fs.readFileSync("src/ui/settings/DesktopAiSettingsPage.cpp", "utf8");

for (const required of [
  "WS_VSCROLL",
  "WS_HSCROLL",
  "WM_VSCROLL",
  "WM_HSCROLL",
  "WM_MOUSEWHEEL",
  "WM_MOUSEHWHEEL",
  "UpdateScrollMetrics",
  "HandleScroll",
  "HandleMouseWheel",
  "SetScrollPosition",
  "SetScrollInfo",
  "GetScrollInfo",
  "scrollX",
  "scrollY",
  "contentWidth",
  "contentHeight",
  "viewportWidth",
  "viewportHeight",
]) {
  assert.ok(source.includes(required), `DesktopAiSettingsPage must keep scroll contract: ${required}`);
}

assert.ok(
  source.includes("x - scrollX") && source.includes("y - scrollY"),
  "child controls must move with both scroll offsets",
);
assert.ok(
  source.includes("SetViewportOrgEx(dc, -scrollX, -scrollY"),
  "custom-drawn panels/labels must use the same scroll origin as child controls",
);
assert.ok(
  source.includes("contentWidth = std::max(viewportWidth, S(900))"),
  "horizontal scrolling must appear only below the adaptive minimum canvas width",
);
assert.ok(
  source.includes("contentHeight = std::max(viewportHeight, S(760))"),
  "vertical scrolling must appear only below the adaptive minimum canvas height",
);
assert.ok(
  source.includes("MK_SHIFT"),
  "Shift + mouse wheel must route to horizontal scrolling",
);

console.log("API settings scroll contract: PASS");
