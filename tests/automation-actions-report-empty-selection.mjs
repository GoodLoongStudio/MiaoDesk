import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const automation = read("src/ui/automation/WallpaperAutomationWindow.cpp");

// The automation window gates nothing, and six of its handlers assumed something else had.
//
// There is no EnableWindow call anywhere in this file. Every button is live from the
// moment it is drawn. So "no selection" is not a state a click cannot land in -- it is the
// state the window opens in whenever the automation store is empty, which is every fresh
// install. Six handlers returned silently on exactly that:
//
//   ApplySelectedProfile  DeleteSelectedProfile  DeletePlaylist
//   DeleteSchedule        AddPlaylistEntry       RemovePlaylistEntry
//
// Each of them read a selection out of a combo or list box, got nothing, and returned. No
// status line, no beep. Clicking 应用 on an empty Profile list was indistinguishable from
// clicking a dead region of the window.
//
// The library sweep found the same shape twice this session (library-apply-failure-is-
// visible.mjs, every-library-action-talks-back.mjs), and both times the reasoning error was
// identical: a gate existed *nearby*, so the guard looked unreachable. Here there is no
// nearby gate at all -- so the check is simply that every selection guard reports, with no
// exemptions to reason about.

const bodyOf = (text, signature) => {
  const at = text.indexOf(signature);
  assert.notStrictEqual(at, -1, `cannot find ${signature}`);
  const open = text.indexOf("{", at);
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === "{") ++depth;
    else if (text[i] === "}") {
      if (--depth === 0) return text.slice(open, i + 1);
    }
  }
  assert.fail(`unbalanced braces after ${signature}`);
};

// The body of the branch opened by a matched `if (...)`, as `match[0]` (which ends at the
// `return;`) plus whatever follows INSIDE that branch.
//
// It has to distinguish the two spellings, because searching for the next "{" blindly
// lands on some later statement's block: for an unbraced `if (!id) return;` the next "{"
// belongs to the failure path two statements down, which is precisely the feedback this
// check is asking about. Reading that block says "reports" for a guard that says nothing.
function guardBranch(body, match) {
  const afterCondition = match.index + match[0].search(/return;/);
  let i = match.index + match[0].length;
  while (i < body.length && /\s/.test(body[i])) ++i;
  if (body[i] !== "{") {
    // Unbraced: the branch is the single statement that was matched.
    return match[0];
  }
  let depth = 0;
  for (let j = i; j < body.length; ++j) {
    if (body[j] === "{") ++depth;
    else if (body[j] === "}") {
      if (--depth === 0) return body.slice(i, j + 1);
    }
  }
  return match[0];
}

const SAYS_SOMETHING = /SetStatus\(|MessageBoxW\(/;
const saysSomething = (text) => SAYS_SOMETHING.test(text);

// Handlers reachable from WM_COMMAND, read from the dispatch table so the set cannot drift.
const dispatched = [...automation.matchAll(/id == (k\w+Id) && notification == BN_CLICKED\) self->(\w+)\(/g)]
  .map((m) => ({ control: m[1], handler: m[2] }));
assert.ok(dispatched.length >= 4, `expected the window's action handlers, found ${dispatched.length}`);

const dupes = dispatched.map((d) => d.control).filter((id, i, all) => all.indexOf(id) !== i);
assert.deepStrictEqual(dupes, [],
  "each control may be dispatched once; the chain is else-if, so a second line is dead:\n  "
  + dupes.join("\n  "));

// The premise of this whole file: no gating. If this window ever grows EnableWindow, the
// analysis has to be redone rather than inherited.
const gating = [...automation.matchAll(/EnableWindow\(\w+,/g)];
assert.deepStrictEqual(gating, [],
  `this window now has EnableWindow call(s), so "every button is always live" is no longer`
  + ` true. Redo the analysis: guards that a control now prevents are exempt again, and`
  + ` silently-exempt ones are the defect. Do not just delete this assertion.`);

// A guard that reports is fine. Only a bare early exit is the shape under test.
const silent = [];
for (const { control, handler } of dispatched) {
  let body;
  try {
    body = bodyOf(automation, `void ${handler}() {`);
  } catch {
    continue;
  }
  // Both spellings, and both selection idioms: `if (!id) return;` and the list-box form
  // `if (sel == LB_ERR || sel < 0 || ...) return;`. Only the branch text matters, so this
  // is matched on the condition rather than the shape of the test.
  const bare = [...body.matchAll(/if \(([^;{}]*)\)\s*(?:\{\s*)?return;(?:\s*\})?/g)]
    .filter((m) => /(?:!id|LB_ERR|selected < 0|SelectedIndex|!current)/.test(m[1]))
    .filter((m) => {
      // Internal invariants cannot be produced by a click.
      if (["!automation", "!library", "!entriesList", "!profileCombo", "!applyDecision"].includes(m[1].trim())) return false;
      // THE BRANCH, not what follows it. The first version of this filter read 200
      // characters past the guard, which let a `SetStatus` belonging to a *later* branch --
      // the failure path two statements down -- satisfy a guard that said nothing at all.
      // That is a false negative in exactly the shape this file exists to catch, and it
      // hid ActivatePlaylist and NextPlaylist, whose only feedback is below them.
      return !saysSomething(guardBranch(body, m));
    });
  if (bare.length) {
    silent.push(`${handler} (${control}) drops the click silently:\n      `
      + bare.map((b) => `if (${b[1].trim().slice(0, 70)}) return;`).join("\n      "));
  }
}
assert.deepStrictEqual(silent, [],
  "automation handlers whose selection guard can be reached and which say nothing:\n  "
  + silent.join("\n  "));

// --- each fixed guard names what to select -------------------------------
// A beep on its own is not a fix: the message has to say which list to pick from, because
// this window has three combos and two list boxes and "没有选中" would not tell anyone
// which one.
const expected = {
  ApplySelectedProfile: "没有选中的 Profile；先保存一个，或从上方列表里选。",
  DeleteSelectedProfile: "没有选中的 Profile 可删除。",
  DeletePlaylist: "没有选中的 Playlist 可删除。",
  DeleteSchedule: "没有选中的 Schedule 可删除。",
  AddPlaylistEntry: "先在左侧选择一个壁纸库项目，再点添加。",
  ActivatePlaylist: "没有选中的 Playlist；先新建或选中一个播放列表。",
  NextPlaylist: "没有选中的 Playlist；先新建或选中一个播放列表。",
  RemovePlaylistEntry: "先在右侧选中一个要移出的条目，再点移除。",
};
for (const [handler, message] of Object.entries(expected)) {
  const body = bodyOf(automation, `void ${handler}() {`);
  assert.match(body, new RegExp(message.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")),
    `${handler} must name what to select, not just that nothing was`);
  assert.match(body, /MessageBeep\(MB_ICONERROR\)/,
    `${handler} must be audible -- a status line alone is easy to miss on a click that`);
}
assert.equal(Object.keys(expected).length, 8,
  "the eight handlers found above; a ninth silent one in this window means this map is stale");

console.log(`every automation action reports an empty selection: PASS (${dispatched.length} handlers)`);
