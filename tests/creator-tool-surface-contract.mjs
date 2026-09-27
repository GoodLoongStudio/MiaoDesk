// CCA-04:Pi 侧的 --tools 与宿主的 worker 允许表必须是同一份名册。
//
// 计划 §5 的验收原句:"按进程/会话配置验证扩展实际拿到的工具清单"。
// 这句话防的是两份独立清单各自漂移 —— Pi 的 --tools 写在 PiLaunchProfile.cpp,
// worker 的允许表写在 src/app/main.cpp,彼此都不知道对方。而不一致的故障是单向静默的:
//
//   * Pi 给了、worker 不认 -> 调用失败,模型重试,用户看到"它一直不成功",
//     而任何编译器和测试都不响 —— worker 只会安静地返回 exit code 26;
//   * worker 认、Pi 没给   -> 能力躺在那里没人能用,没有任何东西报错。
//
// 所以这里从三个源头读:名册(CreatorToolRegistry)、Pi 的 allowlist 字面量、
// worker 的 IsAllowedPiNativeTool 函数体,要求它们两两覆盖一致。
import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const registry = read("src/desktop/control/CreatorToolRegistry.cpp");
const registryHeader = read("src/include/miaodesk/CreatorToolRegistry.h");
const profiles = read("src/desktop/control/PiLaunchProfile.cpp");
const worker = read("src/app/main.cpp");
const extension = read("src/ai/pi/PiNativeToolsExtension.cpp");

// --- 名册:CreatortoolNames() 是唯一事实来源 ------------------------------------

const names = [...registry.matchAll(/"([a-z_]+)",/g)].map((m) => m[1]);
const creatorNames = names.filter((n) => n.startsWith("creator_") || n === "content_skill_get");
assert.ok(creatorNames.length >= 8, `the registry must list all eight creator tools, got ${creatorNames.length}`);

// The registry must expose them through the names table, not scattered literals.
assert.match(registryHeader, /const std::vector<std::string>& CreatorToolNames\(\) noexcept;/,
  "CreatorToolNames() must be the single source Pi and the worker both derive from");
assert.match(registryHeader, /bool IsCreatorTool\(std::string_view tool\) noexcept;/,
  "IsCreatorTool() must be the predicate the worker uses");
assert.match(registry, /static const std::vector<std::string> names = BuildToolNames\(\);/,
  "the names vector must be built once from the table");

// --- Pi side: every creator tool must be in the creator --tools allowlist -------

function concatenatedLiterals(source, name) {
  const decl = source.indexOf(`const wchar_t ${name}[] =`);
  assert.notStrictEqual(decl, -1, `${name} must be defined`);
  const semi = source.indexOf(";", decl);
  const body = source.slice(decl, semi);
  return [...body.matchAll(/L"([^"]*)"/g)].map((m) => m[1]).join("");
}
const namesOf = (list) => list.split(",").map((t) => t.trim()).filter(Boolean);

const creatorList = concatenatedLiterals(profiles, "kPiCreatorToolAllowlist");
const creatorListNames = new Set(namesOf(creatorList));
const chatListNames = new Set(namesOf(concatenatedLiterals(profiles, "kPiChatToolAllowlist")));

for (const tool of creatorNames) {
  assert.ok(creatorListNames.has(tool),
    `Pi's creator --tools must offer ${tool} -- Pi offers it or the worker never sees it`);
}
// ...and the reverse: nothing in the creator allowlist may be outside the registry.
for (const tool of creatorListNames) {
  assert.ok(creatorNames.includes(tool),
    `${tool} is in Pi's creator --tools but not in the tool registry -- one of the two lists drifted`);
}

// --- Worker side: IsAllowedPiNativeTool must accept every creator tool ----------

// Read the function body of IsAllowedPiNativeTool by brace matching.
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

const workerFn = functionBody(worker, "bool IsAllowedPiNativeTool(std::string_view tool)");
assert.match(workerFn, /IsCreatorTool\(/,
  "the worker must route creator tools through the shared registry, not a second literal list");

// The registry header must be included so that call actually compiles.
assert.match(worker, /#include "miaodesk\/CreatorToolRegistry\.h"/,
  "src/app/main.cpp must include CreatorToolRegistry.h");
assert.match(worker, /miaodesk::creator::IsCreatorTool\(tool\)/,
  "IsAllowedPiNativeTool must call IsCreatorTool so the two lists cannot drift");

// --- The two allowlists must not silently overlap outside content_skill_get ------
// (Already asserted by tests/pi-launch-profile-isolation.mjs, but restated here
// because this gate is the one that fails when someone merges them by accident.)
const sharedWithChat = creatorNames.filter((t) => chatListNames.has(t));
assert.deepStrictEqual(sharedWithChat.sort(), ["content_skill_get"],
  `only content_skill_get may be shared with the chat allowlist; found: ${sharedWithChat.join(", ") || "(none)"}`);

// The mutating vs read-only split must be decided in one place.
assert.match(registryHeader, /bool IsMutatingCreatorTool\(CreatorToolName tool\) noexcept;/,
  "the mutating classification must live in the registry, not in two callers");
assert.match(registry, /case CreatorToolName::PackageUpdate:\s*\n\s*case CreatorToolName::AssetImport:/,
  "PackageUpdate and AssetImport must both be classified as mutating");

// --- The Pi extension must register the creator tools under the Creator variant --
// A tool that Pi never registers is indistinguishable from one it refuses: the model
// simply never sees it, and nothing fails.
const creatorExtension = read("src/ai/pi/PiNativeToolsExtension.cpp");
assert.match(creatorExtension, /PiNativeToolsVariant::Creator/,
  "the extension installer must still special-case the Creator variant");
assert.match(creatorExtension, /miaodesk-creator-tools\.ts/,
  "the Creator variant must materialise its own file, not share the chat one");

// The extension's TOOL_NAMES must not be the only registration path for creator tools:
// if it were, adding a tool to the registry would not make Pi offer it.
assert.match(extension, /const TOOL_NAMES = \[/,
  "the extension keeps its explicit tool list (CCA-04 registers the creator tools there)");

console.log(`creator tool surface contract: OK (${creatorNames.length} tools, worker + Pi + registry agree)`);
