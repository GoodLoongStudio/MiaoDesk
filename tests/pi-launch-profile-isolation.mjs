// CCA-03:创作会话必须和普通聊天真的分开。
//
// 这个文件防的是一类很难当场发现的污染:PiRuntime 原来把 agent 目录、扩展路径、
// 工具 allowlist、system prompt 和 cwd 全部写死在函数体里。创作模式接入时如果只加
// 一个参数,两份配置会共用同一个扩展文件 —— 后写的那份覆盖前一份,于是**另一个模式**
// 下一次启动的进程加载到的是这一模式的工具集。表现是"聊天突然不能写文件了",
// 而没有任何一处代码改过聊天的 allowlist。那种缺陷不看这里就发现不了。
//
// 断言分三组:
//   1. 聊天侧一个字节都没变(allowlist、system prompt、默认目录);
//   2. 创作侧确实每一项都分开(目录 / session / cwd / 扩展文件 / allowlist / signature);
//   3. 两类通用工具从创作 allowlist 里整体消失。
import fs from "node:fs";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const profile = read("src/desktop/control/PiLaunchProfile.cpp");
const profileHeader = read("src/include/miaodesk/PiLaunchProfile.h");
const runtime = read("src/ai/pi/PiRuntime.cpp");
const runtimeHeader = read("src/include/miaodesk/PiRuntime.h");
const extHeader = read("src/include/miaodesk/PiNativeToolsExtension.h");
const extCpp = read("src/ai/pi/PiNativeToolsExtension.cpp");

// --- 1. 聊天侧必须一个字节都没变 -------------------------------------------

const CHAT_TOOLS = [
  "read", "bash", "edit", "write", "grep", "find", "ls",
  "settings_open", "ppt_create", "file_create", "folder_list", "file_open",
  "image_generate", "wallpaper_validate_package", "wallpaper_state_get",
  "desktop_widget_list", "desktop_preview_wallpaper", "desktop_preview_examples",
  "content_skill_get",
];

// C++ 把长字面量按行断开再隐式拼接:
//     const wchar_t kPiChatToolAllowlist[] =
//         L"read,bash,edit,write,grep,find,ls,"
//         L"settings_open,..."
// 只取第一段会把后半截整批工具判成"不存在",所以要从声明处扫到分号,把每一段 L"..." 拼起来。
function concatenatedLiterals(source, name) {
  const decl = source.indexOf(`const wchar_t ${name}[] =`);
  assert.notStrictEqual(decl, -1, `${name} must be defined`);
  const semi = source.indexOf(";", decl);
  assert.notStrictEqual(semi, -1, `${name} declaration must end with a semicolon`);
  const body = source.slice(decl, semi);
  const parts = [...body.matchAll(/L"([^"]*)"/g)].map((m) => m[1]);
  assert.ok(parts.length > 0, `${name} must have at least one L"..." literal`);
  return parts.join("");
}

const chatList = concatenatedLiterals(profile, "kPiChatToolAllowlist");
// 工具名必须整体匹配。用 includes() 会把 creator_image_generate 判成"带了 image_generate",
// 于是两条断言一条必然失败、另一条必然失效,而失败信息指向的是不存在的问题。
const names = (list) => list.split(",").map((t) => t.trim()).filter(Boolean);
const chatNames = new Set(names(chatList));
for (const tool of CHAT_TOOLS) {
  assert.ok(chatNames.has(tool), `chat allowlist must still include ${tool}`);
}
// 顺序也要在:它曾经是这样一个字面量,顺序变化说明有人重排过,值得再看一眼。
const chatOrder = chatList.replace(/\s+/g, " ").match(/(read,bash,edit,write,grep,find,ls,)/);
assert.ok(chatOrder, "chat allowlist must keep its original generic-file-tool block intact");

