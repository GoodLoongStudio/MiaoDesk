# MiaoDesk 功能 Changelog

这份文档只记录**用户能感知到的功能与体验变化**。工程重构、纯 CI 调整、代码移动等如果不改变用户体验，不单独记在这里。

版本发布级历史仍看仓库根目录的 `CHANGELOG.md`；本文件用于持续开发期间回答：

> 最近这个软件实际多了什么、变好了什么、修掉了什么？

## 维护规则

每一条用户可感知改动记录：

- 日期
- 类型：Added / Improved / Fixed / Changed / Removed
- 所属界面：Search / Desktop / AI / Creator / Settings / Harness / Packaging
- 用户变化
- 对应 commit 或候选 SHA
- 验证级别：CI / Windows 真机 / 待真机

自动持续开发完成用户可感知任务时，必须同步更新这里。

---

## 专业版规划入口 — 2026-10-03（非功能发布）

项目目标扩展为 Wallpaper Engine 级壁纸、macOS 级组件与专业 AI 创作，详见 [总体规划](PROFESSIONAL_DESKTOP_PLAN.md)。本次变更是文档规划，完整记录在 [根 CHANGELOG](../CHANGELOG.md)；以下 Available 状态仍只描述已有基础功能，不能推导专业目标已达成。

后续用户可感知能力实际交付时，在本文件记任务 ID、实现提交、用户变化与验证级别，并更新持续开发面板。

## 能力目录与创作自述 — 2026-10-03

- **任务**：CAP-01（运行时能力目录与查询）；附带修复 D-1、关闭 D-3。
- **用户可感知变化**：
  1. 创作者会话里问"现在能做什么"，回答不再只列工具名，而会列出可创作的内容能力并分级
     （会执行 / 仅声明 / 该后端不支持）。写进包不产生任何像素的那一类（例如 `textRenderer`
     在 D3D11 上、`asset.font` 的任何后端）会明确说出来，而不是等作者在桌面上自己发现。
  2. `manifest.json` 的 `capabilities` 写了目录里不存在的名字时，包现在**加载失败**并指出是哪一个。
     此前它静悄悄地通过，然后什么也不做 —— 声明 `audio.read` 不会拿到音频，音频走
     `scene.json` 的 `inputs[]`。
  3. 修复：创作流程的"渲染取证"此前在真机上必然失败（每一帧绑定的候选摘要是空的），
     表现为"证据采集总是不通过"但看不出原因。现在摘要由构造保证绑定到当前候选。
- **验证级别**：本机自动检查（33 个纯逻辑目标、16 道仓库门、4 道相关 node 契约门、mingw 交叉
  语法门）全部通过；新增测试均通过变异检测。**Windows 真机未验证** —— 取证链路要等真机跑一次
  才算签收。

## 原生待办组件显示真实待办 — 2026-10-03

- **任务**：D-2（台账登记缺陷）。
- **用户可感知变化**：管理界面里添加的"今日待办"原生组件，此前显示的是三条**写死的示例待办**
  （完成产品设计方案 / 与团队同步项目进度 / 回复客户邮件）和一个恒为 1/3 的进度条，用户在
  "编辑今日待办"里改的内容**不会**出现在桌面的卡片上。现在卡片显示真实待办、真实完成数与
  真实进度；读不到存储时显示"任务数据暂不可用"，而不是假装有 3 项。库里的小预览同样显示真实数据。
- **未变**：仍需从管理界面编辑待办（在卡片上点击还不能直接完成，那是后续任务）；卡片尺寸仍是固定一档。
- **验证级别**：本机自动检查（19 项纯逻辑断言 + 宿主/painter 源码闸门 + 全部仓库门）通过，
  新增测试通过变异检测；**Windows 真机未验证**。

## 待办组件不再沉默截断 — 2026-10-03

- **任务**：D-8（台账登记缺陷）。
- **用户可感知变化**："今日待办"组件顶部显示的是真实待办数（8 条就说"8 项"），
  但下面只画 4 行，而且没有任何地方说明剩下 4 条去哪了 —— 用户会以为组件坏了，
  或者以为自己只加了 4 条。现在多一行"还有 N 项"。原生待办卡片与内容组件也改用同一个
  可见条数，不会再出现两边对不上。
- **验证级别**：本机自动检查（27 项纯逻辑断言 + 发行包包级校验门 + 变异检测）通过；
  **Windows 真机未验证** —— 提供方只能在 Windows 上链接，桌面上的实际显示未签收。

