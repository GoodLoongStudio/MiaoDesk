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
