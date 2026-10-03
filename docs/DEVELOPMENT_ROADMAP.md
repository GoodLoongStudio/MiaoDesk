# MiaoDesk 开发基线与路线

- 状态：当前唯一开发路线
- 建立：2026-09-05；当前目标与执行状态校正：2026-10-03
- 设计基线：`docs/DESIGN_BASELINE.md`
- 正式开发分支：`main`

本文描述当前能力、偏差和阶段顺序。旧 M3 与大型 Editor 实现路线不复活；Wallpaper Engine 级表现力、macOS 级组件和专业 AI 创作按 2026-10-03 新目标正式进入路线。

## 当前执行位置（2026-10-03）

当前“下一项做什么、优先级和自动推进资格”统一看 [CONTINUOUS_DEVELOPMENT_BOARD.md](CONTINUOUS_DEVELOPMENT_BOARD.md)。[TODO.md](TODO.md) 保留详细验收、诊断和历史证据，不再承担执行排序。下文 Phase 0～6 保留原有架构建设分解；当前交付顺序采用本节 S0～S7，已有实现不重做。详细能力/验收见 [PROFESSIONAL_DESKTOP_PLAN.md](PROFESSIONAL_DESKTOP_PLAN.md)，任务规格见 TODO 第 11 节。

- 三款官方 Widget 内容包及三款官方 Wallpaper 的 `scene.json` 正式入口已存在，当前需完成真实桌面使用与视觉验证。
- 内置 Widget 的尺寸修改保护已实现；壁纸停用/reload 已有回归检查，仍需候选版本和真实 Windows 状态验证。
- 壁纸/组件库、AI 创作入口、实时 Scene 预览及播放/暂停/重载/全屏、API 模型下拉与滚动均已进入当前代码；不再列为从零建设任务。
- 性能采集与回归比较工具已具备，缺参考机的可比较数字。ARM64 构建/打包流程已建立，当前 SHA 是否通过仍须单独核实。
- 当前优化执行顺序：准备证据 → 桌面稳定/布局/性能 → 搜索/视觉/组件/管理 → AI/配置/Harness → 发布收口。发布只受当前发布范围内的阻塞项和既有 RC 证据规则约束。
- **本地 AI 属于独立扩展架构**。DGX、推理服务、模型选型和路由部署不成为主产品优化或 RC 的前置条件；通用 Provider 兼容与用户配置仍属于主产品。

### 专业版阶段顺序

| 阶段 | 目标 | 出口与任务 |
| --- | --- | --- |
| S0 | 基线、对标样本、设备与度量 | PRO-01～03；范围与证据冻结 |
| S1 | 可靠基础版 | 原 P0/UX/SEARCH/AI/CREATE/PERF/HAR/REL；真机与同 SHA 发布证据 |
| S2 | 可创作平台 | CAP-01～05；能力目录、知识、包兼容、预览、数据/动作契约 |
| S3 | 专业 2D/2.5D 壁纸 | WALL-01～07；12 场景及 WE 同条件对照 |
| S4 | 专业组件 | WPRO-01～07；8 类组件、尺寸/数据/交互/节能/无障碍 |
| S5 | 专业 AI 制作 | AIP-01～07；能力规划、素材、真实观察、修复/续改与保留集 |
| S6 | 高级动态表现力 | ADV-01～04；3D、材质/灯光、形变/物理、程序化视觉与隔离 |
| S7 | 专业版交付 | PRO-04～06；整桌创作、长稳/迁移/证据、作者资料与签收 |

S2 可在等待 S1 真机期间推进契约/知识工作；S3/S4 高风险运行时扩展需守住 S1 稳定门。S3/S4 可以交替实施，AI 随能力交付；S5 最终放行依赖两者参考集。S6 完成后重跑相关 AI 与兼容评测，再进入 S7。S1 可单独发布基础版，不能据此宣布专业目标完成。

本次核对已确认官方壁纸入口、Scene 指针/音频接线和 Web 音频推帧存在；对应 DESK 项应补真实验证而非从零实现。原能力百分比只适用于旧范围。

旧任务编号与当时的结论保留在 [历史快照](history/TODO_SNAPSHOT_2026-09-27.md)，不替代当前验收。

## 1. 当前工程基线

正式二进制：

```text
MiaoDesk.exe
MiaoDeskWallpaper.exe
MiaoDeskHarness.exe
```

职责：

```text
MiaoDesk.exe
  Search / AI / Settings

MiaoDeskWallpaper.exe
  Wallpaper runtime
  DesktopShellHost
  Native Widget runtime

MiaoDeskHarness.exe
  DeepSeek Harness WebView2 host
```

正式 Windows x64 包已经具备：

```text
CMake configure/build/install
Runtime staging
path budget verification
Pi / DSH probes
Native Widget PaintReady smoke
movable-install verification
NSIS installer
install/uninstall verification
artifact upload
```

## 2. 当前已经实现

### 2.1 Wallpaper

已存在：

