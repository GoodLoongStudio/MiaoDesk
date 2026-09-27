import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const service = read("src/desktop/control/DesktopControlService.cpp");
const wallpaper = read("src/desktop/wallpaper/WallpaperService.cpp");
const serviceHeader = read("src/include/miaodesk/WallpaperService.h");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");

// The AI creator (and the wallpaper library) could not land an AI-generated wallpaper.
//
// Applying a Content Scene package through the global entry fails, and that is not a bug
// in itself: the global path persists a builtin scene key or an image/video/web source, so
// there is nothing for a content-scene package to be written into. The bug is what the
// product did about it -- it failed, on the target the library defaults to, for the entire
// class (every generated scene wallpaper and every imported .mdwall scene package), and
// told the user to go assign it per monitor by hand.
//
// The fix fans out to the per-monitor mechanism, which does support Content Scene because
// it resolves content:<id> through the content resolver. Two things make that honest:
//
//   1. the layout must switch to Independent, because that is the only mode that consumes
//      the assignments -- without it the writes happen and nothing changes, which is a
//      false success, the exact failure shape this repo has fixed three times already;
//   2. if the switch fails, the call must report failure, not the success it was about to
//      claim.

// --- the question, asked instead of guessed -------------------------------
assert.match(serviceHeader, /bool NeedsPerMonitorApply\(/,
  "the service must expose whether an item needs per-monitor placement. Detecting it by"
  + " matching the failure message text would couple a routing decision to copy.");
assert.match(wallpaper, /bool WallpaperService::NeedsPerMonitorApply\(/,
  "NeedsPerMonitorApply must be implemented in WallpaperService, which owns the kind rules");

// The rule is narrow: Content Scene only. A builtin scene, an image, a video and a Web
// wallpaper all still take the global path, which keeps working.
const rule = wallpaper.slice(
  wallpaper.indexOf("bool WallpaperService::NeedsPerMonitorApply("),
  wallpaper.indexOf("\n}", wallpaper.indexOf("bool WallpaperService::NeedsPerMonitorApply("))
);
assert.match(rule, /item\.kind != wallpaper::LibraryWallpaperKind::Scene\) return false;/,
  "only Scene items can lack a global representation -- image/video/web all have one");
assert.match(rule, /!IsContentId\(item\.id\)\) return false;/,
  "a non-content id is a builtin scene, which the global path carries by key");
assert.match(rule, /RuntimeSceneKey\(item\)\.empty\(\);/,
  "the decisive test is that no builtin runtime key resolves for it");

// --- the fan-out ----------------------------------------------------------
const apply = service.slice(
  service.indexOf("DesktopControlResult DesktopControlService::ApplyLibraryItem("),
  service.indexOf("\n}\n", service.indexOf("DesktopControlResult DesktopControlService::ApplyLibraryItem("))
);
assert.match(apply, /service\.NeedsPerMonitorApply\(item\)/,
  "the global entry must ask the service before giving up");
assert.match(apply, /AssignLibraryItemToMonitor\(/,
  "the recovery is the per-monitor assignment, not a new global capability");
assert.match(apply, /QueryMonitorTopology\(\)/,
  "it must fan out over the real topology, not assume one display");
assert.match(apply, /for \(const auto& monitor : topology\.monitors\)/,
  "every monitor is assigned -- 'apply to the desktop' means the whole desktop");
assert.match(apply, /assigned == 0/,
  "a fan-out where nothing succeeded is a failure, not a partial report");

// --- the two things that make it honest -----------------------------------
assert.match(apply, /L"Wallpaper", L"Layout", L"independent"/,
  "the layout MUST switch to Independent. Assignments are only consumed from"
  + " StartIndependent; in Span/Clone/PrimaryOnly the engine renders the global selection,"
  + " so without this the writes happen and the desktop does not change -- a false success."
  + " WallpaperWebRuntimeCoordinator.cpp PersistMonitorWeb already does exactly this.");
assert.match(apply, /!layoutSwitched[\s\S]*?return \{false,/,
  "if the layout switch fails the call must report FAILURE, not the success it was about to"
  + " claim. Returning success here would be the '已应用到桌面' lie all over again.");
assert.match(apply, /text::EnsureUtf16LeProfileFile\(config, &unicodeError\)/,
  "the config write must go through the same unicode guard every other writer uses");
assert.match(apply, /已应用到全部/,
  "the success message must state what was actually done (all displays), not imply a global"
  + " selection that does not exist");

// The Web path that already had this right stays the reference.
const web = read("src/desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp");
assert.match(web, /L"Wallpaper", L"Layout", L"independent"/,
  "the Web per-monitor path must keep switching to Independent too -- this fix follows it,");
assert.match(web, /targetMonitorId\.empty\(\) \? PersistGlobalWeb\(item, error\)/,
  "and the Web global path must stay separate from its per-monitor one");

// The AI creator reaches this through the same service call, so it is fixed without a
// change of its own. That is the point of fixing it in the service rather than in the UI.
assert.match(creator, /control\.ApplyLibraryItem\(item\)/,
  "the creator must still go through the shared ApplyLibraryItem, so it inherits the fix");
assert.doesNotMatch(creator, /AssignLibraryItemToMonitor\(/,
  "the creator must not reimplement the fan-out -- one implementation, in the service");

console.log("content-scene wallpapers apply to the desktop instead of failing: PASS");
