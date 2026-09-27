import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const root = path.join(path.dirname(new URL(import.meta.url).pathname), "..");

// One invariant for every window whose keys the dialog manager routes.
//
// IsDialogMessageW consumes VK_ESCAPE and posts WM_COMMAND/IDCANCEL. So for a window
// whose pump calls it, "there is no IDCANCEL handling" is not neutral -- it means Esc is
// dead. That is not hypothetical: the defect has appeared six separate times in this
// repo, twice inside a single file, one level apart, where one window answered Esc and
// the one next to it did not.
//
// So: every surface below must either answer IDCANCEL, or appear in EXEMPT with a
// concrete reason. A seventh surface added without answering it fails here.

// Pumps: files that route keys on behalf of surfaces declared elsewhere, and the surfaces
// each one serves. `token` is the distinctive string that must appear in the pump's
// message loop for that surface, so "registered" cannot drift from "actually served" --
// the advanced settings window was added as a fourth surface to WallpaperEngine's pump
// and a later edit could quietly drop it from the array again.
const PUMPS = {
  "src/desktop/wallpaper/legacy/WallpaperEngine.cpp": {
    // The advanced settings window (壁纸/显示器/性能) is declared inside this same
    // translation unit, so it is a surface without its own file.
    "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp": "libraryWindow_.Window()",
    "src/ui/automation/WallpaperAutomationWindow.cpp": "automationWindow_.Window()",
    "src/ui/automation/WallpaperApplicationRulesWindow.cpp": "automationWindow_.RulesWindow()",
    "src/desktop/wallpaper/legacy/WallpaperEngine.cpp": "settings_",
  },
  "src/ui/search/SearchWindow.cpp": {
    "src/ui/ai/ContentCreatorDialog.cpp": "creator::DialogManagedCreatorWindows()",
  },
};

// Surfaces whose own nested pump routes their own keys.
const OWN_PUMP = [
  "src/ui/wallpaper/TodayTaskEditorDialog.cpp",
  "src/ui/wallpaper/ContentPackageManagerDialog.cpp",
  "src/ui/wallpaper/ContentSkillBrowserDialog.cpp",
  "src/ui/wallpaper/ContentWidgetSettingsDialog.cpp",
];

// (pump, surface, token) triples, and the pump's message loop body.
const SERVED = Object.entries(PUMPS).flatMap(([pump, surfaces]) =>
  Object.entries(surfaces).map(([surface, token]) => ({ pump, surface, token })));

// Surfaces that deliberately do not close on Esc. Each entry must state the reason the
// key is left dead -- an unexplained exemption is the defect this test exists to catch.
const EXEMPT = {
  "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp":
    "The settings centre hides instead of closing, and the AI/API page it hosts discards"
    + " unsaved edits by reloading its profile list every time it is shown, so Esc in the"
    + " API-key field would silently drop what was just typed. X and Alt+F4 close it"
    + " deliberately; Esc is the key people hit reflexively while in a text field.",
  "src/ui/ai/ContentCreatorDialog.cpp":
    "In fullscreen preview Esc is already bound to leaving the preview, and that window"
    + " opts out of the dialog manager entirely so the binding survives. Outside preview"
    + " the key belongs to the browser-style preview, not to discarding a transcript and a"
    + " generated package that took a turn to produce.",
};

const answersCancel = (text) => /(?:case\s+|id\s*==\s*)IDCANCEL\b/.test(text);

const allSources = [];
const walk = (dir) => {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === ".git" || entry.name === "node_modules") continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full);
    else if (/\.(cpp|inc|h)$/.test(entry.name)) allSources.push(path.relative(root, full));
  }
};
walk(path.join(root, "src"));

