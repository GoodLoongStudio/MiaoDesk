# MiaoDesk 文档索引

当前只保留一套设计真相与一套开发路线。旧 Wallpaper/Scene/Widget Editor、M3 阶段文档和 Wallpaper Engine parity 路线均已退出当前基线。

## 唯一基线

- `DESIGN_BASELINE.md` — 当前唯一设计基线
- `DEVELOPMENT_ROADMAP.md` — 当前唯一开发基线与开发路线

任何其他文档与这两份冲突时，以这两份为准。

## 当前技术契约

- `NATIVE_SOURCE_LAYOUT.md` — 当前源码目录与 ownership
- `DESKTOP_DOMAIN_ARCHITECTURE.md` — Desktop domain/service 边界
- `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` — Windows Shell / wallpaper 行为研究参考，不是产品路线
- `LIVELY_WALLPAPER_BEHAVIOR_SPEC.md` — Windows wallpaper 行为研究参考，不是功能清单
- `MIAODESK_GLASS_UI_DESIGN_LANGUAGE.md` — Native UI 视觉语言
- `SEARCH_BAR_VISUAL_SPEC.md` — Search Bar 视觉规范
- `WINDOWS_CUSTOM_INPUT_IME.md` — Windows 自定义输入/IME
- `WINDOWS_LAYERED_DIRECT2D_UI_RENDERING.md` — Layered Window + Direct2D 渲染
- `PATH_LAYOUT_CONTRACT.md` — 路径、MAX_PATH 与安装布局约束

## AI / Agent

- `L3-PI-RUNTIME-CONTRACT.md` — Pi-first Agent Runtime 契约
- `PI_AGENT_ACTIVITY_FEEDBACK.md` — Agent 活动反馈
- `PI_AGENT_CONVERSATION_UX.md` — 对话体验
- `AI_GENERATED_DESKTOP_SANDBOX.md` — AI 预览/沙箱边界

## 发布入口

正式 Windows x64 打包：

```text
.github/workflows/package-windows-x64.yml
```

本地 staging：

```powershell
packaging/windows/stage.ps1 -Architecture x64
```

正式交付只进入 `main`。
