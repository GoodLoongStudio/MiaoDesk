#!/usr/bin/env node

const required = [
  "Windows x64 Build",
  "Windows x64 Package",
  "Windows x64 MSIX",
  "Repo Hygiene",
  "Windows ARM64 Package",
];

function evaluate(runs, sha) {
  // 每种工作流取**该 SHA 上最新一次** run —— 不管它是什么状态。
  //
  // 上一版在这里就 `status !== "completed"` 直接跳过,于是"还在跑"和"没触发过"
  // 落进同一个桶,对外都报 missing。那时这道门只在手工 dispatch 下跑,所以看不出
  // 问题;要让它跟着 push 自动跑,三种结局就必须分开:
  //
  //   success → 这一条算过
  //   failure / cancelled / timed_out / … → 真失败,门该红
  //   还在跑 / 没触发 → **还没轮到它**,门还不能下结论
  //
  // 把"还没跑完"读成"失败"的门,会在每次推送上红一次(push 之后第一个跑完的工作流
  // 就触发它,另外四个还没影)。每次推送都红的门等于没有门,所以它单独一个 verdict。
  const latest = new Map();
  for (const run of runs) {
    if (run.head_sha !== sha) continue;
    const previous = latest.get(run.name);
    if (!previous || Number(run.run_attempt || 1) >= Number(previous.run_attempt || 1)) latest.set(run.name, run);
  }
  return required.map((name) => {
    const run = latest.get(name);
    if (!run) return { name, ok: false, pending: true, conclusion: "not-triggered", url: null };
    if (run.status !== "completed") {
      return { name, ok: false, pending: true, conclusion: run.status, url: run.html_url || null };
    }
    if (run.conclusion === "success") {
      return { name, ok: true, pending: false, conclusion: "success", url: run.html_url || null };
    }
    return { name, ok: false, pending: false, conclusion: run.conclusion || "unknown", url: run.html_url || null };
  });
}

// 退出码是这道门语义的一部分:
//   0 = 五条全是这个 SHA 的 success
//   1 = 至少一条真的失败了(skipped / cancelled 都算 —— REL-02 的原文就是
//       "skipped/cancelled 不算通过")
//   3 = 一条都没失败,但还有没跑完的:不是"没通过",是"还不能说"
// 分开的理由见 evaluate 上面那段。调用方必须自己挑怎么处理 3:
// rc-same-sha-gate.yml 的 workflow_run 那一支把它当 notice,手工 dispatch 那一支当失败。
function verdictOf(results) {
  const failed = results.filter((r) => !r.ok && !r.pending);
  const pending = results.filter((r) => r.pending);
  if (failed.length > 0) return { code: 1, failed, pending };
  if (pending.length > 0) return { code: 3, failed, pending };
  return { code: 0, failed: [], pending: [] };
}

