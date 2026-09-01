# MiaoDesk 文档索引

只保留当前仍指导产品、代码或发布链的文档。历史 preview / acceptance / evidence 流程不再作为规范。

## 产品与技术基线

- `MIAODESK-PRODUCT-BASELINE.md` — 产品基线
- `MIAODESK-NATIVE-TECH-BASELINE.md` — Native 技术基线
- `DESKTOP_COMPOSITION_ARCHITECTURE.md` — Windows 桌面组合/层级
- `DESKTOP_DOMAIN_ARCHITECTURE.md` — Desktop domain 边界
- `DESKTOP_LIBRARY_UI.md` — 桌面库 UI
- `WALLPAPER_ENGINE_PARITY.md` — Wallpaper Engine 能力目标
- `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` — Native wallpaper 实现参考
- `LIVELY_WALLPAPER_BEHAVIOR_SPEC.md` — wallpaper 行为规范

## AI / Agent

- `L3-PI-RUNTIME-CONTRACT.md` — Pi-first Agent Runtime 契约
- `PI_AGENT_ACTIVITY_FEEDBACK.md` — Agent 活动反馈
- `PI_AGENT_CONVERSATION_UX.md` — 对话体验
- `AI_GENERATED_DESKTOP_SANDBOX.md` — AI 生成桌面沙箱边界

## 构建与发布

- `PATH_LAYOUT_CONTRACT.md` — 路径、MAX_PATH 与安装布局约束
- `REPOSITORY_RUNTIME_V3_REFACTOR_PLAN.md` — 仓库 / Runtime V3 重构规划
- `ARM64_ACCEPTANCE_UPDATER.md` — 当前 ARM64 updater 说明（迁移期）

正式 Windows x64 打包入口是 `.github/workflows/package-windows-x64.yml`；本地 staging 入口是 `packaging/windows/stage-x64.ps1`。
