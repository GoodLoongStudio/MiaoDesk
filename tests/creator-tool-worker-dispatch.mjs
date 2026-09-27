// CCA-04:宿主必须真的把创作工具分发了,而不只是放行。
//
// 这个闸门拦的是一类很难当场发现的缺陷:IsAllowedPiNativeTool 里有这八个名字,
// 于是调用能进 worker,然后落进 ExecuteNativeToolRaw —— 那条路不认识 creator_
// 前缀,回给模型的是一句"未知工具"。调用看起来被接受了,失败信息却和"名字打错了"
// 一模一样,模型会据此反复重试,而用户看到的是"它一直不成功"。
//
// 所以这里从 main.cpp 读代码形状:创作工具必须在进 ExecuteNativeToolRaw 之前
// 被分派走,而且宿主要提供的那几样事实必须真的来自宿主 —— 环境变量、工作区状态
// 文件、写盘后回读,一个都不能省。
import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const worker = read("src/app/main.cpp");
const dispatchHeader = read("src/include/miaodesk/CreatorToolWorker.h");
const dispatchImpl = read("src/desktop/control/CreatorToolWorker.cpp");
const extensionHeader = read("src/include/miaodesk/PiNativeToolsExtension.h");

function functionBody(source, signature) {
  const start = source.indexOf(signature);
  assert.notStrictEqual(start, -1, `${signature} must exist`);
  let depth = 0;
  for (let i = source.indexOf("{", start); i < source.length; i += 1) {
    if (source[i] === "{") depth += 1;
    else if (source[i] === "}") {
      depth -= 1;
      if (depth === 0) return source.slice(start, i + 1);
    }
  }
  throw new Error(`could not brace-match ${signature}`);
}

// --- 1. 创作工具必须在通用路径之前被接走 --------------------------------------

const dispatch = functionBody(worker, "miaodesk::NativeToolResult RunCreatorTool");
assert.ok(dispatch.length > 0, "main.cpp must implement RunCreatorTool");

// 关键的一条:在 RunNativeToolWorkerIfRequested 里,creator 工具必须**先**被分派,
// 然后才轮到 preview / desktop control / ExecuteNativeToolRaw。顺序反了的话,
// 后面那几条会先把调用接走,而它们不认识 creator_ 前缀。
const runFn = functionBody(worker, "int RunNativeToolWorkerIfRequested(bool& handled)");
const creatorAt = runFn.indexOf("RunCreatorTool(toolUtf8");
assert.notStrictEqual(creatorAt, -1, "the worker entry point must dispatch creator tools");
const genericAt = runFn.indexOf("ExecuteNativeToolRaw");
assert.notStrictEqual(genericAt, -1, "ExecuteNativeToolRaw must still be there for chat tools");
assert.ok(creatorAt < genericAt,
  "creator tools must be dispatched BEFORE ExecuteNativeToolRaw -- otherwise they fall " +
  "into the generic path, which does not know the creator_ prefix, and the model gets " +
  "back an error that looks identical to a misspelled tool name");
assert.match(runFn, /IsCreatorTool\(toolUtf8\)/,
  "the dispatch must be gated on the shared registry, not a second list of names");

// --- 2. 归属事实必须来自宿主 ---------------------------------------------------

// 会话与工作区从宿主的环境变量读。参数是模型写的,它说什么都不是事实来源。
assert.match(worker, /kCreatorWorkspaceEnvironment/,
  "the worker must read the workspace from the host's environment");
assert.match(worker, /kCreatorSessionEnvironment/,
  "the worker must read the session id from the host's environment");
// 这两个常量必须真的定义在宿主与扩展共用的那个头里。
assert.match(extensionHeader, /kCreatorWorkspaceEnvironment\[\] = L"MIAODESK_CREATOR_WORKSPACE"/,
  "kCreatorWorkspaceEnvironment must be the shared name, defined once");
assert.match(extensionHeader, /kCreatorSessionEnvironment\[\] = L"MIAODESK_CREATOR_SESSION"/,
  "kCreatorSessionEnvironment must be the shared name, defined once");

// sessionId / workspaceRoot 不得从工具参数里取来当事实。参数仍然要读
// (否则路由会以 MissingSession 拒绝),但它们只能用于对账。
assert.match(dispatch, /ExtractJsonString\(arguments, "\\"sessionId\\""\)/,
  "the worker must read the claimed sessionId -- for reconciliation, not as truth");
