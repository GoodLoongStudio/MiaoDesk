# TuringDesk Native 技术路线基线

- 状态：**已确认**
- 日期：2026-08-24
- 适用范围：TuringDesk Native 主线
- 正式开发分支：`main`
- 产品基线：`docs/TURINGDESK-PRODUCT-BASELINE.md`
- AI Runtime 契约：`docs/L3-PI-RUNTIME-CONTRACT.md`
- Desktop Composition 架构：`docs/DESKTOP_COMPOSITION_ARCHITECTURE.md`
- Windows Wallpaper 实现参考：`docs/LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md`

## 1. 三个核心能力

```text
TuringDesk
├─ A. 顶部统一入口：应用 / 文件 / 图灵 AI
├─ B. Wallpaper Engine 级 Desktop Composition
└─ C. DeepSeek Harness 高级工作台
```

“Wallpaper Engine 级”只表示产品能力深度。**具体 Windows 壁纸运行时实现不得以 Wallpaper Engine 为工程参考**；统一以 `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md`、Lively 的公开行为和 Microsoft Windows API 文档为技术参考，并由 TuringDesk 独立 C++23 实现。

Native 主线以 Windows 性能、稳定性、低常驻资源、清晰故障边界和真实用户可用为优先目标。

## 2. 总体技术原则

1. 主桌面核心使用 C++23 / Win32。
2. Search UI 不使用 WPF、Electron、CEF、Qt 或 WebView。
3. 普通 AI 默认主路由固定为 Pi Agent Runtime。
4. Pi 运行在 TuringDesk 自带 Node 24 上，不要求用户安装 Node/npm。
5. Provider 路由按协议能力判断，不能绑定模型品牌。
6. Pi / Node / Agent Loop 失败时才回退 Direct Model Runtime。
7. Pi 负责 Agent Loop、上下文、Skills、Extensions 和通用工具体系。
8. TuringDesk 只保留真正依赖产品内部状态的 Desktop Tools。
9. AI、Settings、Editor 最终必须共用版本化 Desktop Control API，不允许长期存在多套状态修改路径。
10. Web Runtime 只在 Web Wallpaper、Widget 或高级工作台等确有需要的场景按需启动。
11. 普通 Scene 不得因为 AI/Widget 无条件加载 Chromium。
12. 真实 Windows 设备可用才算完成；编译、CI、Mock 只属于中间验证。
13. Windows Shell / WorkerW / Progman / Raised Desktop / Explorer recovery 逻辑只能由统一 `DesktopShellHost` 持有，不允许 Wallpaper、Web、Widget 各自实现一套。
14. Lively 是 GPL-3.0、TuringDesk 是 MIT；只能研究行为和 API 序列，禁止复制或逐行翻译 Lively 源码。

## 3. Desktop Search / AI

### 3.1 UI 与本地搜索

```text
语言                 C++23
窗口                 Win32 HWND
UI 绘制              Direct2D
文字                 DirectWrite
合成/动画            DirectComposition
凭据                 Windows Credential Manager
文件搜索正式后端     goz / gozd · NTFS MFT + USN Journal
```

顶部 Search 保持 Native UI。AI Runtime 可以按请求启动子进程，但不得把 WebView 或高级工作台 UI 塞进 Search。

### 3.2 AI 唯一默认运行链

```text
用户请求
  ↓
TuringDesk Native AI UI
  ↓
PiRuntime
  ↓
Bundled Node 24
  ↓
@earendil-works/pi-coding-agent
  ↓
Pi AgentSession / RPC
  ↓
当前配置 Provider / Model / Base URL / API Key
```

失败才进入 Direct Model fallback。

### 3.3 Provider 兼容

支持按协议映射：

```text
openai-completions
openai-responses
anthropic-messages
google-generative-ai
```

API Key 只长期存 Windows Credential Manager。TuringDesk Pi 目录：

```text
%LOCALAPPDATA%\TuringDesk\PiAgent\
```

### 3.4 Agent 工具层

通用能力由 Pi 提供：

