# MiaoDesk 文档索引

只保留当前仍指导产品、代码或发布链的文档。历史 preview / acceptance / evidence / updater 流程不再作为规范。

## 产品与技术基线

- `MIAODESK-PRODUCT-BASELINE.md` — 产品基线
- `MIAODESK-NATIVE-TECH-BASELINE.md` — Native 技术基线
- `NATIVE_SOURCE_LAYOUT.md` — 当前源码目录与 ownership
- `DESKTOP_COMPOSITION_ARCHITECTURE.md` — Windows 桌面组合/层级
- `DESKTOP_DOMAIN_ARCHITECTURE.md` — Desktop domain 边界
- `DESKTOP_LIBRARY_UI.md` — 桌面库 UI
- `WALLPAPER_ENGINE_PARITY.md` — Wallpaper Engine 能力目标
- `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` — Native wallpaper 实现参考
- `LIVELY_WALLPAPER_BEHAVIOR_SPEC.md` — wallpaper 行为规范
- `MIAODESK_GLASS_UI_DESIGN_LANGUAGE.md` — UI 设计语言
- `SEARCH_BAR_VISUAL_SPEC.md` — Search Bar 视觉规范
- `WINDOWS_CUSTOM_INPUT_IME.md` — Windows 自定义输入/IME
- `WINDOWS_LAYERED_DIRECT2D_UI_RENDERING.md` — Layered Window + Direct2D 渲染

## AI / Agent

- `L3-PI-RUNTIME-CONTRACT.md` — Pi-first Agent Runtime 契约
- `PI_AGENT_ACTIVITY_FEEDBACK.md` — Agent 活动反馈
- `PI_AGENT_CONVERSATION_UX.md` — 对话体验
- `AI_GENERATED_DESKTOP_SANDBOX.md` — AI 生成桌面沙箱边界

## Widget / Desktop

- `WIDGET_PRODUCT_MODEL_M3.md` — Widget 产品模型
- `WIDGET_PLACEMENT_HEALTH_M3.md` — Widget 放置与恢复约束
- `WIDGET_RUNTIME_HEALTH_M3.md` — Widget Runtime 健康约束

## 构建与发布

- `PATH_LAYOUT_CONTRACT.md` — 路径、MAX_PATH 与安装布局约束

正式 Windows x64 打包入口是 `.github/workflows/package-windows-x64.yml`；本地 staging 入口是 `packaging/windows/stage.ps1 -Architecture x64`。
