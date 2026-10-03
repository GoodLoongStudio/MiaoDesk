# MiaoDesk 文档索引

当前为产品愿景 → 设计基线 → 开发路线 → 专业版详细规划/技术契约。执行状态只在持续开发面板维护，TODO 保存规格与证据。旧大型 Editor 实现路线退出；2026-10-03 确认的 Wallpaper Engine 级表现力、macOS 级组件与专业 AI 创作正式进入基线。

## 产品愿景

- `PRODUCT_VISION.md` — 产品是什么、用户感受到什么：漂亮的智能桌面，以及用户感知到的三个界面（搜索框 / 动态桌面 / DeepSeek Harness 工作台）。所有功能以此为设计基准。

## 持续开发

- [CONTINUOUS_DEVELOPMENT_BOARD.md](CONTINUOUS_DEVELOPMENT_BOARD.md) — **当前唯一执行队列**：P0/P1/P2、自动推进资格、依赖、完成标准和当前状态。人工与自动开发都从这里领取下一项。
- [FEATURE_CHANGELOG.md](FEATURE_CHANGELOG.md) — 用户可感知的功能 Changelog；持续开发每次产生产品变化时同步更新。
- [TODO.md](TODO.md) — 详细验收、问题分析与历史证据库。保留大量真机条件、诊断过程与专项验收，但不再承担“下一项做什么”的排序职责。
- [历史清单快照](history/TODO_SNAPSHOT_2026-09-27.md) — 完整保留旧 P0/P1/P2/P3/B 编号与历史过程，不作为当前排期或发布结论。

## 专业版总体规划

- [PROFESSIONAL_DESKTOP_PLAN.md](PROFESSIONAL_DESKTOP_PLAN.md) — 三条专业目标、当前事实校正、壁纸/组件能力、AI 知识和制作闭环、S0～S7 阶段、参考作品/保留集/质量/性能门、实施节奏与交接。
- [TODO.md](TODO.md) 第 11 节 — 36 项 PRO/CAP/WALL/WPRO/AIP/ADV 任务的依赖、实施、交付、验收、环境与证据栏；状态只在面板。
- [../CHANGELOG.md](../CHANGELOG.md) — 规划/范围/验收口径变更记录；实际产品行为继续写 FEATURE_CHANGELOG，避免把计划当发布。
- [CAPABILITY_EVIDENCE_LEDGER.md](CAPABILITY_EVIDENCE_LEDGER.md) — PRO-01 能力与证据台账：每项能力分开登记"可声明 / 可运行 / 可预览 / AI 可教学 / 真机已验"五种状态、代码入口与缺口。台账是 CAP-01 生成目录之前的人工核对结论。

## 能力基准

- [WALLPAPER_ENGINE_BENCHMARK.md](WALLPAPER_ENGINE_BENCHMARK.md) — 官方来源、当前能力与缺口、对照方法、G1～G9 旧编号迁移；修正已有接线/推帧/Skill 的过期结论。
- [MACOS_WIDGET_BENCHMARK.md](MACOS_WIDGET_BENCHMARK.md) — Apple 官方依据、Windows 适配、布局/数据/动作/节能/无障碍能力矩阵、固定用户任务与 WPRO 映射。

## 唯一基线

- `DESIGN_BASELINE.md` — 当前唯一设计基线
- `DEVELOPMENT_ROADMAP.md` — 当前唯一开发基线与开发路线

任何其他文档与这两份冲突时，以这两份为准。产品愿景定义目标，不定义实现；若两者冲突根源是愿景本身变化，应连同修订 `PRODUCT_VISION.md`，不允许长期保留互相矛盾的三层描述。

## 技术契约的保鲜机制

技术契约不是历史档案，落后于代码即失效。以下两份由 CI 强制与源码同步：

- `NATIVE_SOURCE_LAYOUT.md` — 物理源码树与 ownership
- `DESKTOP_DOMAIN_ARCHITECTURE.md` — domain 边界与依赖方向

`scripts/verify-path-layout-contract.ps1` 会反向检查：`src/` 下任何未登记为 canonical source domain、或未在 `NATIVE_SOURCE_LAYOUT.md` 中出现的顶层目录，都会让 path layout contract 直接失败。因此新增或移除一个 source domain 时，必须同一改动更新文档与脚本中的域列表。

## 当前技术契约

