import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");
const read = (p) => fs.readFileSync(path.join(root, p), "utf8");

// A `std::function` member that is assigned and never invoked is a dead seam.
//
// This one cost real work: WallpaperLibraryWindow had a NavigateCallback. The library
// stored it, WallpaperEngine passed a lambda believing it drove the nav -- with branches for
// Playlists / Displays / Performance / AI and a MessageBox telling the user where the AI
// configuration lived -- and nothing ever called it. The window handles its own nav clicks
// (HandleNav -> SetPage), and its AI nav already shows the API page inline.
//
// So the sinkhole is not "a callback is unused", which is trivial to see once you look. It is
// that the dead end is invisible from the outside: I read the engine's branch, concluded the
// AI nav was a pointer with no path, and designed a cross-process fix for behaviour that
// cannot occur. A whole feature was nearly added to a switch nobody executes.

// Survey per TRANSLATION UNIT, not per file. The conversation panel is assembled from six
// .inc files #included by one .cpp; a callback member declared in one of them is invoked in
// another, so a per-file survey reports dead members for perfectly live ones.
const TRANSLATION_UNITS = [
  {
    name: "ConversationPanel.cpp",
    files: [
      "src/ui/ai/ConversationPanel.cpp",
      "src/ui/ai/ConversationPanelImpl.inc",
      "src/ui/ai/ConversationPanelLayeredSurface.inc",
      "src/ui/ai/ConversationPanelInputOverlay.inc",
      "src/ui/ai/ConversationPanelCornerResize.inc",
      "src/ui/ai/ConversationPanelImageIntent.inc",
      "src/ui/ai/ConversationPanelPreviewBridge.inc",
    ],
  },
  { name: "WallpaperLibraryWindowV2.cpp", files: ["src/ui/wallpaper/WallpaperLibraryWindowV2.cpp"] },
  { name: "ContentCreatorDialog.cpp", files: ["src/ui/ai/ContentCreatorDialog.cpp"] },
  { name: "WallpaperAutomationWindow.cpp", files: ["src/ui/automation/WallpaperAutomationWindow.cpp"] },
  { name: "WallpaperApplicationRulesWindow.cpp", files: ["src/ui/automation/WallpaperApplicationRulesWindow.cpp"] },
  { name: "ContentPackageManagerDialog.cpp", files: ["src/ui/wallpaper/ContentPackageManagerDialog.cpp"] },
  { name: "ContentSkillBrowserDialog.cpp", files: ["src/ui/wallpaper/ContentSkillBrowserDialog.cpp"] },
  { name: "ContentWidgetSettingsDialog.cpp", files: ["src/ui/wallpaper/ContentWidgetSettingsDialog.cpp"] },
  { name: "TodayTaskEditorDialog.cpp", files: ["src/ui/wallpaper/TodayTaskEditorDialog.cpp"] },
];

let checked = 0;
const failures = [];
for (const unit of TRANSLATION_UNITS) {
  const text = unit.files.map(read).join("\n");
  // (CallbackType) memberName;   and   CallbackType memberName;
  for (const m of text.matchAll(/^\s*((?:using\s+)?\w*[Cc]allback\w*)\s+(\w+)\s*(?:=\s*[^;]*)?;/gm)) {
    const name = m[2];
    // `using X = std::function<...>` (a typedef) does not match this shape, so any name
    // reaching here is a member -- including the ones that merely *contain* "Callback".
    ++checked;

    // Occurrences outside this declaration: the header that declares the typedef does not
    // count as a call, and neither does the constructor's default member initialiser.
    const callPattern = new RegExp(`\\b${name}\\s*\\(`, "g");
    const plainPattern = new RegExp(`\\b${name}\\b`, "g");
    const declarationLine = text.slice(0, m.index).split("\n").length;
    const lines = text.split("\n");
    const linesWithName = lines
      .map((line, i) => ({ line, i }))
      .filter(({ line }) => plainPattern.test(line));

    // A store (`x = std::move(member)` / `member = ...`) is not a call. A real invocation
    // calls it: `member(...)`.
    const invoked = linesWithName.some(({ line: l }) => callPattern.test(l));
    if (!invoked) {
      const where = linesWithName.slice(0, 3).map(({ line, i }) => `${i + 1}: ${line.trim().slice(0, 70)}`);
      failures.push(
        `${unit.name}: \`${name}\` is declared and assigned but never invoked.\n`
        + `      ${where.join("\n      ")}`);
    }
  }
}

assert.deepStrictEqual(failures, [],
  "callback members that are stored but never called -- each one is a seam that reads as live"
  + " behaviour and is not:\n  " + failures.join("\n  "));
assert.ok(checked >= 3, `expected the UI callbacks to be surveyed, got ${checked}`);

// The same survey for plain data members: a field that is assigned and never read is not a
// seam that looks live, but it is still a lie about the design. ActivityCard::terminal was
// exactly this -- written to false on every tool start, read by nothing, superseded by
// ClearActivityCard() which resets the whole card. Leaving it there makes "terminal" look
// like a state the card has.
//
// Restricted to this panel's TU because that is where the class lives and where the growth
// of unreferenced state was observed; extending it everywhere is a separate change.
const panelTu = TRANSLATION_UNITS[0];
const panelText = panelTu.files.map(read).join("\n");
assert.doesNotMatch(panelText, /\bterminal\{\}/,
  "ActivityCard::terminal was written once and never read -- it must not come back");
assert.doesNotMatch(panelText, /activity\.terminal/,
  "...and nothing may read a `terminal` state the card does not have");

// The public API must not grow a callback parameter that nothing invokes either.
const libraryHeader = read("src/include/miaodesk/WallpaperLibraryWindow.h");
assert.doesNotMatch(libraryHeader, /NavigateCallback/,
  "the library window must not re-introduce a navigation callback: the window owns its own"
  + " page switching, and a callback it never calls reads as an integration point");
// The apply callback must REPORT, not just fire. It returned void until the false success
// was found: `applied` was pinned true on the engine path, so a bail-out still produced
// "已应用到桌面" and a MarkUsed stamp. See library-apply-reports-outcome.mjs for the full
// argument and for the assertions that keep it reporting.
assert.match(libraryHeader, /using ApplyCallback = std::function<std::wstring\(const WallpaperLibraryItem&, const std::wstring& targetMonitorId\)>;/,
  "the apply callback must stay -- it IS invoked -- and must return the outcome so the"
  + " window can tell the user when the apply did not happen");
const libraryImpl = read("src/ui/wallpaper/WallpaperLibraryWindowV2.cpp");
assert.doesNotMatch(libraryImpl, /WallpaperSettingsSection/,
  "...and its SectionForNav mapper must not come back");
assert.doesNotMatch(libraryHeader, /WallpaperSettingsSection/,
  "...and the section enum it parameterised must not come back: with no navigator reading it"
  + " it is a type that exists only to describe a route that is never taken");
assert.match(libraryImpl, /void HandleNav\(int id\) \{[\s\S]*?SetPage\(Page::AI\);/,
  "the AI nav must stay the window's own action -- switching to the inline API page is what"
  + " it does, and that is why the old callback branch was unreachable");

console.log(`UI callback members are all invoked: PASS (${checked} surveyed)`);
