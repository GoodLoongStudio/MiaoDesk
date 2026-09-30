import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const bridgeHeader = read("src/include/miaodesk/ContentCreatorBridge.h");
const bridge = read("src/desktop/control/ContentCreatorBridge.cpp");
const search = read("src/ui/search/SearchWindow.cpp");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");

// Cross-process open must ACK/queue only. Creator construction performs API,
// transcript and Skill I/O and must never be inside a 3-second sender timeout.
assert.match(bridgeHeader, /kContentCreatorOpenAck/);
assert.match(bridge, /RegisterWindowMessageW\(\s*L"MiaoDesk\.ContentCreator\.OpenRequest\.v2"\s*\)/);
assert.match(bridge, /SendMessageTimeoutW\([\s\S]*openMessage[\s\S]*kContentCreatorOpenAck/);
assert.match(search, /message == creatorOpenMessage[\s\S]*PostMessageW\([\s\S]*kDeferredOpenCreatorMessage[\s\S]*return creator::kContentCreatorOpenAck;/,
  "v2 IPC must queue creator creation and ACK before doing heavy initialization");

const copyAt = search.indexOf("case WM_COPYDATA:");
assert.notStrictEqual(copyAt, -1);
const copyEnd = search.indexOf("case WM_DISPLAYCHANGE:", copyAt);
const copyBlock = search.slice(copyAt, copyEnd);
assert.match(copyBlock, /PostMessageW\([\s\S]*kDeferredOpenCreatorMessage/,
  "legacy WM_COPYDATA must also be converted to deferred creator creation");
assert.doesNotMatch(copyBlock, /OpenContentCreator\(/,
  "cross-process WM_COPYDATA must never synchronously construct the creator");

// The actual work happens later in the normal UI message loop.
assert.match(search, /case kDeferredOpenCreatorMessage:[\s\S]*OpenContentCreator\(kind\)/);

// DialogState has exactly one owner across CreateWindowExW failure.
// WM_CREATE returning -1 causes WM_DESTROY before CreateWindowExW returns.
assert.match(creator, /bool windowOwnsLifetime\{\};/);
assert.match(creator, /auto stateHolder = std::make_unique<DialogState>\(\);[\s\S]*auto\* state = stateHolder\.get\(\);/);
assert.match(creator, /if \(!window\) \{[\s\S]*MakeChatLaunchProfile[\s\S]*return false;\s*\}/,
  "failed creation must restore the shared runtime to Chat profile");
assert.match(creator, /state->windowOwnsLifetime = true;\s*\n\s*stateHolder\.release\(\);/,
  "window ownership must transfer only after CreateWindowExW succeeds");
assert.match(creator, /SetWindowLongPtrW\(hwnd, GWLP_USERDATA, 0\);\s*\n\s*if \(state->windowOwnsLifetime\) delete state;/,
  "WM_DESTROY may delete only state whose lifetime was transferred to the HWND");

const showAt = creator.indexOf("bool ShowContentCreatorDialog(");
const showBody = creator.slice(showAt);
assert.doesNotMatch(showBody, /if \(!window\) \{\s*delete state;/,
  "CreateWindowExW failure must never double-delete DialogState");

// Failed control creation needs an exact diagnostic instead of a generic
// "窗口没有成功创建" symptom.
assert.match(creator, /WM_CREATE 控件初始化失败；缺失=/);
assert.match(creator, /CreateWindowExW 创建 AI 创作窗口失败/);

console.log("creator open stability contract: PASS");
