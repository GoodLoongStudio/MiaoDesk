# MiaoDesk 设计基线

- 状态：当前唯一设计基线
- 日期：2026-09-05
- 适用分支：`main`
- 目标：先把 Windows 桌面核心做稳定、做轻、做快，再在稳定 Host 之上扩展可配置、可参数化的内容能力

与本文冲突的旧设计、阶段计划、旧 Wallpaper Editor / Scene Editor / Widget Editor 方案均作废。

## 1. 产品定位

MiaoDesk 是 Windows 原生桌面增强程序，当前核心由四部分组成：

```text
MiaoDesk
├─ Wallpaper
├─ Widgets
├─ Search
└─ AI
```

当前产品底座仍优先保证 Wallpaper 与 Widgets 在真实 Windows 桌面上长期稳定运行，并维持低常驻资源占用。

在此基础上，下一阶段正式进入：

```text
MiaoDesk Content Framework
妙喵内容框架
```

目标不是恢复旧式大型 Editor，而是把 Wallpaper / Widget 的内容从宿主代码中抽离为统一的 Package、Definition、Instance、Parameter、Scene Runtime 与 Capability 模型，让官方内容与用户内容使用同一套底层框架。

详细契约：`docs/MIAODESK_CONTENT_FRAMEWORK.md`。

## 2. 最高设计原则：Native C++ 与性能优先

Wallpaper、Widgets、Search Bar 都属于常驻桌面的核心路径，默认技术选择为：

```text
C++23
Win32
Direct2D / DirectWrite
WIC
Media Foundation
Windows Shell APIs
```

原则：

1. 能用 Native C++ 稳定实现的常驻能力，不引入 Web Runtime。
2. WebView2 只用于内容本身就是 Web 的能力、独立 Harness，以及明确隔离的临时 Preview / 高级内容 Runtime；不得成为所有 Wallpaper / Widget 的默认承载层。
3. AI、Node、Pi、Harness 等较重 Runtime 不进入桌面渲染主路径。
4. 桌面 Surface 不依赖系统 Node、全局 npm 或在线安装。
5. 真实 Windows 用户体验、稳定性和性能优先于架构形式上的“完整”。
6. 官方内容与用户内容优先共享同一 Content Runtime；不允许长期维护“官方 hardcode renderer / 用户另一套 renderer”的双轨架构。

## 3. Windows 桌面组合模型

Wallpaper 与 Widgets 是两个独立层，由同一个 `DesktopShellHost` 负责 Windows Shell 挂载、恢复和层级管理。

正常桌面的逻辑层级（自上而下）：

```text
Native Widgets       可交互，直接接收鼠标输入
─────────────
Desktop Icons
─────────────
Wallpaper
─────────────
Windows Background / WorkerW
```

对齐 Windows 官方小组件模型（Win7 Desktop Gadgets / Windows 11 Widgets）：组件是可交互 Surface，始终位于图标层之上。

必须满足：

- Widget 始终位于 Desktop Icons 之上、普通应用窗口之下；
- Widget 默认可交互（不设置 `WS_EX_TRANSPARENT`），Wallpaper Surface 默认 click-through；
- 桌面图标在 Widget 未覆盖的区域始终可点击、可框选、可右键；
- Widget 不属于某张 Wallpaper；
- 切换 Wallpaper 不销毁 Widget；
- 停用 Wallpaper 后 Widget 仍正常显示；
- Widget 故障不能拖垮 Wallpaper；
- Wallpaper 故障不能拖垮 Widget；
- Progman / WorkerW / Raised Desktop / Explorer recovery 只能由 `DesktopShellHost` 统一处理。

Content Framework 只能复用这些 Host 能力，不得重新实现第二套 Shell attachment / z-order ownership。

## 4. Wallpaper 设计基线

正式保留四类 Wallpaper：

```text
Image
Video
Web
Scene
```

技术优先级：

