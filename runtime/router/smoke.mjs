import { performance } from "node:perf_hooks";

const BASE_URL = (process.env.MIAODESK_ROUTER_BASE_URL || "http://127.0.0.1:8000/v1").replace(/\/+$/, "");
const API_KEY = process.env.MIAODESK_ROUTER_API_KEY || "";
const RUNS = Math.max(1, Number(process.env.MIAODESK_ROUTER_SMOKE_RUNS || 3));

const headers = { "content-type": "application/json" };
if (API_KEY) headers.authorization = `Bearer ${API_KEY}`;

async function request(label, body) {
  const started = performance.now();
  const response = await fetch(BASE_URL + "/chat/completions", {
    method: "POST",
    headers,
    body: JSON.stringify({ model: "miaodesk", stream: false, ...body }),
    signal: AbortSignal.timeout(120000),
  });
  const elapsedMs = performance.now() - started;
  const text = await response.text();
  if (!response.ok) throw new Error(`${label}: HTTP ${response.status}: ${text.slice(0, 500)}`);
  return {
    label,
    elapsedMs,
    route: response.headers.get("x-miaodesk-route") || "",
    upstreamModel: response.headers.get("x-miaodesk-upstream-model") || "",
  };
}

function percentile(values, p) {
  if (!values.length) return 0;
  const sorted = [...values].sort((a,b)=>a-b);
  const index = Math.min(sorted.length - 1, Math.max(0, Math.ceil((p / 100) * sorted.length) - 1));
  return sorted[index];
}

const cases = [
  {
    label: "short",
    expectedPrefix: "fast-short",
    body: { messages: [{ role: "user", content: "Reply with exactly: OK" }] },
  },
  {
    label: "tools",
    expectedPrefix: "primary-tools",
    body: {
      messages: [{ role: "user", content: "Reply with exactly: OK. Do not call the tool." }],
      tools: [{
        type: "function",
        function: {
          name: "noop_probe",
          description: "Smoke-test tool that should not be called.",
          parameters: { type: "object", properties: {} },
        },
      }],
    },
  },
  {
    label: "skill",
    expectedPrefix: "primary-skill",
    body: {
      messages: [
        { role: "system", content: "name: content-review\nThis is a routing smoke signature only." },
        { role: "user", content: "Reply with exactly: OK" },
      ],
    },
  },
  {
    label: "long",
    expectedPrefix: "primary-chat",
    body: {
      messages: [{ role: "user", content: "Context: " + "x".repeat(1000) + "\nReply with exactly: OK" }],
    },
  },
];

const samples = [];
for (let n = 0; n < RUNS; ++n) {
  for (const testCase of cases) {
    const result = await request(testCase.label, testCase.body);
    if (!result.route.startsWith(testCase.expectedPrefix)) {
      throw new Error(`${testCase.label}: expected route ${testCase.expectedPrefix}, got ${result.route || "<missing>"}`);
    }
    samples.push(result);
    console.log(`${testCase.label}: ${result.elapsedMs.toFixed(1)} ms route=${result.route} model=${result.upstreamModel}`);
  }
}

const metricsResponse = await fetch(BASE_URL.replace(/\/v1$/, "") + "/metrics", {
  headers: API_KEY ? { authorization: `Bearer ${API_KEY}` } : {},
  signal: AbortSignal.timeout(10000),
});
const metrics = metricsResponse.ok ? await metricsResponse.json() : null;

const byCase = {};
for (const testCase of cases) {
  const values = samples.filter(x => x.label === testCase.label).map(x => x.elapsedMs);
  byCase[testCase.label] = {
    count: values.length,
    p50Ms: Number(percentile(values, 50).toFixed(1)),
    p95Ms: Number(percentile(values, 95).toFixed(1)),
    maxMs: Number(Math.max(...values).toFixed(1)),
  };
}

const report = {
  generatedAt: new Date().toISOString(),
  baseUrl: BASE_URL,
  runsPerCase: RUNS,
  cases: byCase,
  routes: samples.map(x => ({ label: x.label, route: x.route, upstreamModel: x.upstreamModel })),
  routerMetrics: metrics,
};
console.log("\n" + JSON.stringify(report, null, 2));
