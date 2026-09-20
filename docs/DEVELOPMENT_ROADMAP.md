# MiaoDesk 开发基线与路线

- 状态：当前唯一开发路线
- 日期：2026-09-05
- 设计基线：`docs/DESIGN_BASELINE.md`
- 正式开发分支：`main`

本文只描述“当前已经实现什么、哪里与设计有偏差、下一步按什么顺序修”。历史 M3、Wallpaper Engine parity、Editor 路线均不再作为开发依据。

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

- Image / Video / Web / Scene 路径；
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

当前已有修复，但还需要正式 smoke，避免以后 Shell repair / reload 再把 Wallpaper 拉回。

### P0-3：Content Framework 开始前先收紧旧 Widget mutation 边界

当前三款内置 Widget 已经有固定 Preset 尺寸，但通用 `WidgetUpdateRequest` / `WidgetService::Update` 仍允许直接修改 `width/height`。

在进入新的 Content Framework 之前，需要先明确：

```text
Legacy built-in preset
  → geometry 由 preset 拥有

New ContentDefinition
  → geometry policy 由 Definition 拥有

ContentInstance
  → 只能在 Definition 允许的范围内修改 size
```

不能继续让任意 caller 绕过 Definition / Preset 直接写尺寸。

## 4. 立即开发顺序

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
3. Image / Video / Web / Scene 分类型 smoke；
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

下一阶段主线正式命名为：

```text
MiaoDesk Content Framework
妙喵内容框架
```

详细技术契约：

```text
docs/MIAODESK_CONTENT_FRAMEWORK.md
```

目标不是先做 Wallpaper Editor / Widget Editor，而是先把 Wallpaper 与 Widget 的“内容”从宿主实现中抽离成可配置、可参数化、可打包的统一 Runtime。

第一阶段按以下顺序实现：

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

x64 产品链稳定后：

- 复用同一 staging scripts；
- 使用 `runtime/arm64` Native base；
- 复用 shared `runtime/agent/package-lock.json`；
- 建立与 x64 等价的 ARM64 build/package smoke；
- Content Runtime package/schema 保持跨 x64 / ARM64 一致，Native runtime 实现按架构构建。

## 5. 当前不做

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
完整 Wallpaper Engine feature parity
```

但是“用户创建 Wallpaper / Widget”已经重新进入路线，方式是先建设 `MiaoDesk Content Framework`，再在稳定 Runtime 之上增加 Parameter tooling 和 Visual Creator。

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
8. 旧三款 Widget geometry mutation 边界收紧
9. 进入 MiaoDesk Content Framework Phase 3
```

在桌面 Host / Shell 稳定性没有守住前，不允许 Content Framework 破坏现有层级、拖动、停用、多显示器和低常驻资源基线。
