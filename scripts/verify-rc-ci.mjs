#!/usr/bin/env node

const required = [
  "Windows x64 Build",
  "Windows x64 Package",
  "Windows x64 MSIX",
  "Repo Hygiene",
  "Windows ARM64 Package",
];

function evaluate(runs, sha) {
  const byName = new Map();
  for (const run of runs) {
    if (run.head_sha !== sha || run.status !== "completed") continue;
    const previous = byName.get(run.name);
    if (!previous || Number(run.run_attempt || 1) >= Number(previous.run_attempt || 1)) byName.set(run.name, run);
  }
  return required.map((name) => {
    const run = byName.get(name);
    return { name, ok: run?.conclusion === "success", conclusion: run?.conclusion || "missing", url: run?.html_url || null };
  });
}

if (process.argv.includes("--self-test")) {
  const sha = "abc";
  const sample = required.map((name, i) => ({ name, head_sha: sha, status: "completed", conclusion: "success", run_attempt: 1, html_url: `https://example/${i}` }));
  const good = evaluate(sample, sha);
  if (!good.every((x) => x.ok)) throw new Error("expected complete fixture to pass");
  sample[0].conclusion = "failure";
  if (evaluate(sample, sha)[0].ok) throw new Error("expected failed workflow to fail");
  if (evaluate(sample, "other").some((x) => x.ok)) throw new Error("must never reuse runs from another SHA");
  console.log("verify-rc-ci self-test passed");
  process.exit(0);
}

const sha = process.argv[2] || process.env.GITHUB_SHA;
if (!sha) {
  console.error("usage: node scripts/verify-rc-ci.mjs <commit-sha>");
  process.exit(2);
}
const repo = process.env.GITHUB_REPOSITORY || "GoodLoongStudio/MiaoDesk";
const token = process.env.GITHUB_TOKEN || "";
const headers = { accept: "application/vnd.github+json", ...(token ? { authorization: `Bearer ${token}` } : {}) };
let page = 1;
const runs = [];
while (page <= 10) {
  const url = `https://api.github.com/repos/${repo}/actions/runs?head_sha=${encodeURIComponent(sha)}&per_page=100&page=${page}`;
  const response = await fetch(url, { headers });
  if (!response.ok) throw new Error(`GitHub Actions query failed: ${response.status} ${await response.text()}`);
  const json = await response.json();
  runs.push(...(json.workflow_runs || []));
  if ((json.workflow_runs || []).length < 100) break;
  page += 1;
}

const results = evaluate(runs, sha);
console.log(JSON.stringify({ sha, required: results }, null, 2));
if (!results.every((x) => x.ok)) process.exit(1);
