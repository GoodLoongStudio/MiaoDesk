# MiaoDesk 产品基线

- Status: 唯一产品基线
- Date: 2026-08-29
- Scope: Windows 11 ARM64 原生桌面（壁纸 + 小组件 + Search + AI）

与本文冲突的任何文档，以本文为准。

本文按 **八条产品原则** 组织。所有设计、实现和验收都必须能追溯到其中一条。

---

## 1. 八条产品原则

| # | 原则 | 含义 | 状态 |
|---|---|---|---|
| 1 | 极致性能，C++ 首选 | 常驻桌面渲染层必须 Native C++；UI 方案成熟；状态切换与异常路径有明确设计 | 已确立 |
| 2 | 应用与文件搜索 | 已实现 | 已完成 |
| 3 | Pi Agent 对话体验 | 交互 UI 与体验对标真人聊天 | 进行中 |
| 4 | DeepSeek Harness | WebView2 套壳宿主 | 已实现 |
| 5 | 壁纸激活与停用 | Image / Video / Web / Scene 四类能力保留，产品面只暴露激活与停用 | 进行中 |
| 6 | 小组件激活、停用与移动 | 桌面拖动。不含缩放、数值 Inspector、显示器重分配 | 进行中 |
| 7 | 桌面模板化（将来） | AI 写出配置文件，预览后由用户 Apply | 规划中 |
| 8 | 小组件模板化（将来） | 同上 | 规划中 |

**当前最紧迫目标**：产出一个可在自媒体上展示的 demo。因此第 5、6 条的范围在 demo 阶段被刻意收窄；第 7、8 条只做协议与沙盒准备，不作为 demo 交付项。

---

## 2. 原则 1 — 性能边界

这一条是架构取舍的最高优先级，并直接裁决了若干历史争议。

### 2.1 常驻渲染层必须 Native

桌面壁纸层、小组件层、Search Bar 属于常驻渲染层，必须：

- Native C++23 + Direct2D / Win32，不引入 Chromium 常驻；
- 低常驻内存，无后台轮询；
- 状态切换（壁纸启停、小组件增删、显示器变化、DPI 变化）必须有明确的状态机与异常路径。

### 2.2 AI 链路允许重资源，但不得进入渲染主路径

Pi Runtime、Bundled Node、WebView2 Harness 属于按需启动的异步链路：

- 允许异步、允许较重、允许独立进程；
- 不允许进入桌面渲染主路径；
- 崩溃或超时必须回退，不得阻塞或拖垮桌面渲染。

### 2.3 因此：小组件以 Native 优先

**裁决**：M3 展示用小组件（玻璃时钟 / 今日待办 / 玻璃天气）是 Native Widget，由 `NativeWidgetHost` 原生绘制，不是 WebView2 Widget。

理由：小组件常驻桌面，属于原则 1 的常驻渲染层。WebView2 只用于按需的复杂 Web 小组件，不作为小组件的默认实现路径。

这意味着：小组件的运行时健康不要求 WebView2 的 Environment / Controller / Navigation 三阶段生命周期遥测。那是 Web 路径的指标，Native 路径使用自身的窗口与可见性状态。

### 2.4 因此：不采用 WinUI 3

主桌面核心使用 C++23 / Win32。历史上曾出现过 WinUI 3 / C++/WinRT 目标架构的提案，本基线明确不予采用。Search UI 不使用 WPF、Electron、CEF、Qt 或 WebView。

---

## 3. 原则 2 — 搜索（已完成）

应用与文件搜索已实现，后端为 `goz.exe`。

- 索引服务 `gozd.exe` 由安装脚本以管理员身份注册为常驻服务，卸载时移除；
- 客户端通过命名管道通信；
- 交互与视觉规范见 `SEARCH_BAR_VISUAL_SPEC.md`。

---

## 4. 原则 3 — Pi Agent 对话体验

### 4.1 固定运行链

```
MiaoDesk
  -> Pi Runtime
  -> Bundled Node 24
  -> @earendil-works/pi-coding-agent --mode rpc
  -> 当前配置 Provider / Model / Base URL / API Key
```

Pi / Node / Provider / Agent Loop 失败时，才回退 **Direct Model** 直连当前配置 API。

API 不绑定模型品牌，路由按 Provider 的协议能力判断。Pi 支持 MiaoDesk 需要的 OpenAI Chat Completions、OpenAI Responses、Anthropic Messages 和 Google Generative AI 等协议。

强制契约见 `L3-PI-RUNTIME-CONTRACT.md`。

### 4.2 体验要求

对话 UI 与体验对标真人聊天：流式输出、真实的忙碌与完成反馈、可中断、错误可恢复。

详细规范见 `PI_AGENT_CONVERSATION_UX.md` 与 `PI_AGENT_ACTIVITY_FEEDBACK.md`；视觉语言见 `MIAODESK_GLASS_UI_DESIGN_LANGUAGE.md`。

### 4.3 工具能力

通用 Agent 能力由 Pi 管理：read / write / edit / grep / find / ls / shell / Skills / Extensions / Pi Packages。

