#!/usr/bin/env node
// Pi RPC 能力探测(CCA-01 的可复现证据)。
//
// 为什么需要一个会跑起来的探测,而不是读 .d.ts:
//   docs/CREATOR_AGENT_PI_CAPABILITY_PROBE.md 记录的决定性结论是 ——
//   RpcCommand 的 prompt 接受 images,但当 models.json 的 input 只有 ["text"] 时,
//   pi-ai 会把图片**静默替换**成一句 "(image omitted: model does not support images)"。
//   RPC 回 success:true、agent_settled 无 error、stdout 没有任何异常。
//   也就是说:一旦宿主把"发过图片"当成"模型看过图",这条链路上的失败
//   在产品里完全不可观测。这个事实只有真跑一次才能确认。
//
// 因此它做的每个断言都对着 **Provider 实际收到的 HTTP 请求体**,而不是对着 RPC 的返回值。
//
// 用法:
//   npm install --prefix /tmp/pi-probe @earendil-works/pi-coding-agent@0.83.0
//   PI_PACKAGE=/tmp/pi-probe/node_modules/@earendil-works/pi-coding-agent \
//     node scripts/probe-pi-rpc-capability.mjs
//
// 退出码 0 = 全部探测项得到预期结论;1 = 有探测项和记录不一致(锁版本升级后最先响的是它)。
//
// 它不接 CI:repo-hygiene 的 node 闸门全部是"读仓库文本"的纯检查,而这个探测要
//   npm install 整个 Pi 依赖树。装一次几分钟,作为推送前的手动门更合适。
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { mkdirSync, writeFileSync, rmSync, readFileSync, existsSync } from "node:fs";
import { join, dirname } from "node:path";

const PI_PACKAGE = process.env.PI_PACKAGE ?? "";
const PI_VERSION = process.env.PI_VERSION ?? "0.83.0";
const PORT = 45871;
// 探测用的扩展 import 了 typebox,Node 从扩展所在目录逐级向上找 node_modules。
// 所以默认工作目录必须落在装了 Pi 的那棵树里(而不是系统临时目录),
// 否则扩展加载失败,而失败在 RPC 层同样是静默的 —— stderr 空、session_start 不上报。
// dirname(PI_PACKAGE) 是 node_modules 树的根;再往上一层才是 npm 项目根。
const PI_PROJECT_ROOT = dirname(dirname(PI_PACKAGE));
const WORK = process.env.PI_PROBE_WORK ?? join(PI_PROJECT_ROOT, ".miaodesk-pi-rpc-probe");
const PNG_1PX =
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFAAH/q842iQAAAABJRU5ErkJggg==";

const failures = [];
const notes = [];
function check(name, ok, detail) {
  (ok ? notes : failures).push(`${ok ? "PASS" : "FAIL"}  ${name}${detail ? " — " + detail : ""}`);
  console.log(`${ok ? "  ✓" : "  ✗"} ${name}${detail ? " — " + detail : ""}`);
}
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

if (!PI_PACKAGE) {
  console.error("设置 PI_PACKAGE 指向已安装的 @earendil-works/pi-coding-agent 目录");
  process.exit(2);
}
const PI_CLI = join(PI_PACKAGE, "dist", "cli.js");
if (!existsSync(PI_CLI)) {
  console.error(`找不到 Pi CLI:${PI_CLI}`);
  process.exit(2);
}
const resolvedVersion = JSON.parse(readFileSync(join(PI_PACKAGE, "package.json"), "utf8")).version;
console.log(`\nPi RPC 能力探测 · 运行时 ${resolvedVersion}(记录基线 ${PI_VERSION})\n`);

rmSync(WORK, { recursive: true, force: true });
mkdirSync(WORK, { recursive: true });

// ---------------------------------------------------------------------------
// 一个会记账的 Provider:把收到的每个请求体原样落盘,断言由此而来。
// ---------------------------------------------------------------------------
const requests = [];
const server = createServer((req, res) => {
  let body = "";
  req.on("data", (c) => (body += c));
  req.on("end", () => {
    let parsed = null;
    try { parsed = JSON.parse(body); } catch {}
    requests.push({ url: req.url, auth: req.headers.authorization ?? null, body: parsed ?? body });

    res.writeHead(200, { "Content-Type": "text/event-stream" });
    const chunk = (delta, finish) => `data: ${JSON.stringify({
      id: "chatcmpl-probe", object: "chat.completion.chunk", created: 1,
      model: "probe-model", choices: [{ index: 0, delta, finish_reason: finish ?? null }],
    })}\n\n`;
    res.write(chunk({ role: "assistant", content: "PROBE_OK" }, null));
    res.write(chunk({}, "stop"));
    res.write(`data: ${JSON.stringify({
      id: "chatcmpl-probe", object: "chat.completion.chunk", created: 1,
      model: "probe-model", choices: [{ index: 0, delta: {}, finish_reason: "stop" }],
      usage: { prompt_tokens: 1, completion_tokens: 1, total_tokens: 2 },
    })}\n\n`);
    res.write("data: [DONE]\n\n");
    res.end();
  });
});
await new Promise((resolve, reject) => {
  server.once("error", (e) => reject(new Error(`无法监听 ${PORT}:${e.code}(上一次探测的 Provider 还在跑?)`)));
  server.listen(PORT, "127.0.0.1", resolve);
});