// 聊天仍走 DesktopDirectory,且默认没有 profile 时行为不变。
assert.match(
  runtime,
  /setup\.workingDirectory\.empty\(\) \? DesktopDirectory\(\) : setup\.workingDirectory/,
  "an empty working directory must still resolve to the Desktop directory",
);
assert.match(
  runtime,
  /setup\.systemPrompt\.empty\(\)\s*\n?\s*\? std::wstring\(\s*\n?\s*L"You are MiaoDesk's persistent desktop Agent/,
  "an empty profile system prompt must fall back to the historical one",
);
assert.match(
  runtime,
  /setup\.toolAllowlist\.empty\(\) \? kPiChatToolAllowlist\s*\n?\s*: setup\.toolAllowlist/,
  "an empty profile allowlist must fall back to the chat allowlist",
);
assert.match(
  runtime,
  /setup\.agentDir = launchProfile_\.agentDir\.empty\(\)\s*\n?\s*\? PiAgentDirectory\(\)\.wstring\(\)/,
  "an empty profile agent dir must still fall back to PiAgentRoot()",
);

// 聊天侧默认仍在同一个进程里跑,不能在没人设置 profile 时被改变。
assert.match(
  runtimeHeader,
  /PiLaunchProfile launchProfile_;/,
  "PiRuntime owns a launch profile",
);
assert.match(
  runtimeHeader,
  /void SetLaunchProfile\(PiLaunchProfile profile\)/,
  "the profile must be settable before the first turn",
);

// --- 2. 创作侧每一项都分开 -------------------------------------------------

// 扩展文件按 variant 分开命名。这是防污染的关键:
// 共用一个文件时后写者胜,另一个模式的下一次启动会加载到错的工具集。
assert.match(
  extHeader,
  /enum class PiNativeToolsVariant \{\s*Chat,\s*Creator,\s*\};/,
  "the extension installer must distinguish Chat and Creator variants",
);
assert.match(
  extHeader,
  /PiNativeToolsVariant variant = PiNativeToolsVariant::Chat/,
  "the Chat variant must remain the default so existing callers are unchanged",
);
assert.match(
  extHeader,
  /const std::wstring& targetDirectory = \{\}/,
  "the extension target directory must be overridable",
);
assert.match(
  extCpp,
  /variant == PiNativeToolsVariant::Creator\s*\n?\s*\? L"miaodesk-creator-tools\.ts"\s*\n?\s*: L"miaodesk-native-tools\.ts"/,
  "the two variants must materialise two different files, not one shared path",
);
assert.match(
  extCpp,
  /targetDirectory\.empty\(\) \? fs::path\(PiAgentDirectory\(\)\) : fs::path\(targetDirectory\)/,
  "an empty target directory falls back to PiAgentRoot so chat is unchanged",
);
assert.match(
  extCpp,
  /后写的那份会替换前一份/,
  "the reason the files are separate must be recorded where the code lives",
);

// 创作者 profile 的每一项都必须来自 workspace / agentDir,而不是复用聊天的。
const creatorFn = profile.slice(
  profile.indexOf("PiLaunchProfile MakeCreatorLaunchProfile"),
);
assert.match(creatorFn, /profile\.sessionDir = profile\.agentDir \+ L"\\\\sessions";/,
  "the Creator profile must have its own Pi session directory");
assert.match(creatorFn, /profile\.workingDirectory = std::move\(workspaceRoot\);/,
  "the Creator profile's cwd must be that work's workspace");
assert.match(creatorFn, /profile\.extensionPath = std::move\(extensionPath\);/,
  "the Creator profile must carry its own extension path");
assert.match(creatorFn, /profile\.toolAllowlist = kPiCreatorToolAllowlist;/,
  "the Creator profile must use the creator allowlist");
assert.match(creatorFn, /profile\.signatureSalt = L"creator-v1";/,
  "the Creator profile must have its own signature salt");

// 两份 profile 的 signature 必须不同,否则同一份 Provider 配置下 EnsureSession 会
// 把另一个模式还活着的子进程当成"可复用",于是创作跑在聊天的进程上(或反过来)。
const saltOf = (fn) => {
  const body = profile.slice(profile.indexOf(`PiLaunchProfile Make${fn}LaunchProfile`));
  const m = body.match(/profile\.signatureSalt = L"([^"]+)";/);
  assert.ok(m, `Make${fn}LaunchProfile must set a signature salt`);
  return m[1];
};
assert.notStrictEqual(saltOf("Chat"), saltOf("Creator"),
  "the two profiles must not share a signature salt, or one child process could be reused");
assert.match(
  runtime,
  /launchProfile_\.mode == PiLaunchMode::Creator \? L"\|creator" : L"\|chat"/,
  "the process signature must name the mode, or the two runtimes could reuse one child",
);

// Pi 的 session 目录必须真的通过环境变量传进去。
assert.match(
  runtime,
  /\{L"PI_CODING_AGENT_SESSION_DIR", setup\.sessionDir\.empty\(\) \? setup\.agentDir : setup\.sessionDir\}/,
  "PI_CODING_AGENT_SESSION_DIR must be exported from the profile, not guessed inside the child",
);

// --- 3. 创作 allowlist 拿掉通用工具 ----------------------------------------

const creatorList = concatenatedLiterals(profile, "kPiCreatorToolAllowlist");
const creatorNames = new Set(names(creatorList));

for (const tool of ["bash", "edit", "write", "grep", "find", "ls"]) {
  assert.ok(!creatorNames.has(tool),
    `the creator allowlist must NOT inherit ${tool} from generic chat`);
}
for (const tool of ["ppt_create", "settings_open", "file_create", "file_open",
                    "folder_list", "wallpaper_validate_package", "wallpaper_state_get",
                    "desktop_widget_list", "desktop_preview_wallpaper",
                    "desktop_preview_examples", "image_generate"]) {
  assert.ok(!creatorNames.has(tool),
    `the creator allowlist must not carry chat-only ${tool}`);
}
for (const tool of ["content_skill_get", "creator_capabilities_get", "creator_package_read",
                    "creator_package_update", "creator_asset_import", "creator_image_generate",
                    "creator_candidate_submit", "creator_preview_evidence"]) {
  assert.ok(creatorNames.has(tool),
    `the creator allowlist must offer ${tool} so authoring is still possible`);
}

// 两份 allowlist 必须不同,而且只允许共享一个工具:content_skill_get。
// 计划 §5 把它标为"已有,复用" —— 两模式都要读同一份 Skill 规范,它是只读的,
// 不涉及包写入或桌面状态。除它之外再出现共享项,就说明两份配置又开始共用工具集了。
assert.notStrictEqual(creatorList, chatList, "the two allowlists must differ");
const shared = names(creatorList).filter((t) => chatNames.has(t));
shared.sort();
assert.deepStrictEqual(shared, ["content_skill_get"],
  `only content_skill_get may be shared; found: ${shared.join(", ") || "(none)"}`);

// 创作 session 的 allowlist 必须真的被用到,不能定义了却没人读。
assert.match(
  runtime,
  /setup\.toolAllowlist = launchProfile_\.toolAllowlist;/,
  "the profile allowlist must reach the launch command",
);
assert.match(
  runtime,
  /L" --tools " \+ toolAllowlist/,
  "--tools must come from the profile, not a hardcoded literal",
);

// CMake 必须编译新文件,否则它只存在于磁盘上(2026-09-22 LNK2019 那一课)。
const cmake = read("src/CMakeLists.txt");
assert.match(cmake, /desktop\/control\/PiLaunchProfile\.cpp/,
  "PiLaunchProfile.cpp must be in a CMake source list");

console.log("creator session isolation contract: OK");