MiaoDesk 只保留真正属于桌面产品的专属工具（设置、壁纸预览、小组件列举、包校验）。详见第 7 节。

---

## 5. 原则 4 — DeepSeek Harness

MiaoDesk 不 fork、不魔改 DeepSeek Harness。后台服务启动必须禁止自动打开外部浏览器：

```
Runtime\Node\node.exe
Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js web --host 127.0.0.1 --port 3080 --no-open
```

Harness UI 只在用户明确点击时由 `MiaoDeskHarness.exe` 的 WebView2 窗口显示。不回退到系统 Node、全局 npm、npx 或在线安装。

---

## 6. 原则 5 与 6 — 壁纸与小组件

### 6.1 分层与 z-order

```
桌面图标（始终最上层，可点击、可选中）
--------------------------------
小组件层（默认 click-through；拖动时启用命中测试）
--------------------------------
壁纸层
--------------------------------
Windows 背景 / WorkerW
```

**不变式 1**：桌面图标始终在小组件之上。
**不变式 2**：壁纸停用后小组件仍可显示，且必须可见，不得被 z 顺序回退吞掉。

### 6.2 壁纸

- 底层宿主与 Shell 挂载唯一由 `DesktopShellHost` 负责（Progman / WorkerW / 0x052C）；
- 支持四类：Image（WIC）、Video（Media Foundation）、Web（WebView2）、Scene（原生 Direct2D）；
- 产品面只暴露激活与停用；
- 多显示器：每台显示器独立控制启停。

Windows Shell 挂载与 z-order 的权威实现参考：`LIVELY_WALLPAPER_BEHAVIOR_SPEC.md` 第 3、5 节与 `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md`。

### 6.3 小组件

- kind 为 Native，由 `NativeWidgetHost` 原生绘制；
- 允许的操作：创建（固定三款）、激活、停用、拖动、删除；
- 不含：缩放、数值化位置/尺寸 Inspector、显示器重分配；
- 尺寸由预设拥有（preset-owned），拖动只持久化归一化 x / y；
- 默认 click-through，不遮挡桌面图标；仅拖动时启用命中测试；
- 运行时健康必须报告真实的窗口、可见性与进程状态，不得把 enabled = true 当作已渲染的证据。

详细契约见 `WIDGET_RUNTIME_HEALTH_M3.md`、`WIDGET_PRODUCT_MODEL_M3.md`、`WIDGET_PLACEMENT_HEALTH_M3.md`。

### 6.4 小组件独立于壁纸

小组件是小组件，壁纸是壁纸。小组件不属于任何壁纸，不随壁纸切换而销毁。

---

## 7. 原则 7 与 8 的前提 — AI 桌面控制边界

模板化（原则 7、8）意味着 AI 会产出配置。配置也是 mutation，因此受同一条边界约束。

### 7.1 预览优先，宿主提交

**不变式**：AI 只能生成预览，永远不能替用户点击 Apply / Reject。本机 Apply 按钮是唯一的提交权威。

```
AI 输出（A2UI JSON / 模板配置）
  -> 沙盒预览（隔离，不入桌面）
  -> 用户点击 Apply
  -> Desktop Control API 提交
```

### 7.2 Pi 工具白名单（只读 + 预览）

Pi 只允许暴露以下桌面工具：

| 工具 | 用途 |
|---|---|
| `settings_open` | 打开设置页 |
| `wallpaper_validate_package` | 校验 .mdwall 包 |
| `wallpaper_state_get` | 读取壁纸状态 |
| `desktop_widget_list` | 列举小组件 |
| `desktop_preview_examples` | 列出预览示例 |
| `desktop_preview_wallpaper` | 生成壁纸预览 |
| `desktop_preview_widget` | 生成小组件预览 |

**明确禁止**向 Pi 暴露：`wallpaper_create_web_package`、`wallpaper_apply_web_package`、`desktop_widget_create_web`、`desktop_widget_update`、`desktop_widget_remove`。

这些内部 mutation API 仍然存在，但只供宿主 Apply 按钮调用。

### 7.3 Desktop Control API

长期目标是版本化的 Desktop Control API（含 wallpaper_apply、widget_create 等），但目前实现的是上表的 Pi 适配工具名。两者不是同一套命名，适配层负责映射，不得假定工具名等于领域 API 名。

---

## 8. 信息架构（统一）

设置中心一级导航（demo 阶段）：

```
壁纸  ·  小组件  ·  妙喵 AI
```

- **壁纸**：缩略图卡片库 + 底部命令栏（激活 / 停用 / 编辑 / 删除）
- **小组件**：三款固定小组件的激活与停用；桌面拖动
- **妙喵 AI**：Provider / Model / Base URL / API Key

**约束**：demo 阶段只暴露上述三项，由 `verify-desktop-ui-parity.ps1` 守卫锁定。
领域枚举 `WallpaperSettingsSection` 保留更多取值以便后续扩展，但不得在 UI 上呈现。
新增第四项（如「设置」）必须同步更新该守卫。

**布局裁决**：采用底部命令栏，不使用常驻的右侧大详情面板。选中项的状态通过卡片选中态与命令栏可用性表达。

