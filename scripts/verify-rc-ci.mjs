#!/usr/bin/env node

const required = [
  "Windows x64 Build",
  "Windows x64 Package",
  "Windows x64 MSIX",
  "Repo Hygiene",
  "Windows ARM64 Package",
];

// 4 = 这道门自己坏了(网络/认证/API),**不是**发布链的结论。
//
// 上一版在这里 `throw new Error(...)`:一个未捕获的顶层 throw 在 ESM 里退出码是 1,
// 而 1 已经被用作"五条链里有一条真的失败了"。两者混在一起,一次 API 抖动就会把
// "我不知道"显示成"发布是坏的" —— 那正是这道门存在的理由的反面。
// 实测在 3ffa2eea 上,同一道门的五次跑里有两次以 exit 1 结束,而那个 SHA 的
// 五条链最终**全是 success**(见开发面板 2026-10-04 再续十五)。日志拿不到
// (匿名 API 只能读 annotations,里面只有"Process completed with exit code 1"),
// 所以哪一次是 API 抖动无法逐条证实;但"退出码把两种完全不同的情况压成同一个"
// 这件事本身是确定的,与原因无关。
const INFRA = 4;

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

  // ---- 退出码 4:门自己坏了,不是发布链的结论 ----
  // 这一条只能在一个**真的 API** 上验,所以本机起一个假 API(GITHUB_API_URL 指过去)。
  // 上一版这里 `throw new Error(...)`,而未捕获的顶层 throw 退出码是 1 —— 与
  // "五条链里有一条真的失败了"混在一起。一次 API 抖动于是把"我不知道"显示成
  // "发布是坏的"。4 就是要把这两种情况分开。
  const http = await import("node:http");
  const { spawn } = await import("node:child_process");
  const goodFixture = required.map((n, i) => done(n, "success", i));
  const startFakeApi = (handler) =>
    new Promise((resolve) => {
      // 不把 handler 传给 createServer:那样它会被调两次。
      const server = http.createServer();
      // 响应一律带 Connection: close。undici 的 fetch 默认保持连接,而子进程
      // 会因为这个 socket 不关而不退出 —— 于是 spawnSync 永久挂住,整道自检卡死
      // (第一版就是这样挂的:退出码没验到,先把门自己卡成了不动的那一种)。
      server.on("request", (req, res) => {
        res.setHeader("connection", "close");
        handler(req, res);
      });
      server.listen(0, "127.0.0.1", () => resolve({ server, port: server.address().port }));
    });
  // 必须用**异步** spawn,不能用 spawnSync。
  //
  // 第一版用的是 spawnSync,结果整道自检挂死:spawnSync 会阻塞父进程的事件循环,
  // 而假 API 就长在这个事件循环上 —— 子进程的 fetch 等一个父进程永远发不出的响应,
  // 父进程等一个永远不会退出的子进程。这就是一次纯粹的自我死锁,症状是"自检没输出、
  // 一直不动",与一道被掏空的门在日志上看起来一模一样。
  //
  // 另外:超时之后必须真的杀掉子进程并报出来。自检不许挂 —— 一次卡死会让后面每一条
  // 断言都跑不到,而"没跑"与"通过"在日志上是一样的。
  const probe = async (handler, timeoutMs = 25000) => {
    const { server, port } = await startFakeApi(handler);
    let stdout = "";
    let stderr = "";
    let timer;
    try {
      const code = await new Promise((resolve, reject) => {
        const child = spawn(nodeExe, [self, "abc"], {
          env: { ...process.env, GITHUB_API_URL: `http://127.0.0.1:${port}`, GITHUB_TOKEN: "" },
          stdio: ["ignore", "pipe", "pipe"],
        });
        child.stdout.on("data", (d) => { stdout += d; });
        child.stderr.on("data", (d) => { stderr += d; });
        child.on("error", reject);
        child.on("exit", (c, signal) => resolve(signal ? `signal:${signal}` : c));
        timer = setTimeout(() => {
          child.kill("SIGKILL");
          reject(new Error(`假 API 上的探针 ${timeoutMs}ms 没退出(子进程已杀) —— 自检自己挂了`));
        }, timeoutMs);
      });
      return { code, out: stdout + stderr };
    } finally {
      clearTimeout(timer);
      server.closeAllConnections?.();
      server.close();
    }
  };
  const jsonReply = (body, status = 200) => (req, res) => {
    res.writeHead(status, { "content-type": "application/json", connection: "close" });
    res.end(JSON.stringify(body));
  };

  // 4-pre. INFRA 必须是独一份的退出码。
  // 它存在的全部理由就是与 1("五条链里有一条真的失败了")分开;撞车就等于没分开。
  for (const taken of [0, 1, 2, 3]) {
    if (INFRA === taken) throw new Error(`INFRA must be distinct from ${taken} -- that is its whole point`);
  }

  // 4a. API 一路 500 → 4,而且要说明门自己坏
  let p = await probe(jsonReply({ message: "boom" }, 500));
  if (p.code !== INFRA) throw new Error(`exit ${INFRA} expected for a broken API, got ${p.code}\n${p.out}`);
  if (!/门自己坏|不是发布链/.test(p.out)) throw new Error(`a broken API must say so:\n${p.out}`);

  // 4a-2. 第一次失败、第二次成功 → 0,不是 4。
  //       上一版如果不在成功时清掉 lastError,一次重试后的成功会被报成门自己坏 ——
  //       "明明拿到了清单,却说不算数"。这一条钉住那次清理。
  let failures = 0;
  p = await probe((_req, res) => {
    res.setHeader("connection", "close");
    if (failures++ === 0) {
      res.writeHead(500, { "content-type": "application/json", connection: "close" });
      res.end(JSON.stringify({ message: "transient" }));
      return;
    }
    res.writeHead(200, { "content-type": "application/json", connection: "close" });
    res.end(JSON.stringify({ total_count: goodFixture.length, workflow_runs: goodFixture }));
  });
  if (p.code !== 0) throw new Error(`exit 0 expected when a retry succeeds, got ${p.code}\n${p.out}`);

  // 4a-3. 重试要真的退避:三次零间隔的重试等于没有退避,只会把已经不稳的 API 打得更稳不住。
  const retryStarted = Date.now();
  p = await probe(jsonReply({ message: "boom" }, 500));
  const retryElapsed = Date.now() - retryStarted;
  if (p.code !== INFRA) throw new Error(`exit ${INFRA} expected for a broken API, got ${p.code}\n${p.out}`);
  if (retryElapsed < 2500) throw new Error(`retries must back off (took ${retryElapsed}ms)`);

  // 4b. 403(认证问题)→ 4,且不该重试三次(重试认证失败没有意义,只会拖慢每一次红)
  const authStarted = Date.now();
  p = await probe(jsonReply({ message: "Must have admin rights to Repository." }, 403));
  if (p.code !== INFRA) throw new Error(`exit ${INFRA} expected for a 403, got ${p.code}\n${p.out}`);
  if (Date.now() - authStarted > 4000) throw new Error("a 403 must not be retried three times");

  // 4c. 查到这个 SHA 一条 run 都没有 → 4,不当"五条都没触发"
  //     (后者是一个发布结论,而空清单更可能是问错了:HEAD SHA 不完整、权限不对…)
  p = await probe(jsonReply({ total_count: 0, workflow_runs: [] }));
  if (p.code !== INFRA) throw new Error(`exit ${INFRA} expected for an empty run list, got ${p.code}\n${p.out}`);
  if (!/一条 run 都没查到|查询很可能坏/.test(p.out)) throw new Error(`an empty run list must say so:\n${p.out}`);

  // 4d. 假 API 上五条全绿 → 0。这一条证明上面几条红的是 API,不是接线。
  p = await probe(jsonReply({ total_count: goodFixture.length, workflow_runs: goodFixture }));
  if (p.code !== 0) throw new Error(`exit 0 expected against a healthy fake API, got ${p.code}\n${p.out}`);

  // 4e. 假 API 上五条里一条在飞 → 3(不是 4:能查到东西就不算门坏了)
  p = await probe(jsonReply({
    total_count: 5,
    workflow_runs: [
      done(required[0], "success", 0),
      ...required.slice(1).map((n, i) => ({ name: n, head_sha: "abc", status: "in_progress", conclusion: null, run_attempt: 1, html_url: at(i) })),
    ],
  }));
  if (p.code !== 3) throw new Error(`exit 3 expected when the fake API shows in-flight runs, got ${p.code}\n${p.out}`);

  console.log("verify-rc-ci self-test passed (exit 4: the gate itself broke)");
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

