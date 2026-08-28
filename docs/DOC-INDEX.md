# TuringDesk 文档索引

- Status: normative index
- Date: 2026-08-29

本文是全部文档的**唯一索引**。新增、删除或改名的文档必须同步更新这里。

每篇文档必须能在下表中找到自己的角色。不在表中的文档视为孤儿，应当删除或登记。

---

## 1. 权威链

```
TURINGDESK-PRODUCT-BASELINE.md      <- 唯一产品基线，冲突时以其为准
  |- L3-PI-RUNTIME-CONTRACT.md      <- AI 架构强制契约
  |- TURINGDESK-NATIVE-TECH-BASELINE.md
  |- NATIVE_SOURCE_LAYOUT.md
  |- DESKTOP_DOMAIN_ARCHITECTURE.md
  |- DESKTOP_COMPOSITION_ARCHITECTURE.md
  |- WALLPAPER_ENGINE_PARITY.md
  |- STORE_DEMO_V0.1_PLAN.md
  '- DOC-INDEX.md                   <- 本文
```

---

## 2. 产品与技术基线

| 文档 | 角色 | 日期 |
|---|---|---|
| `TURINGDESK-PRODUCT-BASELINE.md` | 唯一产品基线，按八条原则组织 | 2026-08-29 |
| `TURINGDESK-NATIVE-TECH-BASELINE.md` | Native 技术基线 | — |
| `L3-PI-RUNTIME-CONTRACT.md` | AI 架构强制契约 | — |
| `NATIVE_SOURCE_LAYOUT.md` | 源码布局规范 | 2026-08-25 |
| `STORE_DEMO_V0.1_PLAN.md` | 当前 demo 交付范围 | — |
| `STORE_DEMO_V0.1_ACCEPTANCE.md` | demo 验收步骤 | — |

## 3. 架构契约

| 文档 | 角色 | 日期 |
|---|---|---|
| `DESKTOP_DOMAIN_ARCHITECTURE.md` | 领域服务与状态契约（规范） | 2026-08-25 |
| `DESKTOP_COMPOSITION_ARCHITECTURE.md` | 桌面分层与 z-order 契约 | — |
| `AI_GENERATED_DESKTOP_SANDBOX.md` | A2UI 生成与沙盒预览 | — |
| `WALLPAPER_ENGINE_PARITY.md` | 能力清单（从属产品基线） | — |

> `WALLPAPER_ENGINE_PARITY.md` 中超出八条原则的部分，以产品基线第 14 节「明确不在范围内」为准。

## 4. 小组件契约（M3）

| 文档 | 角色 | 日期 |
|---|---|---|
| `WIDGET_PRODUCT_MODEL_M3.md` | 产品模型（规范） | 2026-08-28 |
| `WIDGET_RUNTIME_HEALTH_M3.md` | 运行时健康契约 | 2026-08-26 |
| `WIDGET_PLACEMENT_HEALTH_M3.md` | 放置与拖动契约 | 2026-08-26 |
| `WIDGET_ACCEPTANCE_SEQUENCE_M3.md` | 真机验收序列 | 2026-08-26 |
| `WIDGET_ACCEPTANCE_EVIDENCE_M3.md` | 验收证据契约 | — |
| `WIDGET_INSTALLED_ACCEPTANCE_M3.md` | 安装态验收入口 | 2026-08-27 |

## 5. 视觉与交互

| 文档 | 角色 | 日期 |
|---|---|---|
| `TURINGDESK_GLASS_UI_DESIGN_LANGUAGE.md` | 玻璃拟态设计语言 | — |
| `SEARCH_BAR_VISUAL_SPEC.md` | Search Bar 视觉规范 | — |
| `PI_AGENT_CONVERSATION_UX.md` | 对话体验规范（原则 3） | — |
| `PI_AGENT_ACTIVITY_FEEDBACK.md` | 活动反馈规范（原则 3） | — |
| `DESKTOP_LIBRARY_UI.md` | 桌面库 / 设置中心 UI 契约 | 2026-08-25 |

## 6. Windows 实现参考

| 文档 | 角色 | 日期 |
|---|---|---|
| `LIVELY_WALLPAPER_BEHAVIOR_SPEC.md` | Shell 挂载与 z-order 行为研究 | 2026-08-24 |
| `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` | Windows 壁纸运行时实现参考 | 2026-08-24 |
| `WINDOWS_LAYERED_DIRECT2D_UI_RENDERING.md` | 分层窗口与 Direct2D 渲染 | — |