function agentDir(name, input) {
  const dir = join(WORK, name);
  mkdirSync(dir, { recursive: true });
  writeFileSync(join(dir, "models.json"), JSON.stringify({
    providers: {
      miaodesk: {
        name: "Probe", baseUrl: `http://127.0.0.1:${PORT}`, api: "openai-completions",
        apiKey: "$MIAODESK_MODEL_API_KEY",
        models: [{ id: "probe-model", name: "probe-model", input,
                   contextWindow: 128000, maxTokens: 16384 }],
      },
    },
  }, null, 2));
  writeFileSync(join(dir, "settings.json"), JSON.stringify({
    defaultProjectTrust: "always", defaultProvider: "miaodesk",
    defaultModel: "probe-model", quietStartup: true,
  }, null, 2));
  return dir;
}

function launch(agentDir, tools, extensionPath) {
  const child = spawn(process.execPath, [PI_CLI,
    "--mode", "rpc", "--no-session", "--approve",
    "--provider", "miaodesk", "--model", "probe-model",
    "--no-extensions", ...(extensionPath ? ["--extension", extensionPath] : []),
    ...(tools ? ["--tools", tools] : []),
    "--append-system-prompt", "MiaoDesk Pi RPC capability probe."], {
    cwd: WORK,
    env: {
      ...process.env,
      PI_CODING_AGENT_DIR: agentDir,
      PI_CODING_AGENT_SESSION_DIR: join(agentDir, "sessions"),
      MIAODESK_MODEL_API_KEY: "probe-key-not-a-real-credential",
      PI_OFFLINE: "1", PI_SKIP_VERSION_CHECK: "1", PI_TELEMETRY: "0",
    },
    stdio: ["pipe", "pipe", "pipe"],
  });
  const replies = new Map();
  const events = [];
  let buffer = "";
  let stderr = "";
  child.stdout.on("data", (d) => {
    buffer += d.toString();
    for (let i = buffer.indexOf("\n"); i >= 0; i = buffer.indexOf("\n")) {
      const line = buffer.slice(0, i).trim();
      buffer = buffer.slice(i + 1);
      if (!line) continue;
      try { const o = JSON.parse(line); events.push(o); if (o.id) replies.set(o.id, o); } catch {}
    }
  });
  child.stderr.on("data", (d) => { stderr += d.toString(); });
  let n = 0;
  const call = async (obj) => {
    const id = `r-${++n}`;
    child.stdin.write(JSON.stringify({ id, ...obj }) + "\n");
    for (let i = 0; i < 100; i++) { if (replies.has(id)) return replies.get(id); await wait(50); }
    return { id, missing: true };
  };
  const settle = async () => {
    for (let i = 0; i < 80; i++) { if (events.some((e) => e.type === "agent_settled")) return true; await wait(250); }
    return false;
  };
  // stderr 必须是取值函数,不能直接塞进返回对象:对象字面量会在 return 那一刻
  // 把字符串拷成空值,之后子进程写的东西一概看不到。
  return { child, call, settle, events, getStderr: () => stderr };
}

// ---------------------------------------------------------------------------
// 1. 图片是否真的进入 Provider 请求 —— 这是 CCA-01 的核心断言
// ---------------------------------------------------------------------------
async function imageProbe(label, input) {
  const before = requests.length;
  const p = launch(agentDir(label, input));
  await wait(1400);
  p.child.stdin.write(JSON.stringify({
    id: "p1", type: "prompt",
    message: "Look at this image and reply with PROBE_OK.",
    images: [{ type: "image", data: PNG_1PX, mimeType: "image/png" }],
  }) + "\n");
  const settled = await p.settle();
  await wait(300);
  p.child.kill("SIGTERM");
  await p.child.exited ?? wait(200);

  const reply = p.events.find((e) => e.id === "p1" && e.type === "response");
  const http = requests.slice(before).find((r) => typeof r.body === "object" && Array.isArray(r.body?.messages));
  const user = (http?.body?.messages ?? []).filter((m) => m.role === "user").map((m) => m.content);
  const sawBase64 = JSON.stringify(http?.body ?? {}).includes(PNG_1PX);
  return { label, settled, replyOk: reply?.success === true, user, sawBase64, httpCount: requests.length - before };
}

console.log("1) 带图 prompt:RPC 说成功,Provider 是否真的收到图");
const withVision = await imageProbe("vision-declared", ["text", "image"]);
check("input 声明含 image → Provider 请求里出现 image_url data URL",
  withVision.sawBase64,
  withVision.user.length ? JSON.stringify(withVision.user[0]).slice(0, 160) : "没有 user 消息");
check("该轮 RPC 回 success 且 agent_settled", withVision.replyOk && withVision.settled);

console.log("\n2) 同一 prompt,但 input 只有 text(今天产品里的写法)");
const textOnly = await imageProbe("text-only-declared", ["text"]);
check("input 只有 text → 请求里只剩占位文本,base64 不出现",
  !textOnly.sawBase64,
  textOnly.user.length ? JSON.stringify(textOnly.user[0]) : "没有 user 消息");
