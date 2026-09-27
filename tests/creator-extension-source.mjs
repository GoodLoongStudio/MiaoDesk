// CCA-04:创作扩展必须真的把那八个工具注册给模型,而且必须是另一套,不是少几个的聊天。
//
// 计划 §5 的验收原句:"按进程/会话配置验证扩展实际拿到的工具清单"。注册漏一个工具的
// 故障是**静默**的:Pi 不报错,worker 也不报错,模型只是从来没见过那个工具,而用户看到
// 的现象是"它好像不会做这件事"。没有任何编译器和运行时会替我们发现这件事。
//
// 所以这里做三件事,每件都对着一种真实的失效:
//   1. 把 C++ 里那段 TypeScript 按 CreatorExtensionSource() 的做法拼出来,
//      用 Node 自己的解析器做一次真正的语法检查 —— 原始字符串里打错一个括号,
//      平时只会在 Windows 上启动 Pi 的那一刻才炸;
//   2. 名册里每个名字都必须在注册体里出现一次(漏注册 -> 红);
//   3. 创作扩展里不能有聊天那一套的影子:通用 shell/文件/图片/桌面工具一个都不行
//      (拿错了源 -> 红)。少了这条,"关闭通用工具后仍可完整制作"就无法验证,
//      因为没人说得清制作到底依赖了哪些能力。
import fs from "node:fs";
import { execFileSync } from "node:child_process";
import { writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import assert from "node:assert/strict";

const read = (p) => fs.readFileSync(p, "utf8");
const cpp = read("src/ai/pi/PiNativeToolsExtension.cpp");
const registry = read("src/desktop/control/CreatorToolRegistry.cpp");

// C++ 把 TypeScript 存在 raw string literal 里。这里必须按同样的规则取,
// 否则测到的是另一份源码。
function literal(name) {
  const re = new RegExp(`constexpr std::string_view ${name} = R"PIEXT\\(([\\s\\S]*?)\\)PIEXT";`);
  const m = re.exec(cpp);
  assert.ok(m, `${name} must be a raw string literal in PiNativeToolsExtension.cpp`);
  return m[1];
}

// --- 名册:仍然只有一份 --------------------------------------------------------

const registryNames = [...registry.matchAll(/^    "([a-z_]+)",$/gm)].map((m) => m[1]);
const creatorNames = registryNames.filter((n) => n.startsWith("creator_") || n === "content_skill_get");
assert.ok(creatorNames.length === 8, `the registry must list exactly eight creator tools, got ${creatorNames.length}`);

// --- 1. TOOL_NAMES 必须由名册生成,而不是第二份手写清单 ------------------------

// 这一条是整份测试的支点。如果 CreatorExtensionSource() 改成手写八个名字,
// "名册加了工具但扩展没注册"就重新变成可能的,而这个测试的其余部分全都是在
// 给一个已经漂移过的清单做复核。
assert.match(
  cpp,
  /for \(const auto& name : miaodesk::creator::CreatorToolNames\(\)\)/,
  "the creator TOOL_NAMES must be generated from CreatorToolNames(), not hand-written a second time",
);
assert.match(
  cpp,
  /source \+= "  \\"";/,
  "each registry name must be emitted as its own quoted array element",
);
assert.match(
  cpp,
  /if \(variant == PiNativeToolsVariant::Creator\) \{\s*\n\s*expected = CreatorExtensionSource\(\);/,
  "the Creator variant must install CreatorExtensionSource(), not the chat source",
);
// 反方向同样要钉住:聊天那份不能悄悄变成创作那份。
assert.match(
  cpp,
  /expected\.append\(kExtensionSourcePart1\);\s*\n\s*expected\.append\(kExtensionSourcePart2\);/,
  "the Chat variant must keep installing the original two-part source unchanged",
);

// --- 2. 拼出真正的源文件,并用 Node 自己的解析器检查语法 ----------------------

const head = literal("kCreatorExtensionSourceHead");
const body = literal("kCreatorExtensionSourceBody");
const generatedToolNames =
  "const TOOL_NAMES = [\n" + creatorNames.map((n) => `  "${n}",`).join("\n") + "\n] as const;\n\n";
const source = head + generatedToolNames + body;

// 生成的源必须先能解析。原始字符串里少一个括号,在 macOS 上没有任何编译器会报,
// 因为它只是 C++ 的一个字符串常量;到了 Windows 上 Pi 加载扩展的那一刻才炸,
// 而那一刻的现象是"创作工具一个都没有",看起来像模型不会用。
//
// 具体做法是 import() 它,然后按**错误种类**判断:
//   * SyntaxError            -> 没解析过,闸门必须红;
//   * ERR_MODULE_NOT_FOUND   -> 解析过了,只是 @earendil-works/pi-ai 没装,这是预期的。
//
// 为什么不直接用 `node --check`:它只对 CJS 生效,而对带 import 的 .ts 文件**无条件
// 返回 0**。我第一版就是这么写的,拿一个故意写错的文件试过才知道它什么都不查 ——
// 一个永远绿的闸门比没有闸门更糟,因为它会让人觉得这里验证过了。
//
// 同理,探针也必须真的失败过一次才算数:如果这台机器的 Node 连"故意写错"都抓不到,
// 那不是"没有语法问题",是"这个闸门在这里不工作",必须响亮地报出来。
const workDir = mkdtempSync(join(tmpdir(), "creator-ext-"));

function parseOutcome(file) {
  try {
    execFileSync(process.execPath, ["--experimental-strip-types", PARSE_PROBE, file], {
      stdio: ["ignore", "pipe", "pipe"],
    });
    return "ran";
  } catch (error) {
    const out = `${error.stdout ?? ""}${error.stderr ?? ""}`;
    return /SyntaxError/.test(out) ? "syntax" : "other";
  }
}

const PARSE_PROBE = join(workDir, "parse-probe.mjs");
writeFileSync(
  PARSE_PROBE,
  "const target = process.argv[2];\n" +
    "try { await import(target); } catch (error) {\n" +
    "  if (error && error.name === 'SyntaxError') { process.stdout.write('SyntaxError\\n'); process.exit(3); }\n" +
    "}\n" +
    "process.stdout.write('parsed\\n');\n",
);

try {
  // 探针一:故意写坏的文件必须报 SyntaxError。抓不到就说明这个闸门在这里是空的。
  const broken = join(workDir, "broken.ts");
  writeFileSync(broken, 'const x: number = 1;\nexport default function (: {\n');
  const brokenOutcome = parseOutcome(broken);
  if (brokenOutcome !== "syntax") {
    console.error(
      "creator extension source gate CANNOT RUN on this Node " +
        `(${process.version}): it did not report a SyntaxError for a deliberately broken ` +
        "TypeScript file, so a pass here would prove nothing.\n" +
        "Use Node >= 22.6 (the CI runner's version).",
    );
    process.exit(2);
  }

  // 探针二:能解析的文件必须报 parsed。
  const fine = join(workDir, "fine.ts");
  writeFileSync(fine, 'import { tmpdir as t } from "node:os";\nconst y: number = 1;\nexport default t() + String(y);\n');
  if (parseOutcome(fine) !== "ran") {
    console.error(
      `creator extension source gate CANNOT RUN on this Node (${process.version}): ` +
        "its own control file did not parse.",
    );
    process.exit(2);
  }

  // 真正的源文件。
  const file = join(workDir, "miaodesk-creator-tools.ts");
  writeFileSync(file, source);
  const outcome = parseOutcome(file);
  if (outcome === "syntax") {
    console.error("the generated creator extension does not parse as TypeScript.");
    process.exit(1);
  }
} finally {
  rmSync(workDir, { recursive: true, force: true });
}

// --- 3. 每个名册里的工具都必须在注册体里注册 ----------------------------------

// 注册调用形如 creator("creator_package_read", "Write Package File", ...
// 逐个名字找:漏一个就红。这是上面那个"TOOL_NAMES 由名册生成"的反方向 ——
// 生成只保证 --tools 里有它,不保证模型见过它。
const registered = new Set(
  [...body.matchAll(/\bcreator\("([a-z_]+)",\s*"/g)].map((m) => m[1]),
);
for (const name of creatorNames) {
  assert.ok(registered.has(name),
    `the creator extension must register ${name} -- a tool Pi never registers is ` +
    "indistinguishable from one it refuses: the model simply never sees it, and nothing fails");
}
for (const name of registered) {
  assert.ok(creatorNames.includes(name),
    `${name} is registered by the creator extension but is not in the tool registry ` +
    "-- one of the two lists drifted");
}

// --- 4. 创作扩展不能带聊天那一套 ----------------------------------------------

// 这几条不是风格检查。chat 那份源能写文件、能调图片接口、能读桌面状态;创作这份
// 一个都不行。把两份源混在一起(哪怕只是复制了 import 行)的后果是:创作会话拿到
// 了本不该有的能力,而"关闭通用工具后仍可完整制作"这句话从此无法验证。
const chatOnlyRegistrations = [
  "settings_open", "ppt_create", "file_create", "folder_list", "file_open",
  "image_generate", "wallpaper_validate_package", "wallpaper_state_get",
  "desktop_widget_list", "desktop_preview_wallpaper", "desktop_preview_examples",
];
for (const tool of chatOnlyRegistrations) {
  assert.ok(!registered.has(tool),
    `${tool} is a chat-only tool and must not be registered by the creator extension`);
  // 名字也不能以别的方式出现(例如写进 system prompt 让模型去调)。
  assert.ok(!source.includes(`"${tool}"`),
    `${tool} must not appear anywhere in the creator extension source`);
}
// 图片那条链是把二进制写进 process.cwd() 的,而创作的 cwd 就是工作区。
// 放进创作扩展意味着模型可以绕开 assets/ 布局直接落盘。
assert.ok(!/pi-ai\/compat/.test(source),
  "the creator extension must not import pi-ai's image compat: image generation goes through the host worker");
assert.ok(!source.includes("Buffer.from"),
  "the creator extension must not write image bytes itself -- creator_image_generate is executed by the host");
assert.ok(!source.includes("MiaoDesk Images"),
  "the creator extension must not have the chat image output directory");
// 通用 shell 与文件能力:一条都不该有。
for (const forbidden of ["bash", "grep", "read", "write", "edit", "find", "ls"]) {
  assert.ok(!registered.has(forbidden), `${forbidden} must not be a creator tool`);
}

// --- 5. 会话绑定必须由宿主提供,而不是让模型填 ---------------------------------

// 模型填 sessionId 的后果是它可以声称自己属于另一个作品。宿主通过环境变量给出,
// 扩展补进参数只是为了让"忘了带"不变成失败;worker 侧仍然用自己的环境变量复核。
assert.match(
  head,
  /const SESSION = process\.env\.MIAODESK_CREATOR_SESSION \?\? "";/,
  "the creator extension must take its session id from the host environment",
);
assert.match(
  head,
  /const WORKSPACE = process\.env\.MIAODESK_CREATOR_WORKSPACE \?\? "";/,
  "the creator extension must take its workspace from the host environment",
);
assert.match(
  body,
  /\{ \.\.\.params, sessionId: SESSION, workspaceRoot: WORKSPACE \}/,
  "every creator tool call must carry the host-owned session and workspace",
);
// worker 的等待上限比聊天长:封存与渲染都比"打开一个文件"慢得多。
// 但它仍然必须有上限 —— 没有上限的等待会挂住整个会话,而用户看不到原因。
assert.match(source, /}, 120000\);/, "creator tool calls must keep a bounded timeout");

// --- 6. 聊天侧那份必须原样保留 ------------------------------------------------

// 图片 provider 的两个闸门从 kExtensionSourcePart1 里抽取源码来跑真实字节。
// 结构调整必须让它们继续抽到同一段逻辑,否则那两个闸门会在"看起来通过"的状态下
// 什么都不测。
const chatPart1 = literal("kExtensionSourcePart1");
const chatPart2 = literal("kExtensionSourcePart2");
for (const marker of [
  "async function resolveImageApiKey()",
  "function usesOpenAICompatibleImageShim",
  "const LOOPBACK_HOSTS",
]) {
  assert.ok(chatPart1.includes(marker),
    `the chat extension must keep ${marker}: tests/extract-image-provider.mjs runs these real bytes`);
}
assert.match(chatPart2, /export default function turingDeskNativeTools/,
  "the chat extension entrypoint must keep its name");
assert.match(chatPart2, /pi\.on\("session_start", \(\) => activateNativeTools\(pi\)\)/,
  "the chat extension must still activate its tools on session_start");

console.log(
  `creator extension source: OK (parses; ${creatorNames.length} tools registered, ` +
    "chat-only surface absent, binding from host env)",
);
