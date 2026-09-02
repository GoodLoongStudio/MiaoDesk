# MiaoDesk 设计基线

- 状态：当前唯一设计基线
- 日期：2026-09-02
- 适用分支：`main`
- 目标：先把 Windows 桌面核心做稳定、做轻、做快，再扩展能力

与本文冲突的旧设计、阶段计划、Wallpaper Editor / Scene Editor / Widget Editor 方案均作废。

## 1. 产品定位

MiaoDesk 是 Windows 原生桌面增强程序，当前核心由四部分组成：

```text
MiaoDesk
├─ Wallpaper
├─ Widgets
├─ Search
└─ AI
```

当前开发优先级不是“做一个完整桌面编辑器”，而是先保证 Wallpaper 与 Widgets 在真实 Windows 桌面上长期稳定运行，并维持低常驻资源占用。

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
2. WebView2 只用于内容本身就是 Web 的能力，例如 Web Wallpaper 与 DeepSeek Harness。
3. AI、Node、Pi、Harness 等较重 Runtime 不进入桌面渲染主路径。
4. 桌面 Surface 不依赖系统 Node、全局 npm 或在线安装。
5. 真实 Windows 用户体验、稳定性和性能优先于架构形式上的“完整”。

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
- Scene：Native Direct2D，后续只有明确性能收益时才引入更重 GPU 路径；
- Web：独立 WebView2 Host，按需启动。

当前用户侧核心操作：

```text
选择
激活
停用
删除可删除资源
多显示器应用
```

当前阶段不做 Wallpaper Editor。

明确删除/停止规划：

- Scene Editor；
- Wallpaper Editor；
- Timeline / Keyframe Editor；
- Inspector；
- Shader / Particle 可视化编辑器；
- 旧 Wallpaper Engine parity 驱动的编辑器路线；
- 为编辑器预留的大型 typed-property UI。

`.mdwall` 当前是运行时资源包，不等于“编辑器工程格式”。

## 5. Widget 设计基线

Widget 只提供 Native Widget（Web Widget / WebView2 承载组件已整体移除，AI 不能创建或修改组件，只能通过 `desktop_widget_list` 读取状态）：

```text
玻璃时钟    GlassClock
今日待办    TodayTasks
玻璃天气    WeatherGlass
```

Native Widget 使用：

```text
NativeWidgetPainter
  ↓
Direct2D
  ↓
32-bit premultiplied-alpha DIB
  ↓
UpdateLayeredWindow
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

### 5.1 Widget 尺寸与位置

内置 Widget 的尺寸由 Preset 拥有：

```text
GlassClock     30% × 30%
TodayTasks     22% × 48%
WeatherGlass   28% × 28%
```

用户当前只需要修改：

```text
x
y
enabled
```

不在当前范围：

- 自由 resize；
- 数值 Inspector；
- 任意 z-index；
- Widget Editor；
- 自定义 Widget 包编辑器。

### 5.2 Widget 交互模型

正常状态（自上而下）：

```text
Widget Surface（可交互）
  > Desktop Icons
  > Wallpaper
```

Widget Surface 直接接收鼠标输入：

- 按住 Widget 表面拖动即移动组件，结束后只持久化归一化 `x/y`；
- 不引入临时移动模式、Move Overlay 或其他额外交互 Surface；
- Widget 不进入 click-through 状态；click-through 只属于 Wallpaper Surface。

## 6. Settings Center

当前一级入口保持简单：

```text
壁纸
组件
API 配置
```

设置中心负责“管理和控制”，不承担编辑器职责。

Wallpaper 页面：资源库 + 激活/停用/删除等明确动作。

Widget 页面：三款内置 Widget 的创建、激活、停用、删除与运行状态；位置通过在桌面上直接拖动修改。

API 配置：Provider / Model / Base URL / API Key。

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

## 8. 明确移除的旧方向

以下内容不再作为当前设计的一部分：

- Wallpaper / Scene / Widget Editor；
- Timeline / Keyframe / Inspector；
- 以 Wallpaper Engine 全功能 parity 为开发清单；
- Web Widget（WebView2 承载的常驻组件）；
- AI 生成/应用组件的 A2UI 预览链；
- 编辑器和 AI 共用 typed property schema 的旧方案；
- 为未来编辑器保留的 UI、导航和架构层。

如果未来重新出现编辑需求，必须基于届时的产品目标重新立项，不允许从旧 Editor 代码/文档直接复活。

## 9. 完成标准

```text
代码存在        ≠ 完成
编译通过        ≠ 完成
CI 绿色         ≠ 完成
HWND 存在       ≠ Widget 正常
配置 enabled    ≠ Runtime 正常
真实 Windows 用户流程稳定通过 = 完成
```

当前所有正式交付只进入 `main`。
