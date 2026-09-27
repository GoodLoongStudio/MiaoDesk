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

const UI_IMPLS = [
  "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp",
  "src/ui/ai/ContentCreatorDialog.cpp",
  "src/ui/ai/ConversationPanelInputOverlay.inc",
  "src/ui/automation/WallpaperAutomationWindow.cpp",
  "src/ui/automation/WallpaperApplicationRulesWindow.cpp",
  "src/ui/wallpaper/ContentPackageManagerDialog.cpp",
  "src/ui/wallpaper/ContentSkillBrowserDialog.cpp",
  "src/ui/wallpaper/ContentWidgetSettingsDialog.cpp",
  "src/ui/wallpaper/TodayTaskEditorDialog.cpp",
];

let checked = 0;
const failures = [];
for (const file of UI_IMPLS) {
  const text = read(file);
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
        `${file}: \`${name}\` is declared and assigned but never invoked.\n`
        + `      ${where.join("\n      ")}`);
    }
  }
}

assert.deepStrictEqual(failures, [],
  "callback members that are stored but never called -- each one is a seam that reads as live"
  + " behaviour and is not:\n  " + failures.join("\n  "));
assert.ok(checked >= 3, `expected the UI callbacks to be surveyed, got ${checked}`);

// The public API must not grow a callback parameter that nothing invokes either.
const libraryHeader = read("src/include/miaodesk/WallpaperLibraryWindow.h");
assert.doesNotMatch(libraryHeader, /NavigateCallback/,
  "the library window must not re-introduce a navigation callback: the window owns its own"
  + " page switching, and a callback it never calls reads as an integration point");
assert.match(libraryHeader, /using ApplyCallback = std::function<void\(const WallpaperLibraryItem&, const std::wstring& targetMonitorId\)>;/,
  "the apply callback must stay -- it IS invoked");
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