## 校验器不再拒绝随产品发行的内容包 — 2026-10-03

- **任务**：D-6/CAP-03（回归门首次跑出）。
- **用户可感知变化**：内容包的**包级校验**此前要求 `manifest.json` 的 `entry` 以
  `scene/` 开头，而三个内置壁纸、三个内置组件和两个示例包用的都是根下的 `scene.json`。
  照这些合法包的样式写出来的内容，会被判"校验不通过"，提示是"entry 必须指向 scene/ 下的
  一个 .json 文件"——而那句话指向的正是能正常加载的包。现在两种布局都接受；仍然拒绝
  `assets/a.json`、`manifest.json`、`scene/scene.png` 和越出包根的路径。
  这一条此前没有任何自动检查覆盖，因为既有的 `BuiltinWallpaperPackages` 走的是另一条链
  （load → 场景校验 → 初始化），不跑包级校验。
- **验证级别**：本机自动检查（新增发行包包级校验门 + 131 项校验器断言 + 变异检测）通过；
  **Windows 真机未验证**。

## 两条动画写同一个属性现在被拒绝 — 2026-10-03

- **任务**：WALL-03（动画、状态与有界行为）。
- **用户可感知变化**：内容场景里两条动画写同一个属性此前**不报错，也看不到其中一条**：
  先声明的那条从来不出现在桌面上（它连起始值都贡献不了，另一条每帧都把它盖掉）；
  事件触发的那条更糟 —— 它只在触发那一帧赢一帧，之后又被时间线动画盖回去，
  看起来是一次一帧的闪。现在这类内容**加载失败**，错误信息直接点名是哪两条
  （"`animation://a` 与 `animation://b` 都写 `component://…/opacity`"），
  而不是只告诉你是哪个属性。随产品发行的 8 个包逐个确认不触发这一条。
- **没变的一条**：一条绑定（binding）和一条动画写同一个属性**仍然合法**，而且是一个
  自洽的写法 —— 绑定只在场景初始化时跑一次，它给的是起始值，动画一开跑就接管。
  这里要小心的只有一件事：别指望绑定在动画跑起来之后还能压住它，引擎里没有叠加。
- **未变**：`SceneClock`（暂停/恢复不跳变）与参数平滑过渡这两个新能力本轮**还没有接到
  任何界面**，桌面上暂时看不到差别 —— 它们属于播放宿主那一轮，因为要先决定暂停由谁调、
  参数面板失焦后怎么办。循环接缝与一帧瞬移的审查也已就位，但那是对**将来**的保护：
  三个内置壁纸当前没有循环接缝问题。
- **一个已知未修**：`MiaoCloud` 的"眨眼"淡入/淡出各为 1/240 秒，也就是产品支持的最高
  帧率下的一帧，所以那一"淡变"在任何帧率下都渲染不出来，实际看到的是硬切。
  没有改它：改法是拉长淡变，那是改美术，而本机不是 Windows，拉长之后好看不好看
  给不出证据。代码里以登记表的形式盯着这件事。
- **验证级别**：本机自动检查（174 项时间策略断言 + 48 项发行内容审查 + 6 项新的拒绝断言，
  26 处变异全部变红）通过；**Windows 真机未验证**。

## 当前功能基线 — 2026-10-03

基线 SHA：`0b0e986cfed979057f8d16e26233b798391a50f0`

### Search

- **Available** — Native 顶部搜索入口。
- **Available** — 应用搜索。
- **Available** — Goz/gozd 文件搜索。
- **Available** — 搜索结果可进入妙喵 AI。
- **Needs further optimization** — 固定查询集、排序质量、异步取消和异常恢复仍需系统验收。

### Desktop / Wallpaper

- **Available** — Wallpaper 启用/停用与桌面宿主。
- **Available** — Scene / Image / Video / Web 运行路径。
- **Available** — 多显示器基础分配。
- **Available** — Explorer/Shell surface repair 基础能力。
- **Needs device signoff** — 多 DPI、多显示器、Explorer 重启、休眠恢复的最终稳定性。

### Widgets

- **Available** — 玻璃时钟、今日待办、玻璃天气三款 Native Widget。
- **Available** — 创建、启停、删除、拖动位置持久化。
- **Available** — Direct2D / layered surface / PaintReady。
- **Available** — Content Widget 与 Content Framework 路径。
- **Needs device signoff** — 混合 DPI、竖屏、Explorer repair 后的视觉与交互。

