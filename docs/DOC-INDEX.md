# MiaoDesk 文档索引

当前只保留一套设计真相与一套开发路线。旧 Wallpaper/Scene/Widget Editor、M3 阶段文档、Wallpaper Engine parity 路线和早期 Lively 对照研究均已退出当前基线。

## 唯一基线

- `DESIGN_BASELINE.md` — 当前唯一设计基线
- `DEVELOPMENT_ROADMAP.md` — 当前唯一开发基线与开发路线

任何其他文档与这两份冲突时，以这两份为准。

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
