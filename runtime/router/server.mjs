import http from "node:http";
import { URL } from "node:url";

const HOST = process.env.MIAODESK_ROUTER_HOST || "0.0.0.0";
const PORT = Number(process.env.MIAODESK_ROUTER_PORT || 8000);
const PUBLIC_MODEL = process.env.MIAODESK_ROUTER_PUBLIC_MODEL || "miaodesk";
const PRIMARY_BASE_URL = (process.env.MIAODESK_PRIMARY_BASE_URL || "http://127.0.0.1:8001/v1").replace(/\/+$/, "");
const FAST_BASE_URL = (process.env.MIAODESK_FAST_BASE_URL || "http://127.0.0.1:8002/v1").replace(/\/+$/, "");
const PRIMARY_MODEL = process.env.MIAODESK_PRIMARY_MODEL || "primary";
const FAST_MODEL = process.env.MIAODESK_FAST_MODEL || "fast";
const PRIMARY_API_KEY = process.env.MIAODESK_PRIMARY_API_KEY || "";
const FAST_API_KEY = process.env.MIAODESK_FAST_API_KEY || "";
const INBOUND_API_KEY = process.env.MIAODESK_ROUTER_API_KEY || "";
const SHORT_USER_CHARS = Number(process.env.MIAODESK_ROUTER_SHORT_USER_CHARS || 240);
const SHORT_TOTAL_CHARS = Number(process.env.MIAODESK_ROUTER_SHORT_TOTAL_CHARS || 900);

const SKILL_SIGNATURES = Object.freeze([
  "name: content-package-basics",
  "name: wallpaper-content",
  "name: widget-content",
  "name: content-review",
  "MiaoDesk Artifact and Desktop Safety",
]);

function textOf(content) {
  if (typeof content === "string") return content;
  if (!Array.isArray(content)) return "";
  return content.map(part => {
    if (typeof part === "string") return part;
    if (part && typeof part === "object" && part.type === "text") return String(part.text || "");
    return "";
  }).join("\n");
}

function classifyRequest(body) {
  const messages = Array.isArray(body?.messages) ? body.messages : [];
  const hasTools = Array.isArray(body?.tools) && body.tools.length > 0;
  const hasToolChoice = body?.tool_choice !== undefined && body?.tool_choice !== null && body.tool_choice !== "none";
  if (hasTools || hasToolChoice) return { route: "primary-tools", upstream: "primary" };

  const systemText = messages
    .filter(m => m?.role === "system")
    .map(m => textOf(m?.content))
    .join("\n");
  if (SKILL_SIGNATURES.some(signature => systemText.includes(signature)))
    return { route: "primary-skill", upstream: "primary" };

  const userMessages = messages.filter(m => m?.role === "user");
  const lastUser = userMessages.length ? textOf(userMessages[userMessages.length - 1]?.content) : "";
  const totalChars = messages.reduce((sum, m) => sum + textOf(m?.content).length, 0);
  if (lastUser.length <= SHORT_USER_CHARS && totalChars <= SHORT_TOTAL_CHARS)
    return { route: "fast-short", upstream: "fast" };

  return { route: "primary-chat", upstream: "primary" };
}

function upstreamFor(classification) {
  if (classification.upstream === "fast") {
    return { baseUrl: FAST_BASE_URL, model: FAST_MODEL, apiKey: FAST_API_KEY };
  }
  return { baseUrl: PRIMARY_BASE_URL, model: PRIMARY_MODEL, apiKey: PRIMARY_API_KEY };
}

function authorized(req) {
  if (!INBOUND_API_KEY) return true;
  return req.headers.authorization === `Bearer ${INBOUND_API_KEY}`;
}

function json(res, status, body, headers = {}) {
  const payload = Buffer.from(JSON.stringify(body));
  res.writeHead(status, {
    "content-type": "application/json",
    "content-length": String(payload.length),
    ...headers,
  });
  res.end(payload);
}