### 妙喵 AI

- **Available** — Pi Runtime + bundled Node + Provider profile。
- **Available** — 多 API profile，Provider / Model / Base URL / API Key 配置。
- **Available** — Windows Credential Manager 保存凭据。
- **Available** — 持续多会话：默认续聊、新对话、历史切换、跨程序/系统重启恢复。
- **Available** — General / Wallpaper Creator / Widget Creator 会话隔离。
- **Needs optimization** — Cancel/Retry、活动反馈、长上下文、附件与错误分类。

### AI Content Creator

- **Available** — AI 制作壁纸。
- **Available** — AI 制作组件。
- **Available** — Skills 按需加载。
- **Available** — 生成 → 校验 → 预览 → 显式应用主流程。
- **Available** — Creator 对话历史与独立 workspace。
- **Needs systematic quality testing** — 固定 10+10 生成集、预览/桌面一致性、失败回滚与生成质量数据。

### Content Framework

- **Available** — ContentDefinition / ContentInstance。
- **Available** — ParameterSchema / ParameterValues。
- **Available** — `.mdwall` / `.mdwidget` Package。
- **Available** — Package validator / loader。
- **Available** — Native Scene Runtime MVP 与数据绑定。
- **Available** — Preview / reload / apply 基础链。
- **Needs verification** — 官方 Wallpaper 已迁移 scene.json，Input/Audio host 接线与 Web 音频推帧已有；正式运行、恢复、性能和真机质量仍待验证。
- **Planned** — 专业版能力目录、深层 Scene 表现力、组件平台和 AI 质量门，见总体规划。

### Harness

- **Available** — 独立 DeepSeek Harness WebView2 宿主。
- **Available** — 与主产品共用 Provider 配置基础。
- **Needs optimization** — 冷启动、重连、后台生命周期和故障隔离的系统验收。

### Packaging / Development

- **Available** — x64 build/package/installer。
- **Available** — ARM64 build/package。
- **Available** — ARM64 Fast Dev + sccache。
- **Available** — `ARM64-Quick-Test.cmd` 双击快速测试。
- **Needs decision** — MSIX 下 Goz 文件搜索服务能力边界。

---

## 2026-09-30

### Fixed · Creator

- **AI 制作壁纸 / AI 制作组件不再把 IPC “请求已入队”误报成“窗口已打开”。**
  现在跨进程打开协议区分 ACK、Visible、Ready；只有主进程确认真实 Creator 窗口存在才算成功。
  Commit：`37413e44`、`ac45f13`、`8efc44e`。

- **Creator 窗口先显示，再加载 API、历史和 Skills。**
  配置/历史/Skill 加载慢或异常时，窗口本身仍应先出现，避免用户看到“按钮点了没反应”。
  Commit：`f849bd00`、`50494299`。

- **新增真实跨进程 Creator E2E。**
  Windows CI 启动主 MiaoDesk 后，再用第二进程分别请求 Wallpaper / Widget Creator；窗口未真正出现则构建失败。
  Commit：`e0dd2bb8`、`3406529d`。

### Added · AI

- **妙喵 AI、壁纸 Creator、组件 Creator 支持持久多会话。**
  默认继续上次对话；用户可创建新对话并从历史切回旧会话；退出程序或 Windows 重启后仍可恢复。

- **Creator 历史与作品 workspace 绑定。**
  切回旧创作对话时，同时切回对应内容工作区，避免“聊天是旧作品、实际改到新作品”。

### Improved · Development

- **ARM64 快速开发链改为云端编译 + 小型 Overlay。**
  测试机不需要 CMake/Visual Studio，双击 `ARM64-Quick-Test.cmd` 即可同步、下载、验证并启动 Dev 版本。

- **ARM64 编译接入跨 Runner sccache。**
  热缓存曾实测达到 124/124 cache hit，显著缩短日常 ARM64 编译等待。

---

## 后续记录模板

```markdown
## YYYY-MM-DD

### Added / Improved / Fixed / Changed / Removed · <Surface>

- **一句话说明用户感知变化。**
  说明行为变化、边界和验证级别。
  Commit：`<sha>`
  验证：CI / Windows x64 / Windows ARM64 / Needs device
```