实现见 `DESKTOP_LIBRARY_UI.md`。

---

## 9. 品牌与文案

| 层 | 名称 |
|---|---|
| 项目 / 仓库 | MiaoDesk |
| 面向用户的中文产品名 | 妙喵 |
| AI 助手 | 妙喵 AI |
| 高级工作台 | DeepSeek Harness |

用户可见文案统一使用妙喵。不得向用户暴露 Pi、Node、Bundled Runtime、Harness 端口等实现细节。

---

## 10. 展示内容命名（唯一权威）

demo 与预览使用的内置内容名称以此表为准，任何文档、代码、守卫脚本都不得另起别名。

### 壁纸

| 名称 | 展示名 | 预览 key | 场景 id |
|---|---|---|---|
| 妙喵云境 | 妙喵云境 | `aurora_flow` | `scene-aurora` |
| 霓虹之城 | 霓虹之城 | `neon_flow` | `scene-neon` |
| 月影秘境 | 月影秘境 | `ocean_flow` | `scene-grid` |

**一个场景只有一个名字。** 展示名、AI 预览 key、代码字符串、守卫 marker 必须全部一致——
用户在壁纸库看到的、AI 在对话里说的、预览里显示的，都必须是同一个词。

历史遗留名 `ocean_glass` / `Ocean Glass` / `Quiet Grid` 已于 2026-08-29 全部统一为
**月影秘境**；代码、脚本、守卫与文档中不再保留旧名。

### 小组件

| 中文名 | 标识 |
|---|---|
| 玻璃时钟 | `GlassClock` |
| 今日待办 | `TodayTasks` |
| 玻璃天气 | `WeatherGlass` |

A2UI 预览模板是独立的预览命名空间（`focus_clock` / `today_tasks` / `weather_glass` / `system_pulse`），用于 AI 生成预览，与上述用户可见的三款固定小组件不是同一层。

---

## 11. 二进制与运行时

正式平台：Windows 11 ARM64，C++23 / Native Win32，正式交付只从 `main` 构建。

| 可执行文件 | 职责 |
|---|---|
| `MiaoDesk.exe` | Search / AI 对话 / 设置中心 |
| `MiaoDeskWallpaper.exe` | 桌面壁纸引擎 + 小组件运行时 |
| `MiaoDeskHarness.exe` | DeepSeek Harness WebView2 宿主 |

第三方运行时不做现场下载。Node、Pi、DeepSeek Harness、goz 与 WebView2 SDK 均由 `runtime/arm64/` 的固定版本清单管理。

---

## 12. 日志契约

所有 Native 运行日志统一写入用户实际 Windows Desktop known folder：

```
Desktop\MiaoDesk-Logs\l3-runtime.log
Desktop\MiaoDesk-Logs\pi-runtime.log
Desktop\MiaoDesk-Logs\harness.log
Desktop\MiaoDesk-Logs\widget-runtime.log
```

API Key、Token 和其他凭据不得写入日志。新增日志路径必须通过 `RuntimeLogPath()`，否则 `verify-runtime-log-paths.ps1` 会拦截。

---

## 13. 文档权威链

```
MIAODESK-PRODUCT-BASELINE.md          <- 本文，唯一产品基线
  |- L3-PI-RUNTIME-CONTRACT.md          <- AI 架构强制契约
  |- MIAODESK-NATIVE-TECH-BASELINE.md
  |- NATIVE_SOURCE_LAYOUT.md
  |- DESKTOP_DOMAIN_ARCHITECTURE.md     <- 领域服务与状态契约
  |- DESKTOP_COMPOSITION_ARCHITECTURE.md
  |- WALLPAPER_ENGINE_PARITY.md
  |- STORE_DEMO_V0.1_PLAN.md            <- 当前 demo 交付范围
  '- DOC-INDEX.md                       <- 全部文档索引与状态
```

实现参考（Windows Shell / 渲染 / 视觉）：`LIVELY_WALLPAPER_BEHAVIOR_SPEC.md`、
`LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md`、`WINDOWS_LAYERED_DIRECT2D_UI_RENDERING.md`、
`MIAODESK_GLASS_UI_DESIGN_LANGUAGE.md`、`SEARCH_BAR_VISUAL_SPEC.md`。

完整清单与每篇的日期、状态见 `DOC-INDEX.md`。

---

## 14. 明确不在范围内

以下内容不属于当前产品范围，出现在其他文档中一律以本节为准：

- 小组件缩放、数值化位置/尺寸 Inspector、显示器重分配
- 播放列表、壁纸排程（Schedule）、Profile、应用规则、屏保、锁屏集成
- 音频响应、时间轴、粒子、着色器编辑器、3D / Unity 壁纸引擎
- Lively / Wallpaper Engine 完整特性对等
- 小组件多实例、小组件包导入导出
- Steam Workshop、Lively Gallery、非 Windows 平台

这些不是被永久否决，而是超出八条原则的当前范围。

---

## 15. 自动化边界

GitHub Actions 只负责检查、编译、测试、打包和固定第三方 RuntimeBundle；不得通过 workflow 自动改产品源码、设计基线或架构 Guard 后再 push `main`。