```text
read / write / edit / grep / find / ls
shell / PowerShell
Skills / Extensions / Packages
```

TuringDesk 专属能力：

```text
Pi Agent
  ↓
TuringDesk Desktop Tools
  ↓
Desktop Control API
  ↓
Wallpaper / Widget / Multi-monitor / Playlist / Performance / Settings
```

Native Tool worker 必须隔离执行、可超时、可诊断，并且只允许白名单产品工具。

### 3.5 AI Desktop Control 约束

AI 不能把直接编辑 `wallpaper.ini`、widget 私有清单或进程内存作为长期控制接口。

目标 API：

```text
desktop_state_get
wallpaper_apply
wallpaper_properties_update
playlist_update
display_assignment_update
performance_policy_update
widget_create
widget_update
widget_remove
widget_list
desktop_undo
```

每次 mutation 必须验证；宽范围 mutation 必须事务化并支持 Undo / Redo；执行成功必须返回真实新状态。

当前第一阶段桥接接口包括：桌面状态读取、Web `.tdwall` 校验/应用、Widget CRUD。它们后续收口到版本化 Desktop Control API。

## 4. Pi 与高级工作台边界

```text
普通 AI = Pi Runtime → 当前 API → Direct API fallback
高级工作台 = DeepSeek Harness WebUI
```

两者共享 Provider / Model / Base URL / API Key 配置，但 Runtime 生命周期独立。后台启动高级工作台服务不得自动弹浏览器。

## 5. Wallpaper Engine 级 Desktop Composition

### 5.1 实现参考边界

产品功能深度可以对标 Wallpaper Engine-class，但 Windows 实现统一走：

```text
Product requirement
  ↓
Lively public behavior + Microsoft APIs
  ↓
TuringDesk DesktopShellHost / Surface Managers
  ↓
Independent C++23 implementation
```

具体 WorkerW / Progman / Raised Desktop / WebView2 surface / Explorer recovery 契约见 `docs/LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md`。

### 5.2 总体分层

```text
Desktop Composition
├─ Wallpaper Layer
│  ├─ Image
│  ├─ Video
│  ├─ Web
│  └─ Scene
├─ Widget Layer
│  ├─ Web
│  ├─ Native
│  └─ Data-bound
└─ Control Layer
   ├─ Settings Center
   ├─ Scene / Widget Editor
   └─ Desktop Control API
```

Wallpaper 与 Widget 独立持久化。切换 Wallpaper 不得删除 Widget 布局。

### 5.3 基础技术栈

```text
语言                  C++23
桌面集成              Win32 / Explorer / WorkerW / Progman
桌面 Shell 抽象       DesktopShellHost
图形 API              Direct3D 11（新 Scene Renderer 方向）
2D                    Direct2D
Shader                HLSL
显示/适配器           DXGI
图片                  WIC
视频                  Media Foundation
音频                  WASAPI Loopback + FFT
Web Wallpaper         独立 WebView2 Host
Web Widget            独立 WebView2 Host
```

现有图片/视频/原生 Scene 路径保持轻量；WebView2 只按需创建。

### 5.4 DesktopShellHost

所有桌面 surface 共用同一 Shell Host：

```text
DesktopShellHost
├─ Discover Progman / SHELLDLL_DefView / WorkerW
├─ Detect raised desktop via WS_EX_NOREDIRECTIONBITMAP
├─ Request wallpaper layer via encapsulated shell message
├─ Attach surface
├─ Validate parent / style / z-order
├─ Repair z-order
└─ Rediscover after Explorer rebuild
```

Raised Desktop 与 Legacy WorkerW 必须分支处理，不能使用同一套 `SetParent` 假设。

`WallpaperEngine.cpp` 里现有 Shell 逻辑应逐步抽出，不再继续扩张。

### 5.5 Wallpaper Layer

正式资源类型：Image / Video / Web / Scene。

必须持续支持：

