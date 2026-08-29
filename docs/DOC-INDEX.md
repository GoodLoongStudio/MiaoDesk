# MiaoDesk 文档索引

- Status: normative index
- Date: 2026-08-29

本文是全部文档的**唯一索引**。新增、删除或改名的文档必须同步更新这里。

每篇文档必须能在下表中找到自己的角色。不在表中的文档视为孤儿，应当删除或登记。

---

## 1. 权威链

```
MIAODESK-PRODUCT-BASELINE.md      <- 唯一产品基线，冲突时以其为准
  |- L3-PI-RUNTIME-CONTRACT.md      <- AI 架构强制契约
  |- MIAODESK-NATIVE-TECH-BASELINE.md
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
| `MIAODESK-PRODUCT-BASELINE.md` | 唯一产品基线，按八条原则组织 | 2026-08-29 |
| `MIAODESK-NATIVE-TECH-BASELINE.md` | Native 技术基线 | — |
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
| `MIAODESK_GLASS_UI_DESIGN_LANGUAGE.md` | 玻璃拟态设计语言 | — |
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

当前无待修项。本表保留作为后续排查的模板。

### 已修复

| 文档 | 原问题 | 处理 |
|---|---|---|
| `WIDGET_PRODUCT_MODEL_M3.md` | 按 Web Widget 描述 | 改为 Native 优先，并注明 WebView2 三阶段对 Native 不适用 |
| `WIDGET_RUNTIME_HEALTH_M3.md` | 全篇按 Web Widget 描述 | 区分 Web / Native 两条生命周期路径，保留验收严格性表述 |
| `WIDGET_ACCEPTANCE_SEQUENCE_M3.md` | config schema 写作 v1 | 已改为 v2 |
| `WIDGET_ACCEPTANCE_EVIDENCE_M3.md` | config schema 写作 v1 | 已改为 v2 |
| `AI_GENERATED_DESKTOP_SANDBOX.md` | 含 WinUI 3 目标架构提案 | 已明确否决，改为 Native C++ / Win32 |
| `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` | §11 屏保、§13 迁移顺序超出范围或已完成 | §11 标注超出范围；§13 标注 1–3 已完成、8–9 超出范围 |
| `README.md` | 日志缺 widget-runtime.log；文档清单与现状不符 | 补齐日志，文档清单改为以 DOC-INDEX 为首 |

> 注意：`widget-acceptance-evidence.v1`、`widget-visual-acceptance.v1`、
> `widget-window-evidence.v1` 这三个 schema **合法保持 v1**，与 config schema 的 v2
> 不是冲突，不要一并改掉。

---

## 9. 已删除文档及原因

| 文档 | 删除原因 |
|---|---|
| `DESKTOP_SHELL_RECOVERY.md` | 整篇过期，称 M2 未完成，与已完成的 M2 及守卫矛盾 |
| `DESKTOP_SHELL_M2_MIGRATION.md` | M2 已完成，迁移过程文档 |
| `MIAODESK-DESKTOP-COMPLETION-PLAN.md` | M4–M13 超出八条原则，是冲突主要来源 |
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

---

## 11. 技术债清单（TD）

以下改动**都必须能在 Windows 上编译验证后才可执行**，在 macOS 上硬做风险大于收益。
按优先级排序；完成后在此表登记。

| ID | 项 | 问题 | 证据 | 建议做法 |
|---|---|---|---|---|
| **TD-1** | 桥接 include 游离于 CMake 视图外 | `WallpaperEngine.cpp`(89KB)、`WallpaperLibraryWindowV2.cpp`(64KB)、`WallpaperAutomationWindow.cpp`(34KB) 被 `#include` 进 Production 包装 TU，不在源列表里。IDE/clangd 跳转失效、静态工具误判为死文件、文本搜索不可靠 | `WallpaperEngineProduction.cpp:190`、`CMakeLists.txt:73,160` | 把正文文件直接列进 `MIAODESK_WALLPAPER_SOURCES`，宏重定向移进正文或改为显式调用 |
| **TD-2** | `MessageBoxW` 全局劫持 | 仅为拦截一条含占位文案的对话框，用字符串匹配重定向整个 Win32 API | `WallpaperEngineProduction.cpp:167-175` | 正文直接调用新设置页 |
| **TD-3** | UTF-8/UTF-16 转换 15+ 份 TU-local 重复 | 13 文件 15 处各自定义，两种签名混用；另有 16 文件直接内联调 Win32 转换 API | `PiRuntime.cpp:35,44`、`main.cpp:50` 等 | 建共享 `detail/Text.hpp`，参照 `RuntimeLogPaths.h` 的唯一 inline 范式 |
| **TD-4** | `Trim` 6 份同签名重复 | 6 个文件各自实现 `std::wstring Trim(std::wstring)` | `L3Agent.cpp:29`、`WebWallpaperHost.cpp:47` 等 | 并入 TD-3 的共享头 |
| **TD-5** | 向 `namespace std` 注入重载 —— **UB** | 程序代码向 std 添加 `max/clamp` 重载是标准明令禁止的未定义行为，MSVC 当前容忍但升级编译器或开 `/permissive-` 可能崩 | `ConversationPanelCompileCompat.h:9-19` | 调用点改显式 `static_cast<LONG>`；升级编译器前必须处理 |
| **TD-6** | Widget 拖拽逻辑复制粘贴 | `NativeWidgetHost` 与 `WebDesktopSurfaceChild` 的拖拽实现逐行等价（数据成员、`UpdateDrag`/`UpdateWidgetDrag`、`DragProc` 消息分派） | `NativeWidgetHost.cpp:240-251,305-332` vs `WebDesktopSurfaceChild.cpp:445-462` | 抽共享 `WidgetDragHandle` 组件；需真机验证拖拽手感 |
| **TD-7** | 21 个源文件被 2–3 个 target 重复编译 | 4 个 exe 之间无静态库，公共代码各编译一份，构成 ODR 漂移面（TD-3 正是其症状） | `CMakeLists.txt`：`WidgetService.cpp`×3、`DesktopWidgetStore.cpp`×3 等 | 抽 2–3 个 `add_library(... OBJECT)`，exe 只链接库 |
| **TD-8** | `include/miaodesk/` 58 头平铺 | 无按模块子目录，随模块增长难导航 | `include/miaodesk/` | 分 `ai/ desktop/ ui/` 子目录；改动面大，不急 |
| **TD-9** | SelfTest 内嵌产品二进制 | `RunNativeSelfTest()` 在 `main.cpp` 串联各模块 SelfTest，无法独立运行、无断言框架 | `main.cpp:208-225` | 抽独立 `MiaoDeskSelfTest.exe`，不进产品体积。**不建议**为此引入 GoogleTest |

### 已完成

| ID | 项 | 处理 |
|---|---|---|
| — | `NativeWidgetPreset.cpp` 在 WALLPAPER target 重复列出 | 已删其一 |
| — | `ModelSettingsWindow.cpp/.h` 死入口（零调用方） | 已删除并移出构建 |
| — | 缺 `.clang-format` / `.clang-tidy` / `.editorconfig` / `.gitattributes` / `CMakePresets.json` | 已补齐 |
| — | `.gitignore` 缺 `build/`，且含已不存在的 `src/MiaoDesk.Desktop/`（.NET 旧布局）条目 | 已补 `build/`、清除陈旧条目 |
| — | `.gitignore` 中 `bin/` `obj/` `dist/` 等为 .NET/Node 遗留 | 保留（无害），待后续按需精简 |

### 明确不做

| 项 | 原因 |
|---|---|
| 引入 GoogleTest/Catch2 | Win32/Shell 逻辑大部分依赖真机环境，纯单元测试价值有限；现有 `SelfTest()` 已覆盖契约 |
| 引入 vcpkg / FetchContent | 项目刻意零第三方 C++ 依赖并锁死运行时（`runtime-lock.json`），引入包管理反而破坏该目标 |