- Image：WIC + Native rendering；
- Video：Media Foundation；
- Scene：Native Direct2D / Content Scene Runtime；只有明确性能收益时才引入更重 GPU 路径；
- Web：独立 WebView2 Host，按需启动。

当前用户侧核心操作：

```text
选择
激活
停用
删除可删除资源
多显示器应用
```

下一阶段增加的能力不是旧 Wallpaper Editor，而是：

```text
导入 / 创建 Content Package
配置 ParameterValues
Preview
保存 / 应用 ContentInstance
```

`.mdwall` 继续作为 Wallpaper 内容包，并逐步迁移到 `MiaoDesk Content Framework` 的共享 Package / Parameter / Scene 契约。

明确不恢复旧路线：

- 旧 Scene Editor；
- 旧 Wallpaper Editor；
- 旧 Timeline / Keyframe Editor；
- 旧 Inspector-first 编辑模式；
- Shader / Particle 可视化编辑器；
- 旧 Wallpaper Engine parity 驱动的编辑器路线；
- 为旧编辑器预留的大型 typed-property UI。

用户创作能力应建立在 Content Runtime 之上，而不是复活旧 Editor 代码。

## 5. Widget 设计基线

### 5.1 当前生产基线

当前正式内置 Widget 仍是三款 Native Widget：

```text
玻璃时钟    GlassClock
今日待办    TodayTasks
玻璃天气    WeatherGlass
```

Legacy Web Widget / WebView2 常驻组件路径已整体移除。

Native Widget 当前使用两种 presentation path，由 Windows Desktop parent 能力决定：

```text
Layered path
Direct2D
  ↓
32-bit premultiplied-alpha DIB
  ↓
UpdateLayeredWindow

Raised Desktop direct path
Direct2D
  ↓
D3D11 / DXGI swapchain
  ↓
DWM composition
```

一个 Widget 必须有真实的渲染成功状态，不能只因为 `enabled=true` 或 HWND 存在就认为正常。

当前健康链必须至少覆盖：

```text
configured
host running
HWND exists
parent valid
style valid
geometry valid
z-order valid（Widget 位于 Desktop Icons 之上）
visible
PaintReady
last error
```

### 5.2 Widget 尺寸与位置

当前三款内置 Widget 的尺寸由 Preset 拥有：

```text
GlassClock     30% × 30%
TodayTasks     22% × 48%
WeatherGlass   28% × 28%
```

在旧三款 Preset 完成迁移前，普通 caller 只能修改：

```text
x
y
enabled
```

不能绕过 Preset 直接修改 `width/height`。

进入 Content Framework 后，尺寸策略由 `ContentDefinition.geometry` 拥有：

```text
defaultWidth / defaultHeight
resize allowed
min / max size
aspect ratio policy
```

`ContentInstance` 只能在 Definition 允许的范围内修改尺寸。

### 5.3 Widget 交互模型

正常状态（自上而下）：

```text
Widget Surface（可交互）
  > Desktop Icons
  > Wallpaper
```

Widget Surface 直接接收鼠标输入：

- 按住 Widget 表面拖动即移动组件，结束后持久化归一化位置；
- 允许 resize 的 ContentDefinition 以后可增加直接 resize，但必须受 geometry policy 约束；
- 不引入临时移动模式、Move Overlay 或其他额外交互 Surface；
- Widget 不进入 click-through 状态；click-through 只属于 Wallpaper Surface。

### 5.4 下一阶段 Widget Content

用户自定义 Widget 正式重新进入路线，但采用新框架：

```text
.mdwidget
  ↓
ContentDefinition
  ↓
ParameterSchema
  ↓
Scene Runtime
  ↓
Widget Host
```

第一阶段默认 Runtime 是 Native Scene Runtime，不恢复 Web Widget 作为默认路径。

官方 GlassClock 将作为第一份 Widget dogfood，验证官方内容与用户内容共享同一框架。

## 6. Settings Center

当前一级入口保持简单：

```text
壁纸
组件
API 配置
```