- Span / Clone / Primary / Independent；
- 稳定 Monitor ID；
- 每屏独立资源；
- 图片 Cover / Contain / Stretch / Center / Tile；
- 视频循环、音量、倍速、Seek；
- Web 隔离、导航限制、Crash Recovery；
- Playlist / Schedule / Profile；
- per-app performance rules；
- 全屏、最大化、电池、锁屏、Idle、Remote Desktop 性能策略；
- Screensaver。

### 5.6 Widget Layer

Widget 第一阶段数据模型：

```text
id
kind
title
source
monitorId
x / y / width / height   # monitor-relative normalized geometry
zIndex
enabled
managedSource
```

归一化坐标保证分辨率、DPI 和显示器变化后的布局可迁移。

Widget v1：

- 本地自包含 HTML；
- TuringDesk managed package；
- WebView2 隔离进程；
- Monitor-relative position/size；
- persistent CRUD；
- AI CRUD；
- 默认 click-through，不挡桌面图标；
- 复用 Web runtime 的 pause/recovery 机制；
- 必须通过 `DesktopShellHost` 完成真实桌面挂载；
- 必须能诊断 configured/process/HWND/parent/style/z-order/WebView/visible/healthy。

未来类型：Text、Clock/Calendar、Image、System Status、Media Controls、Data-bound Widget。

### 5.7 Scene / Widget Editor

两者共享一个编辑器 Shell：

```text
┌──────────────┬──────────────────────────┬─────────────────┐
│ Project      │                          │ Inspector       │
│ Layers       │      Live Preview        │ typed props     │
│ Assets       │                          │ bindings        │
├──────────────┴──────────────────────────┴─────────────────┤
│ Timeline / Keyframes / Events                              │
└────────────────────────────────────────────────────────────┘
```

统一 typed property schema：bool、integer/float、enum、string、color、vector2/3、resource reference、curve/easing、data binding。

该 schema 同时供手工 Inspector、序列化、Runtime mutation、AI 编辑和 Undo/Redo 使用。

### 5.8 高级 Scene 能力方向

```text
2D / 3D Scene
Shader
Particle
Animation
Audio Reactive
Mouse Interaction
Transitions
HDR / Color
Device Lost Recovery
```

旧的 Aurora / Neon / Grid 最终应迁移为内置 Scene Project，而不是继续扩大 hard-coded renderer。

### 5.9 包格式

```text
.tdwall   Wallpaper Project / Package
.tdwidget Widget Project / Package
```

两者最终共享安全包核心：manifest schema、版本、metadata、entry、permission、hash、safe extraction、preview、provenance。

AI 生成资源必须标记 provenance，并在 import/apply 前验证。

## 6. 多显示器与生命周期

必须处理：

```text
每屏独立壁纸 / Widget
复制 / 跨屏
不同 DPI / 刷新率
显示器插拔 / 重排
Explorer 重启
睡眠 / 唤醒
锁屏
Remote Desktop
GPU Device Lost
桌面层重建
```

性能规则支持 Normal / Throttle / Pause / Stop，并同时作用于需要资源的 Wallpaper 和 Widget runtime。

Explorer 或 WorkerW 重建后必须：重新发现 Shell hierarchy → 重新挂载 Wallpaper → 重新挂载 Widget → 修复 z-order → 验证 visible health。

## 7. 当前工程推进顺序

```text
1. DesktopShellHost + Lively-informed Windows shell behavior
2. Web Wallpaper / Widget shared surface attachment
3. Visible-surface diagnostics
4. Desktop Composition + Widget Runtime
5. AI Desktop Control bridge
6. Installed/Widgets UI 产品化
7. Typed Property System
8. Scene / Widget Editor v1
9. Shader / Particle / Audio / Interaction / 3D
10. Transactional Desktop Control API + Undo/Redo
```

能力执行清单以 `docs/WALLPAPER_ENGINE_PARITY.md` 为准；Windows 壁纸运行时实现以 `docs/LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` 为准。

## 8. 完成标准

```text
代码存在       ≠ 完成
编译通过       ≠ 完成
CI 通过        ≠ 完成
Loopback Mock  ≠ 完成
真实 Windows 用户流程通过 = 完成
```

所有正式交付只进入 `main`。
