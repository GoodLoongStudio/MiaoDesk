import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const here = path.dirname(new URL(import.meta.url).pathname);
const root = path.join(here, "..");

const dialog = read(path.join(root, "src/ui/wallpaper/ContentWidgetSettingsDialog.cpp"));

// Two validation gaps in the content-widget parameter form.
//
// 1. Int parsing had no ERANGE guard. std::wcstoll clamps on overflow and leaves
//    the end pointer at the terminator, so the only check present (*end == L'\0')
//    passed: "99999999999999999999" was committed as 9223372036854775807. The
//    Float branch has its equivalent (!isfinite) and the storage decoder has this
//    exact check; only this call site was missing it.
// 2. step is real schema -- documented and declared by every shipped package --
//    and the form advertises it as a constraint ("step N"), but nothing enforced
//    it, so an off-step value was accepted.
//
// The behaviour of both is verified by extracting the real code and running it
// (see the change description). This file pins presence and coverage, and the
// safety property that let the step check be added at all.

const intBranch = dialog.slice(
  dialog.indexOf("if (parameter.type == content::ContentParameterType::Int) {"),
  dialog.indexOf("}\n    if (parameter.type == content::ContentParameterType::Float) {")
);
assert.match(intBranch, /errno = 0;/,
  "the Int branch must reset errno before parsing");
assert.match(intBranch, /errno == ERANGE/,
  "the Int branch must reject an overflowing parse instead of taking the clamp");
assert.match(intBranch, /OffStepMessage\(parameter, number\)/,
  "the Int branch must enforce step");

const floatBranch = dialog.slice(
  dialog.indexOf("if (parameter.type == content::ContentParameterType::Float) {"),
  dialog.indexOf("暂不支持该参数类型")
);
assert.match(floatBranch, /!std::isfinite\(parsed\)/,
  "the Float branch must keep its finiteness guard");
assert.match(floatBranch, /OffStepMessage\(parameter, parsed\)/,
  "the Float branch must enforce step");

const helper = dialog.slice(
  dialog.indexOf("std::optional<std::wstring> OffStepMessage("),
  dialog.indexOf("\n}\n", dialog.indexOf("std::optional<std::wstring> OffStepMessage("))
);
assert.match(helper, /std::fabs\(offset - std::round\(steps\)/,
  "the step check must measure distance from the nearest grid point");
assert.match(helper, /parameter\.minimum\.value_or\(0\.0\)/,
  "the grid must be anchored at minimum when declared");
assert.match(helper, /!\(\*parameter\.step > 0\.0\)/,
  "a non-positive step must be treated as absent");
assert.match(helper, /std::isfinite\(\*parameter\.step\)/,
  "a non-finite step must be treated as absent");

// Safety property: every default shipped by a content package must already sit on
// its own declared grid. Otherwise the new check would reject a value the product
// itself ships, and the user could not re-save it unchanged.
const packageFiles = [];
const walk = (dir) => {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === ".git" || entry.name === "node_modules") continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full);
    else if (entry.name === "parameters.json") packageFiles.push(full);
  }
};
walk(root);

let checked = 0;
const offGrid = [];
for (const file of packageFiles) {
  let parsed;
  try {
    parsed = JSON.parse(fs.readFileSync(file, "utf8"));
  } catch {
    continue;
  }
  for (const p of Array.isArray(parsed.parameters) ? parsed.parameters : []) {
    if (!p || typeof p !== "object" || p.step == null || p.default == null) continue;
    checked += 1;
    const offset = (Number(p.default) - Number(p.minimum ?? 0)) / Number(p.step);
    if (Math.abs(offset - Math.round(offset)) > 1e-9) {
      offGrid.push(`${path.relative(root, file)}: ${p.key} step=${p.step} default=${p.default}`);
    }
  }
}

assert.ok(packageFiles.length > 0, "no parameters.json found -- the sweep is not running");
assert.ok(checked > 0, "no stepped parameter found in any package");
assert.deepStrictEqual(offGrid, [],
  `shipped defaults that the new step check would reject: ${offGrid.join("; ")}`);

console.log(`widget parameter validation: PASS (${packageFiles.length} packages, ${checked} stepped params all on-grid)`);
