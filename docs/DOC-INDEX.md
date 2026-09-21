# MiaoDesk 文档索引

当前分三层：产品愿景、一套设计真相、一套开发路线。旧 Wallpaper/Scene/Widget Editor、M3 阶段文档、Wallpaper Engine parity 路线和早期 Lively 对照研究均已退出当前基线。

## 产品愿景

- `PRODUCT_VISION.md` — 产品是什么、用户感受到什么：漂亮的智能桌面，以及用户感知到的三个界面（搜索框 / 动态桌面 / DeepSeek Harness 工作台）。所有功能以此为设计基准。

## 开发 Todo

- `TODO.md` — 活清单:下一步具体做什么、什么还没做,每项带依据与验收标准。按 P0(阻塞发布)/ P1(门的验收)/ P2(本地 AI 落地)/ P3(技术债)分级。`DEVELOPMENT_ROADMAP.md` 是阶段级规划,本清单是任务级执行单。

## 能力基准

- `WALLPAPER_ENGINE_BENCHMARK.md` — 对标 Wallpaper Engine 的能力差距分析。官方三类创作类型(Scene 2D/3D、Web、Video)与 MiaoDesk 现有能力的逐项对照,含代码证据;给出正确的"载体 × 运行时"分类、差异化定位(组件层 / AI 创作 / 本地 AI),以及 9 条差距 + 5 条明确不做项。`TODO.md` 中 `B-x` 编号项的来源。

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
- `MIAO_SCENE_ENGINE_ROADMAP.md` — Phase 3 Scene/GPU Runtime 执行路线：M0 合同冻结 → M1 Offscreen/Multi-Pass → Post Process → Animation → Particle → Input/Audio → Hot Reload/Preview → Sandbox → Widget 复用 → Dogfood/性能 → AI/Creator
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

- `L3-PI-RUNTIME-CONTRACT.md` — Pi-first Agent Runtime 契约
- `PI_AGENT_ACTIVITY_FEEDBACK.md` — Agent 活动反馈
- `PI_AGENT_CONVERSATION_UX.md` — 对话体验
- `AI_GENERATED_DESKTOP_SANDBOX.md` — AI 壁纸预览/沙箱边界

## 本地 AI

- `LOCAL_AI_ARCHITECTURE.md` — 在 DGX Spark 上用开源模型驱动全部 AI 功能的架构：硬件约束、推理服务器与模型选型、按任务切换模型（模型路由）、安全边界、性能预算。**含一项阻塞级发现：`image_generate` 硬编码 OpenRouter + Gemini，本地模式下必然失效（§7.1）**
- `LOCAL_AI_DEPLOYMENT.md` — 部署手册：vLLM 容器、模型拉取与校验、访问控制、客户端 profile、上线前验收清单、故障排查
- `../skills/` — AI 内容创作 skill 集（生成壁纸与组件内容包，含正反提示词与安全/性能门禁）。skill 产出的包目录可直接交 `wallpaper_validate_package` 校验

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