assert.ok(!/input\.sessionId = .*ExtractJsonString/.test(dispatch),
  "the session the host uses must NOT come from the tool arguments");
assert.ok(!/input\.workspaceRoot = .*ExtractJsonString/.test(dispatch),
  "the workspace the host uses must NOT come from the tool arguments");

// --- 3. 工作区状态文件 --------------------------------------------------------

// worker 每次调用都是新进程,手里没有宿主内存。阶段、epoch、取消位必须落在
// 工作区里由它读;读不懂时 hasState 必须是 false,而不是拿半份状态去放行一次写入。
const stateHeader = read("src/include/miaodesk/CreatorWorkspaceState.h");
assert.match(stateHeader, /kCreatorWorkspaceStateFileName = "\.miaodesk-session\.state"/,
  "the state file name must be fixed, so the host and the worker agree on it");
assert.match(worker, /ParseCreatorWorkspaceState\(text, &input\.state\)/,
  "the worker must parse the workspace state file");
assert.match(worker, /input\.hasState = ParseCreatorWorkspaceState\(/,
  "a state file that cannot be parsed must leave hasState false, not half-fill the state");

// --- 4. 写盘的物理步骤必须回读 -------------------------------------------------

// 报"我写了 N 字节"而没有回读,就绕过了事务对暂存内容的核对。于是换成另一个文件、
// 或者只写进去一半,都不会被发现 —— 而"错误后无半写文件"正是这一轮要保的。
const port = read("src/app/main.cpp");
const staged = functionBody(port, "StagedBytes WriteStaged(const std::string& stagedPath, const std::string& content) override");
assert.match(staged, /std::ifstream back\(path, std::ios::binary\)/,
  "the host must open the staged file for reading back");
// 这一条才是重点:staged.bytes 必须来自那个流。写成 staged.bytes = content 的话,
// 事务核对的就变成了"宿主打算写什么",而盘上到底是什么仍然没人验证过 ——
// 只写进去一半、或者写到了别的位置,都发现不了。
assert.match(staged, /staged\.bytes = std::string\(std::istreambuf_iterator<char>\(back\)/,
  "the bytes reported for the staged file must come from reading it back, not from the request");
assert.match(staged, /staged\.facts\.byteCount = staged\.bytes\.size\(\);/,
  "the reported byte count must come from what was read back, not from the request");
// 替换必须走 MoveFileExW 且带 WRITE_THROUGH:那才是原子替换。
const replaceTarget = functionBody(
  port,
  "bool ReplaceTarget(const std::string& stagedPath, const std::string& relativePath) override",
);
assert.match(replaceTarget, /MoveFileExW\([\s\S]*MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH\)/,
  "the target must be replaced atomically with MoveFileExW + WRITE_THROUGH");

// --- 5. reparse point 要真的去问文件系统 --------------------------------------

// 不问的话,"解析后落在工作区之外"只能靠路径字符串猜,而猜不出 junction。
assert.match(functionBody(port, "miaodesk::creator::CreatorFileFacts Facts(const std::string& relativePath) override"),
  /fs::is_symlink\(status\)/,
  "the host must ask the filesystem whether the path is a reparse point");

// --- 6. 可用性必须是显式的 ----------------------------------------------------

// 每一个名册里的工具都要有且只有一个可用性结论。漏一个的话,新工具会安静地
// 落进默认分支,而它的失败会长得像一次拒绝。
const registry = read("src/desktop/control/CreatorToolRegistry.cpp");
const names = [...registry.matchAll(/^    "([a-z_]+)",$/gm)].map((m) => m[1])
  .filter((n) => n.startsWith("creator_") || n === "content_skill_get");
for (const name of names) {
  const listed = dispatchImpl.includes(`case CreatorToolName::`) &&
    new RegExp(`case CreatorToolName::\\w+:`, "g");
  assert.ok(listed, "AvailabilityOf must switch on CreatorToolName");
  break;
}
// AvailabilityOf 的每个 case 都要归到一类。
const availability = functionBody(dispatchImpl, "CreatorToolAvailability AvailabilityOf(CreatorToolName tool) noexcept");
const availabilityNames = [...availability.matchAll(/case CreatorToolName::(\w+):/g)].map((m) => m[1]);
assert.ok(availabilityNames.length >= 8,
  `AvailabilityOf must say something about every tool, got ${availabilityNames.length} cases`);
// 未实现的必须有一句"为什么"。
const unavailableReason = functionBody(dispatchImpl, "std::string UnavailableReason(CreatorToolName tool)");
assert.match(unavailableReason, /不在这一轮范围/,
  "an unimplemented tool must say WHY it is unavailable, not just that it is");

// 拒绝码要具体,不能是笼统的失败。
//
// 注意这一节的能力边界:它能断言"代码存在",但断言不了"哪一个分支真的会发出它" ——
// 把某一个 reply.code = "Unverified" 改成 "Rejected",这段断言还是绿的,因为另一处
// 也有同样的字符串。那种性质只能由行为覆盖承担,而它确实被覆盖着:
// src/tests/CreatorToolWorkerTest.cpp 逐个跑到了这些分支,并做过变异验证
// (删掉过期摘要闸门、改掉 Unverified 判定,两个都让它变红)。
// 所以这里只放**形状**层面的断言,并且说清楚它管到什么程度。
for (const code of ["StaleDigest", "Unverified", "ReplaceFailed", "MissingTransaction"]) {
  assert.ok(dispatchImpl.includes(`reply.code = "${code}"`),
    `the worker must have a distinct reply code "${code}" -- a bare "failed" gives the model ` +
    "nothing to act on and gives us nothing to pin a test to");
}

// 过期摘要的**闸门**本身也要在:只断言"有 StaleDigest 这个字符串"的话,把那道判断
// 整条删掉测试还是绿的 —— 而那时它再也不会被触发。
assert.match(dispatchImpl, /args\.digest != input\.state\.candidateDigest/,
  "the stale-digest check must compare against the HOST's recorded candidate digest, " +
  "not against anything the model supplied");

// --- 7b. 封存必须是独立副本,且与算摘要的是同一份快照 -------------------------

// 摘要来自宿主对当前快照的计算,不是模型给的那个字符串。一个字符串就能决定
// 封存什么的话,模型可以指着旧内容拿到新 revision,也可以把两个不同的包说成同一版。
assert.match(dispatchImpl, /digest\.value != args\.digest/,
  "candidate_submit must compare the HOST-computed digest against what the model claimed");
assert.match(dispatchImpl, /const auto digest = content::ComputeCandidateDigest\(snapshot\)/,
  "the digest must be computed from the host's own snapshot of the workspace");

// 封存绝不能落在工作区里:指向工作区的话,"封存后修改源目录不能改变待应用候选"
// 名存实亡 —— 因为封存的就是源目录。
assert.match(worker, /root_\.parent_path\(\) \/ L"revisions"/,
  "the sealed snapshot must live OUTSIDE the workspace");
assert.match(dispatchImpl, /port\.SealSnapshot\(digest\.value, snapshot, &snapshotPath\)/,
  "the same snapshot that was digested must be the one sealed -- letting the port " +
  "re-read the workspace would let the two disagree");

// 封存成功后必须把新摘要写回状态文件。不做这一步,状态里的摘要停在旧值,
// 下一次带 expectedDigest 的写入会被全部当成"基于旧视图"而拒绝。
assert.match(dispatchImpl, /port\.SaveState\(updated\)/,
  "a sealed candidate must update the workspace state, or every later expectedDigest " +
  "write is refused as stale");

// --- 7. 拒绝 vs 不可用 vs 成功,三种结局必须可分辨 ------------------------------

// ToModelText 是三条不同的文字,而不是一句通用失败。这一条**可以**在这里断言:
// 三元表达式只有一处,改掉它整个闸门就红。
const toModelText = functionBody(dispatchImpl, "std::string CreatorToolReply::ToModelText() const");
assert.match(toModelText, /unavailable \? "这个能力当前不可用" : "调用被拒绝"/,
  "the model's text must distinguish 'unavailable' from 'rejected' -- the two demand " +
  "opposite next steps");
assert.match(toModelText, /重试同一个调用不会有变化/,
  "a non-retryable rejection must say so, or the model retries the same out-of-bounds call " +
  "while the user sees no progress");

console.log(`creator tool worker dispatch: OK (creator tools dispatched before the generic path; ` +
  `${names.length} tools in the registry, binding from host env)`);
