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
let ready = false;
let promptSent = false;
let promptAccepted = false;
let finalResponseSent = false;
let stdoutBuffer = "";
let stderr = "";
const rpcTrace = [];

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

function traceRpc(value) {
  let text;
  try { text = typeof value === "string" ? value : JSON.stringify(value); }
  catch { text = String(value); }
  if (text.length > 1200) text = text.slice(0, 1200) + "...";
  rpcTrace.push(text);
  if (rpcTrace.length > 40) rpcTrace.shift();
}

function diagnostics() {
  return `turn=${turn} ready=${ready} promptSent=${promptSent} promptAccepted=${promptAccepted} finalResponseSent=${finalResponseSent} state=${JSON.stringify(artifactState())} trace=${JSON.stringify(rpcTrace)} stderr=${stderr}`;
}

function fail(message) {
  if (finished) return;
  finished = true;
  console.error(`${message}\n${diagnostics()}`);
  try { child?.kill(); } catch {}
  try { server?.close(); } catch {}
  cleanupPackage();
  process.exitCode = 1;
}

function sendRpc(command) {
  if (!child?.stdin || child.stdin.destroyed || !child.stdin.writable) {
    fail(`Pi RPC stdin is not writable for ${command.type}.`);
    return;
  }
  traceRpc({ direction: "in", ...command });
  child.stdin.write(JSON.stringify(command) + "\n", "utf8", (error) => {
    if (error) fail(`Pi RPC write failed for ${command.type}: ${error.message}`);
  });
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
    fail("Pi E2E artifacts did not settle after final agent_settled.");
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
      finalResponseSent = true;
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
  }, null, 2));

  child = spawn(process.execPath, [
    piCli,
    "--mode", "rpc",
    "--no-session",
    "--approve",
    "--offline",
    "--provider", "turingdesk",
    "--model", "ci-model",
    "--no-extensions",
    "--extension", extensionTarget,
    "--no-skills",
    "--no-prompt-templates",
    "--no-themes",
    "--no-context-files",
    "--tools", "read,bash,edit,write,grep,find,ls,settings_open,wallpaper_create_web_package,wallpaper_validate_package",
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
    fail("Pi E2E timed out.");
  }, 120000);

  child.stderr.on("data", (chunk) => { stderr += chunk.toString("utf8"); });
  child.stdout.on("data", (chunk) => {
    stdoutBuffer += chunk.toString("utf8");
    for (;;) {
      const newline = stdoutBuffer.indexOf("\n");
      if (newline < 0) break;
      const line = stdoutBuffer.slice(0, newline).replace(/\r$/, "");
      stdoutBuffer = stdoutBuffer.slice(newline + 1);
      if (!line.trim()) continue;

      let message;
      try { message = JSON.parse(line); }
      catch {
        traceRpc({ direction: "out-non-json", line });
        continue;
      }
      traceRpc({ direction: "out", message });

      if (message.type === "extension_error") {
        clearTimeout(timeout);
        fail(`Pi extension failed to load or execute: ${message.error || JSON.stringify(message)}`);
        continue;
      }

      if (message.type === "response" && message.command === "get_state" && message.id === "ready-1") {
        if (!message.success) {
          clearTimeout(timeout);
          fail(`Pi RPC get_state rejected: ${message.error || "unknown error"}`);
          continue;
        }
        const model = message.data?.model;
        if (!model || model.id !== "ci-model") {
          clearTimeout(timeout);
          fail(`Pi RPC ready state has wrong model: ${JSON.stringify(model)}`);
          continue;
        }
        ready = true;
        if (!promptSent) {
          promptSent = true;
          sendRpc({
            id: "turn-1",
            type: "prompt",
            message: "Use the requested tools, including the TuringDesk wallpaper tool, then finish.",
          });
        }
        continue;
      }

      if (message.type === "response" && message.command === "prompt" && message.id === "turn-1") {
        if (!message.success) {
          clearTimeout(timeout);
          fail(`Pi RPC rejected prompt: ${message.error || "unknown error"}`);
          continue;
        }
        promptAccepted = true;
        continue;
      }

      if (message.type !== "agent_settled" || !promptAccepted || !finalResponseSent || finished || verifying) continue;

      verifying = true;
      clearTimeout(timeout);
      verifyArtifacts();
    }
  });

  child.on("error", (error) => {
    clearTimeout(timeout);
    fail(`Pi process spawn failed: ${error.message}`);
  });

  child.on("exit", (code) => {
    if (!finished && code !== null) {
      clearTimeout(timeout);
      fail(`Pi exited before successful verification: ${code}`);
    }
  });

  child.once("spawn", () => {
    sendRpc({ id: "ready-1", type: "get_state" });
  });
});