async function readJson(req) {
  const chunks = [];
  let bytes = 0;
  for await (const chunk of req) {
    bytes += chunk.length;
    if (bytes > 4 * 1024 * 1024) throw new Error("request body too large");
    chunks.push(chunk);
  }
  if (!chunks.length) return {};
  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

async function proxyChat(req, res) {
  let body;
  try {
    body = await readJson(req);
  } catch (error) {
    json(res, 400, { error: { message: error instanceof Error ? error.message : String(error) } });
    return;
  }

  const classification = classifyRequest(body);
  const upstream = upstreamFor(classification);
  const requestUpstream = async (target) => {
    const targetHeaders = { "content-type": "application/json" };
    if (target.apiKey) targetHeaders.authorization = `Bearer ${target.apiKey}`;
    return fetch(target.baseUrl + "/chat/completions", {
      method: "POST",
      headers: targetHeaders,
      body: JSON.stringify({ ...body, model: target.model }),
      signal: AbortSignal.timeout(600000),
    });
  };

  let selected = upstream;
  let upstreamResponse;
  try {
    upstreamResponse = await requestUpstream(selected);
    // The fast path is an optimisation, never a single point of failure. Network
    // failures and 5xx responses fall back to the primary model. 4xx responses are
    // preserved because retrying an invalid request against a larger model only hides
    // the real client error.
    if (classification.upstream === "fast" && upstreamResponse.status >= 500) {
      try { await upstreamResponse.body?.cancel(); } catch {}
      selected = upstreamFor({ upstream: "primary" });
      upstreamResponse = await requestUpstream(selected);
    }
  } catch (error) {
    if (classification.upstream === "fast") {
      try {
        selected = upstreamFor({ upstream: "primary" });
        upstreamResponse = await requestUpstream(selected);
      } catch (fallbackError) {
        json(res, 502, { error: { message: "MiaoDesk router upstream unavailable: " +
          (fallbackError instanceof Error ? fallbackError.message : String(fallbackError)) } }, {
          "x-miaodesk-route": classification.route + "-fallback-failed",
        });
        return;
      }
    } else {
      json(res, 502, { error: { message: "MiaoDesk router upstream unavailable: " +
        (error instanceof Error ? error.message : String(error)) } }, {
        "x-miaodesk-route": classification.route,
      });
      return;
    }
  }

  const responseHeaders = {};
  for (const name of ["content-type", "cache-control"]) {
    const value = upstreamResponse.headers.get(name);
    if (value) responseHeaders[name] = value;
  }
  const fellBack = classification.upstream === "fast" && selected.model !== upstream.model;
  responseHeaders["x-miaodesk-route"] = fellBack ? classification.route + "-fallback-primary" : classification.route;
  responseHeaders["x-miaodesk-upstream-model"] = selected.model;

  res.writeHead(upstreamResponse.status, responseHeaders);
  if (!upstreamResponse.body) {
    res.end();
    return;
  }

  const reader = upstreamResponse.body.getReader();
  req.once("close", () => reader.cancel().catch(() => {}));
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      if (!res.write(Buffer.from(value))) await new Promise(resolve => res.once("drain", resolve));
    }
  } finally {
    res.end();
  }
}

export function createRouterServer() {
  return http.createServer(async (req, res) => {
    if (!authorized(req)) {
      json(res, 401, { error: { message: "unauthorized" } });
      return;
    }

    const url = new URL(req.url || "/", "http://router.local");
    if (req.method === "GET" && (url.pathname === "/health" || url.pathname === "/v1/health")) {
      json(res, 200, {
        ok: true,
        publicModel: PUBLIC_MODEL,
        primary: PRIMARY_BASE_URL,
        fast: FAST_BASE_URL,
      });
      return;
    }

    if (req.method === "GET" && url.pathname === "/v1/models") {
      json(res, 200, {
        object: "list",
        data: [{ id: PUBLIC_MODEL, object: "model", owned_by: "miaodesk" }],
      });
      return;
    }

    if (req.method === "POST" && url.pathname === "/v1/chat/completions") {
      await proxyChat(req, res);
      return;
    }

    json(res, 404, { error: { message: "not found" } });
  });
}

export { classifyRequest, textOf };

if (import.meta.url === new URL(process.argv[1] || "", "file://").href) {
  const server = createRouterServer();
  server.listen(PORT, HOST, () => {
    console.log(`MiaoDesk L1 router listening on http://${HOST}:${PORT}/v1 model=${PUBLIC_MODEL}`);
  });
}
