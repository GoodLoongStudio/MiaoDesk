import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");
// Overridable so the mutations in the doc can be run against copies of the
// source without ever editing the real file.
const service = read(process.env.WIDGET_SERVICE_CPP ?? "src/desktop/widgets/WidgetService.cpp");
const header = read(process.env.WIDGET_SERVICE_H ?? "src/include/miaodesk/WidgetService.h");

// Regression for the built-in preset geometry guard.
//
// WidgetService::Update is the mutation path that persists widget geometry.
// Built-in (Native preset) widgets take their size from the preset definition,
// not the instance, so a generic Update carrying a different width/height must
// be REJECTED rather than silently resized.
//
// Why a test at all: this guard had no coverage whatsoever. The only callers of
// WidgetUpdateRequest today (NativeWidgetHost/ContentWidgetHost EndDrag,
// DesktopWidgetController SetEnabled/MoveTo) never populate width or height, so
// the guard is exercised by no caller that exists -- and CI stayed green whether
// or not it was there. The repo rule is to add regressions only for real state
// transitions and boundaries, not for prose; geometry ownership is exactly such
// a boundary.

const signature =
  "WidgetServiceResult WidgetService::Update(const WidgetUpdateRequest& request) const {";
const start = service.indexOf(signature);
assert.notStrictEqual(start, -1, "WidgetService::Update must exist");

// Brace-match instead of slicing to the first "\n}", so an inner block closing
// before the function body ends cannot truncate the region under test.
let depth = 0;
let end = -1;
for (let i = service.indexOf("{", start); i < service.length; i += 1) {
  if (service[i] === "{") depth += 1;
  else if (service[i] === "}") {
    depth -= 1;
    if (depth === 0) {
      end = i + 1;
      break;
    }
  }
}
assert.notStrictEqual(end, -1, "could not brace-match WidgetService::Update");
const body = service.slice(start, end);

for (const axis of ["width", "height"]) {
  const field = axis[0].toUpperCase() + axis.slice(1);
  const compare = new RegExp(
    `request\\.${axis} &&
     std::fabs\\(\\*request\\.${axis} -
     definition->default${field}\\) >
     kPresetGeometryTolerance`
      .replace(/\s+/g, "\\s*")
  );
  assert.match(body, compare,
    `Update must compare request.${axis} against the preset default with a tolerance`);

  const label = axis === "width" ? "宽度" : "高度";
  assert.match(body, new RegExp(`内置小组件${label}由 preset 拥有`),
    `Update must reject ${axis} with the preset-ownership message`);
}

// The guard has to run before anything is persisted, or it rejects a row it has
// already written.
const rejectAt = body.indexOf("内置小组件宽度由 preset 拥有");
const persistAt = body.indexOf("store.Upsert");
assert.notStrictEqual(rejectAt, -1, "the preset reject must be present");
assert.notStrictEqual(persistAt, -1, "the persist must be present");
assert.ok(rejectAt < persistAt,
  "the preset geometry guard must run before store.Upsert");

// Content widgets use the other ownership model: validated, not preset-owned.
// Both branches must still be present so neither can quietly take over the other.
assert.match(body, /DesktopWidgetKind::Native/,
  "the Native preset branch must stay");
assert.match(body, /DesktopWidgetKind::Content && !ValidateContentWidget/,
  "content widgets must keep going through ValidateContentWidget");

// Tolerance must remain a named constant. An inline 0 would reject an exact-fit
// resize, and a large tolerance would let a widget drift off its preset size.
assert.match(service, /constexpr float kPresetGeometryTolerance = 0\.0001f;/,
  "the geometry tolerance must stay a named constant");

// width/height stay optional: an absent field means "leave geometry alone", which
// is what lets the drag/move callers through the guard untouched.
assert.match(header,
  /struct WidgetUpdateRequest \{[\s\S]*?std::optional<float> width;[\s\S]*?std::optional<float> height;/,
  "width/height must remain optional in WidgetUpdateRequest");

console.log("widget preset geometry guard: PASS");