设置中心负责“管理和控制”，不承担旧式大型编辑器职责。

Wallpaper 页面：资源库 + 激活/停用/删除等明确动作；Content Framework 进入实现后增加 Package 导入、参数设置与 Preview。

Widget 页面：当前三款内置 Widget 的创建、激活、停用、删除与运行状态；位置通过在桌面上直接拖动修改。Content Framework 进入实现后增加 `.mdwidget` 导入、Instance 管理、参数配置与受策略约束的尺寸能力。

API 配置：Provider / Model / Base URL / API Key。

完整 Visual Creator 不作为 Content Framework 第一阶段的前置条件。

## 7. Search 与 AI

Search 继续保持 Native C++ UI，本地搜索由 goz/gozd 提供。

普通 AI 主路径：

```text
MiaoDesk
  → Pi Runtime
  → Bundled Node
  → 当前 Provider / Model
```

DeepSeek Harness 是独立的高级工作台，用户明确打开时才显示 WebView2 UI。

AI 不得进入 Wallpaper / Widget 的每帧渲染路径。

当前 AI 对既有 Widget 仍保持只读 `desktop_widget_list` 边界，不直接修改 Widget persistence。

当 Content Framework 的 Definition / Parameter / Preview / commit 边界稳定后，AI 可以在后续阶段通过同一声明式 Content API 修改 ParameterValues 或生成候选 ContentDefinition，但不得绕过 Preview / capability / validation 直接写底层状态。

## 8. MiaoDesk Content Framework 原则

新的用户创作路线必须满足：

```text
Definition ≠ Instance
Package ≠ Runtime host
ParameterSchema ≠ hardcoded settings UI
Content capability ≠ arbitrary system permission
Preview ≠ Apply
```

统一目标：

```text
官方内容 ─┐
用户内容 ─┼→ Package / Definition / Parameters / Scene
AI 内容 ──┤                         ↓
Creator ──┘                   Content Runtime
                                  ↓
                        Wallpaper / Widget Host
```

第一阶段优先实现：

```text
ContentDefinition + ContentInstance
ParameterSchema + ParameterValues
.mdwidget / .mdwall shared package contract
Native Scene Runtime MVP
Data Binding MVP
Capability Broker MVP
Package validation / Preview / reload
GlassClock dogfood
一个 Scene Wallpaper dogfood
```

## 9. 明确移除的旧方向

以下内容不再作为当前设计的一部分：

- 旧 Wallpaper / Scene / Widget Editor 架构；
- 旧 Timeline / Keyframe / Inspector-first 编辑器；
- 以 Wallpaper Engine 全功能 parity 为开发清单；
- Legacy Web Widget（WebView2 承载的默认常驻组件）；
- 旧 AI 生成/应用组件的 A2UI 预览链；
- 编辑器和 AI 共用旧 typed property schema 的方案；
- 为旧编辑器保留的 UI、导航和架构层。

这些旧方向的移除不再等于“禁止用户创作”。用户创作现在通过 `MiaoDesk Content Framework` 重新立项，采用 Runtime-first 路线：

```text
Content Runtime
→ Parameter / Package tooling
→ Visual Creator
```

不允许直接从旧 Editor 代码/文档复活实现。

## 10. 完成标准

```text
代码存在        ≠ 完成
编译通过        ≠ 完成
CI 绿色         ≠ 完成
HWND 存在       ≠ Widget 正常
配置 enabled    ≠ Runtime 正常
Package 可解析   ≠ Content Framework 完成
真实 Windows 用户流程稳定通过 = 完成
```

Content Framework 额外要求：

```text
至少一个官方 Widget 通过用户内容框架运行
至少一个官方 Wallpaper 通过用户内容框架运行
同一 Definition 可产生不同参数 Instance
Parameter schema 可以驱动设置 UI / Preview
Package / capability / schema validation 生效
新内容不破坏现有 Shell / z-order / multi-monitor / low-resource 基线
```

当前所有正式交付只进入 `main`。