- 载体 × 运行时路径（壁纸/组件 × Scene/Web）；
- `DesktopShellHost`；
- Progman / WorkerW / Raised Desktop 处理；
- 多显示器基础结构；
- 壁纸激活与停用；
- Web Wallpaper 独立 WebView2 Host；
- Native Scene / Image / Video 渲染路径；
- Explorer/Shell surface repair 基础能力。

### 2.2 Widgets

已存在：

- `DesktopWidgetStore`；
- `WidgetService`；
- `NativeWidgetHost`；
- 三款内置 Native Preset；
- normalized geometry；
- 创建 / 激活 / 停用 / 删除；
- 桌面拖动位置持久化；
- Direct2D + DIB + premultiplied alpha；
- `UpdateLayeredWindow`；
- Windows 11 Raised Desktop 下的 Direct2D + DXGI swapchain presentation fallback；
- `PaintReady` 真实呈现标记；
- CI PaintReady smoke；
- Web Widget（WebView2 承载组件）与 AI 组件生成链（A2UI 预览/应用）已整体移除，旧编辑器状态字段 `zIndex`/`managedSource` 已删除；
- Widget 位于 Desktop Icons 之上的 z-order 修复与 CI 可见性断言（`verify-widget-visibility.ps1`）。

### 2.3 Search / AI

已存在：

- Native Search UI；
- goz/gozd 搜索；
- Pi Runtime；
- Bundled Node；
- Provider / Model / Base URL / API Key；
- Windows Credential Manager；
- DeepSeek Harness 独立宿主。

### 2.4 Packaging

当前 x64 正式链已统一到：

```text
packaging/windows/
  stage.ps1
  prepare-runtime-base.ps1
  build-agent-runtime.ps1
  verify-path-budget.ps1
  installer.nsi
```

## 3. 当前必须承认的实现偏差

Widget z-order（Widget 在 Desktop Icons 之上、可交互）与 click-through 归属（只属于 Wallpaper Surface）是产品确认的最终契约，代码、遥测与 CI 均按此判断，不再作为偏差项。

### P0-1：Widget 视觉验证仍需真实机器闭环

CI 已从“HWND 可见”升级到 `PaintReady`，但最终仍需要真实 Windows 多 DPI / 多显示器验证：

- 组件内容完整；
- alpha 正确；
- 不漏出错误背景；
- Widget 位于 Desktop Icons 之上且可交互；
- 跨 DPI 不裁切；
- Explorer 重建后恢复。

### P0-2：Wallpaper 停用必须形成回归门禁

停用必须是幂等状态：

```text
Enabled=0
  → runtime stop/hide
  → reload 不得重新拉起
  → Widgets 不受影响
```

停用/reload 已有自动回归检查。当前任务是核实候选 SHA 的检查结果，并按 `TODO.md` 的 STAB-01 / STAB-02 完成真实 Windows 连续操作与恢复验证，避免把旧构建通过当作当前版本证据。

### P0-3：Widget mutation 边界已收紧，继续保护回归

`WidgetService::Update` 已拒绝通过通用请求改变内置 Preset 的默认宽高；这项不再是尚未实现的 Content Framework 前置阻塞。当前通过 `TODO.md` 的 WIDGET-01 / LIB-03 验证拖动、实例参数与几何策略。

继续维持以下归属：

```text
Legacy built-in preset
  → geometry 由 preset 拥有

New ContentDefinition
  → geometry policy 由 Definition 拥有

ContentInstance
  → 只能在 Definition 允许的范围内修改 size
```

任何后续修改都不得让 caller 绕过 Definition / Preset 直接写尺寸。

## 4. 既有架构分解（Phase 0～6；当前排期见 S0～S7）

### Phase 0 — 清理基线

目标：删除旧 Editor 路线和第二真相。

- 删除旧 Wallpaper / Scene / Widget Editor 设计；
- 删除 Timeline / Inspector / typed-property editor 规划；
- 删除旧 Wallpaper Engine parity 作为开发清单的文档；
- 代码注释不再引用 `future editor`；
- `DESIGN_BASELINE.md` 为唯一设计基线；
- 本文为唯一开发路线。

完成标准：仓库搜索 Editor 时不再出现旧 Wallpaper Editor 架构承诺。

### Phase 1 — Wallpaper 稳定性

1. 激活 / 停用形成明确状态机；
2. stop/reload/Explorer repair 幂等；
3. 按载体 × 运行时分类型 smoke；
4. 多显示器启停；
5. 停用 Wallpaper 后 Widgets 独立存活。

### Phase 2 — Widget 稳定性与性能

1. 三个 Native Painter 分别做真实视觉验收；
2. 多 DPI / 竖屏 / 横屏；
3. 旧三款 Preset 的 width/height 在领域层锁定，普通 Update 不得随意修改；
4. 组件刷新只按需要进行，避免无意义内容重绘；
5. Weather event-driven；
6. Clock 按分钟边界刷新；
7. Tasks 无变化不重绘；
8. Direct Surface 的 compositor re-present 与 painter 内容重绘解耦；
9. 对常驻内存、CPU、句柄数建立基线。

### Phase 3 — MiaoDesk Content Framework

内容框架主线命名为：