// 1. The set of files routing keys through the dialog manager must match the registry. A
//    pump that is not registered here is a pump whose Esc behaviour nobody has decided.
const actualPumps = allSources
  .filter((f) => /IsDialogMessageW\s*\(/.test(fs.readFileSync(path.join(root, f), "utf8")))
  .sort();
assert.deepStrictEqual(actualPumps.sort(), [...Object.keys(PUMPS), ...OWN_PUMP].sort(),
  "the set of files routing keys through IsDialogMessageW changed. If a surface was added,"
  + " decide its Esc behaviour and register it here; if one was removed, drop it.\n"
  + "  found:  " + actualPumps.join("\n          "));

// 1a) Every registered pump must actually name every surface it is supposed to serve.
for (const { pump, surface, token } of SERVED) {
  const pumpText = fs.readFileSync(path.join(root, pump), "utf8");
  const run = pumpText.slice(
    pumpText.indexOf("GetMessageW(&msg"),
    pumpText.indexOf("DispatchMessageW(&msg")
  );
  assert.ok(run.includes(token),
    `${pump} is registered as serving ${surface} but its message loop never mentions`
    + ` \`${token}\`. A pump that forgets a surface leaves its keys unrouted -- the`
    + ` surface becomes unreachable by keyboard again with nothing else failing.`);
}

// 2. Every registered surface must answer IDCANCEL, or be exempted with a reason.
const ALL_SURFACES = [...new Set([...SERVED.map((entry) => entry.surface), ...OWN_PUMP])];
for (const surface of ALL_SURFACES) {
  const file = path.join(root, surface);
  assert.ok(fs.existsSync(file), `${surface} is registered but does not exist`);
  const text = fs.readFileSync(file, "utf8");
  const exempt = Object.hasOwn(EXEMPT, surface);
  if (answersCancel(text)) {
    assert.ok(!exempt,
      `${surface} answers IDCANCEL but is also exempt -- pick one, or the exemption is stale`);
    continue;
  }
  assert.ok(exempt,
    `${surface} takes part in dialog keyboard navigation but never answers IDCANCEL.`
    + ` IsDialogMessageW consumes VK_ESCAPE and posts WM_COMMAND/IDCANCEL, so with no`
    + ` handler that key is dead. Either answer IDCANCEL (normally by doing exactly what`
    + ` the window's 关闭 button and WM_CLOSE do), or exempt it with a stated reason.`);
  assert.ok(EXEMPT[surface].length > 120,
    `${surface} is exempted but the reason is too thin to count as a decision`);
}

// 3. No exemption, and no pump, may name a surface that is not registered: a stale entry
//    hides a real regression.
for (const file of Object.keys(EXEMPT)) {
  assert.ok(ALL_SURFACES.includes(file), `EXEMPT lists ${file}, which is not registered`);
}
const claimed = SERVED.map((entry) => entry.surface);
assert.deepStrictEqual(claimed.filter((f, i) => claimed.indexOf(f) !== i), [],
  "a surface is served by two pumps -- that would hand the same message to two dialog"
  + " managers and make key routing depend on registration order");

// 4. The exemptions must stay true in the code, not only in the prose above.
assert.match(fs.readFileSync(path.join(root, "src/ui/wallpaper/WallpaperLibraryWindowV2.cpp"), "utf8"),
  /case WM_CLOSE: ShowWindow\(hwnd, SW_HIDE\);/,
  "the library window still has to hide rather than destroy, or its exemption is stale");
assert.match(fs.readFileSync(path.join(root, "src/ui/settings/DesktopAiSettingsPage.cpp"), "utf8"),
  /state->LoadProfiles\(\);/,
  "the AI page still reloads on show, which is what makes Esc dangerous there -- without"
  + " it the exemption has to be re-decided rather than inherited");
assert.match(fs.readFileSync(path.join(root, "src/ui/ai/ContentCreatorDialog.cpp"), "utf8"),
  /if \(wParam == VK_ESCAPE\) \{ state->SetFullscreenPreview\(false\);/,
  "the creator still binds Esc to leaving fullscreen, or its exemption is stale");
assert.match(fs.readFileSync(path.join(root, "src/ui/ai/ContentCreatorDialog.cpp"), "utf8"),
  /DialogManagedCreatorWindows/,
  "the creator must still expose the windows the pump should serve");

console.log(`esc answers every dialog-managed surface: PASS (${SERVED.length + OWN_PUMP.length} surfaces, ${Object.keys(EXEMPT).length} exempt)`);
