#!/usr/bin/env node

const endpoints = ["A", "B"].map((label) => ({
  label,
  baseUrl: process.env[`MIAODESK_EVAL_${label}_BASE_URL`],
  model: process.env[`MIAODESK_EVAL_${label}_MODEL`],
  apiKey: process.env[`MIAODESK_EVAL_${label}_API_KEY`] || "",
}));

for (const candidate of endpoints) {
  if (!candidate.baseUrl || !candidate.model) {
    console.error(`Missing MIAODESK_EVAL_${candidate.label}_BASE_URL or MIAODESK_EVAL_${candidate.label}_MODEL`);
    process.exit(2);
  }
}

const runs = Math.max(1, Number.parseInt(process.env.MIAODESK_EVAL_RUNS || "3", 10));
const timeoutMs = Math.max(1000, Number.parseInt(process.env.MIAODESK_EVAL_TIMEOUT_MS || "120000", 10));

const tool = {
  type: "function",
  function: {
    name: "content_skill_get",
    description: "Read a MiaoDesk content skill.",
    parameters: { type: "object", properties: { kind: { type: "string", enum: ["wallpaper", "widget"] } }, required: ["kind"] },
  },
};

const cases = [
  {
    id: "short-chat",
    body: { messages: [{ role: "user", content: "Reply with exactly: MiaoDesk ready" }], temperature: 0 },
    check: (json) => textOf(json).trim() === "MiaoDesk ready",
  },
  {
    id: "tool-selection",
    body: { messages: [{ role: "user", content: "Use content_skill_get to read the wallpaper skill before answering." }], tools: [tool], tool_choice: "required", temperature: 0 },
    check: (json) => toolCallsOf(json).some((call) => call?.function?.name === "content_skill_get"),
  },
  {
    id: "structured-json",
    body: { messages: [{ role: "user", content: "Return only JSON with keys kind and count, where kind is widget and count is 3." }], temperature: 0 },
    check: (json) => {
      try {
        const parsed = JSON.parse(stripFence(textOf(json)));
        return parsed.kind === "widget" && parsed.count === 3;
      } catch { return false; }
    },
  },
];

function textOf(json) {
  const content = json?.choices?.[0]?.message?.content;
  if (typeof content === "string") return content;
  if (Array.isArray(content)) return content.map((part) => part?.text || "").join("");
  return "";
}
function toolCallsOf(json) { return json?.choices?.[0]?.message?.tool_calls || []; }
function stripFence(value) { return value.trim().replace(/^```(?:json)?\s*/i, "").replace(/\s*```$/, ""); }
function percentile(values, p) {
  if (!values.length) return null;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.ceil(p * sorted.length) - 1)];
}

async function invoke(candidate, testCase) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  const started = performance.now();
  try {
    const response = await fetch(`${candidate.baseUrl.replace(/\/$/, "")}/chat/completions`, {
      method: "POST",
      headers: {
        "content-type": "application/json",
        ...(candidate.apiKey ? { authorization: `Bearer ${candidate.apiKey}` } : {}),
      },
      body: JSON.stringify({ model: candidate.model, ...testCase.body }),
      signal: controller.signal,
    });
    const latencyMs = Math.round(performance.now() - started);
    const raw = await response.text();
    let json = null;
    try { json = JSON.parse(raw); } catch {}
    return {
      ok: response.ok && json !== null && testCase.check(json),
      httpStatus: response.status,
      latencyMs,
      usage: json?.usage || null,
      error: response.ok ? (json === null ? "non-json response" : null) : raw.slice(0, 500),
    };
  } catch (error) {
    return { ok: false, httpStatus: null, latencyMs: Math.round(performance.now() - started), usage: null, error: String(error?.message || error) };
  } finally { clearTimeout(timer); }
}

const report = { schema: 1, generatedAt: new Date().toISOString(), runsPerCase: runs, candidates: [] };
for (const candidate of endpoints) {
  const result = { label: candidate.label, baseUrl: candidate.baseUrl, model: candidate.model, cases: [] };
  for (const testCase of cases) {
    const attempts = [];
    for (let i = 0; i < runs; i += 1) attempts.push(await invoke(candidate, testCase));
    const latencies = attempts.filter((x) => x.httpStatus !== null).map((x) => x.latencyMs);
    result.cases.push({
      id: testCase.id,
      passed: attempts.filter((x) => x.ok).length,
      total: attempts.length,
      passRate: attempts.filter((x) => x.ok).length / attempts.length,
      p50LatencyMs: percentile(latencies, 0.5),
      p95LatencyMs: percentile(latencies, 0.95),
      attempts,
    });
  }
  result.passRate = result.cases.reduce((sum, item) => sum + item.passed, 0) / result.cases.reduce((sum, item) => sum + item.total, 0);
  report.candidates.push(result);
}

console.log(JSON.stringify(report, null, 2));
if (report.candidates.some((candidate) => candidate.passRate < 1)) process.exitCode = 1;
