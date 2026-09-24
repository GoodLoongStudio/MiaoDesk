import assert from "node:assert/strict";
import http from "node:http";
import { once } from "node:events";
import { pathToFileURL } from "node:url";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

function backend(label) {
  const requests = [];
  const server = http.createServer(async (req, res) => {
    let raw = "";
    for await (const chunk of req) raw += chunk;
    const body = raw ? JSON.parse(raw) : {};
    requests.push({ url: req.url, body, authorization: req.headers.authorization || "" });
    if (body.stream) {
      res.writeHead(200, { "content-type": "text/event-stream" });
      res.write(`data: {"label":"${label}","model":"${body.model}"}\n\n`);
      res.end("data: [DONE]\n\n");
      return;
    }
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({
      id: "chatcmpl-test",
      object: "chat.completion",
      model: body.model,
      choices: [{ index: 0, message: { role: "assistant", content: label }, finish_reason: "stop" }],
    }));
  });
  return { server, requests };
}

async function listen(server) {
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  return server.address().port;
}

async function post(base, body) {
  const response = await fetch(base + "/v1/chat/completions", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  return { response, text: await response.text() };
}

const primary = backend("primary");
const fast = backend("fast");
const primaryPort = await listen(primary.server);
const fastPort = await listen(fast.server);

process.env.MIAODESK_PRIMARY_BASE_URL = `http://127.0.0.1:${primaryPort}/v1`;
process.env.MIAODESK_FAST_BASE_URL = `http://127.0.0.1:${fastPort}/v1`;
process.env.MIAODESK_PRIMARY_MODEL = "primary-model";
process.env.MIAODESK_FAST_MODEL = "fast-model";
process.env.MIAODESK_ROUTER_PUBLIC_MODEL = "miaodesk";

const here = dirname(fileURLToPath(import.meta.url));
const modulePath = join(here, "..", "runtime", "router", "server.mjs");
const { createRouterServer, classifyRequest } =
  await import(pathToFileURL(modulePath).href + `?t=${Date.now()}`);

assert.equal(classifyRequest({
  messages: [{ role: "user", content: "hello" }],
  tools: [{ type: "function", function: { name: "x" } }],
}).route, "primary-tools");

assert.equal(classifyRequest({
  messages: [
    { role: "system", content: "---\nname: wallpaper-content\n---" },
    { role: "user", content: "make one" },
  ],
}).route, "primary-skill");

assert.equal(classifyRequest({
  messages: [{ role: "user", content: "hello" }],
}).route, "fast-short");

assert.equal(classifyRequest({
  messages: [{ role: "user", content: "x".repeat(1000) }],
}).route, "primary-chat");

const router = createRouterServer();
const routerPort = await listen(router);
const base = `http://127.0.0.1:${routerPort}`;

try {
  let out = await post(base, {
    model: "miaodesk",
    messages: [{ role: "user", content: "hi" }],
  });
  assert.equal(out.response.status, 200);
  assert.equal(out.response.headers.get("x-miaodesk-route"), "fast-short");
  assert.equal(out.response.headers.get("x-miaodesk-upstream-model"), "fast-model");
  assert.equal(JSON.parse(out.text).choices[0].message.content, "fast");
  assert.equal(fast.requests.at(-1).body.model, "fast-model");

  out = await post(base, {
    model: "miaodesk",
    messages: [{ role: "user", content: "use a tool" }],
    tools: [{ type: "function", function: { name: "content_skill_get", parameters: {} } }],
  });
  assert.equal(out.response.headers.get("x-miaodesk-route"), "primary-tools");
  assert.equal(primary.requests.at(-1).body.model, "primary-model");

  out = await post(base, {
    model: "miaodesk",
    messages: [
      { role: "system", content: "name: content-review" },
      { role: "user", content: "review" },
    ],
  });
  assert.equal(out.response.headers.get("x-miaodesk-route"), "primary-skill");

  out = await post(base, {
    model: "miaodesk",
    messages: [{ role: "user", content: "long ".repeat(250) }],
  });
  assert.equal(out.response.headers.get("x-miaodesk-route"), "primary-chat");

  out = await post(base, {
    model: "miaodesk",
    stream: true,
    messages: [{ role: "user", content: "stream" }],
  });
  assert.equal(out.response.headers.get("x-miaodesk-route"), "fast-short");
  assert.match(out.text, /data: .*fast.*fast-model/);
  assert.match(out.text, /data: \[DONE\]/);

  const models = await fetch(base + "/v1/models");
  const modelsJson = await models.json();
  assert.deepEqual(modelsJson.data.map(x => x.id), ["miaodesk"]);

  const health = await fetch(base + "/health");
  assert.equal((await health.json()).ok, true);

  assert.equal(primary.requests.length, 3);
  assert.equal(fast.requests.length, 2);
  console.log("ALL CHECKS PASSED: L1 router classification, model rewrite and streaming proxy");
} finally {
  router.close();
  primary.server.close();
  fast.server.close();
}
