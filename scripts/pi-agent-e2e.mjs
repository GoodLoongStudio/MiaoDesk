import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";

const [work, piCli, powershell, nativeHost, extensionSource, desktop] = process.argv.slice(2);
if (![work, piCli, powershell, nativeHost, extensionSource, desktop].every(Boolean)) {
  console.error("usage: pi-agent-e2e.mjs <work> <pi-cli> <powershell> <native-host> <extension-source> <desktop>");
  process.exit(2);
}

const writeFile = path.join(work, "pi-write-result.txt");
const shellFile = path.join(work, "pi-shell-result.txt");
const packageName = `TuringDesk-Pi-E2E-${process.pid}-${Date.now()}`;
const packagePath = path.join(desktop, "TuringDesk Wallpapers", `${packageName}.tdwall`);
const manifestPath = path.join(packagePath, "manifest.json");
const htmlPath = path.join(packagePath, "index.html");
let turn = 0;
let server;
let child;
let finished = false;
let verifying = false;
let stdout = "";
let stderr = "";

function cleanupPackage() {
  try { fs.rmSync(packagePath, { recursive: true, force: true }); } catch {}
}

function sendChunk(res, delta, finish = null) {
  const payload = {
    id: "chatcmpl-turingdesk-pi",
    object: "chat.completion.chunk",
    created: 1,
    model: "ci-model",
    choices: [{ index: 0, delta, finish_reason: finish }],
  };
  res.write(`data: ${JSON.stringify(payload)}\n\n`);
}

function fail(message) {
  if (finished) return;
  finished = true;
  console.error(message);
  try { child?.kill(); } catch {}
  try { server?.close(); } catch {}
  cleanupPackage();
  process.exitCode = 1;
}

function artifactState() {
  return {
    write: fs.existsSync(writeFile),
    shell: fs.existsSync(shellFile),
    manifest: fs.existsSync(manifestPath),
    html: fs.existsSync(htmlPath),
  };
}

function verifyArtifacts(attempt = 0) {
  if (finished) return;
  const state = artifactState();
  if (!(state.write && state.shell && state.manifest && state.html)) {
    if (attempt < 150) {
      setTimeout(() => verifyArtifacts(attempt + 1), 100);
      return;
    }
    fail(`Pi E2E artifacts did not settle. turn=${turn} state=${JSON.stringify(state)} stderr=${stderr}`);
    return;
  }

  try {
    const writeText = fs.readFileSync(writeFile, "utf8").trim();
    const shellText = fs.readFileSync(shellFile, "utf8").trim();
    if (writeText !== "PI_WRITE_OK") throw new Error(`write tool mismatch: ${writeText}`);
    if (shellText !== "PI_SHELL_OK") throw new Error(`PowerShell tool mismatch: ${shellText}`);
    const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
    if (manifest.type !== "web" || manifest.entry !== "index.html") {
      throw new Error(`Native .tdwall manifest mismatch: ${JSON.stringify(manifest)}`);
    }

    finished = true;
    cleanupPackage();
    console.log("Pi RPC -> provider -> built-in write -> PowerShell -> TuringDesk Pi extension -> native .tdwall: OK");
    try { child?.kill(); } catch {}
    server.close(() => process.exit(0));
  } catch (error) {
    fail(error?.stack || String(error));
  }
}

server = http.createServer((req, res) => {
  if (req.method !== "POST" || req.url !== "/v1/chat/completions") {
    res.writeHead(404);
    res.end();
    return;
  }

  let raw = "";
  req.on("data", (chunk) => { raw += chunk; });
  req.on("end", () => {
    let body;
    try {
      body = JSON.parse(raw || "{}");
    } catch (error) {
      res.writeHead(400, { "content-type": "application/json" });
      res.end(JSON.stringify({ error: { message: String(error) } }));
      return;
    }

    const names = new Set((body.tools || []).map((tool) => tool?.function?.name));
    const requiredTools = [
      "write",
      "bash",
      "settings_open",
      "wallpaper_create_web_package",
      "wallpaper_validate_package",
    ];
    const missing = requiredTools.filter((name) => !names.has(name));
    if (missing.length > 0) {
      res.writeHead(500, { "content-type": "application/json" });
      res.end(JSON.stringify({ error: { message: `Pi did not expose required tools: ${missing.join(", ")}` } }));
      return;
    }

    turn += 1;
    res.writeHead(200, {
      "content-type": "text/event-stream",
      "cache-control": "no-cache",
      connection: "keep-alive",
    });

    if (turn === 1) {
      sendChunk(res, {
        role: "assistant",
        tool_calls: [{
          index: 0,
          id: "call_write",
          type: "function",
          function: {
            name: "write",
            arguments: JSON.stringify({ path: "pi-write-result.txt", content: "PI_WRITE_OK" }),
          },
        }],
      });
      sendChunk(res, {}, "tool_calls");
    } else if (turn === 2) {
      sendChunk(res, {
        role: "assistant",
        tool_calls: [{
          index: 0,
          id: "call_shell",
          type: "function",
          function: {
            name: "bash",
            arguments: JSON.stringify({ command: "Set-Content -LiteralPath 'pi-shell-result.txt' -Value 'PI_SHELL_OK'" }),
          },
        }],
      });
      sendChunk(res, {}, "tool_calls");
    } else if (turn === 3) {
      sendChunk(res, {
        role: "assistant",
        tool_calls: [{
          index: 0,
          id: "call_tdwall",
          type: "function",
          function: {
            name: "wallpaper_create_web_package",
            arguments: JSON.stringify({
              name: packageName,
              title: "TuringDesk Pi E2E",
              html: "<!doctype html><html><body>TURINGDESK_PI_NATIVE_TOOL_OK</body></html>",
              open_after_create: false,
            }),
          },
        }],
      });
      sendChunk(res, {}, "tool_calls");
    } else {
      const toolMessages = (body.messages || []).filter((message) => message?.role === "tool");
      const sawNativeResult = toolMessages.some((message) => {
        const text = JSON.stringify(message);
        return text.includes(packageName) && text.includes(".tdwall");
      });
      if (!sawNativeResult) {
        sendChunk(res, { role: "assistant", content: "PI_E2E_NATIVE_RESULT_MISSING" });
        sendChunk(res, {}, "stop");
      } else {
        sendChunk(res, { role: "assistant", content: "PI_E2E_OK" });
        sendChunk(res, {}, "stop");
      }
    }

    res.write("data: [DONE]\n\n");
    res.end();
  });
});