```text
MiaoDesk Content Framework
妙喵内容框架
```

详细技术契约：

```text
docs/MIAODESK_CONTENT_FRAMEWORK.md
```

目标不是先做 Wallpaper Editor / Widget Editor，而是先把 Wallpaper 与 Widget 的“内容”从宿主实现中抽离成可配置、可参数化、可打包的统一 Runtime。

第一阶段的建设依赖如下（已有实现以当前代码为准，不重复创建）：

1. `ContentDefinition + ContentInstance`；
2. `ParameterSchema + ParameterValues`；
3. `.mdwidget / .mdwall` shared package contract；
4. Native `SceneRuntime` MVP；
5. Data Binding MVP，优先 `time.*`；
6. Capability Broker 最小接口；
7. Package validator / loader；
8. Preview / reload path；
9. 把 GlassClock 迁移为第一份 `.mdwidget` dogfood；
10. 把一个现有 Scene Wallpaper 迁移为新 Content Runtime 的 `.mdwall` dogfood。

核心原则：

```text
官方内容
用户内容
AI 内容
未来 Creator
    ↓
同一套 Package / Parameters / Scene / Capability / Runtime
```

Wallpaper 与 Widget 共用内容 Runtime，但保持不同 Host 语义：Wallpaper 默认 click-through，Widget 默认可交互。

第一阶段明确不做大型 Visual Editor、Timeline、Shader Editor、Particle Editor、Node Graph，也不允许第三方内容直接获得 Windows API / filesystem / registry / native DLL 权限。

完成标准：至少一个官方 Widget 与一个官方 Wallpaper 已经通过同一套用户内容框架运行，并在真实 Windows 多显示器 / DPI 环境完成验证。

### Phase 4 — 工程继续精简

1. Runtime V3 C++ canonical path 完成；
2. 删除 Pi / DSH compatibility shim；
3. 修复 `ConversationPanelCompileCompat` 对 `std` 的污染；
4. 清理零引用 compatibility header；
5. 保持三个正式 EXE，不重新增加 acceptance/test 可执行程序；
6. 不为“目录好看”移动高风险 runtime ownership。

### Phase 5 — Search / AI 体验

桌面核心与 Content Framework 基础稳定后再继续：

- Pi 对话体验；
- Agent 活动反馈；
- Desktop read/preview tools；
- Content Parameter 修改与 Preview；
- Harness UX；
- Provider 配置体验。

AI 不应成为 Wallpaper / Widget 稳定性的前置依赖，也不得绕过 ContentDefinition / ContentInstance 直接写底层 persistence。

### Phase 6 — ARM64

ARM64 构建/打包流程已建立。继续随 x64 维护同等布局、schema 与发布验证；真机视觉验收单独完成：

- 复用同一 staging scripts；
- 使用 `runtime/arm64` Native base；
- 复用 shared `runtime/agent/package-lock.json`；
- 建立与 x64 等价的 ARM64 build/package smoke；
- Content Runtime package/schema 保持跨 x64 / ARM64 一致，Native runtime 实现按架构构建。

## 5. 编辑器与生态边界

旧式、重型、与底层数据模型耦合的 Editor 路线仍不恢复：

```text
旧 Wallpaper Editor
旧 Scene Editor
旧 Widget Editor
大型 Timeline / Keyframe Editor
Inspector-first typed-property editor
Shader Editor
Particle Editor
Node Graph
逐项复制 Wallpaper Engine 编辑器 UI / 专有格式 / 社区生态
```

专业版同时建设 WE 级效果、macOS 级组件与专业 AI Creator，3D/形变属于 S6 必达能力。用户创建 Wallpaper / Widget 的实现方式仍是先建设 `MiaoDesk Content Framework`，再在稳定 Runtime 之上增加 Parameter tooling 和 Visual Creator。

因此：

```text
不做旧 Editor ≠ 不允许用户创作

当前路线：
Content Runtime
→ Parameter / Package tooling
→ Visual Creator
```

## 6. 每个阶段的完成定义

每项功能至少经过：

```text
代码实现
→ 编译
→ 自动 smoke
→ 正式 staging/package
→ 真实 Windows 验证
```

涉及桌面显示的功能，仅 CI 绿色不能判定完成。

Content Framework 的功能还必须增加：

```text
schema validation
package validation
capability validation
Definition / Instance migration
官方内容 dogfood
```

## 7. 当前 P0 验收顺序

```text
1. Widget 内容完整显示
2. 桌面层级保持 Widget > Desktop Icons > Wallpaper
3. Widget 可交互，拖动后只持久化 x/y
4. Wallpaper 可以可靠停用且不会被 reload 拉起
5. Wallpaper 停用后 Widget 继续显示
6. Explorer / DPI / 显示器变化后仍保持以上状态
7. x64 installer 全绿并真实安装验证
8. Widget geometry mutation 与已实现内容框架的回归保持通过
9. 参考机性能证据与同一候选 SHA 的发布链齐备
```

在桌面 Host / Shell 稳定性没有守住前，不允许 Content Framework 破坏现有层级、拖动、停用、多显示器和低常驻资源基线。
