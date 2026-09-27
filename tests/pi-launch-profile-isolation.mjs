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

// --- 4. 创作 profile 必须真的被装上 -----------------------------------------
//
// 这一节是 2026-09-28 补的,而它当时是**红着写进来的**。
//
// 上面三节把两份 profile 的形状钉得很死:字段分开、salt 不同、allowlist 不共用工具、
// 签名带模式名。它们全绿。但整条创作链在出厂构建里**一个字节都没生效** ——
// `SetLaunchProfile` 在 PiRuntime.h 里定义,而 `MakeCreatorLaunchProfile` 在
// PiLaunchProfile.cpp 里定义,**两个都没有任何调用者**(src/ 下 grep 只有定义处自己)。
//
// 于是 PiRuntime::launchProfile_ 始终是结构体默认值,而它的 mode 默认是 Chat:
//   · sessionDir 空 → PI_CODING_AGENT_SESSION_DIR 退回 agentDir,创作会话与聊天同目录;
//   · workingDirectory 空 → 没有创作 cwd;
//   · toolAllowlist 空 → `--tools` 根本不传,Pi 用它自己的默认工具集 —— 也就是
//     CCA-04 要拿掉的 read/bash/edit/write/grep/find/ls **全都在**;
//   · systemPrompt 空 → 退回聊天那份;
//   · mode != Creator → MIAODESK_CREATOR_WORKSPACE / MIAODESK_CREATOR_SESSION 不导出,
//     而 src/app/main.cpp 的 RunCreatorTool 正是从这两个环境变量取工作区与会话 ——
//     所以 creator_* 八个工具即使注册了,拿到的也是空工作区。
//
// 这就是"宿主持有会话 ID、工作区、epoch、台账"那句话真正的根因:不是界面上取不到,
// 是**导出它们的那个调用从来没发生**。前半句(UI 取不到)是我先前的判断,错了。
//
// 为什么用"有调用者"而不是别的形状判据:这个缺口在所有语法门、在所有断言 profile
// 内部形状的门面前都是绿的 —— 两个函数都存在、都被 CMake 编译、都有人读。
// 唯一能发现"定义完好却没人调用"的办法就是数调用者。
import path from "node:path";

function callersOf(symbol) {
  // 整个 src/ 都扫,包括 .inc:创作面板的实现按片段 include 进 ConversationPanel.cpp,
  // 只看 .cpp 会漏掉真正的挂载点。
  const roots = ["src"];
  const hits = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) { walk(full); continue; }
      if (!/\.(cpp|h|inc)$/.test(entry.name)) continue;
      const text = read(full);
      const re = new RegExp(`\\b${symbol}\\s*\\(`, "g");
      for (const m of text.matchAll(re)) {
        // 定义处自己不算调用者。两者形状不同,分开排。
        const lineStart = text.lastIndexOf("\n", m.index) + 1;
        const line = text.slice(lineStart, text.indexOf("\n", m.index));
        if (/^(inline\s+)?[\w:<>\s*&]+\b\w+\s*\(/.test(line) && !/=/.test(line.slice(0, m.index - lineStart))) {
          // 声明/定义行
        }
        if (/\bvoid\s+SetLaunchProfile\s*\(/.test(line)) continue;         // 头里的声明
        if (/PiLaunchProfile\s+Make\w+LaunchProfile\s*\(/.test(line)) continue; // 定义
        hits.push(`${full}:${text.slice(0, m.index).split("\n").length}`);
      }
    }
  };
  walk(roots[0]);
  return hits;
}

for (const symbol of ["SetLaunchProfile", "MakeCreatorLaunchProfile"]) {
  const hits = callersOf(symbol);
  assert.ok(hits.length > 0,
    `${symbol} 在 src/ 下没有任何调用者 —— 它定义完好、被 CMake 编译、也有人读,`
    + `但创建/安装的那一次调用从未发生,于是整条创作链在出厂构建里不生效。`
    + `(本轮实测:sessionDir 退回 agentDir、--tools 不传、两个 creator 环境变量不导出)`);
}

// CMake 必须编译新文件,否则它只存在于磁盘上(2026-09-22 LNK2019 那一课)。
const cmake = read("src/CMakeLists.txt");
assert.match(cmake, /desktop\/control\/PiLaunchProfile\.cpp/,
  "PiLaunchProfile.cpp must be in a CMake source list");

console.log("creator session isolation contract: OK");
