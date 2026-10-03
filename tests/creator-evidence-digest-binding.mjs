import fs from "node:fs";
import assert from "node:assert/strict";

// D-1:取证样本的摘要绑定,**宿主侧**那一半。
//
// 为什么需要这一道:MakeRenderedEvidenceSample 让"帧属于哪个候选"在 C++ 里只能由
// 一个参数决定,而那道保障只在宿主真的调它时成立。src/app/main.cpp 在 macOS 上
// 编译不了(它是 Windows 入口),所以这个调用点此前没有任何本地门看得见 ——
// 原来的缺陷正是在这里:另声明了一个 `std::string digest;` 从没赋值,每帧都带空摘要,
// `creator_preview_evidence` 在真机上一次都没成功过,而单元测试全绿、工具自述"可用"。
//
// 这道门不做语义分析,只钉住三个可读的事实:
//   1. CollectEvidence 确实经过 MakeRenderedEvidenceSample 构造样本;
//   2. 交给它的摘要就是 ComputeCandidateDigest 算出来、并且查过 UsableAsIdentity 的那一个;
//   3. 宿主里不再有第二处 `sample.digest = ...` 绑定路径。
// 三条同时成立时,摘要不可能在调用点写错;少任何一条,上面那种回退就能悄悄回来。

const read = (path) => fs.readFileSync(path, "utf8");
const main = read(process.env.MIAODESK_MAIN_CPP ?? "src/app/main.cpp");

const signature =
  "bool CollectEvidence(const miaodesk::creator::RenderEvidenceRequirements& requirements,";
const start = main.indexOf(signature);
assert.notStrictEqual(start, -1, "CollectEvidence must exist in src/app/main.cpp");

// Brace-match so an inner block closing early cannot truncate the region under test.
let depth = 0;
let end = -1;
for (let i = main.indexOf("{", start); i < main.length; i += 1) {
  if (main[i] === "{") depth += 1;
  else if (main[i] === "}") {
    depth -= 1;
    if (depth === 0) {
      end = i + 1;
      break;
    }
  }
}
assert.notStrictEqual(end, -1, "CollectEvidence body must close");
const body = main.slice(start, end);

// 1. 样本必须来自那一个构造点。
assert.ok(
  body.includes("MakeRenderedEvidenceSample("),
  "CollectEvidence must build every evidence sample through MakeRenderedEvidenceSample; " +
    "a host that assigns the fields itself can bind the wrong candidate summary again.",
);

// 2. 摘要必须是宿主对当前工作区算出来的那一个:同一个变量,既查可用性,又交给构造点。
const digestVar = /const\s+auto\s+computed\s*=\s*[A-Za-z_:]*ComputeCandidateDigest\(/;
assert.ok(
  digestVar.test(body),
  "CollectEvidence must compute the candidate summary from the workspace itself.",
);
assert.ok(
  /computed\.UsableAsIdentity\(\)/.test(body),
  "The computed summary must be checked for usability before it is used as identity.",
);
assert.ok(
  /MakeRenderedEvidenceSample\([\s\S]*?computed\.value/.test(body),
  "The summary handed to MakeRenderedEvidenceSample must be computed.value -- the same " +
    "one that was just checked. A separate local (or an empty string) is the D-1 bug.",
);

// 3. 不能出现第二条绑定路径。
assert.ok(
  !/\.digest\s*=/.test(body.replace(/MakeRenderedEvidenceSample\([^;]*\)/g, "")),
  "CollectEvidence must not assign sample.digest anywhere else; the binding lives in " +
    "exactly one place so it cannot be half-updated.",
);

console.log("证据取证:宿主经同一构造点绑定候选摘要");