// 反空洞自检。两个方向都要喂:
//   · 把"失败"读成"还没跑完"的门,失败永远不响;
//   · 把"还没跑完"读成"失败"的门,每次推送都红。
if (process.argv.includes("--self-test")) {
  const sha = "abc";
  const at = (i) => `https://example/${i}`;
  const done = (name, conclusion, i) =>
    ({ name, head_sha: sha, status: "completed", conclusion, run_attempt: 1, html_url: at(i) });
  const others = (skip, conclusion) =>
    required.filter((_, i) => i !== skip).map((n, i) => done(n, conclusion, i));

  // 1. 五条全 success → 0
  if (verdictOf(evaluate(required.map((n, i) => done(n, "success", i)), sha)).code !== 0) {
    throw new Error("expected complete fixture to pass");
  }

  // 2. 一条 failure → 1,而且不许因为它被算成 pending 而逃掉
  const broken = evaluate([done(required[0], "failure", 0), ...others(0, "success")], sha);
  if (verdictOf(broken).code !== 1) throw new Error("expected failed workflow to fail");
  if (verdictOf(broken).pending.length !== 0) throw new Error("a failure must not be reported as pending");

  // 3. skipped / cancelled 不算成功
  for (const notAPass of ["skipped", "cancelled", "timed_out", "action_required"]) {
    const r = evaluate([done(required[2], notAPass, 2), ...others(2, "success")], sha);
    if (verdictOf(r).code !== 1) throw new Error(`${notAPass} must count as a failure, not a pass`);
  }

  // 4. 别的工作流的 run 不能拼进来:"不同提交的通过结果不能拼接"
  const foreign = evaluate(required.map((n, i) => done(n, "success", i)), "other");
  if (foreign.some((x) => x.ok)) throw new Error("must never reuse runs from another SHA");
  if (verdictOf(foreign).code === 0) throw new Error("another SHA's green runs must never read as green here");

  // 5. 四条还在跑 → 3,不是 1(push 之后第一个跑完的工作流就会触发这道门)
  const inFlight = required.slice(1).map((n, i) =>
    ({ name: n, head_sha: sha, status: "in_progress", conclusion: null, run_attempt: 1, html_url: at(i) }));
  const running = evaluate([done(required[0], "success", 9), ...inFlight], sha);
  if (verdictOf(running).code !== 3) throw new Error("workflows still running must read as pending, not failure");
  if (running.filter((r) => r.pending).length !== 4) throw new Error("each in-flight workflow must be named as pending");

  // 6. 只给了一条 run,其余没触发 → 3;一条失败 + 其余没触发 → 1(失败优先,别被 pending 盖掉)
  if (verdictOf(evaluate([done(required[0], "success", 0)], sha)).code !== 3) {
    throw new Error("untriggered workflows must read as pending");
  }
  if (verdictOf(evaluate([done(required[0], "failure", 0)], sha)).code !== 1) {
    throw new Error("a real failure must outrank pending");
  }

  // 7. 重跑要用新 attempt 的结论:attempt 1 失败、attempt 2 成功 → 0
  const rerun = [
    { name: required[0], head_sha: sha, status: "completed", conclusion: "failure", run_attempt: 1, html_url: at(0) },
    { name: required[0], head_sha: sha, status: "completed", conclusion: "success", run_attempt: 2, html_url: at(1) },
    ...others(0, "success"),
  ];
  if (verdictOf(evaluate(rerun, sha)).code !== 0) throw new Error("the newest attempt must be the one judged");

  // 8. 反过来:attempt 1 成功、attempt 2 失败 → 1(不能拿旧的那次蒙过去)
  const regressed = [
    { name: required[0], head_sha: sha, status: "completed", conclusion: "success", run_attempt: 1, html_url: at(0) },
    { name: required[0], head_sha: sha, status: "completed", conclusion: "failure", run_attempt: 2, html_url: at(1) },
    ...others(0, "success"),
  ];
  if (verdictOf(evaluate(regressed, sha)).code !== 1) throw new Error("a regressed rerun must not be judged by the old attempt");

  // 9. 同一 attempt 里两条同名的 run(理论上不会,但别让后来者悄悄覆盖)
  const dupes = [
    { name: required[0], head_sha: sha, status: "completed", conclusion: "success", run_attempt: 1, html_url: at(0) },
    { name: required[0], head_sha: sha, status: "completed", conclusion: "failure", run_attempt: 1, html_url: at(1) },
    ...others(0, "success"),
  ];
  if (verdictOf(evaluate(dupes, sha)).code === 0) throw new Error("same-attempt duplicates must not silently read as green");

  console.log("verify-rc-ci self-test passed (in-process)");

  // ---- 退出码只能在**真的进程**上验 ----
  // 上面全是进程内断言,碰不到 `process.exit`,于是"退出码恒 0"这种变异在本文件里
  // 是活的(实测确实活了两个)。这里用 --runs-file 把三种 fixture 喂回自己,
  // 在子进程上看真实的退出码。REL-02 的验收就是按退出码判的,所以它不能不测。
  const nodeExe = process.execPath;
  const self = process.argv[1];
  const fsp = await import("node:fs/promises");
  const fsm = await import("node:fs");
  const pathm = await import("node:path");
  const osm = await import("node:os");
  const { spawnSync } = await import("node:child_process");
  const tmp = await fsp.mkdtemp(pathm.join(osm.tmpdir(), "vrc-"));
  let fixtureSeq = 0;
  const write = (runs) => {
    const p = pathm.join(tmp, `f${fixtureSeq++}.json`);
    fsm.writeFileSync(p, JSON.stringify(runs));
    return p;
  };
  const codeOf = (runs) => {
    const r = spawnSync(nodeExe, [self, "abc", "--runs-file", write(runs)], { encoding: "utf8" });
    return { code: r.status, out: `${r.stdout}${r.stderr}` };
  };

  // 五条全 success → 0
  let r = codeOf(required.map((n, i) => done(n, "success", i)));
  if (r.code !== 0) throw new Error(`exit 0 expected, got ${r.code}\n${r.out}`);

  // 一条 failure → 1
  r = codeOf([done(required[0], "failure", 0), ...others(0, "success")]);
  if (r.code !== 1) throw new Error(`exit 1 expected, got ${r.code}\n${r.out}`);

  // 四条还在跑 → 3(不是 0:那会把"还不能说"读成"已经过了")
  r = codeOf([
    done(required[0], "success", 9),
    ...required.slice(1).map((n, i) => ({ name: n, head_sha: sha, status: "in_progress", conclusion: null, run_attempt: 1, html_url: at(i) })),
  ]);
  if (r.code !== 3) throw new Error(`exit 3 expected, got ${r.code}\n${r.out}`);

  // 别的 SHA → 一条都不算过,所以是 3(没触发),不是 0
  const foreignSha = "zzz";
  r = codeOf(required.map((n, i) => ({ ...done(n, "success", i), head_sha: foreignSha })));
  if (r.code !== 3) throw new Error(`exit 3 expected for a foreign SHA, got ${r.code}\n${r.out}`);
  if (!r.out.includes("not-triggered")) throw new Error(`a foreign SHA must read as not-triggered:\n${r.out}`);

  // 不给 --runs-file 的路径 → 2
  r = spawnSync(nodeExe, [self, "abc", "--runs-file"], { encoding: "utf8" });
  if (r.status !== 2) throw new Error(`exit 2 expected for a missing --runs-file path, got ${r.status}`);

  console.log("verify-rc-ci self-test passed (exit codes)");
  process.exit(0);
}

