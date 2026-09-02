# MiaoDesk 开发基线与路线

- 状态：当前唯一开发路线
- 日期：2026-09-02
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
3. Native Preset 的 width/height 在领域层锁定，普通 Update 不得随意修改；
4. 组件刷新只按需要进行，避免无意义轮询；
5. Weather event-driven；
6. Clock 按分钟边界刷新；
7. Tasks 无变化不重绘；
8. 对常驻内存、CPU、句柄数建立基线。

### Phase 3 — 工程继续精简

1. Runtime V3 C++ canonical path 完成；
2. 删除 Pi / DSH compatibility shim；
3. 修复 `ConversationPanelCompileCompat` 对 `std` 的污染；
4. 清理零引用 compatibility header；
5. 保持三个正式 EXE，不重新增加 acceptance/test 可执行程序；
6. 不为“目录好看”移动高风险 runtime ownership。

### Phase 4 — Search / AI 体验

桌面核心稳定后再继续：

- Pi 对话体验；
- Agent 活动反馈；
- Desktop read/preview tools；
- Harness UX；
- Provider 配置体验。

AI 不应成为 Wallpaper / Widget 稳定性的前置依赖。

### Phase 5 — ARM64

x64 产品链稳定后：

- 复用同一 staging scripts；
- 使用 `runtime/arm64` Native base；
- 复用 shared `runtime/agent/package-lock.json`；
- 建立与 x64 等价的 ARM64 build/package smoke。

## 5. 当前不做

以下内容不进入当前路线：

```text
Wallpaper Editor
Scene Editor
Widget Editor
Timeline
Keyframe Editor
Inspector
Shader Editor
Particle Editor
可视化 typed-property editor
完整 Wallpaper Engine feature parity
Widget 自定义编辑器
```

如果以后有明确商业/用户需求，再独立立项。

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

## 7. 当前 P0 验收顺序

```text
1. Widget 内容完整显示
2. Widget 位于 Desktop Icons 之上、Wallpaper 之下
3. Widget 可交互，拖动后只持久化 x/y
4. Wallpaper 可以可靠停用且不会被 reload 拉起
5. Wallpaper 停用后 Widget 继续显示
6. Explorer / DPI / 显示器变化后仍保持以上状态
7. x64 installer 全绿并真实安装验证
```

在这 7 项稳定前，不重新扩展 Editor、复杂 Widget 类型或高级 Wallpaper 编辑能力。
