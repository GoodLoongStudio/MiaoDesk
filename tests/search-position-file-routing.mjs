import fs from "node:fs";
import assert from "node:assert/strict";

const searchWindow = fs.readFileSync("src/ui/search/SearchWindow.cpp", "utf8");
const goz = fs.readFileSync("src/search/GozSearch.cpp", "utf8");

assert.ok(
  searchWindow.includes("GetCursorPos(&cursor)") &&
  searchWindow.includes("MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST)"),
  "search launcher must choose the monitor the user is actively pointing at",
);
assert.ok(
  searchWindow.includes("(workWidth - kWindowWidth) / 2") &&
  searchWindow.includes("std::clamp(workHeight / 16, 18, 64)"),
  "search launcher must stay horizontally centered and adapt its top inset to the monitor work area",
);
assert.ok(
  /void SearchWindow::ShowAndFocus\(\)[\s\S]*PositionWindow\(\);[\s\S]*ShowWindow/.test(searchWindow),
  "every search activation must re-anchor before it becomes visible",
);

const mergeAt = searchWindow.indexOf("void SearchWindow::MergeResults()");
assert.notStrictEqual(mergeAt, -1);
const mergeBody = searchWindow.slice(mergeAt, searchWindow.indexOf("\n}\n", mergeAt) + 3);
assert.ok(mergeBody.includes("for (const auto& result : fileResults_) results_.push_back(result);"));
assert.ok(mergeBody.includes("std::stable_sort(results_.begin(), results_.end()"));
assert.ok(
  mergeBody.includes("const bool aFile = a.kind == ResultKind::File || a.kind == ResultKind::Folder"),
  "file/folder results must participate in the same ranking instead of being appended behind five apps",
);

assert.ok(
  /bool GozSearch::Available\(\) const \{[\s\S]*return !FindClientBinary\(\)\.empty\(\);/.test(goz),
  "temporary service/pipe startup delay must not make the file-search feature disappear from the UI",
);
assert.ok(
  goz.includes("OpenServiceW(") &&
  goz.includes('manager, L"goz", SERVICE_QUERY_STATUS | SERVICE_START') &&
  goz.includes("StartServiceW(service, 0, nullptr)"),
  "file search must attempt to recover the installed goz service on demand",
);
// 钉不变量,不钉拼写:`EnsurePipeAvailable` 现在多一个出参(把这一轮恢复实际
// 发生了什么交出来给界面说),所以照抄旧的 `EnsurePipeAvailable(2000)` 会在一次
// 刻意改进上红。这里断言的是那件要紧的事 —— 异步查询**先等恢复再判失败**,
// 而不是一看见管道不通就回报失败。
assert.ok(
  /EnsurePipeAvailable\(2000/.test(goz) &&
  /const bool pipeReady = EnsurePipeAvailable\(2000/.test(goz) &&
  goz.includes("pipeReady && RunGozQuery"),
  "async file queries must wait briefly for service recovery before failing",
);

console.log("search position + file routing contract: PASS");