// GitHub Actions 自己就设这个变量(GHES 上它不是 api.github.com)。尊重它既让这道门
// 在 GHES 上能跑,也让自检能在本机起一个假 API —— 否则"退出码 4"这条新路只能靠猜。
const apiBase = process.env.GITHUB_API_URL || "https://api.github.com";

const repo = process.env.GITHUB_REPOSITORY || "GoodLoongStudio/MiaoDesk";
const token = process.env.GITHUB_TOKEN || "";
const headers = { accept: "application/vnd.github+json", ...(token ? { authorization: `Bearer ${token}` } : {}) };
const runs = [];
if (runsFile) {
  const saved = JSON.parse(await fs.readFile(runsFile, "utf8"));
  runs.push(...(Array.isArray(saved) ? saved : saved.workflow_runs || []));
  console.log(`run 清单来自文件:${runsFile}(${runs.length} 条) —— 没有访问 GitHub API`);
} else {
  // 瞬时失败重试:5xx / 429 / 网络错误都不该换来一个红。一次抖动让发布看起来是坏的,
  // 比晚两分钟知道贵得多。
  let lastError = "";
  outer: for (let attempt = 1; attempt <= 3; ++attempt) {
    for (let page = 1; page <= 10; ++page) {
      const url = `${apiBase}/repos/${repo}/actions/runs?head_sha=${encodeURIComponent(sha)}&per_page=100&page=${page}`;
      let response;
      try {
        response = await fetch(url, { headers });
      } catch (error) {
        lastError = `attempt ${attempt}: ${error}`;
        await new Promise((resolve) => setTimeout(resolve, 1000 * attempt));
        continue outer;
      }
      if (!response.ok) {
        lastError = `attempt ${attempt}: HTTP ${response.status} ${(await response.text()).slice(0, 200)}`;
        // 403 / 401 是认证问题,重试三次也还是一样;直接当门自己坏。
        if (response.status === 401 || response.status === 403) break outer;
        await new Promise((resolve) => setTimeout(resolve, 1000 * attempt));
        continue outer;
      }
      const json = await response.json();
      runs.push(...(json.workflow_runs || []));
      if ((json.workflow_runs || []).length < 100) break;
    }
    lastError = "";
    break;
  }
  if (lastError) {
    console.error(`这道门自己坏了(GitHub Actions 查询失败),不是发布链的结论:${lastError}`);
    console.error("重试三次仍拿不到 run 清单;此时既不能说通过也不能说失败。");
    process.exit(INFRA);
  }
  if (runs.length === 0) {
    // 一个刚刚有工作流跑完的 SHA,不可能一条 run 都没有。空清单一律当查询坏了,
    // 不当"五条都没触发" —— 后者是一个发布结论,而空清单更可能是问错了。
    console.error(`这个 SHA 上一条 run 都没查到:${sha}`);
    console.error("查询很可能坏了(HEAD SHA 不完整、或仓库/权限不对);不据此下发布结论。");
    process.exit(INFRA);
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
