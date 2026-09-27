import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");

// Regenerating left the previous package looking current.
//
// SendPrompt does not tear the preview down, and that is a deliberate choice -- tearing it
// down would leave the user with a blank pane for the whole round and nothing to look at.
// But nothing said so either: the previous Scene keeps animating, the pane keeps its label,
// and when the new package lands it is indistinguishable from the one that was there
// before. On top of that, the previous round's failure message ("本轮未产出可用内容包 ·
// 当前预览仍是上一版候选") is the only place the user was ever told -- a status line they
// read once, minutes before the regenerate that made it relevant again.

assert.match(creator, /PreviewSandboxState::Loading: return busy \? L"生成中（上一版预览）" : L"加载中";/,
  "the preview heading must name the pane as last round's while a turn is running");
// The badge must be drawn in BOTH paint branches, because the one that matters -- a
// regenerating Scene wallpaper -- is the early-returning live branch.
const pane = creator.slice(
  creator.indexOf("void DrawPreviewPane(const DRAWITEMSTRUCT* draw) const {"),
  creator.indexOf("\n    }", creator.indexOf("void DrawPreviewPane(const DRAWITEMSTRUCT* draw) const {"))
);
const badges = [...pane.matchAll(/上一版预览 · 生成中/g)];
assert.equal(badges.length, 2,
  `the staleness badge must be drawn on both paint branches (live scene and placeholder);`
  + ` found ${badges.length}. The live branch returns early, so a single badge at the end`
  + ` never renders during a Scene regeneration -- which is the main case.`);

// The live preview must NOT be torn down on send: that would leave a blank pane for the
// whole round. The badge is the answer instead.
const bodyOf = (text, signature) => {
  const at = text.indexOf(signature);
  assert.notStrictEqual(at, -1, `cannot find ${signature}`);
  const open = text.indexOf("{", at);
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === "{") ++depth;
    else if (text[i] === "}") {
      --depth;
      if (depth === 0) return text.slice(open, i + 1);
    }
  }
  assert.fail(`unbalanced braces after ${signature}`);
};

const sendBody = bodyOf(creator, "void SendPrompt() {");
assert.doesNotMatch(sendBody, /StopLivePreview\(/,
  "SendPrompt must not tear down the preview -- the badge is what marks it stale");

// The busy flag is what drives both, so it has to be set before the first paint and be
// readable from DrawPreviewPane (it is a const member function).
assert.match(creator, /SetBusy\(true\);\s*\n\s*generatedPackageIsCurrentRound = false;/,
  "the busy latch must be set at the start of the round");
assert.match(pane, /if \(busy\) \{/, "the paint path must read the busy latch");

console.log("preview pane marks itself as last round while generating: PASS");