check("该轮 RPC 依然回 success 且 agent_settled(所以失败在 RPC 层不可观测)",
  textOnly.replyOk && textOnly.settled);

// ---------------------------------------------------------------------------
// 3. --tools 是否真的约束扩展工具(CCA-04 的前提)
// ---------------------------------------------------------------------------
console.log("\n3) --tools 是否约束扩展注册的工具");
const extDir = join(WORK, "ext-probe");
mkdirSync(join(extDir, "extensions"), { recursive: true });
writeFileSync(join(extDir, "models.json"), JSON.stringify({
  providers: { miaodesk: {
    name: "Probe", baseUrl: `http://127.0.0.1:${PORT}`, api: "openai-completions",
    apiKey: "$MIAODESK_MODEL_API_KEY",
    models: [{ id: "probe-model", name: "probe-model", input: ["text", "image"],
               contextWindow: 128000, maxTokens: 16384 }],
  } },
}, null, 2));
writeFileSync(join(extDir, "settings.json"), JSON.stringify({
  defaultProjectTrust: "always", defaultProvider: "miaodesk",
  defaultModel: "probe-model", quietStartup: true,
}, null, 2));
writeFileSync(join(extDir, "extensions", "probe-tools.ts"), `import { Type } from "typebox";
export default function probeTools(pi) {
  pi.registerTool({ name: "probe_creator_only", label: "A", description: "A",
    parameters: Type.Object({}, { additionalProperties: false }),
    async execute() { return { content: [{ type: "text", text: "ran" }], details: {} }; } });
  pi.registerTool({ name: "probe_second", label: "B", description: "B",
    parameters: Type.Object({}, { additionalProperties: false }),
    async execute() { return { content: [{ type: "text", text: "ran" }], details: {} }; } });
  pi.on("session_start", () => {
    console.error("[PROBE] active=" + JSON.stringify([...(pi.getActiveTools() ?? [])].sort()));
  });
}
`);

async function allowlistProbe(tools) {
  const p = launch(extDir, tools, join(extDir, "extensions", "probe-tools.ts"));
  await wait(3500);
  p.child.kill("SIGTERM");
  const m = [...p.getStderr().matchAll(/\[PROBE\] active=(\[.*\])/g)];
  if (!m.length) console.log(`    (无 session_start 上报;stderr 末 5 行:${JSON.stringify(p.getStderr().split("\n").filter(Boolean).slice(-5))})`);
  return m.length ? JSON.parse(m[m.length - 1][1]) : null;
}
const granted = await allowlistProbe("read,probe_creator_only,probe_second");
check("allowlist 里点名的扩展工具才会激活,且通用 shell/文件工具不混入",
  Array.isArray(granted) && granted.includes("probe_creator_only") &&
  !granted.includes("bash") && !granted.includes("edit") && !granted.includes("write"),
  JSON.stringify(granted));
const narrowed = await allowlistProbe("read");
check("未点名的扩展工具被 --tools 移除(创作专属 allowlist 可行的证据)",
  Array.isArray(narrowed) && narrowed.length === 1 && narrowed[0] === "read",
  JSON.stringify(narrowed));

// ---------------------------------------------------------------------------
// 4. 会话隔离与并发进程(CCA-03 的前提)
// ---------------------------------------------------------------------------
console.log("\n4) 并发进程与会话隔离");
const isoDir = agentDir("iso", ["text", "image"]);
const a = launch(isoDir);
const b = launch(isoDir);
await wait(1400);
const stateA = await a.call({ type: "get_state" });
const stateB = await b.call({ type: "get_state" });
const bothAlive = a.child.exitCode === null && b.child.exitCode === null;
check("同一 agent 目录下两个进程各自拿到不同 sessionId",
  !!stateA?.data?.sessionId && !!stateB?.data?.sessionId &&
  stateA.data.sessionId !== stateB.data.sessionId,
  `${stateA?.data?.sessionId} vs ${stateB?.data?.sessionId}`);
check("两个进程都还活着(没有互斥锁把第二个挡在外面)", bothAlive);
const newSession = await a.call({ type: "new_session" });
check("new_session 可用(换作品后可重置上下文)", newSession?.success === true && newSession?.data?.cancelled === false);
const sessionsCreated = existsSync(join(isoDir, "sessions"));
notes.push(`NOTE   --no-session 下 session 目录是否被创建:${sessionsCreated}`);
console.log(`  · --no-session 下 session 目录被创建:${sessionsCreated}(false 表示宿主必须自己保存作品状态)`);
a.child.kill("SIGTERM");
b.child.kill("SIGTERM");
await wait(400);

server.close();
console.log(`\n通过 ${notes.length} 项,失败 ${failures.length} 项`);
if (failures.length) {
  console.log("\n失败项:");
  for (const f of failures) console.log("  " + f);
  process.exit(1);
}
console.log("全部探测项与 docs/CREATOR_AGENT_PI_CAPABILITY_PROBE.md 记录一致。\n");