const sha = process.argv[2] || process.env.GITHUB_SHA;
if (!sha) {
  console.error("usage: node scripts/verify-rc-ci.mjs <commit-sha>");
  process.exit(2);
}

// 离线复核:run 清单可以从文件读,不必每次打 API。
//
// REL-02 要求"保留 run 链接与产物身份",而只存链接的话,回头想再核一次就得重新
// 上网、还可能撞匿名配额(--self-test 在 CI 上有 token,开发者本机没有)。
// 把当时的 run 清单存下来,`--runs-file` 就能离线重放同一个结论。
// 它还有一个更实在的用处:退出码只能在一个**真的进程**上验,而 --self-test
// 走的是进程内断言,碰不到 process.exit。少了这个缝,"退出码恒 0"这种变异
// 在本文件里是活的(实测确实活了两个)。
const runsFileIndex = process.argv.indexOf("--runs-file");
const runsFile = runsFileIndex >= 0 ? process.argv[runsFileIndex + 1] : "";
if (runsFileIndex >= 0 && !runsFile) {
  console.error("--runs-file needs a path");
  process.exit(2);
}
const fs = runsFile ? await import("node:fs/promises") : null;

const repo = process.env.GITHUB_REPOSITORY || "GoodLoongStudio/MiaoDesk";
const token = process.env.GITHUB_TOKEN || "";
const headers = { accept: "application/vnd.github+json", ...(token ? { authorization: `Bearer ${token}` } : {}) };
const runs = [];
if (runsFile) {
  const saved = JSON.parse(await fs.readFile(runsFile, "utf8"));
  runs.push(...(Array.isArray(saved) ? saved : saved.workflow_runs || []));
  console.log(`run 清单来自文件:${runsFile}(${runs.length} 条) —— 没有访问 GitHub API`);
} else {
  for (let page = 1; page <= 10; ++page) {
    const url = `https://api.github.com/repos/${repo}/actions/runs?head_sha=${encodeURIComponent(sha)}&per_page=100&page=${page}`;
    const response = await fetch(url, { headers });
    if (!response.ok) throw new Error(`GitHub Actions query failed: ${response.status} ${await response.text()}`);
    const json = await response.json();
    runs.push(...(json.workflow_runs || []));
    if ((json.workflow_runs || []).length < 100) break;
  }
}

const results = evaluate(runs, sha);
const verdict = verdictOf(results);
console.log(JSON.stringify({ sha, verdict: { code: verdict.code }, required: results }, null, 2));
if (verdict.code !== 0) {
  for (const r of verdict.failed) console.error(`FAILED  ${r.name}: ${r.conclusion}  ${r.url || ""}`);
  for (const r of verdict.pending) console.error(`PENDING ${r.name}: ${r.conclusion}  ${r.url || ""}`);
}
process.exit(verdict.code);
