# Changelog

MiaoDesk 所有显著变更均记录于此文件。

格式遵循 [Keep a Changelog 1.1.0](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本 2.0.0](https://semver.org/lang/zh-CN/)。

**版本锚点**：正式交付只进入 `main`。版本号在 x64 / ARM64 两条 CI 链验证通过后提升。

> **历史说明**
> `66a1371 security: clean repository root`（2026-09-09）重建了 Git 历史，清理前的 2667 个提交不再从 `main` 可达。
> 本文件 `[0.1.0]` 条目依据清理前的历史与设计文档整理；标签 `v0.1.0` 目前仍指向清理前历史线上的 `94ab91a`，需要在 `main` 上重打。
> `[0.1.3]` 及以后的条目与 `main` 上的提交一一对应。

---

## [未发布]

> 当前内容以 `main` 为准。RC 版本号尚未提升；最终 version bump 只在 Issue #60 的真机验收、参考机性能基线和同一 SHA 发布链验证完成后执行。

### 2026-10-04 · 执行记录：P0-08 剩下那一半的诊断（单实例锁不可恢复）

P0-08 验收原话是"无永久 Node/WebView2/Wallpaper/Harness 孤儿，**无不可恢复单实例锁**"。
本轮做完前半句,后半句诊断完成、修复需要产品决策,记录如下。

**结构**：全仓 5 处 `CreateMutexW`,而"该不该再起一个"全部靠同一个判据
`OpenMutexW(SYNCHRONIZE, FALSE, name)` 能不能打开。三处启动点的第一行都是
`if (NamedMutexExists(...)) return true;`。

**为什么不可恢复**：命名 mutex 只要有任何一个句柄开着就存在。`OpenMutexW` 只回答
"它在不在",**不回答"它属于谁、那个进程还活着吗"**。

- 持有者**死了** —— 内核关掉它的句柄,mutex 随之消失,这条本来就能恢复;
- 持有者**卡住但没死** —— mutex 一直在,`NamedMutexExists` 一直 true,`LaunchHelper`
  一直 `return true`,宿主一直以为 helper 在跑。而**没有任何地方记录持有者的 PID**,
  连"该杀谁"都答不出来,用户只能手工开任务管理器。

它不是假想:helper 是渲染进程,卡死是它最常见的故障形态。

**为什么没修**：要在 mutex 之外另存"持有者身份 + 心跳"(PID + 时间戳 + 存活检查),
由启动方决定接管。接管意味着杀掉一个还活着、可能在干活的进程 —— 那是产品决策
(要不要先试重启?要不要提示用户?),不该由自动开发循环单方面定。

**下一步(可自动做)**：把"看到锁之后该不该接管"抽成纯逻辑 —— 给定(记录的 PID、现在、
心跳、进程是否存活、租约时长)→ 起新的 / 已经在跑 / 可以接管 / 明确放弃并说出原因。
**真正需要人拍板的**：接管时是否允许 `TerminateProcess`,以及要不要给用户提示。

### 2026-10-04 · 执行记录：P0-08 后半句 —— 卡住的持有者第一次能被说出来

上一轮我说"剩下那一半需要产品决策",这个判断切错了地方:把**发现卡住**和**接管卡住**
当成了一件事。发现卡住不需要任何决策,它只是把 `OpenMutexW` 给的那个布尔换成带理由的裁决;
接管才需要决定要不要 `TerminateProcess` 一个还活着的进程。

- **本轮做发现那一半**:`MiaoLockOwnership` 把"这把锁现在是什么状态"判成五种 ——
  `Available` / `Running` / `Wedged`(活着但心跳过期) / `StaleRecord` / `Unusable`。
  `Wedged` 是当前结构上判不出来的那一类:`OpenMutexW` 只回答"锁在不在",不回答
  "它属于谁、那个进程还活着吗"。有了它,宿主第一次能说出
  "壁纸渲染进程 4242 还活着但已 61 秒没有心跳,它卡住了",而不是静默地以为一切正常。
- **两条不许含糊的边界**:租约没配时**拒绝裁决**,不许默默用默认租约;
  锁存在但没有身份记录时按不可判处理,**不按"没有持有者"**(那会让人以为这里判过)。
- **接管被隔在判定之外**:`MayStartNewOwner(Wedged)` 为假。要不要杀、要不要先试重启,
  整个留给宿主 —— 判定本身不含这个选择。
- **自动检查**:`LockOwnershipTest` 27 项(含反空洞自检);**9 处变异全红,0 存活**。
- **未取证**:五处启动点还是老的 `if (NamedMutexExists(...)) return true;`,
  接到新裁决是宿主侧改动,要 Windows 上真起一次 helper 才敢签收。

### 2026-10-04 · 执行记录：P0-09 升级把用户的 AI 会话变成不可达字节

用户在 AI 窗口的 API 下拉里选一个中央 profile,`SetProfileId(selected.id)` →
`ReloadConfig` 会:(1) 从 `api-profiles.ini` 填 `config_`,`profileId` 从空变成 "main";
(2) `SessionHash` 里含 `profileId`,于是会话文件哈希随之改变;(3)
`if (profile.configured) RetireLegacyShadowState();` 把 `model-settings.json` ——
也就是 `L3Agent` 自己那份配置文件、记录"用户旧配置是什么"的唯一地方 —— 删掉。
旧 `.bin` 还在盘上,但再也没有人能算出它的路径。`L3PersistenceSelfTest` 没覆盖这条路。

- **修法**:退休旧文件**之前**先把会话搬到新身份下(`MigrateSessionFromLegacyState`)。
  判定在 `MiaoSessionMigration`(纯逻辑,本机有门):只有"服务地址与模型还是同一套"才算
  来源提升,那时才搬;用户真换了配置时不搬。搬完把结论写进 `LastSessionMigration()`,
  宿主能说出口。
- **顺手把 `SessionHash` 收成一份实现**(L3Agent 转一手调纯逻辑那份):搬运要靠两边算出
  同一个路径,两处各写一遍时改一边不改另一边会静默失败 —— 那比不搬更难查。
- **顺手把哈希的具体数值钉住**:`l3-sessions/<hex>.bin` 就是用户的历史对话,
  改常量或改参与字段 = 全员历史对话不可达,这正是本轮缺陷的反面。第一版只钉
  "不同/相同"这类关系,改常量它照样全绿 —— 又一处"没钉具体值"的洞。
- **自动检查**:`SessionMigrationTest` 23 项(含反空洞自检);**8 处变异全红,0 存活**。
- **未取证**:"选了 profile 之后历史对话还在不在"要 Windows 上真选一次;
  旧文件不存在/读不出来这两条分支也没有真机证据。

### 2026-10-04 · 执行记录：CREATE-04 复用 candidateId 会把上一版可预览结果带走

`ValidationFailed` / `EvidenceFailed` / `ReviewCompleted` 都按
`FindCandidate(pendingCandidateId_)` "找到谁就改谁"。而候选 ID 是**原样收下模型给的**
(`CandidateSubmitted` 的注释写着"candidateId 由宿主重新分配",实际并没有 —— 只有模型
留空时才由我们编一个)。于是新一轮复用一个 ID 时,被改的是**上一轮那条已经成功的**记录:

    第 1 轮: cand-1 / digest-1 校验通过 → LastValidCandidate() = digest-1
    第 2 轮: 模型又交出 cand-1(摘要 digest-2),校验失败
             → FindCandidate("cand-1") 命中第 1 轮那条,validated = false
             → LastValidCandidate() = 空

用户在改需求重新生成之后指着上一版说"就用这个",而它已经不在了。这正是 CREATE-04 的验收:
"新一轮生成/修复失败不覆盖上一份可预览结果"。现有测试
`TestPreviousCandidateSurvivesFailure` 是绿的,只因为它给第二轮用了不同的 ID。

- **修法**:`pendingDigest_` 一起记,内部五处(实际四处)查找换成 `FindPendingCandidate()` ——
  **ID 与摘要都对上才认**。新一轮的失败找不到自己要改的那条,什么都不动,上一版留在原地。
- **本机跑了什么**:`CreationWorkflowStateTest` 174 项通过(新增
  `TestReusedCandidateIdDoesNotTakeAwayTheLastGoodOne`,并补了反向一条:
  ID 与摘要都相同就是同一条记录,它失败时上一版确实失效);
  变异检测:把两个重载都退回"只按 ID" → **红**,而且报的就是 CREATE-04 那两条。
- **一个值得记下的坑**:第一次变异只改了其中一个重载,测试**仍然全绿**。
  因为 `Apply` 走的是另一个重载。变异检测的意义正在于此 —— 一个没打到执行路径上的
  "破坏",看起来和"测试没覆盖"一模一样。
- **顺带**:`Windows x64 Build` 在 `20bd7639` 上红过一次(组件可见性冒烟,15 秒窗口)。
  同一条门在同一 SHA 的**另外三条链**(`Windows x64 Package`、`Windows x64 MSIX`、
  `Windows ARM64 Package`)上**全绿**;加上 ARM64 Fast Dev / Repo Hygiene /
  Path Layout Contract / Cleanup 也全绿 ——**同 SHA 八道里七道 success**。
  同一个 .ps1,同一份提交,五局四绿而代码只有一份,且我这三处改动都不在壁纸进程里执行
  (`ReaperJob()` 只有 `PiRuntime::LaunchProcess` 会调)。判为 runner flake。
  没有改宽容期:那是拿"让门别响"换"门还在查",而这个仓库里已经栽过四次。

### 2026-10-04 · 执行记录：AI-03 取消不是完成

同一个仓库里有两个 AI 运行时,而它们对"取消"的处理**不一样**:

    L3Agent::AskAsync   Stop(); worker_.join(); busy_ = true;      ← 等旧轮次真的结束
    PiRuntime::Stop()   request_stop(); WriteLine(abort); busy_ = false;  ← worker 还在跑

于是"取消 + 立刻重试"在 Pi 上是活的:`busy_` 已经是 false,`AskAsync` 的
`busy_.exchange(true)` 放行,第二轮起来了 —— 与还没退完的第一轮共用同一个
Node 进程、同一根 stdin/stdout 管子,第一轮没读完的 delta 会递进第二轮的 onDelta。
这就是 AI-03 原话里的"串会话 / 不留错误 Busy 状态"。`PiRuntime::AskAsync` 里连
`Stop()` 都不调,只 `request_stop()`,旧 worker 句柄被直接覆盖。

- **修法**:`busy_` 这个孤立 bool 换成轮次**相位**(Idle / Running / Stopping),
  判定落在 `MiaoTurnLifecycle`。关键一条:`PhaseAfterStopRequest` **不许返回 Idle**
  —— 取消是请求,完成要等 worker 自己退出。`Busy()` 改成 `phase != Idle`。
  `AskAsync` 用 `compare_exchange_strong` 按相位裁决,且 Stopping 上的拒绝给
  "正在取消上一轮,请稍等再发"而不是"正忙" —— 用户取消后马上重试时,界面上该说的是
  "再等一下","正忙"看着像坏了。
- **又删掉一个不可达的探测量**:第一版 `TurnTimeline::OverlappingTurns()`("曾经并发过")
  被变异检测逮到不可达 —— 取消进 Stopping 挡住起轮,只有 WorkerExited 回 Idle,而它在
  回 Idle 之前已经把 inFlight_ 减掉了。一个永远为 false 的探测量与没有它长得一模一样。
  换成可达的断言:**被拒的起轮不会多造出一个在飞的轮次**。
- **拒绝语的前缀是一个跨模块契约,不只是文案**:`AskAsync` 原本内联
  `onDone(L"Pi Runtime 正忙")`,而 `ContentCreatorDialog` 的 `kBusyRejectionMarker` 就是这
  一个串 —— 它拿 find() 分辨"这个 done 是一次拒绝"还是"这一轮已完成"。措辞挪进相位裁决后
  `tests/content-creator-modes.mjs` 当场红了,而它逮的是对的:前缀一改,一个**从未发送**的
  请求就会被当成"本轮生成已完成"报给用户。修法不是把字面量写回去,而是把契约钉住 ——
  `MiaoTurnLifecycle.h` 新增 `kTurnRejectionPrefix`,门改成从那个常量推导 marker 应有的值。
  prefix / marker / 不 emit 三处都变异验过,全红。
- 顺带:第一版 `JudgeTurnStart` 的 Idle 早退忘了把 `allowed` 置 true(成员默认 false),
  于是"空闲时也发不出消息" —— 12 项断言当场逮到。
- **自动检查**:`TurnLifecycleTest` 41 项(含反空洞自检);**11 处变异全红,0 存活**
  (10 处相位 + 1 处前缀契约)。
- **未取证**:"取消之后马上重试会不会串话"要在 Windows 上真发一轮才知道。

### 2026-10-04 · 执行记录：P0-08 强杀之后 Node 会活下来

`TerminateProcess(MiaoDesk)` **不跑析构函数** —— 于是"父进程自己会收拾子进程"这件
事在强杀路径上一条都不成立。`PiRuntime::CleanupProcess` 写得再好,也只覆盖正常退出。

- **补的**：新增 `include/miaodesk/MiaoChildProcessReaper.h`,把子进程装进一个带
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 的 job —— 父进程一死(不管怎么死的),内核连坐
  杀掉 job 里的全部进程。`PiRuntime::LaunchProcess` 接上它。装不进去时行为与今天一样,
  但写进日志:看不见的收尸缺口比没有更糟。
- **新门 `scripts/verify-child-process-reapers.py`**：每个 `CreateProcessW` 站点分三类,
  每类都要给出证据 —— 装进 job / 有界同步(当场等并在超时·失败时终止) / 已登记的独立
  生命周期。此前没有任何门管这件事。全仓 10 个真实站点(注释里的提及不算):4 个本来就在
  收尸,2 个有界同步,3 个是**故意**分离的生命周期(已登记它们靠什么停),1 个真的没有
  (PiRuntime 的 Node)。
- **顺手发现一处重复**:启动后台 Harness 所有者的函数有两份,而且**名字都不一样** ——
  `src/app/main.cpp:195` 的 `LaunchHarnessBackgroundOwner()` 与 `src/harness/HarnessHost.cpp:54`
  的 `LaunchBackgroundHarnessOwner()`。门把两处都列出来;合并不在本轮。
- **登记只认证据**:stop event 的符号必须在文件里,且它**定义成的事件真名**对得上。
  只查符号名的话,"改了事件名、留着没改名的常量"会一路绿灯,而那时父进程死后没有任何人
  能拦下这个孤儿。第一版写成正则时把不属于反斜杠的点也转义了,于是永远匹配不上,门把三个
  正确登记的站点全判成"登记已过期" —— 改成普通字符串比较。
- **另一处门自己的洞**:`verify-shell-scripts-parse.sh` 只查 `scripts/*.sh`,而仓库里那个
  python 动画门(`verify-builtin-wallpaper-animation-parity.py`)已经跑了一年 —— 它写坏语法
  时,解析门照样报"全部可以解析"。同一条理由,换了个扩展名而已。已扩到 `scripts/*.py`。
- **CI 当头一棒,是我自己的错**:收尸门第一版把仓库根**写死**成
  `ROOT = '/Volumes/Ext/Projects/MiaoDesk'`。本机全绿,ubuntu runner 上一个站点都扫不到。
  **是门自己的"零条比对不可能是通过"守卫报出来的** —— 它没有打印一句"每个站点都有收尸
  路径"就退出 0。修复提交 `9175fb0b`:ROOT 改从 `__file__` 推;守卫现在会打印它解析到的
  两个路径并直接说"那个根目录看着像某台开发机的路径,就是 ROOT 又被写死了"。
  复现方式是把 src/scripts/tests/.github 复制到另一个绝对路径再跑。
- **自动检查**：收尸门 6 处变异 **6 红 0 存活**(含它自己的零站点守卫);mingw 单编
  `PiRuntime.cpp`/`L3Agent.cpp` 均 0 错误;
  21 道 shell 门 + 2 道 python 门 + 17 道 node 契约门全通过。
- **未取证**："强杀之后桌面上真的没有孤儿进程"要 Windows;`AttachToReaper` 在
  ERROR_ACCESS_DENIED 那条路上究竟多常见也要真机才知道。

### 2026-10-04 · 执行记录：P0-07 AI 侧,一把 Key 会被发到另一台服务

修一个会外泄凭据的缺陷,并把"请求 URL 与凭据是否同源"变成本机可测的契约。

- **缺陷**:`L3Agent::ReloadConfig` 从 `api-profiles.ini` 读权威配置,而 `/provider`、
  `/endpoint`、`/key` 这几个**活着的**本地命令(AI 对话里敲的,`ConversationPanelImpl`
  直接转发)就地改 `config_`。`config_.profileId` **只有 `ReloadConfig` 会写**,于是:

      /provider https://别的服务/v1 某个模型
      → URL = https://别的服务/v1/...               ← 用户刚敲的主机
      → Key = MiaoDesk/ApiProfile/<原 profile 的 id>  ← 原 profile 的 Key

  **一把签给 A 服务的 Key,被发到 B 服务上去。** `/key` 更糟:它按同一个 `profileId`
  选槽,把用户敲的新 Key **覆盖回那个 profile 自己存的 Key** 上 —— 其它每个用这个
  profile 的 AI 窗口、以及 Harness,下一轮就都用上这把被换掉的 Key,而设置页里一个字不提。
- **修法**:`/provider` 换主机时丢掉 profile 身份,凭据回到旧槽(`/key` 写的也是旧槽),
  地址一个字符不动。`/endpoint` **故意不丢** —— 只换路径时主机没变,同源关系完好,
  丢了反而让用户莫名失去配好的 Key。
- **顺带说实话**:`/provider` 原先回"模型配置已保存。",而那个值写进
  `model-settings.json`,这个文件的读者 `L3Agent::LoadConfig()` **零调用方**
  (L3Agent.h 自己写着 "ReloadConfig and the normal runtime path do not consume their
  state.")。已改成如实说明权威来源。
- **拆出 `MiaoAgentConfigAuthority.{h,cpp}`**(纯逻辑,不 import Windows 头):
  与 `MiaoD3D11RenderPolicy`、`TodayTaskPresentation`、`WidgetIdentityRules` 同一模式 ——
  `L3Agent.cpp` 为了 `wincred.h`/`winhttp.h` 只能 Windows 上编,于是这条判定本机一行都
  跑不到。`CurrentApiUrl`/`SaveApiKey` 改成走它;槽名用 `static_assert` 两边焊死,
  因为那是 `LoadApiKey`/`SaveApiKey`/`ApiRuntimeProfile::ReadSection` 三方共用的契约。
- **自动检查**:`AgentConfigAuthorityTest` 38 项,含反空洞自检(喂四个明知该被抓的合成输入,
  恒返回 `bound=true` 的判定函数必须露出来);**16 处变异全红,0 存活**;
  21 道仓库门 + mingw 交叉语法门(0 真实错误)全通过。
- **`b5e5403c` 上两个 CI job 红过**:`Repo Hygiene` 与 `Windows x64 Build`。同一根因,不是
  编译错误 —— 两道 job 都会跑 `tests/multi-api-routing.mjs`,而那道门断的是我把凭据槽构造
  搬走之前的**旧写法**。修复提交 `49414565`:门改成断契约本身,并比原来更严
  (槽必须从 profile id 推出来、内联第二份不许回来、static_assert 必须在),两条都变异验过。
- **一处必须说清的未取证**:`L3PersistenceSelfTest`(只能 Windows 上跑)**正好走
  `/provider`**。逐行读过之后判断它不受影响 —— 那个 legacy 块跑在新建的临时
  LOCALAPPDATA 里,此时没有任何 profile,`config_.profileId` 从构造起就是空串,
  而空串时 `RebindForLocalCommand` 是恒等的。但这是**推理,不是证据**。
- **未取证**:P0-07 的 Wallpaper/Widgets/AI 会话/库状态四个恢复面的真机一致性仍要 Windows;
  "外泄的 Key 打中过哪台服务"也没有线上证据。

### 2026-10-04 · 执行记录：P0-04 的自动部分，与一个自我延续的 NaN

本条记录**修正上一轮的一个错误结论**，并修一个会一直存在的坏值。

- **修正**：上一轮把 `P0-03/04/07/08/09` 整块说成"都要 Windows 侧"，这是错的。
  持续开发面板自己就写着这几项的"自动部分 = 是"，规划原文是"完成…的**自动部分**
  并排入真机验收"。本轮做 P0-04 的自动部分。
- **做完的**（P0-04 四条验收里覆盖两条）：
  - **位置丢失** → `NormalizeWidgetLayout` 从 `DesktopWidgetStore::Normalize` 提出来。
    那个 .cpp 为了 UTF-16 配置持久化 include 了 `windows.h`，于是这批不变量本机
    一行都跑不到。与 `MiaoD3D11RenderPolicy`、`TodayTaskPresentation` 同一模式。
  - **重复实例** → `NativeSingletonKey` / `SameNativeSingleton` 提到
    `WidgetIdentityRules.cpp`（纯字符串逻辑）。"同一 preset 同一显示器只允许一个实例"
    此前是 `DesktopWidgetStore.cpp` 匿名命名空间里的文件局部符号。
  - 顺带把 `NativeWidgetPreset.cpp` 整个变纯：它只有 `_wcsnicmp` 一个 Windows 调用，
    换成可移植的宽字符大小写不敏感比较（三个内置 source 全是 ASCII）。
- **修的真实缺陷**：`std::clamp(NaN, lo, hi)` 对 NaN 是**恒等函数** —— 它的实现是
  `v < lo ? lo : (hi < v ? hi : v)`，两个比较对 NaN 都为假，于是返回 v。而
  `DesktopWidgetStore::ReadFloat` 用 `wcstof` 解析配置且不查有限性，`FloatText` 又用
  `%.6f` 写回：一个 NaN 会写进配置文件、再读回来、再写回去，**自我延续**。带着 NaN
  坐标的组件在 `UpdateLayeredWindow` 上直接失败或消失。读与归一化两侧都挡了。
- **又踩到自己两次，同一个模式**：
  - `WidgetLayout` 的成员默认值重复写了一遍常量，于是改常量不改结构体 ——
    "新建组件落在哪里"会有两个答案（变异检测：把常量改成 0.0，104 项断言全绿）。
  - 测试用**同一个常量**做比较，于是"尺寸下限改成 0"也测不出来。修法是把常量本身的
    值单独钉住。
  - 另删掉一处不可达的尺寸下限夹紧（位置已夹到 `<= 0.95`，收边后不可能 `< 0.05`）。
    这是本会话第四次"挡不动任何东西的守卫"，每次都是变异检测逮到的。
- **自动检查**：`WidgetGeometryTest` 126 项；P0-04 几何 6 处、去重 5 处变异全红；
  44 个纯逻辑目标全通过；21 道仓库门 + mingw 交叉语法门（0 真实错误）全通过。
- **未取证**：P0-04 剩下的"无孤立 HWND、错误背景"与 20 次循环本身都要 Windows。
  "组件带 NaN 时桌面上会怎样"也没有真机证据 —— 修的是链条，不是现象。

### 2026-10-04 · 执行记录：WPRO-01 把宽高比裁决接进宿主诊断

本条记录**让上一轮量出来却没人看得见的结论出现在现场**，不改渲染行为。

- **上一轮的遗留**：WPRO-01 量出三个官方组件全部声明 `aspectRatio: 1.0`，而 16:9 上
  实际 576×324（1.778）、32:9 上 3.556，**八个参考屏幕上一个也不成立**。那个结论此前
  只活在测试里 —— 现场（诊断行、日志）一个字都不提，因为没有任何渲染器或宿主读那个字段。
- **本轮**：`ContentWidgetHost::SurfaceState` 加上宽高比裁决。三个官方组件在 FHD 上会打印
  `aspectDeclared=1.000 aspectActual=1.778 aspectDistortion=1.778 aspectHonored=false`。
  没声明 `aspectRatio` 的组件不写这一段 —— 写了会让人以为这里有过一个结论。
- **为此加重载而不是复用**：`JudgeWidgetAspectOfBox(box, declared)` 吃**宿主手里那个真实
  像素盒**，而不是从归一化几何反推。宿主要的渲染目标尺寸是既成事实（夹紧、DPI、
  桌面边界都可能让它与推算差一两个像素），诊断要的是既成事实；两条路算出同一个数时
  它们互相印证，算出不同数时那本身就是该报的信息。
- **第一版实现读 `box.aspect` 这个展示字段，是错的**：宿主拿到盒子之后可能改它
  （DPI、边界夹紧、信箱化），而它没有义务记得同步那个字段。于是"把 height 调小 24 像素"
  拿到的还是旧比例的裁决 —— 而那一条恰好是诊断要报的信息。
  `WidgetGeometryTest` 里"盒子与推算不同时裁决跟着变"逮到的，改成从 width/height 现算。
- **另加 `FormatAspect`**：三位小数，NaN/inf 印成 `nan`。没有用 `std::to_wstring`
  的六位 —— 诊断行已经很长，而人要看的是"1.778 对 1.000"这个对比，不是第七位小数。
- **自动检查**：`WidgetGeometryTest` 75 项通过（新增 7 项重载一致性/盒子变更/未声明/
  空盒子、6 项 `FormatAspect`）；4 处变异全红。mingw 交叉语法门 0 真实错误。
- **接线可达性逐段核对（读代码，未运行）**：`assets/widgets/` 由顶层 CMakeLists 装到 exe 旁的
  `Widgets/`，正是 `paths::BuiltInWidgetPackagesRoot()`；`MiaoWidgetContentCatalog::ResolveOneRoot`
  对每个 `.mdwidget` 调 `MiaoContentDefinitionLoader::Load`（读 `manifest.json`），于是
  `slot.definition.geometry.aspectRatio` 对三个官方组件有值，`SurfaceState` 那段会打印。
  **打印出来的文本本身未在 Windows 上看过** —— 这是"接线到得了真内容"与"那句话真的出现了"
  两件事，前者已证，后者没有。
- **未取证**：诊断行的实际输出本机看不到 —— 它只在 Windows 宿主里被写进配置文件与日志。
  壁纸与组件的视觉表现**一个像素都没动**。

### 2026-10-04 · 执行记录：CAP-03 资产门与 schema 版本矩阵

本条记录**补上两个此前没有任何门的验收项**，不改产品行为。

- **资产门**：验收项"资产不重复打包"此前只能靠人眼翻 manifest。新增
  `src/tests/ShippedPackageAssets.cpp`，对全部发行包与两个示例包走
  `MiaoAssetDatabase::Build`，逐包核三件事：每个声明的资产都有人引用、没有两份资产
  内容哈希相同、合计字节数被记录。本轮基线 **16 条资产记录、2710 KB、无人引用 0 条、
  内容重复 0 处**。
- **在这道门上踩到自己一次**：第一版把判定写在循环里，于是把"无人引用"那一行删掉之后
  门照样全绿 —— 当前内容没有无人引用的资产，"检查还在不在"这件事根本问不出来。
  改成抽出 `AuditPackageAssets` + 两个合成坏包的反空洞自检之后，两处变异都红了。
  **一道永远不失败的门与一道好门，在通过的那一刻长得一模一样。**
- **版本矩阵**：CAP-03 的"旧包可加载、版本可演进；升级与降级路径明确"落成
  `MIAO_CONTENT_PACKAGE_V1.md` 附录 A。只有一版时也写下来，理由是"将来加第 2 版时
  这里必须先改"，而不是等到第 2 版出现时才发现没人说过旧版还算不算数。
  `schema` 与 `kSchemaVersion` 不等即失败（`MiaoContentPackage::Load`）：第 2 版出现
  之前写 `schema: 2` 的包加载失败，而不是被当成第 1 版的近似 —— 一个被静默当成旧版的
  新版比一个加载失败的新版难查得多。capability 名字走另一条路（目录，不由包 schema
  约束），但**不能为了让某个包通过而往目录里加名字**。
- **自动检查**：`ShippedPackageAssets` 14 项通过（含反空洞自检）；两处变异全红。
- **未取证**：本批无 Windows 侧改动，也没有用户可感知变化 —— 它防的是将来。

### 2026-10-04 · 执行记录：WPRO-05 组件失败重试的风扇问题

本条记录**修复一个会一直烧 CPU 的缺陷**，并新增纯逻辑刷新策略。

- **缺陷**：`ContentWidgetHost::RepaintDue` 按 `nextRefreshAt == 0 || now >= nextRefreshAt`
  决定要不要画，而 `nextRefreshAt` 只在**成功**的 `PaintSlot` 末尾被赋值。`PaintSlot`
  有五条提前返回的路径（渲染目标拿不到、渲染器加载失败、宿主数据应用失败、`Draw` 返回
  false、`Present` 失败），一条都不赋值 —— 于是它保持 0，而 0 的含义正是"随时都该画"。
  一个画不出来的组件因此每 16 ms 重试一次：一秒 60 次，一天 520 万次，每次都重跑
  渲染目标/渲染器/宿主数据整条链路。用户看不见任何变化，只看见风扇转。
  `nextRefreshAt` 的赋值点一共四处，失败路径一处都没有。
- **修复**：新增 `src/desktop/widgets/WidgetRefreshPolicy.cpp`（纯逻辑）——
  失败指数退避（1s/2s/4s/8s/16s，30s 封顶；冷启动第一分钟 6 次、稳态每分钟 2 次，
  今天是 3750 次），成功立刻清零回到内容要的间隔（否则一次内容变化会晚最多 30 秒
  才上屏），延迟永不为 0。宿主接线：`ScheduleNextRefresh` 走成功路径，五条失败路径
  调用新增的 `ScheduleNextRefreshAfterFailure`。空闲间隔与直接呈现心跳的常量也并到
  这里，只写一份（原先宿主各写一个 1000 与 2000，两边都不在纯逻辑集合里，漏改一处
  不会有任何测试红）。
- **顺带删掉三处挡不动任何东西的守卫**（都是变异检测逮到的）：
  `SceneClock::Pause` 的 `if (!running_) return;`（`running_ = false` 本身幂等）、
  `ExtractJsonDouble` 的 `!std::isfinite`（`from_chars` 不接受 inf/nan）、
  `NextRefreshDelayAfterFailure` 的 `if (delay == 0)`（delay 从 1000 起只乘不除）。
- **自动检查**：`WidgetRefreshPolicyTest` 174 项通过（含一条专门钉 off-by-one 的断言：
  第一版把 `ApplyRefreshOutcome` 写成"先 ++ 再算"，第一次失败就退避到 2000ms 而不是
  起点 1000ms）；7 处变异全红；43 个纯逻辑目标全通过；21 道仓库门 + mingw 交叉语法门
  （0 真实错误）全通过。
- **未取证**：失败循环的实际 CPU/耗电下降**未测量** —— 那要 Windows 实机。宿主接线
  只过了 mingw 交叉编译。验收里"无变化不重绘 / 多实例不重复轮询 / 锁屏后无请求风暴"
  仍未做：空闲内容仍按 1000ms 重绘一次（与之前一致），多实例共享缓存与请求风暴
  都没有实现。

### 2026-10-04 · 执行记录：WPRO-01 组件几何与 `aspectRatio` 不生效

本条记录**新增纯算术模块与两道门，并改正一处"字段在、校验过、教它有效，而没人读它"的
声明**，不改渲染行为。

- **缺口**：`geometry` 全用归一化写（`defaultWidth: 0.30` 是桌面宽度的 30%），于是
  "这个组件有多大、比例对不对"在写 manifest 时答不上来，而产品此前没有任何地方把它算出来过。
- **实测结论**：三个随产品发行的组件全部声明 `aspectRatio: 1.0`。`ValidateInstance`
  拿 `defaultWidth / defaultHeight`（0.30 / 0.30 = 1.0）去比，一路绿灯；而宿主要的像素盒是
  "归一化宽 × 屏幕宽"与"归一化高 × 屏幕高"—— FHD 16:9 上 576×324（1.778）、32:9 上
  1536×432（3.556）。**八个参考屏幕上没有一个声明成立**。真实比例等于
  `(归一化宽 / 归一化高) × (屏幕宽 / 屏幕高)`。
- **性质**：这与 `asset.font`、`videoRenderer` 同类 —— 字段在、校验过、Skill 还教它有效，
  而没有任何渲染器或宿主读它。差别是这次**连校验都是虚的**：它比的是归一化比例，
  不是用户看见的像素比例。
- **新增**：`src/desktop/widgets/MiaoWidgetGeometry.cpp`（纯算术）—— `ResolveWidgetPixelBox`
  （含夹回屏内：一个越界的盒子会让 `UpdateLayeredWindow` 直接失败）、`JudgeWidgetAspect`
  （"没声明"不等于"比例不对"）、`FitAspectInside`（信箱化，**当前无调用方**；
  接进宿主需要 Windows 上看得见效果）。8 块参考屏幕含 21:9 与 32:9 —— 失真恰恰随屏幕
  比例变，只测 16:9 会漏掉最极端的那些。
- **新回归门**：`src/tests/ShippedWidgetGeometry.cpp` 把每个发行组件在 8 块屏幕上的真实
  像素盒逐行打出来，登记表与实际不符即红。**不因为比例不符就红** —— 不符是现状，
  而为让门变绿去改美术正是本规划禁止的动作。
- **共享读取器**：`ExtractJsonInt` 对 `"0.30"` 解析失败并停在 `'.'` 上，调用方要么把 0
  当成合法值读进去，要么自己写 `strtod`。第一版门就这么踩了：`defaultWidth` 读成"取不到"
  退回默认值 0.30，而默认值恰好等于真值 —— 三个组件全被报成"未声明 aspectRatio"，
  而 manifest 里明明写着 `1.0`。**默认值与真值撞车让整道门看起来在工作**。
  修法是在 `JsonStringField` 里加 `ExtractJsonDouble`（与整数同一条边界纪律：
  `"1abc"` 取不到），并在 `JsonStringFieldTest` 补 14 条断言。
- **自动检查**：`WidgetGeometryTest` 61 项、`ShippedWidgetGeometry` 10 项、
  `JsonStringFieldTest` 51 项全部通过；几何 10 处变异、JSON 3 处变异全红。
  `ExtractJsonDouble` 里一条"挡 NaN"的守卫跑出仍绿（`from_chars` 不接受 inf/nan，
  换成 `strtod` 才用得上），已删掉并把理由写进注释。
- **文档改正四处**：`skills/widget-content`、`docs/MIAODESK_CONTENT_FRAMEWORK.md` 的
  "允许 `aspectRatio = 1.0`"、TODO 第 11 节、能力台账。
- **未取证**：本批无 Windows 侧改动。"组件的真实比例只有 1.778/3.556"是算术结论，
  "用户在桌面上看到的是什么"仍未签收。尺寸族（small/medium/large）没有定义 ——
  那是产品决策，本轮不发明。

### 2026-10-03 · 执行记录：WALL-03 宿主侧时间策略与发行内容动画连续性

本条记录**新增能力与回归门，并给 `Validate` 加一条拒绝规则**。`MiaoSceneRuntime` 的
求值部分本轮未发现缺陷 —— 缺的全在宿主侧，而那正是规划给 WALL-03 的验收项
"昼夜切换、事件触发、**循环接缝**与**暂停恢复**"。

- **暂停恢复此前不存在**。`AdvanceTimeline(timeSeconds)` 收的是宿主给的绝对时间，
  而真实宿主给 `GetTickCount64() / 1000.0`（`ContentWidgetHost.cpp:717`）。于是被挡住 /
  隐藏 / 锁屏的那段时间照算进动画：昼夜壁纸恢复后天空直接跳到半夜，而用户什么都没做。
  新增 `SceneClock`：场景时间只由"正在跑的那段时间"累加，暂停任意时长一秒都不多走；
  `Seek` 在暂停期被忽略，否则暂停可以被绕过。
- **平滑参数过渡此前不存在**。`SetParameter` 是瞬移的。新增 `ParameterSlew` /
  `ParameterSlewSet`：曲线与时间轴共用一份 `ApplyAnimationEasing`（原先是
  `MiaoSceneRuntime.cpp` 里的文件局部符号，宿主调不到），过渡按场景时间推进而不是墙钟，
  结束时通知宿主，之后不再占帧。
- **循环接缝与一帧瞬移此前没有任何检查**。`ValidateAnimation` 只查关键帧时间严格递增，
  于是两种写法都能过：Loop 轨道首尾值不同（每圈整段行程在一帧内被撤回），
  以及两个关键帧相距 1e-9 秒、值差 0.8（任何帧率下插值一次都采不到样）。
  新增 `AuditAnimationContinuity` 与 `ShippedAnimationContinuity`：后者走真实加载链
  审查全部 25 条发行动画轨道，端点与帧步长都记录在案。
- **两条动画写同一个属性此前不拒绝**。先声明的那条**从来不可观测**：`Initialize`
  只跑 binding、不跑动画，所以它连起始值都贡献不了；`AdvanceTimeline` 又按声明顺序跑，
  后一条每帧都把它盖掉。事件触发的那条更糟 —— 它只在触发那一帧赢一帧，下一帧就被
  时间线动画盖回，看起来是一次一帧的闪。现在 `Validate` 拒绝，并在错误信息里点名是哪两条。
- **边界是刻意划在"两条动画"，不是"动画与 binding"**。binding 只在 `Initialize` 跑一次，
  所以它在动画开跑之前**是**可观测的（属性起始值来自参数），"起始值取参数、之后交给动画"
  是一个自洽的模型 —— 产品的序列化夹具与两处 SelfTest 正是这个形状。那一对的优先级
  （动画赢）改为报出来并写进 Skill，不禁止。
- **一处已登记而未修复**：`animation://miao-cloud/blink-blink` 的淡入/淡出各为
  1/240 秒（产品支持的最高帧率下的一帧），所以那一"淡变"渲染不出来。
  没有改它：改法是拉长淡变，那是改美术，而本机不是 Windows，拉长之后好看不好看
  给不出证据。`ShippedAnimationContinuity` 里以登记表的形式盯着这件事 ——
  多一条、少一条都会红。
- **自动检查**：`MiaoSceneTimelinePolicyTest` 174 项、`ShippedAnimationContinuity` 48 项、
  `MiaoSceneRuntimeTest` 33 项（新增 6 项并发写拒绝）全部通过；缓动曲线提为共用实现后
  `MiaoSceneRuntime`、`MiaoSceneRuntimeModel`、`MiaoSceneFrameScheduler` 与
  `MiaoSceneSerializer` 的 `SelfTest` 仍通过。共 26 处变异全部变红（含一处占位空操作，不计）。
- **未取证**：真机动画采样与视觉签收一律未做（本机不是 Windows）。
  `SceneClock` 与 `ParameterSlew` 也尚未接到生产调用方 —— 接进宿主要同时决定
  暂停由谁调、参数面板失焦后怎么办，属于播放宿主那一轮。

### 2026-10-03 · 执行记录：D-6 校验器拒绝全部随产品发行的包

本条记录**发现并修复一处校验与加载不一致**。它由本轮新加的发行包包级校验门首次跑出。

- **缺陷**：`ContentPackageValidator` 要求 `manifest.json` 的 `entry` 以 `scene/` 开头，
  而三个官方壁纸、三个官方组件、两个示例包的 entry 都是根下的 `scene.json`
  （加载器对两种布局都接受）。于是 8 个随产品发行的包全部过不了**包级**校验，
  拒绝原因是"必须指向 scene/ 下的一个 .json 文件"——而那句话指向的是一个合法包。
- **为什么此前没人发现**：`BuiltinWallpaperPackages` 覆盖的是另一条链
  （load → deserialize → runtime validate → initialize → asset database），它不跑
  `ContentPackageValidator`。包级校验只作用于 AI 候选包，而候选包出自创作工作区，
  布局恰好就是 `scene/` —— 所以两边各自正确，合起来把发行包漏掉了。
- **修复**：entry 规则改为"包内的一个安全相对 .json 文件，且不是 manifest / parameters
  本身，也不越出包根"。两种布局都接受；仍然拒绝 `assets/a.json`（资产目录不放 JSON）、
  `manifest.json`、`scene/scene.png` 与 `../x.json`。工作区那套更严的布局规则继续由
  `CreatorWorkspacePolicy` 负责，不在这里重复一遍。
- **新回归门**：`src/tests/ShippedPackagesValidate.cpp` 每次对全部 8 个发行包跑一遍
  `ValidateCandidatePackage`。变异检测通过：把 entry 规则改回只认 `scene/`，门即红。
- **依据**：同一条原则本文件的 id 规则注释里已经写过 —— "校验器比加载器更严的后果是
  拒绝它本来能加载的包"。

### 2026-10-03 · 执行记录：CAP-02 示例包负载链门

本条记录**新增回归门**，不改产品代码。

- **缺口**：作者唯一能照着抄的两个示例包（`examples/content/{AuroraMinimal,ShaderPulse}.mdwall`）
  此前两头都不覆盖 —— `BuiltinWallpaperPackages` 的 spec 表里只有三个内置壁纸，
  `ShippedPackagesValidate` 只走包级校验。示例是教学的入口，它坏了比一个用户包坏了更糟；
  本轮已经吃过一次：Skill 长期把视频教成 `videoRenderer` 循环，而那个组件没有渲染器。
- **新增**：`ExamplePackagesLoad` 走 load → 反序列化 → 运行时校验 → 资产解析 → Initialize，
  并核对 `param://` 绑定都在 `parameters.json` 里有定义、示例不含代码产物、示例 HLSL 的
  入口点符合 shader 契约。17 项通过；变异检测通过（把绑定指向不存在的参数即红）。
- **未做**：像素与真机显示仍未验证（示例的 HLSL 只在契约层检查，没有真正编译）。

### 2026-10-03 · 执行记录：D-8 待办组件沉默截断用户的待办

本条记录**用户可感知缺陷修复**。

- **缺陷**：待办内容组件顶部显示真实的 `{{tasks.pending}} 项待办`（8 条就说"8 项"），
  下面却只画 4 个固定槽位，而且**没有任何地方**提示"还有 4 项"。用户以为组件坏了，
  或者以为自己只加了 4 条。沉默地截断与显示假数据是同一类错 —— 组件都在对自己画出来
  的东西撒谎。
- **修复**：新增纯逻辑 `TaskOverflowText`，内容组件多一个可绑定的 `tasks.overflowText`
  （"还有 N 项"），`TodayTasks.mdwidget` 的 scene 增加一个绑定它的文本节点。
  槽位数改为内容提供方与原生卡片**共用一份**（`kTodayTaskVisibleSlots`）——
  原先两处各写一个 4，迟早出现"组件说还有 4 项、原生卡片画到第 5 条"。
- **验证**：`TodayTaskPresentationTest` 27 项（含"放得下时没有溢出说明""刚好放满也不是
  还有 0 项""槽位为 0 时说全部放不下"）；发行包包级校验门仍 8/8 通过；变异检测通过
  （把放得下的情形也报溢出即红）。
- **未接线**：`TodayTaskContentProvider` 整体只能在 Windows 上链接（它调
  `TodayTaskStore::Load`，而那个实现 import Windows 头）。可测的那一半已拆到
  `TodayTaskPresentation`，但提供方本身与桌面上的实际显示**未在真机验证**。

### 2026-10-03 · 执行记录：SEARCH-01 排序基线入仓（自动部分）

本条记录**测试与可测性变化**，不改排序行为。

- **可测性**：排序规则原先住在 `AppSearch.cpp` 的匿名命名空间，而那个文件 include
  windows.h 才能编（索引来自开始菜单、注册表、App Paths）。结果是排序规则在本机一行都
  跑不到 —— 用户感知最强、也最容易被顺手改坏的那段没有自动保护。现提取为纯逻辑
  `SearchTextScoring.cpp`，逻辑与原实现逐行一致（仅签名加 `noexcept` 与换行不同；
  用 diff 比对确认）。`AppSearch::Query` 改为委托，不再各写一份。
- **固定集**：新增 `SearchRankingBaselineTest`，24 条合成样本 + 14 条固定查询、22 项断言：
  Top-3 顺序、命中/不命中、`maxResults` 截断、重复查询顺序稳定、名字命中优先于关键字命中。
  分数不逐个钉死（钉死它会让任何调优变成"改 40 个数"），钉的是顺序与命中。
- **已知缺口（已钉住）**：拼音检索不支持 —— 打 `jisuan` 找不到"计算器"。规则只做逐字符匹配，
  中文字符串里没有拉丁字母，间隙匹配直接返回 0。按其验收原文"先记录需求与样本，
  不默认扩大检索范围"处理，未实现。
- **未取证**：合成索引不是真机样本。真机索引（开始菜单/注册表/App Paths）与 Goz 文件搜索
  的样本和分母要 Windows 才能采，因此**不得据此宣称 Top-3 ≥90%**。

### 2026-10-03 · 执行记录：CAP-05 组件数据与动作契约

本条记录**新增契约与测试**。契约是纯逻辑，尚未接线，因此没有用户可感知变化。

- **数据侧**：`ProviderSnapshot` 把六个状态分开（Loading / Available / Empty / Offline / Failed /
  Revoked），并要求 `observedAtUnixMs` + `validForMs`。判定规则里最要紧的两条：
  **没有时间戳的快照按过期处理**（不假设新鲜 —— 组件会把不知道多久以前的数据当"现在"显示），
  以及**空与离线不是一回事**（把离线显示成空，用户会以为自己的日程真的空了）。
  现状是三个 Provider 三种结构、谁都没有 loading 态、谁都不处理撤销，所以这一层此前无法表达
  "组件显示的是 40 分钟前的天气"。
- **动作侧**：`WidgetActionRegistry` 提供受控派发，固定四条性质 —— 未知动作明确拒绝（不是"什么也没发生"）、
  缺 `operationId` 拒绝（放行意味着重复点击执行两次）、权限撤销拒绝、以及**同一个 operationId
  回放同一个结果并标记 replayed**（重复点击不产生第二次副作用，也不让用户以为失败）。
  失败的执行不记入已执行凭据，否则同一个凭据再也重试不了。
- **验证**：`WidgetDataActionContractTest` 41 项通过；变异检测通过（去掉回放分支即红 4 项）。
- **未接线**：time/weather/tasks 三个 Provider 尚未改用这份契约，动作注册表尚无生产调用方 ——
  接入属 WPRO-03/04。"组件能完成待办"在那之前不成立。

### 2026-10-03 · 执行记录：CAP-02 能力知识补全与"漏教"门

本条记录**作者/AI 所用知识的变化**，附带三处事实校正。没有修改运行时。

- **漏教**：四份 Skill 此前只提到 3 个场景组件 kind、0 个资产类型、0 个后处理效果，
  而 `textRenderer` 有完整的 D2D 实现、支持 `{{data.*}} 模板，AI 因此不会用它显示日期或待办文字。
  已为 `wallpaper-content` 补上组件能力表（10 个 kind 逐一标明"画/不画"与后端）、图片资产写法与
  上限、8 个后处理效果及其成对规则、时间通道。
- **事实校正一**：Skill 把视频教成"单轨 VideoRenderer 循环"。**没有任何渲染器实现那个组件** ——
  它校验得过但一个像素都不画。视频壁纸实际走另一条路（媒体文件 + 宿主 `VideoWallpaperPlayer`）。
  照旧文档生成的内容在预览和桌面上都是空白。
- **事实校正二**：`asset.font` 没有消费方。textRenderer 用 `fontFamily` 指定**系统字体名**，
  导入字体文件不改变任何字形，只是白占包体量。
- **事实校正三**：`builtinName:"gradient"` 之类校验得过但没有实现（唯一实现的是 `solidColor`）。
- **新门**：`scripts/verify-skill-teaches-executable-capabilities.sh` 钉住"能力目录里每条可创作
  且可执行的能力都至少被一份 Skill 提到"。它与已有的 capability-contract 门是同一个问题的两个
  方向（那一份查虚构，这一份查漏教）。已做变异检测：往目录里加一个没人教的能力即红。
- **未做**："首批壁纸/组件各 3 个真实渲染样例"与"AI 能解释并正确使用"都需要真实渲染与模型评测，
  本轮不声称。

### 2026-10-03 · 执行记录：D-2 原生待办卡片显示假数据

本条记录**用户可感知缺陷修复**。

- **缺陷**：原生 `native:today-tasks` 组件（管理界面可添加）的 `PaintTodayTasks` 在 painter 里
  写死三条待办（完成产品设计方案 / 与团队同步项目进度 / 回复客户邮件）、一个 `L"3"` 计数与
  `L"1 / 3 完成"` 进度，**从不读** `TodayTaskStore`。用户在管理界面编辑的待办因此从不出现在
  常驻桌面的卡片上，而卡片看起来完全正常 —— 读不到存储时也不例外。
- **修复**：新增纯逻辑 `TodayTaskPresentation`（`BuildTodayTaskCardModel`）定义行模型与口径：
  数量/进度/行全部来自快照；空待办进度为 0 而不是 1；读不到快照时 `valid=false`、零行、
  状态写"任务数据暂不可用"。宿主 `NativeWidgetHost::PaintSlot` 与库预览
  `WallpaperLibraryWindowV2` 都改为加载真实快照后交给 painter。
- **验收**：`TodayTaskPresentationTest` 19 项（含"读不到时一行都不给"）；`tests/native-tasks-uses-real-store.mjs`
  守住宿主与 painter 调用点（painter 需 D2D 头，macOS 上此前无本地门覆盖，与 D-1 同形状）。
  新增测试均通过变异检测：把"读不到时给三条假行"写回去，测试即红。
- **限制**：Windows 真机未验证；卡片仍只有一档尺寸、点击不能完成待办（属 WPRO-01/03）。

### 2026-10-03 · 执行记录：CAP-01 能力目录与 D-1/D-3 修复

本条记录**代码与契约变化**。"真机效果"仍全部未取证。

- **能力目录**：新增 `src/content/binding/MiaoCapabilityCatalog.cpp`（68 条，52 条会执行），把
  "可声明／可执行／可预览／AI 可创作／真机已验"分开登记，并给出四类查询（Real / DeclaredOnly /
  BackendUnsupported / Unknown）。目录是唯一一份表：`creator_capabilities_get` 的自述、作者文档导出与
  `scripts/verify-capability-catalog.sh` 都读它，不再有三份手抄能力表。
- **虚构能力被拒（D-3）**：`MiaoContentPackage` 加载器与 `ContentPackageValidator` 都对照目录检查
  `capabilities[]`，目录外的名字让包加载失败并定位到 `manifest.json` / `capabilities`。此前只查字符集，
  `audio.read` 之类能通过并静默无效。
- **事实校正**：`asset.font` 解析与序列化支持，但**没有任何渲染器读它**（`textRenderer` 用 `fontFamily`
  指定的系统字体名），目录标为仅声明。目录第一版把它标成可执行，被自身测试拒绝后改回。
- **共用 JSON 取值器**：`ExtractJsonStringArray` 提进 `JsonStringField.h`，删除 `MiaoContentPackage.cpp`
  里的本地副本。共用的那份要求带引号的键（避免值恰好等于字段名时误命中）；两个调用点都因此补上引号，
  并由 `JsonStringFieldTest` 钉住"裸键取不到"与"`[]` 不等于键不在"。
- **D-1 修复**：取证样本摘要恒为空（宿主另声明了一个从未赋值的局部 `digest`），导致
  `creator_preview_evidence` 在真机必然失败却自述可执行。改为 `MakeRenderedEvidenceSample` 由构造保证
  摘要绑定，宿主侧只提供帧序号与时间。补 `RenderEvidenceSampleBindingTest`（32 项）与
  `tests/creator-evidence-digest-binding.mjs`（宿主调用点闸门，macOS 上此前无任何门覆盖）。
- **验证**：`scripts/run-pure-logic-tests.sh` 33 个目标全通过；mingw 交叉语法门 0 真实错误；
  16 道仓库门 + 4 道相关 node 契约门全通过。新增/修改的测试都做了变异检测（把摘要绑回未赋值变量、
  把仅声明标成可执行、改动组件 kind 名字、数组取值器吞掉非字符串元素 —— 四种都会红）。
- **文档**：更新 CAPABILITY_EVIDENCE_LEDGER、TODO 第 11 节证据栏、持续开发面板、包契约的 capability 表与
  `content-package-basics` Skill（此前它说"别的一律无效"而不说它们现在会被拒绝，也未提
  `theme.wallpaper`）。FEATURE_CHANGELOG 同步记录用户可感知变化。

### 2026-10-03 · 执行记录：PRO-01 能力与证据台账

本条记录**核对结论与登记缺陷**，不代表相关能力已完成或已修复。

- **PRO-01 台账**：新增 `docs/CAPABILITY_EVIDENCE_LEDGER.md`，把"可声明 / 可运行(D2D、D3D11) / 可预览 / AI 可教学 / 真机已验"分开登记。核对基点 `d02812d6`；本机跑了 mingw 交叉语法门（0 真实错误）与 31 个纯逻辑目标（全部通过）；Windows 效果全部未取证。
- **事实校正**：Scene 10 类组件 kind 中 7 类"声明即通过校验但零像素"；D2D 静默忽略 `postProcesses` 与模拟粒子；D3D11 只画第一个 sprite 且无文字；`MiaoAudio*` HLSL 常量无赋值方；16 段频谱算得出但从未发布。`native:today-tasks` 预设是硬编码假待办。创作者会话没有专用 systemPrompt。
- **已登记缺陷（D-1～D-5）**：D-1 取证样本摘要恒空（宿主侧，已在本轮修复前登记）；D-2 原生待办假数据；D-3 未知 capability ID 只做字符集校验；D-4/D-5 声明与运行不一致且无门禁。关闭归属 CAP-01 / WPRO-04。
- **状态**：PRO-01 在面板改为 🟡（自动检查完成，真机签收缺失，不得 Done）；PRO-02/03 需人工选样与实机，未启动。

### 2026-10-03 · 规划变更：专业级动态桌面与 AI 创作

本条记录**目标、范围与验收口径变化**，不代表下列能力已上线；本次没有修改产品代码或提升版本号。

- **目标升级**：按项目所有者要求，Wallpaper Engine 级动态壁纸、macOS 级组件能力、专业 AI 壁纸/组件创作成为完整专业版的必达条件。原稳定基础版 RC 与专业版分别验收。
- **新增总体规划**：`docs/PROFESSIONAL_DESKTOP_PLAN.md` 定义 S0～S7、壁纸/组件基础能力、AI 能力知识/制作闭环、代表作品、保留评测集、质量/性能/成本门与交接规则。
- **新增对标基准**：建立 `docs/MACOS_WIDGET_BENCHMARK.md`；重写 `docs/WALLPAPER_ENGINE_BENCHMARK.md`，加入官方来源、可复现对照方法与旧 G1～G9 映射。
- **扩展任务**：TODO 第 11 节新增 36 项 PRO/CAP/WALL/WPRO/AIP/ADV 规格（依赖、实施、交付、验收、环境、证据）；持续开发面板第 14 节维护唯一状态。保留原稳定性、搜索、AI、配置、Harness 和发布任务。
- **底层与 AI 同步**：每项新能力同时交付运行时/包表达、能力目录、Skill/配方/样例、真实预览和评测；区分“可声明、可运行、可预览、AI 可创作、真机已验”。
- **范围校正**：3D、灯光、骨骼/形变、受限物理与程序化视觉纳入 S6；旧大型编辑器限制不再排除这些表达能力。默认 AI 产物边界保持当前支持范围，高级 Shader authoring 由 ADV-04 同步修改契约与实现后放行。
- **事实校正**：官方三壁纸已用 scene.json，Scene 指针/WASAPI 与 Web 音频推帧实现已存在；DESK-01/03/04/05 改为核验已有实现，未标真机完成。旧百分比只保留为原基础范围快照。
- **文档一致性**：同步 PRODUCT_VISION、DESIGN_BASELINE、DEVELOPMENT_ROADMAP、CONTENT_CREATOR_AGENT_PLAN、Content Framework/Scene 契约、AI sandbox、README、DOC-INDEX 和 FEATURE_CHANGELOG 的规划入口。
- **后续执行**：首项 PRO-01 核实能力与证据；继续 P0-03/04、P0-07/08/09；CAP-01/02 可在真机等待期间推进。下次规划调整必须在本文件记录；实际行为变化再写 FEATURE_CHANGELOG。
- **核对基点**：`d02812d63a2dbf480c4cb00faffb4f69a38b3bc4`。本次仅文档校验，不新增 Windows 或专业质量达标声明。
- **文档验证**：现有四项文档引用/符号/撤回断言检查、差异格式检查通过；17 份新增/修改文档的 59 个本地链接/锚点、36 项任务规格与面板映射通过，新任务依赖图无环。

以下历史开发条目保留过程证据，可能同时含“当时发现缺口”和“后来已修复”。当前能力以本次总体规划第 2 节、代码与持续开发面板为准，不将旧阶段描述当成当前缺陷。

### RC 准备状态

- 内置 MiaoCloud / NeonCity / MysticMoon 已全部以 `scene.json` 为正式运行入口，运行包不再包含 `legacy_entry` / `scene.ini`。
- D2D / D3D11 已共享解析粒子字段实现，并分别有像素级 / GPU readback 回归证据。
- Wallpaper / Widget 管理页已接入 Skill 浏览器与 AI 内容创作入口；Skill 与 AI 共用 `content_skill_get` 源。
- Windows 视觉验收采集器已覆盖截图、surface readiness、mixed-DPI / 横竖屏拓扑与 off-monitor 警告。
- Windows 性能基线采集器已覆盖 CPU、Working Set、Private Memory、Handles、Threads、进程数和 best-effort GPU，并支持回归阈值比较。
- AI 路径已具备独立 OpenAI-compatible image endpoint/model/credential、实际 Pi image-provider 探针、L1 fast→primary fallback router、route/fallback metrics 和 DGX A/B evaluator。
- 正式包禁止未经审核的本地模型权重进入 staging。
- RC exact-SHA gate 已加入：x64 Build / x64 Package / x64 MSIX / Repo Hygiene / ARM64 Package 必须来自同一目标 SHA 才能算通过。

### 新增

- **Today Tasks 待办组件（P0-3 已完成，commit `bca7f9b`）** — 设计基线 §5.1 三款内置 Widget 中最后一块未内容化的部分。此前该整套(9 个文件,25 提交)在未合入分支上且远端分支已被我误删,内容完整保存在本地引用与 bundle 中,现已合入主干,三款内置 Widget 齐备。
  含持久化任务存储（`TodayTaskStore`）、任务编辑器对话框（`TodayTaskEditorDialog`）、
  Content 数据提供者（`TodayTaskContentProvider`）、声明式 Scene 与外观参数。
  同时补上 Phase 2 中"Tasks 无变化不重绘"这一项。
  来源：`feat/content-widget-settings`（25 个提交，9 个新文件）
- **壁纸主题 Unicode / 规范化身份体系** — Content Framework 第一阶段第 10 项缺失的壁纸 dogfood。
  新增 `UnicodeProfileFile` 统一 Unicode 配置文件读写；内置主题规范 ID 与安全别名解析；
  MiaoCloud / MysticMoon / NeonCity 三份官方壁纸的 `scene.json`；
  5 个 canonical derived-view 校验脚本及对应 CI 门禁。
  来源：`fix/unicode-wallpaper-theme-packages`（78 个提交，21 个新文件）

### 工程与 CI

- **Content Widget 独立 runtime host 校验** — 在独立运行时宿主中验证 Content 界面，不再依赖完整桌面宿主。
  来源：`fix/content-widget-install-runtime-reload`（16 个提交）

### 文档

- **本地 AI 架构** — 新增 `docs/LOCAL_AI_ARCHITECTURE.md`：在用户自己的 DGX Spark（GB10 Grace Blackwell，
  128 GB 统一内存 / 273 GB/s 带宽）上用开源模型驱动全部 AI 功能。含：接入本地模型**不需要改产品代码**
  （`PiRuntime::ProviderSetup` 已 provider-neutral，loopback 已免密钥）；推理服务器选 vLLM；
  主模型并行评估 gpt-oss-120b 与 Qwen3.6-35B-A3B 两个候选（前者有实测吞吐，后者纸面占优但无实测），
  以 §8 验收清单定夺；按任务切换模型的模型路由设计（L1 网关分流 + L2 多 provider）。图像模型选型(商用许可是硬过滤器,主选 Z-Image-Turbo)。
- **`image_generate` 本地化（P0-2，方案 A 已实施）** — provider 与 model 从硬编码改为环境变量
  `MIAODESK_IMAGE_PROVIDER` / `MIAODESK_IMAGE_MODEL`，provider 取 profile 已推导的 `providerId`，
  不另造名称表。**未配置 provider 时明确报错，不静默回落到用户没选过的云端**；key 解析重写为
  "专用 image key 优先 → loopback 免密钥 → 复用主 key"，其中 loopback 免密钥与 profile 自身
  对 loopback 的处理一致，这是全本地跑起来的关键。`ApiRuntimeProfile` 增读 `imageModel`，
  `ModelConfig` 纳入 `ReloadConfig()` 变更检测，session `signature` 纳入 `img=provider:model`——
  否则改配置不会重启 Pi 会话，修复会静默失效。原"必须先确认 `getImageModel` 支持哪些 provider
  字符串"这个前置条件随之消失：provider 由用户 profile 决定，产品只透传，不维护白名单。
  验证：`tests/image-provider.mjs` 从 `.cpp` 原始字符串抽取真实逻辑（非手抄副本）跑 28 项断言，
  覆盖 loopback 这条原先必然失效的路径。抽取器最初漏了 import，使 `currentMiaoDeskBaseUrl`
  的 catch 吞掉 ReferenceError 并返回空串，表现与"未配置 profile"完全一致，已修复并注明。

- **阻塞级发现：图片生成在本地模式下必然失效** — `src/ai/pi/PiNativeToolsExtension.cpp` 把
  `image_generate` 硬编码到 `getImageModel("openrouter", "google/gemini-2.5-flash-image")`，
  且凭据只在 baseUrl 含 `openrouter.ai` 时才复用主 key。baseUrl 指向本地推理服务时该工具直接抛错
  "需要 OpenRouter API Key"。这是"全本地 AI"的唯一硬缺口，必须改产品代码（§7.5 列了三个方案）。
  同时记录两个既有缺陷：`models.json` 的 `contextWindow: 128000` / `maxTokens: 16384` 是硬编码常量，
  不随用户所选模型变化。
- **第三方声明补充本地推理栈** — `THIRD-PARTY-NOTICES.md` 追加 vLLM、gpt-oss、Qwen3.6-35B-A3B、
  Qwen3-4B、Qwen3-Embedding、ComfyUI、Z-Image-Turbo、Qwen-Image 的许可证结论,并单列"明确排除的组件"
  清单(FLUX.1 [dev] / FLUX.2-dev / SD 3.5 Large / Qwen-Image-2.1 / HunyuanImage-3.0 / GLM 系列 /
  Llama 4),逐条写明排除的许可原因。
- **本地 AI 部署手册** — 新增 `docs/LOCAL_AI_DEPLOYMENT.md`：vLLM 容器与模型拉取/校验、局域网访问控制、
  Windows 客户端 profile 配置、上线前验收清单、故障排查、升级回滚。
- **本地 AI 隐私模式** — `docs/privacy-policy.md` 从"单一云端服务商"扩展为两种模式：
  模式 A 云端服务商（原有行为不变），模式 B 本地 / 局域网推理（Base URL 指向用户自己的
  DGX Spark / 工作站，对话不离开用户自己的网络）。中英文双语同步修订，
  Microsoft Store 数据声明同步覆盖本地终点这一类别。
- **确立产品愿景层** — 新增 `docs/PRODUCT_VISION.md`：MiaoDesk 是一个漂亮的智能桌面，用户感知到三个界面
  （搜索框 / 动态桌面 / DeepSeek Harness 工作台），所有功能以此为设计基准。
  `docs/DOC-INDEX.md` 与 `README.md` 同步改为三层结构（愿景 → 设计基线 → 开发路线）。
- **技术契约反偏移机制** — 修复 `src/content/`（23 个文件、9 个子域）与 `src/tests/` 未登记进
  `NATIVE_SOURCE_LAYOUT.md` 和 path layout contract 的 canonical 域列表的问题；
  在 `scripts/verify-path-layout-contract.ps1` 增加反向守卫：`src/` 下任何未登记或未写入文档的顶层目录
  都会让 CI 失败，从机制上阻止技术契约再次落后于代码。
  同时为 `DESKTOP_DOMAIN_ARCHITECTURE.md` 补上整个 Content Framework domain 的边界说明。
- **建立 CHANGELOG** — 本文件。此前项目没有任何变更记录，开发过程只存在于 git log。
- **版本号对齐** — `packaging/windows/installer.nsi` 的 `PRODUCT_VERSION` 由 `0.1.2` 提升到 `0.1.3`，
  与两个 MSIX workflow 已断言的 `0.1.3.0` 一致，消除版本漂移。
- **Web 壁纸音频监听 API（B-6）** — `WebDesktopSurfaceChild.cpp` 此前**完全没有 JS 桥**
(无 postMessage、无 host object、无 WebMessage 处理),手工 Web 壁纸拿不到任何宿主数据。
新增 `WallpaperWebAudioBridge.js`:`window.wallpaper.registerAudioListener(fn)` → 退订函数,
帧形状与 Scene 侧 `AudioSpectrumAnalyzer` 输出一致,作者只需记一套。
**单向且闭集**:host→page 只推音频帧,page→host 什么都调不了 —— 这不是 WebView2 的限制,
而是安全姿态(web surface 本就拒绝导航、DevTools、上下文菜单、新窗口和全部权限请求,
开一个可调用的宿主面等于把这些全部作废)。`chrome.webview` 只是传输层,shim 是它前面的稳定 API。
shim 幂等(`AddScriptToExecuteOnDocumentCreated` 每次导航和每个 iframe 都会跑)。
shim 存在两份(js 源真相 + cpp 逐字节副本),`scripts/verify-web-audio-bridge.ps1` 强制一致
并额外禁止 page→host 调用;`tests/WebAudioBridge.mjs` 13 组断言直接读 js 源文件。
测试抓到两个真实缺陷:NaN 处理是永不触发的死代码(读起来像校验实际是强转),
以及一条合法帧(带未知多余字段)被错放进"应丢弃"组。宿主尚未推送帧,依赖 B-2 的 WASAPI 采集。
- **3D 场景声明层（B-4）** — 新增 `SceneSpatialMode{TwoD, ThreeD}`(**不复用 `RuntimeProfile`**,
  后者已承担"壁纸语义 vs 组件语义",再塞 2D/3D 会让两个概念互相遮蔽)、
  `LightType{Point, Spot, Tube, Directional}` + `LightDefinition`(锥角用余弦而非角度,
  上限 12 盏与 Wallpaper Engine 文档一致)、`FogMode{Linear, Exponential}` + `FogDefinition`、
  mesh 资产扩展名限定 `.obj` / `.fbx`。3D 门禁:`lights`/`fog` 非空而 spatial 不是 3D 直接校验失败——
  接受后静默丢弃会让作者反复问"为什么灯不亮"。**刻意不碰渲染器**;契约先落地是为了让渲染器
  有明确靶子。skill 同步禁止生成 3D 内容并要求"用户要 3D 时明说暂不支持、给 2D 替代方案,
  不得静默降级"。
- **Scene 输入总线契约与分析内核（B-2 核心）** — 新增 `src/include/miaodesk/MiaoInputBus.h`(纯 C++,
  无 Windows 依赖):定义全部输入通道的 id / 类型 / 范围 / 是否要求关闭 click-through,
  形状分 `Float01` / `BoolState` / `BoolEdge` 三种(这个区分是必需的——把
  `input://audio/beat` 当电平用会让每帧都变成上升沿);`AudioSpectrumAnalyzer`
  (radix-2 FFT + Hann 窗 → 5 个命名频段 + 16 个对数频谱桶 + 总电平 + 节拍检测,
  非对称缓动、固定分配);`PointerNormalizer`(物理像素 → 所在显示器归一化 [0,1],
  **不用屏幕坐标**,壁纸不得知道桌面布局);`src/content/input/MiaoAudioCapture.cpp`
  多声道交织 PCM 下混(**取平均而非取左声道**,否则居中立体声的低音被砍半)+ 线性重采样 +
  按窗口喂给,半窗口不补零(静音会被当成真静音);
  `MiaoInputBusPublisher.h` 只写 scene 声明过的通道,`interactive=false` 时扣留
  按压通道而非假造 false。click-through 分层定清:指针位置 / 区域内 / 进入离开
  不抢输入、默认允许;按下 / 点击必须 opt-in。**WASAPI 采集与宿主接线未做**。
- **绑定响应曲线(B-3 安全子集)** — 绑定原先只有线性 `*scale + offset`,这会让音频和
  指针输入显得机械。新增闭集 `BindingResponse` 8 条曲线(linear/square/cube/sqrt/
  smoothstep/elastic/threshold/invert)+ `deadzone`,默认 Linear 逐位复现旧行为。
  **刻意不做通用脚本解释器**:闭集内每个成员都是单浮点纯函数,绑定永远无法获得副作用、
  文件访问或无界运行时,"AI 只产声明式内容"的硬规则因此继续成立。通用解释器已记录
  延后理由与重新评估的触发条件。
- **壁纸内容分类与 media 包校验修正（B-5）** — `WallpaperPackageType` 一直就有
  Image/Video/Web/Scene 四值且视频壁纸本就可用,我此前判断"没通"是错的;
  真实缺口是 `Validate` 只对 Web 校验 entry 扩展名,`type: video` + `entry: foo.txt`
  能通过校验、之后在库里才失败且没有可用诊断。已按同款规则补上 Image / Video 校验
  (扩展名集与 `WallpaperLibrary::InferKind` 一致),并新增 `CreateImage` / `CreateVideo`
  提供确定性生成路径。连带修掉资产名只清洗 stem 不清洗 extension 的隐患。
- **内容创作 skill 接入产品（B-1）** — `skills/` 下有 4 份 `SKILL.md` 但 `src/` 零引用,
  创作链断在最后一环。本轮接通:新增 native tool `content_skill_get` 从
  `<install>/skills/<name>/SKILL.md` 按需读取规范(**不调用不产生 token**);
  `PiRuntime` 的 systemPrompt 增加 skill 索引、无条件安全规则与创作流程;
  `CMakeLists.txt` 安装 `skills/`;`packaging/windows/stage.ps1` 断言 5 个文件并加双向
  一致性守卫(磁盘目录 / C++ 白名单 / stage 期望三方一致,frontmatter `name:` 必须等于
  目录名);对话面板问候语加入创作示例。skill 名走闭集白名单而非路径拼接。
  选择"按需加载"而非"常驻注入":4 份规范合计 9,307 UTF-16 字符,虽塞得进 Windows
  命令行上限(实测 1,969 / 32,767),但常驻意味着每轮对话都付这份 token,
  对 32K 上下文的本地小模型不可接受。同时新增 6 个 Windows CI 测试门。
- **P0-4 并未完成(实测结论,勿误记)** — `fix/unicode-wallpaper-theme-packages`
  给三个官方包加了 `scene.json`,但(1) manifest 同时保留 `legacy_entry=scene.ini`,
  而 `LoadAndValidate` 优先取 `legacy_entry`,实测三个包解析出的 entry 全是
  `scene.ini`,`scene.json` 被遮蔽;(2) 这三份 `scene.json` 是空壳 —— 各只有 1 个 root
  节点、0 资产、0 绑定、0 动画,而同目录 `scene.ini` 描述 5 个 Layer、引用 5 个真实资产。
  §19「至少一个官方 Wallpaper 通过 Content Framework 运行」仍未达成;
  当前状态正是 skill 禁止的「scene.ini 与 scene.json 描述同一个包」。
  剩余工作已按依赖顺序写入 `docs/TODO.md` P0-4。

- **未合入分支已全部处理完毕** — 三支此前不在主干的工作已合并:
  `feat/content-widget-settings`(P0-3,TodayTasks,25 提交)、
  `fix/unicode-wallpaper-theme-packages`(canonical manifest 与 Unicode 主题,78 提交)、
  `fix/content-widget-install-runtime-reload`(16 提交)。
  两条独立历史各自创建 `ContentWidgetPreviewRenderer.cpp` 与
  `ContentWidgetSettingsDialog.cpp`(均非对方祖先),已逐行比对天气发布语句确认
  分支版是正确超集后取分支版;按 add/add 常规做法直接取一侧会静默删掉天气发布。

- **对标 Wallpaper Engine 能力基准（B-8 + 差距清单）** — 新增
  `docs/WALLPAPER_ENGINE_BENCHMARK.md`:官方三类创作类型(Scene 含 2D/3D 子类、Web、Video;
  Application 已因恶意软件风险从 Workshop 移除)逐能力对标,每项带代码证据。
  纠正了此前"Image / Video / Web / Scene 四类"的错误分类——MiaoDesk 实际是
  **载体 × 运行时**两个正交维度,静态图是 Scene 的特例而非独立类型。全仓 7 个文件表述已校正。
- **壁纸交互规则修正（B-2 前置）** — `skills/wallpaper-content/SKILL.md` 原文写死
  "壁纸不接收鼠标输入",与对标目标"鼠标控制壁纸"直接冲突。按 Wallpaper Engine 的
  分层模型改为:指针位置类效果(视差/追随/辉亮)与 click-through 不互斥、默认允许;
  指针按下/点击必须显式 opt-in 且会关掉 click-through。`content-review` 壁纸检查项
  由 5 项增至 15 项。
- **测试暴露并修复的缺陷** — ①`fs::file_size` 的 error_code 重订被传入临时对象,
  无法编译;②卸载清单漏掉产品自有子树(`skills/` 必然残留,`Widgets/` 是同类既有漏洞),
  已补 `RMDir /r` 并把 ARM64 卸载残留检查加宽到 10 项;③`ReadSchema` 错误信息不指明
  是 scene.json 还是 parameters.json;④`BindingResponse::Elastic` 原公式
  `1-e^(-kt)(1+cos(wt))/2` 中 `(1+cos)` 恒非负,永不过冲——是穿着弹性外衣的临界阻尼
  逼近,已换成真正的欠阻尼单位阶跃响应;⑤`kPointerInside` 原被归为 Float01 但与发布器
  写 bool 冲突;⑥资产名净化只清 stem 不清 extension。另修正一处契约违背:为让测试链接
  `NativeTools.cpp` 而用 `target_sources` 复编会绕过 path layout 契约的正则,
  已改为把该文件移入 `MIAODESK_CORE_SOURCES`(单一 owner)。
- **内容创作 skill 接入产品（B-1）** — `skills/` 下有 4 份 `SKILL.md` 但 `src/` 零引用，
  创作链断在最后一环。本轮接通：新增 native tool `content_skill_get` 从 `<install>/skills/<name>/SKILL.md`
  按需读取规范（**不调用不产生 token**）；`PiRuntime` 的 systemPrompt 增加 skill 索引、无条件安全规则
  （AI 不产出 HTML/JS/CSS/shell/可执行、不产 Script、不产 Web 运行时）与创作流程；
  `CMakeLists.txt` 安装 `skills/`；`packaging/windows/stage.ps1` 断言 5 个文件并加双向一致性守卫
  （磁盘目录 / C++ 白名单 / stage 期望三方一致，frontmatter `name:` 必须等于目录名）；
  对话面板问候语加入创作示例，`FriendlyToolName` 增加该工具的友好名。
  选择"按需加载"而非"常驻注入"的理由：4 份规范合计 9,307 UTF-16 字符，
  虽塞得进 Windows 命令行上限（实测 1,969 / 32,767），但常驻意味着每轮对话都付这份 token，
  对 32K 上下文的本地小模型是不可接受的。skill 名走闭集白名单而非路径拼接，
  9 种路径穿越与 17 种畸形 JSON 输入均被拒。38 项逻辑单测通过。
  同时修复两个由测试暴露的缺陷:`fs::file_size` 的 error_code 重载被传入临时对象(无法编译);
  卸载清单漏掉产品自有子树 —— `skills/` 必然残留,`Widgets/` 是同类既有漏洞,
  已补 `RMDir /r` 并把 ARM64 卸载残留检查加宽到 10 项。
  为让 CI 测试能链接 `NativeTools.cpp`,该文件从 `MIAODESK_APP_SOURCES` 移入
  `MIAODESK_CORE_SOURCES`,顺带修正一处契约违背(避免用 `target_sources` 复编成第二个 owner)。
- **对标 Wallpaper Engine 能力基准（B-8 + 差距清单）** — 新增 `docs/WALLPAPER_ENGINE_BENCHMARK.md`：
  官方三类创作类型（Scene 含 2D/3D 子类、Web、Video；Application 已因恶意软件风险从 Workshop 移除）
  逐能力对标，每项带代码证据。纠正了此前"Image / Video / Web / Scene 四类"的错误分类 ——
  MiaoDesk 实际是**载体 × 运行时**两个正交维度（`ContentKind` × `ContentRuntimeKind`），
  静态图是 Scene 的特例而非独立类型。全仓 7 个文件的分类表述已校正。
  给出 9 条差距（G1 音频总线 / G2 输入总线 / G3 Script 解释器 / G4 2D-3D 与灯光 / G5 Video 一类 /
  G6 Web 音频 / G7 编辑器特性 / G8 分发链 / G9 skill 接入）与 5 条明确不做项。
- **壁纸交互规则修正（B-2 前置）** — `skills/wallpaper-content/SKILL.md` 原文写死"壁纸不接收鼠标输入"，
  与对标目标"鼠标控制壁纸"直接冲突。按 Wallpaper Engine 的分层模型改为：
  指针位置类效果（视差 / 追随 / 辉亮）与 click-through **不互斥**，默认允许；
  指针按下 / 点击必须显式 opt-in 且会关掉 click-through。`content-review` 的壁纸检查项由 5 项增至 8 项。
- **修复 `models.json` 硬编码常量（P2-4）** — `contextWindow: 128000` 与 `maxTokens: 16384`
  原先是不随所选模型变化的硬编码常量，用户选 32K 模型时产品会宣称 128K。
  现从 profile INI 的可选键读取，并**纳入 `ReloadConfig()` 的变更检测**（否则改配置不会重启
  Pi 会话，修复会静默失效）；`signature` 纳入 `ctx=` / `max=`。`ParsePositiveUInt` 15 个用例通过。
- **移除不可达的 AI HTML 壁纸生成工具（P3-1）** — `wallpaper_create_web_package` 有完整实现，
  入参含完整 HTML/CSS/JS，直接违反 `AI_GENERATED_DESKTOP_SANDBOX.md` 的硬规则。它被三道闸独立阻断
  因而不可达，但保留即是一个潜在风险。已删除实现、tool schema、dispatch 与 UI 友好名；
  helper 均已核实有其他调用方。

---

## [0.1.3] - 2026-09-12

对应 `main` 上 `66a1371..cf2e6c3` 共 57 个提交（23 feat / 17 fix / 7 ci / 4 test / 2 chore），PR #31–#56。

### 新增

#### MiaoDesk Content Framework — 模型与包契约

- 内容框架模型落地：`ContentDefinition` / `ContentInstance` / `ParameterSchema` / `ParameterValues`
- 包参数 schema 加载进 `ContentDefinition`
- `.mdwidget` 内容源稳定解析
- Content Widget 持久化 kind
- Content Widget 来源持久化
- 稳定的 Content Widget 创建 API

#### Native Scene Runtime 与数据绑定

- 受护栏保护的 `time.*` 数据绑定 MVP
- Scene TextRenderer 渲染，带时间绑定
- Scene 圆角精灵渲染

#### GlassClock 内容化 — 第一份官方 dogfood

- GlassClock 成为官方 `.mdwidget` 包（manifest + parameters + scene）
- 通过 Content 场景渲染器自跑，验证官方内容与用户内容共享同一框架
- 场景玻璃深度改进

#### GlassClock 外观参数化

- 强调色参数化
- 强调色灯光细化
- 强调色强度参数化
- Scene 灯光层柔化

#### Content Widget 宿主与设置

- 暂存的 Content GlassClock 经 WidgetHost 路由
- 库内暴露 Content Widget 设置与实时预览
- 被替换的 Content Widget 热重载

#### 内容包托管生命周期

- 托管内容包生命周期（#42）
- 内容包替换状态明确化（#44）
- 旧 Scene 壁纸身份迁移（#43）

#### Web 壁纸接入 Content 运行时

- Content Web 壁纸运行时解析（#54）

### 修复

#### Content Web 显示器分配（PR #55 / #56）

- 允许 Content Web 显示器分配与全局应用
- 按稳定 ID 恢复 Content Web 选择
- 卸载时清理 Content Web 标记与过期选择
- Content Web 恢复无效时回退
- 原子解析 Content 显示器壁纸
- 跟随当前 Content 运行时做显示器分配
- 卸载时保留全局 Content Web 选择

#### 内容包维护

- 目录刷新期间维护 Content 根（#53）
- 安全回收过期 Content staging（#49）
- 机会性清理过期 Content trash（#48）
- Content 包变更后重载壁纸库（#46）
- 宿主路由就绪前保持新 Content Widget 停用

#### 打包与运行时

- 对齐 x64 包 MSIX 校验契约（#52）
- 补上 Content Widget 创建所需工具
- 首次启动前就绪 Windows 文件搜索服务

### 测试

- Content Web 替换连续性回归
- Content 显示器卸载恢复锁定
- Content 显示器运行时替换连续性锁定

### 工程与 CI

- 证明暂存 GlassClock 内容路由（#31）
- 在 MSIX 暂存中证明 Content Widget 路由
- Content Web 替换连续性测试入 CI
- ARM64 可执行文件与完整包构建链
- ARM64 安装器文件搜索端到端验证
- 清理临时 ARM64 诊断代码

### 设计路线推进

| 阶段 | 状态 |
| --- | --- |
| Phase 0 清理基线 | **完成** — 旧 Editor 路线与第二真相全部退出 |
| Phase 1 Wallpaper 稳定性 | 4 / 5 — 停用回归已有 smoke，缺 reload 幂等断言 |
| Phase 2 Widget 稳定性 | 7 / 8 — **P0-3 geometry 边界已收紧**；TodayTasks 待补 |
| Phase 3 Content Framework | 第一阶段 10 项完成 9 项 — 缺 Scene Wallpaper dogfood |
| Phase 4 工程精简 | 2 / 4 — canonical path 与三 EXE 契约守住 |
| Phase 5 Search / AI | 1 / 5 — Content 参数 Preview 已通，体验打磨未开始 |
| Phase 6 ARM64 | 4 / 4 — 与 x64 等价的 build / package smoke 建立 |
| Scene Engine M0–M10 | M0–M4 完成；M5 Input/Audio、M7 Sandbox、M10 Creator 未开始 |

**P0 状态**

- **P0-1** 真实 Windows 多 DPI / 多显示器视觉闭环 — 未关闭，需真机验证
- **P0-2** 停用回归门禁 — 已有 smoke（`verify-widget-visibility.ps1` 断言 Enabled 1→0→1 与壁纸停用下的组件生命周期）；仍缺"reload 不得重新拉起壁纸"的独立断言
- **P0-3** geometry mutation 边界 — **已收紧**。Create 与 Update 双路径均经 `MiaoContentModel::ValidateInstance` 按 Definition 的 geometry policy 校验（固定尺寸 / min-max / 宽高比 / 归一化边界）；Native preset 由 `WidgetService.cpp:394-397` 直接拒绝越界尺寸

### 验证

- x64 Build / x64 MSIX / Path Layout Contract 三条 CI 链全绿
- `verify-widget-visibility.ps1` 覆盖 Widget 创建 / 停用 / 启用，以及壁纸停用下的完整生命周期
- `PaintReady` 真实呈现标记入 CI，不再以 `enabled=true` 或 HWND 存在判定 Widget 正常

---

## [0.1.0] - 2026-09-03

首个可交付的 Windows 桌面版本。此条目依据清理前的历史与设计文档整理，未经主干提交链核对。

### 新增

- MiaoDesk 桌面底座：Search / AI / Settings 主程序、Wallpaper 运行时、DeepSeek Harness 独立 WebView2 宿主
- 妙喵（MiaoMiao）应用图标，x64 与 ARM64 双架构构建
- Windows ARM64 打包流水线
- 路径安全的构建 / 安装契约：外部短路径 staging，源码树内构建目录一律拒绝
- Runtime V3 统一 DSH 与 Pi Agent workspace，浅层 CMake install staging
- 内置 Native Widget：玻璃时钟 / 今日待办 / 玻璃天气，Direct2D + DIB + premultiplied alpha
- 四类壁纸路径：Image（WIC）/ Video（Media Foundation）/ Web（独立 WebView2 Host）/ Scene（Native Direct2D）
- goz/gozd 本地搜索 + Pi Runtime + Bundled Node + Windows Credential Manager 凭据托管

### 工程与 CI

- NSIS 安装器构建与安装 / 卸载验证
- WebView2 SDK vendored，供 x64 / ARM64 使用
- 编译错误以 check annotation 形式浮现
- 路径布局契约在构建前强制执行

---

## 维护约定

1. 每次提升版本号时，把 `[未发布]` 的内容移入新的版本区块，并写上日期。
2. 合并任一分支进 `main` 后，若该分支带来了用户可感知的变化，补一条对应记录。
3. 版本号同时出现在三处，必须一起改，否则 CI 会失败：
   - `packaging/windows/installer.nsi` 的 `PRODUCT_VERSION`
   - `.github/workflows/package-windows-x64-msix.yml` 的 MSIX 版本断言
   - `.github/workflows/package-windows-x64.yml` 的 MSIX 版本断言
4. `Added` / `Changed` / `Fixed` / `Security` 面向使用者书写；`设计路线推进` 与 `P0 状态` 面向维护者，随版本快照一份。
5. 技术契约随代码同步：新增或移除一个 `src/` 顶层 source domain 时，必须同一改动更新 `docs/NATIVE_SOURCE_LAYOUT.md`、`docs/DESKTOP_DOMAIN_ARCHITECTURE.md` 与 `scripts/verify-path-layout-contract.ps1` 的 canonical 域列表。CI 会反向检查，漏改即失败。
