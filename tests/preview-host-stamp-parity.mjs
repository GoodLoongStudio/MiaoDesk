import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");

// Two functions called PackageStamp decide when the preview and the live widget
// reload their scene. They live in different translation units, so the shared name
// tells you nothing about whether they agree -- and they did not: the preview
// stamped only manifest.json and the definition's entry, while the host recursively
// stamped every file. A parameters.json-only change therefore refreshed the desktop
// widget while the preview kept showing the previous effective parameters, which is
// what "预览和正式实例使用一致的有效参数" forbids.
//
// Pinning them together is the only way this stays fixed: nothing else in the repo
// would notice a policy divergence between two same-named functions.

function packageStamp(source, expectParam) {
  const start = source.indexOf("fs::file_time_type PackageStamp(const fs::path& ");
  assert.notStrictEqual(start, -1, "PackageStamp must exist");
  let depth = 0;
  for (let i = source.indexOf("{", start); i < source.length; i += 1) {
    if (source[i] === "{") depth += 1;
    else if (source[i] === "}") {
      depth -= 1;
      if (depth === 0) {
        const body = source.slice(start, i + 1);
        // Normalise the parameter name so the only thing compared is the policy.
        return body.replace(new RegExp(`\\b${expectParam}\\b`, "g"), "PKG");
      }
    }
  }
  throw new Error("could not brace-match PackageStamp");
}

const host = packageStamp(read("src/desktop/widgets/ContentWidgetHost.cpp"), "packageRoot");
const preview = packageStamp(read("src/ui/wallpaper/ContentWidgetPreviewRenderer.cpp"), "root");

assert.strictEqual(preview, host,
  "the preview and the live host must invalidate on exactly the same files; " +
  "a parameters.json-only change has to refresh both or neither");

// The policy itself must be the recursive one, not the old two-file shortcut.
assert.match(preview, /recursive_directory_iterator/,
  "the stamp must be recursive over the whole package");
assert.match(preview, /skip_permission_denied/,
  "the recursive walk must keep skipping unreadable directories");
assert.match(preview, /is_regular_file/, "only regular files count");
assert.doesNotMatch(preview, /manifest\.json/,
  "the stamp must not be limited to manifest.json plus the entry");

// And the preview must actually re-load when the stamp moves, not just record it.
const renderer = read("src/ui/wallpaper/ContentWidgetPreviewRenderer.cpp");
assert.match(renderer, /const auto stamp = PackageStamp\(cached->packageRoot\);\s*\n\s*if \(stamp != cached->packageStamp\) \{[\s\S]*?renderer = std::make_unique<content::MiaoSceneD2DRenderer>\(\)/,
  "a changed stamp must rebuild the scene renderer");

console.log("preview/host cache invalidation parity: PASS");