- `MIAODESK_CONTENT_FRAMEWORK.md` — 下一阶段“配置化 / 参数化”内容框架：统一 Wallpaper / Widget 的 Package、Parameter、Scene Runtime、Capability 与 Authoring 边界
- `MIAO_SCENE_ENGINE.md` — `Miao Scene Engine` 场景/GPU 子系统契约：Scene/Node/Component/Property/Asset、Custom HLSL、Material、Particle、Animation、Input Bus、Render Graph 与 Wallpaper/Widget Runtime Profile；其 Scene/GPU 细节优先于 Content Framework 中较早的简化描述
- `MIAO_SCENE_ENGINE_ROADMAP.md` — Scene/GPU 子系统历史建设分解，当前产品排期见 S0～S7；包括：M0 合同冻结 → M1 Offscreen/Multi-Pass → Post Process → Animation → Particle → Input/Audio → Hot Reload/Preview → Sandbox → Widget 复用 → Dogfood/性能 → AI/Creator
- `MIAO_CONTENT_PACKAGE_V1.md` — 外部内容包与序列化入口契约：`.mdwall/.mdwidget`、manifest、Package Root 路径沙箱、Scene/Parameter/Asset ingress、Slice B 施工顺序与首个外部内容里程碑
- `NATIVE_SOURCE_LAYOUT.md` — 当前源码目录与 ownership
- `DESKTOP_DOMAIN_ARCHITECTURE.md` — Desktop domain/service 边界
- `MIAODESK_GLASS_UI_DESIGN_LANGUAGE.md` — Native UI 视觉语言
- `SEARCH_BAR_VISUAL_SPEC.md` — Search Bar 视觉规范
- `WINDOWS_CUSTOM_INPUT_IME.md` — Windows 自定义输入/IME
- `WINDOWS_LAYERED_DIRECT2D_UI_RENDERING.md` — Layered Window + Direct2D 渲染
- `PATH_LAYOUT_CONTRACT.md` — 路径、MAX_PATH 与安装布局约束
- `LOCAL_PRIVATE_DATA.md` — 本地隐私数据、凭据、签名文件和机器私有配置的仓库边界

## AI / Agent

- [CONTENT_CREATOR_AGENT_PLAN.md](CONTENT_CREATOR_AGENT_PLAN.md) — 方案 2 的专项实施计划：专用创作会话 + 现有 Skills + 真实校验/渲染反馈 + 有界修复。含 CCA-00～14 任务、协议、验收矩阵和可交给其他 AI 的执行说明；当前为待实施方案，不代表功能已完成。
- [CREATOR_AGENT_PI_CAPABILITY_PROBE.md](CREATOR_AGENT_PI_CAPABILITY_PROBE.md) — CCA-01 能力探测记录：锁定 Pi 版本的 RPC/图像/会话/工具 allowlist 实测结论（含"input 不含 image 时图片被静默丢弃"这一决定性事实）。可用 `scripts/probe-pi-rpc-capability.mjs` 复现
- `L3-PI-RUNTIME-CONTRACT.md` — Pi-first Agent Runtime 契约
- `PI_AGENT_ACTIVITY_FEEDBACK.md` — Agent 活动反馈
- `PI_AGENT_CONVERSATION_UX.md` — 对话体验
- `AI_GENERATED_DESKTOP_SANDBOX.md` — AI 壁纸预览/沙箱边界，以及 AI 组件创作边界的更正记录（2026-09-27 撤回“AI 不能生成组件”）。含三张面对照与“工具面无变更路径 ≠ 模型改不动”的精确表述

## 本地 AI（独立扩展架构）

本地推理部署与模型选择独立推进；主产品的通用 Provider、API 配置、对话和创作流程仍由 `TODO.md` 覆盖。

- `LOCAL_AI_ARCHITECTURE.md` — 在 DGX Spark 上用开源模型驱动全部 AI 功能的架构：硬件约束、推理服务器与模型选型、按任务切换模型（模型路由）、安全边界、性能预算。早期硬编码问题已有产品侧修复；真实本地服务部署、模型适配与实测状态需按当前代码和部署记录核对，不能沿用历史发现作为现有缺陷
- `LOCAL_AI_DEPLOYMENT.md` — 部署手册：vLLM 容器、模型拉取与校验、访问控制、客户端 profile、上线前验收清单、故障排查
- `../skills/` — AI 内容创作 skill 集（生成壁纸与组件内容包，含正反提示词与安全/性能门禁）。Scene 壁纸/组件候选必须走当前正式 Content 校验链，不能把旧 Web 壁纸的 `wallpaper_validate_package` 当通用校验器

## 发布入口

正式 Windows x64 打包：

```text
.github/workflows/package-windows-x64.yml
```

Microsoft Store x64 MSIX：

```text
.github/workflows/package-windows-x64-msix.yml
```

本地 staging：

```powershell
packaging/windows/stage.ps1 -Architecture x64
```

正式交付只进入 `main`。