server.listen(0, "127.0.0.1", () => {
  const port = server.address().port;
  const agentDir = path.join(work, "agent");
  const extensionsDir = path.join(agentDir, "extensions");
  const extensionTarget = path.join(extensionsDir, "turingdesk-native-tools.ts");
  fs.mkdirSync(extensionsDir, { recursive: true });
  fs.copyFileSync(extensionSource, extensionTarget);

  fs.writeFileSync(path.join(agentDir, "models.json"), JSON.stringify({
    providers: {
      turingdesk: {
        name: "TuringDesk CI",
        baseUrl: `http://127.0.0.1:${port}/v1`,
        api: "openai-completions",
        apiKey: "$TURINGDESK_MODEL_API_KEY",
        models: [{ id: "ci-model", name: "ci-model", input: ["text"], contextWindow: 128000, maxTokens: 4096 }],
      },
    },
  }, null, 2));

  fs.writeFileSync(path.join(agentDir, "settings.json"), JSON.stringify({
    defaultProjectTrust: "always",
    defaultProvider: "turingdesk",
    defaultModel: "ci-model",
    shellPath: powershell,
    quietStartup: true,
    extensions: [extensionTarget],
  }, null, 2));

  child = spawn(process.execPath, [
    piCli,
    "--mode", "rpc",
    "--no-session",
    "--approve",
    "--provider", "turingdesk",
    "--model", "ci-model",
    "--tools", "read,bash,edit,write,grep,find,ls",
  ], {
    cwd: work,
    windowsHide: true,
    env: {
      ...process.env,
      PI_CODING_AGENT_DIR: agentDir,
      PI_OFFLINE: "1",
      PI_SKIP_VERSION_CHECK: "1",
      PI_TELEMETRY: "0",
      TURINGDESK_MODEL_API_KEY: "ci-key",
      TURINGDESK_NATIVE_TOOL_HOST: nativeHost,
    },
    stdio: ["pipe", "pipe", "pipe"],
  });

  const timeout = setTimeout(() => {
    fail(`Pi E2E timed out. turn=${turn} state=${JSON.stringify(artifactState())} stdout=${stdout} stderr=${stderr}`);
  }, 120000);

  child.stderr.on("data", (chunk) => { stderr += chunk.toString("utf8"); });
  child.stdout.on("data", (chunk) => {
    stdout += chunk.toString("utf8");
    for (;;) {
      const newline = stdout.indexOf("\n");
      if (newline < 0) break;
      const line = stdout.slice(0, newline).replace(/\r$/, "");
      stdout = stdout.slice(newline + 1);
      if (!line.trim()) continue;

      let message;
      try { message = JSON.parse(line); } catch { continue; }
      if (message.type !== "agent_settled" || finished || verifying) continue;

      verifying = true;
      clearTimeout(timeout);
      verifyArtifacts();
    }
  });

  child.on("exit", (code) => {
    if (!finished && code !== null) {
      clearTimeout(timeout);
      fail(`Pi exited before successful verification: ${code}; turn=${turn}; state=${JSON.stringify(artifactState())}; stderr=${stderr}`);
    }
  });

  child.stdin.write(JSON.stringify({
    id: "turn-1",
    type: "prompt",
    message: "Use the requested tools, including the TuringDesk wallpaper tool, then finish.",
  }) + "\n");
});