## 7. 运维

| 文档 | 角色 | 日期 |
|---|---|---|
| `ARM64_ACCEPTANCE_UPDATER.md` | ARM64 真机验收与更新工具 | 2026-08-26 |

---

## 8. 已知待修项

以下文档仍包含已被产品基线推翻的表述，需在后续提交中同步修正。
**修正时必须同时更新引用它们的 guard 脚本**，否则 CI 会红。

| 文档 | 问题 | 依据 |
|---|---|---|
| `WIDGET_RUNTIME_HEALTH_M3.md` | 全篇按 Web Widget 描述，实际展示小组件是 Native | 产品基线 §2.3 |
| `AI_GENERATED_DESKTOP_SANDBOX.md` | 含 WinUI 3 目标架构提案，已被否决 | 产品基线 §2.4 |
| `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` | §13 迁移顺序仍引用已删除的函数 | 产品基线 §6.2 |

### 已修复

| 文档 | 原问题 | 处理 |
|---|---|---|
| `WIDGET_PRODUCT_MODEL_M3.md` | 按 Web Widget 描述 | 已改为 Native 优先，并注明 WebView2 三阶段对 Native 不适用 |
| `WIDGET_ACCEPTANCE_SEQUENCE_M3.md` | config schema 写作 v1 | 已改为 v2 |
| `WIDGET_ACCEPTANCE_EVIDENCE_M3.md` | config schema 写作 v1 | 已改为 v2 |

> 注意：`widget-acceptance-evidence.v1`、`widget-visual-acceptance.v1`、
> `widget-window-evidence.v1` 这三个 schema **合法保持 v1**，与 config schema 的 v2
> 不是冲突，不要一并改掉。

---

## 9. 已删除文档及原因

| 文档 | 删除原因 |
|---|---|
| `DESKTOP_SHELL_RECOVERY.md` | 整篇过期，称 M2 未完成，与已完成的 M2 及守卫矛盾 |
| `DESKTOP_SHELL_M2_MIGRATION.md` | M2 已完成，迁移过程文档 |
| `TURINGDESK-DESKTOP-COMPLETION-PLAN.md` | M4–M13 超出八条原则，是冲突主要来源 |
| `DESKTOP_STATE_CONTRACT.md` | 与 `DESKTOP_DOMAIN_ARCHITECTURE.md` 状态契约重复 |
| `WALLPAPER_SERVICE_LIBRARY_APPLY.md` | 已实现切片，内容已并入产品基线 |
| `DESKTOP_UI_V2_PRODUCTION_CUTOVER.md` | 一次性切换记录，切换已完成 |
| `PI_AGENT_UI_MIGRATION.md` | 迁移已完成；其日志拆分建议未实现且与日志契约冲突 |

---

## 10. 文档与守卫的耦合

**警告**：guard 脚本把源码和文档中的字符串当作契约 marker 断言。修改下列文档前，
必须先确认对应 guard 是否校验了你要改的那段文字。

| Guard | 校验的文档 |
|---|---|
| `verify-l3-runtime-contract.ps1` | PRODUCT-BASELINE、NATIVE-TECH-BASELINE、L3-PI-RUNTIME-CONTRACT、DESKTOP_COMPOSITION_ARCHITECTURE、WALLPAPER_ENGINE_PARITY、AI_GENERATED_DESKTOP_SANDBOX、schemas/a2ui-widget.schema.json |
| `verify-widget-product-model.ps1` | `docs/WIDGET_PRODUCT_MODEL_M3.md` |
| `verify-widget-acceptance-contract.ps1` | WIDGET_RUNTIME_HEALTH_M3、WIDGET_ACCEPTANCE_SEQUENCE_M3、WIDGET_ACCEPTANCE_EVIDENCE_M3、WIDGET_PLACEMENT_HEALTH_M3 |
| `verify-installed-widget-acceptance-contract.ps1` | `docs/WIDGET_INSTALLED_ACCEPTANCE_M3.md` |
| `verify-desktop-domain-contract.ps1` | DESKTOP_DOMAIN_ARCHITECTURE、NATIVE_SOURCE_LAYOUT、WIDGET_PLACEMENT_HEALTH_M3 |
| `verify-native-source-layout.ps1` | `docs/NATIVE_SOURCE_LAYOUT.md` |
| `verify-store-demo-scope.ps1` | STORE_DEMO_V0.1_PLAN、STORE_DEMO_V0.1_ACCEPTANCE |

删除文档前，先在该表中确认它没有被引用的 guard。
