import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");

// InputImeAnchor identifies its target EDIT and window by comparing control ids and
// window class names against constants that are ALSO declared in the surfaces that
// create those controls. Nothing links the copies: change kSearchEditId in
// SearchWindow.cpp and IsMiaoDeskSearchEdit starts returning false, the whole IME
// anchor silently stops working, and every gate stays green because the constants
// are all individually valid.
//
// The conversation control id has three copies. The conversation window class has
// two. This test is the missing link.

const anchor = read("src/include/miaodesk/InputImeAnchor.h");
const search = read("src/ui/search/SearchWindow.cpp");
const convImpl = read("src/ui/ai/ConversationPanelImpl.inc");
const overlay = read("src/ui/ai/ConversationPanelInputOverlay.inc");

const constant = (source, name) => {
  const m = new RegExp(
    `(?:constexpr\\s+)?(?:int|wchar_t)\\s+${name}(?:\\s*\\[\\])?\\s*=\\s*(.+?);`
  ).exec(source);
  if (!m) throw new Error(`${name} not found`);
  return m[1].trim();
};
const intConstant = (source, name) => {
  const value = Number(constant(source, name).replace(/^L?"|"$/g, ""));
  assert.ok(Number.isFinite(value), `${name} is not an integer: ${value}`);
  return value;
};
const stringConstant = (source, name) =>
  constant(source, name).replace(/^L"/, "").replace(/"$/, "");

// --- search surface ------------------------------------------------------------
assert.strictEqual(intConstant(anchor, "kSearchEditControlId"), intConstant(search, "kSearchEditId"),
  "the IME anchor's search control id must match the id SearchWindow creates its EDIT with");
assert.strictEqual(intConstant(anchor, "kSearchEditLeft"), intConstant(search, "kEditLeft"),
  "the anchor's left edge must match the search EDIT's left edge");
assert.strictEqual(intConstant(anchor, "kSearchEditRight"), intConstant(search, "kEditRight"),
  "the anchor's right edge must match the search EDIT's right edge");

// The class name is a bare literal inside HasWindowClass, so match it directly.
const anchorSearchClass = /HasWindowClass\(hwnd, L"([^"]+)"\)/.exec(anchor);
assert.ok(anchorSearchClass, "the anchor must name the search window class");
assert.strictEqual(anchorSearchClass[1], /wc\.lpszClassName = L"([^"]+)"/.exec(search)[1],
  "the anchor's search window class must match the one actually registered");

// --- conversation surface ------------------------------------------------------
assert.strictEqual(intConstant(anchor, "kConversationEditControlId"), intConstant(convImpl, "kInputId"),
  "the anchor's conversation control id must match the panel's input id");
assert.strictEqual(intConstant(convImpl, "kInputId"), intConstant(overlay, "kConversationInputControlId"),
  "the panel's input id is declared twice and the copies must agree");

const anchorConvClass = anchor.slice(anchor.indexOf("inline bool IsMiaoDeskConversationWindow"));
const convClassLiteral = /HasWindowClass\(hwnd, L"([^"]+)"\)/.exec(anchorConvClass);
assert.ok(convClassLiteral, "the anchor must name the conversation window class");
assert.strictEqual(convClassLiteral[1], stringConstant(convImpl, "kConversationClass"),
  "the anchor's conversation window class must match the one actually registered");

// --- the anchored rectangle must fit the window it anchors into ---------------
// kSearchEditTop/Height are the real input row the WM_SIZE handler now installs. If
// they overrun the bar, the anchor points below the window and the composition and
// candidate windows land off-surface with nothing failing.
const top = intConstant(anchor, "kSearchEditTop");
const height = intConstant(anchor, "kSearchEditHeight");
const barHeight = intConstant(search, "kBarHeight");
assert.ok(top >= 0, "the input row must start inside the window");
assert.ok(height > 0, "the input row must have height");
assert.ok(top + height <= barHeight,
  `the anchored input row (${top}+${height}) must fit inside the ${barHeight}px bar`);

console.log("ime anchor constant agreement: PASS");
