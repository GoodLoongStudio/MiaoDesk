import fs from "node:fs";
import assert from "node:assert/strict";

const source = fs.readFileSync("src/include/miaodesk/LayeredSceneRenderer.h", "utf8");
const miaoCloud = fs.readFileSync("assets/wallpapers/MiaoCloud.mdwall/manifest.json", "utf8");
const neonCity = fs.readFileSync("assets/wallpapers/NeonCity.mdwall/manifest.json", "utf8");
const mysticMoon = fs.readFileSync("assets/wallpapers/MysticMoon.mdwall/manifest.json", "utf8");

for (const [name, manifestText] of [
  ["MiaoCloud", miaoCloud],
  ["NeonCity", neonCity],
  ["MysticMoon", mysticMoon],
]) {
  const manifest = JSON.parse(manifestText);
  assert.equal(manifest.entry, "scene.json", `${name} must stay on canonical scene.json`);
  assert.equal(manifest.runtime, "scene", `${name} must stay a Scene runtime package`);
}

assert.match(source, /#include "miaodesk\/MiaoSceneD2DRenderer\.h"/);
assert.match(source, /UsesCanonicalSceneJson\(const WallpaperPackageManifest& manifest\)/);
assert.match(source, /extension == L"\.json"/);
assert.match(source, /MiaoSceneD2DRenderer/);
assert.match(source, /renderer->Load\(packageRoot, context\.target, &entry\.error\)/);
assert.match(source, /entry\.canonicalRenderer->Draw\(context\.time, targetSize, &drawError\)/);

// The compatibility INI renderer may remain, but canonical JSON must be selected before it.
const canonicalBranch = source.indexOf("UsesCanonicalSceneJson(manifest)");
const legacyBranch = source.indexOf("Compatibility only for pre-canonical");
assert.ok(canonicalBranch >= 0 && legacyBranch > canonicalBranch,
  "canonical scene.json renderer must be selected before legacy INI compatibility");

// Pin the original regression: manifest.entry may be scene.json, so blindly feeding entry_
// to GetPrivateProfile* must never be the only production path again.
assert.match(source, /previous[\s\S]*GetPrivateProfile[\s\S]*scene\.ini/);

console.log("wallpaper production bridge contract: PASS");
