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
