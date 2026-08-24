# TuringDesk Native 技术路线基线

- 状态：**已确认**
- 日期：2026-08-24
- 适用范围：TuringDesk Native 主线
- 正式开发分支：`main`
- 产品基线：`docs/TURINGDESK-PRODUCT-BASELINE.md`
- L3 详细契约：`docs/L3-PI-RUNTIME-CONTRACT.md`

## 1. 三个核心能力

```text
TuringDesk
├─ A. 顶部统一入口：应用 / 文件 / AI
├─ B. Wallpaper Engine 级桌面引擎
└─ C. DeepSeek Harness 高级工作台
```

Native 主线以 Windows 性能、稳定性、低常驻资源和清晰故障边界为优先目标。

## 2. 总体技术原则

1. 主桌面核心使用 C++23 / Win32。
2. Search UI 不使用 WPF、Electron、CEF、Qt 或 WebView。
3. 普通 AI 默认主路由固定为 **Pi Agent Runtime**。
4. Pi 运行在 TuringDesk 自带的 Node 24 上，不要求用户安装 Node/npm。
5. Provider 路由按协议能力判断，不能绑定模型品牌。
6. Pi / Node / Agent Loop 失败时才回退 **Direct Model Runtime → 当前配置 API**。
7. Pi 负责 Agent Loop、上下文、Skills、Extensions 和通用工具体系。
8. TuringDesk 只保留真正属于桌面产品的专属工具和权限宿主。
9. 普通 AI 不自动启动 DeepSeek Harness；Harness 是独立高级工作台。
10. Web Runtime 只在 Harness 或 Web Wallpaper 等确有需要的场景按需启动。
11. 代码存在、编译通过、Mock/Loopback 通过都不等于产品完成；真实 Windows 设备可用才算完成。

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

顶部 Search 是最高频入口，保持 Native UI；AI Runtime 可以按请求启动子进程，但不得把 WebView 或 Harness UI 塞进 Search。

### 3.2 应用搜索

TuringDesk 维护应用发现与排序，至少覆盖：

```text
Start Menu
App Paths
注册表应用信息
UWP / MSIX
常用系统程序
```

排序逐步支持 Exact / Prefix / Substring / Fuzzy / 使用频率 / 最近启动。

### 3.3 文件搜索

正式路线：

```text
Search UI
  ↓
GozSearch Adapter
  ↓
goz.exe / gozd.exe
  ↓
NTFS MFT + USN Journal
```

文件搜索故障不得阻塞 AI。

### 3.4 AI 唯一默认运行链

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

Pi 是普通问答和桌面 Agent 请求的默认 Runtime。

如果 Pi Runtime、Node、Provider 配置、Agent Session 或工具循环任一阶段失败：

```text
记录失败
  ↓
Direct Model Runtime
  ↓
当前配置 API
```

Direct Model 是 **fallback**，不是默认主路由。

### 3.5 Provider 兼容原则

API 不限定为特定品牌。

TuringDesk 根据探测结果映射到 Pi 支持的协议：

```text
openai-completions
openai-responses
anthropic-messages
google-generative-ai
```

判断依据：

- Base URL / endpoint；
- API 协议能力；
- Model；
- Credential。

允许为具体 Provider 做参数兼容，但禁止用 Provider 品牌决定是否绕过 Pi。

### 3.6 Provider 配置落地

TuringDesk 使用独立 Pi Agent 目录：

```text
%LOCALAPPDATA%\TuringDesk\PiAgent\
```

TuringDesk 管理的 Provider 配置写入 `models.json`，但 API Key 不写入磁盘。

凭据流：

```text
Windows Credential Manager
  ↓
TuringDesk
  ↓ 当前请求子进程环境
Pi Runtime
```

Pi 的 `models.json` 通过环境变量引用当前 Key。

### 3.7 Agent 工具层

通用能力优先使用 Pi SDK / Pi Tool：

```text
read
write
edit
grep
find
ls
shell
Skills
Extensions
Pi Packages
```

TuringDesk 专属能力通过 Pi `customTools` / Extension 暴露：

```text
Pi Agent
  ↓
TuringDesk Desktop Tools
  ↓
设置 / 壁纸 / Scene / .tdwall / 多屏 / 性能策略
```

文件创建、脚本执行、Git、压缩、CSV、PPTX/DOCX/XLSX 等通用任务不再为每一种任务单独编写 C++ 业务工具。

### 3.8 Windows Shell

Pi 上游默认提供 `bash` 工具，但 TuringDesk 不允许把 Git Bash 作为普通用户必须预装的前提。

正式方案二选一，优先第一种：

```text
方案 A（优先）
Pi SDK custom shell tool
  ↓
PowerShell / cmd

方案 B
Pi builtin bash
  ↓
RuntimeBundle bundled compatible Bash
```

无论采用哪种，Shell 层必须提供：

- cwd；
- stdout / stderr；
- exit code；
- timeout；
- cancellation；
- 风险确认；
- 敏感环境变量隔离。

### 3.9 AI 日志

路由日志：

```text
Windows Desktop known folder\TuringDesk-Logs\l3-runtime.log
```

Pi 详细日志：

```text
Windows Desktop known folder\TuringDesk-Logs\pi-runtime.log
```

日志必须能区分 Node、Pi 版本、Provider、session、prompt、tool call、timeout、exit code 和 Direct API fallback。

API Key / Token 禁止写入日志。

## 4. Pi 与 DeepSeek Harness 的边界

```text
普通 AI = Pi Runtime → 当前 API → Direct API fallback
高级工作台 = DeepSeek Harness WebUI
```

两者共享 Provider / Model / Base URL / API Key 配置，但 Runtime 生命周期互相独立。

必须遵守：

- Harness 关闭时普通 AI 继续可用；
- Pi 失败不能自动打开 Harness；
- Harness 后台可以受 TuringDesk 管理；
- Harness 后台启动不得自动弹浏览器；
- Harness UI 只在用户明确打开时显示。

## 5. Wallpaper Engine 级桌面引擎

桌面引擎按小型专用原生渲染引擎设计。

### 5.1 技术栈

```text
语言                  C++23
桌面集成              Win32 / Explorer / WorkerW
图形 API              Direct3D 11
2D                    Direct2D
Shader                HLSL
显示/适配器           DXGI
视频                  Media Foundation
音频                  WASAPI Loopback + FFT
Web Wallpaper         独立 WebView2 Host
Application Wallpaper 外部 EXE + 原生进程/窗口管理
```

### 5.2 Scene 能力方向

```text
Image
Video
Web
2D Scene
3D Scene
Shader
Particle
Animation
Audio Reactive
Interaction
Multi-monitor
Performance Rules
Application Wallpaper
.tdwall 导入 / 管理 / 编辑
```

普通 Scene 不得因为 AI 或 Web 功能无条件加载 WebView2 / Chromium。

### 5.3 视频与音频

视频：

```text
Video File → Media Foundation → Hardware Decode → D3D11 Texture → Desktop Renderer
```

音频响应：

```text
WASAPI Loopback → FFT → Bass/Mid/Treble/Spectrum → Scene Parameters
```

### 5.4 多显示器与生命周期

必须处理：

```text
每屏独立壁纸
复制 / 跨屏
不同 DPI
显示器插拔
Explorer 重启
睡眠 / 唤醒
锁屏
GPU Device Lost
桌面层重建
```

性能规则支持全屏、最大化、电池、锁屏、休眠下的降帧 / Pause / Stop / 释放 GPU Resource。

## 6. DeepSeek Harness 高级工作台

TuringDesk 不 fork、不重写 DeepSeek Harness。

```text
用户明确打开高级工作台
  ↓
TuringDesk 管理 Harness 后台
  ↓
TuringDeskHarness.exe
  ↓
WebView2
  ↓
官方 DeepSeek Harness WebUI
```

Host 使用 C++ / Win32，Web 容器使用 WebView2。

Harness 启动参数必须保持后台不自动打开外部浏览器，例如使用官方支持的 `--no-open` 行为。

## 7. 推荐进程模型

### 常驻 / 主体

```text
TuringDesk.exe
├─ Tray / Hotkey
├─ Search
├─ Pi Runtime Router
├─ Settings
└─ Desktop coordination
```

### AI 请求时按需

```text
Runtime\Node\node.exe
  ↓
@earendil-works/pi-coding-agent
```

后续稳定后可以保持一个长生命周期 Pi Host 进程，使用 SDK 或 RPC 承载多个 turn，减少重复启动成本。

### 其他按需

```text
TuringDeskWallpaper.exe
TuringDeskHarness.exe + Harness Node runtime
Web Wallpaper Host
Application Wallpaper EXE
```

## 8. RuntimeBundle

ARM64 RuntimeBundle 至少包含：

```text
Node 24 ARM64
Pi production node_modules
Goz / Gozd
DeepSeek Harness production runtime
WebView2 build SDK
```

如果采用 Pi builtin bash，还必须包含兼容 Windows ARM64 的 Bash Runtime。

正式部署必须离线使用 `runtime/arm64/**`，最终用户机器不得在更新时执行 `npm install Pi`。

## 9. Build / CI 架构约束

本地和云端共用同一个 AI 架构 guard：

```text
scripts/verify-l3-runtime-contract.ps1
```

### 本地 CMake

`TuringDesk` target 必须依赖 `TuringDeskL3ContractCheck`，真正编译前先检查 Pi-first 架构。

### ARM64 一键部署

部署与更新脚本必须先执行同一 guard，再准备 RuntimeBundle / 获取已验证 Artifact。

### GitHub Actions

x64 源码验证和 ARM64 正式构建都必须在 Configure / Build 前执行同一 guard。

ARM64 Artifact 必须保留 Pi Runtime 所需的 Node production dependencies。

## 10. Pi CI 最低验证

正式 ARM64 CI 必须验证：

1. Node 24 ARM64 可执行；
2. Pi CLI/SDK 能加载并输出版本；
3. `models.json` 自定义 Provider 可解析；
4. 模拟 OpenAI-compatible Provider 可完成真实 Pi Agent turn；
5. read/write/edit 能产生真实文件结果；
6. Shell 能执行并返回 stdout / stderr / exit code；
7. Pi 路径故意失败时 Direct Model fallback 正常；
8. Goz 集成测试继续通过；
9. DeepSeek Harness smoke test 继续通过；
10. 最终 Artifact 包含 Pi production runtime。

## 11. 完成标准

```text
代码存在        ≠ 完成
编译成功        ≠ 完成
CI 通过         ≠ 完成
Mock 通过       ≠ 完成
真实设备可用    = 完成
```

重点黑盒验收：

- 普通请求显示 `Pi Agent · 主路由`；
- 创建、读取、修改桌面文件真实成功；
- PowerShell/Shell 命令真实执行并回传结果；
- 不依赖 Office 生成 PPTX；
- 故意破坏 Pi 后自动出现 Direct API fallback；
- 两份日志能指出失败层级；
- Wallpaper 真实嵌入 Explorer 并通过多显示器/睡眠/重启验证；
- Harness 后台静默启动，工作台只在用户明确打开时出现。

## 12. 最终边界

```text
TuringDesk Search / AI
  = Native UI + goz + Pi Agent Runtime + Direct API fallback

Wallpaper
  = Native Win32 + D3D11 / Media Foundation / WASAPI

Advanced Workbench
  = Official DeepSeek Harness + WebView2
```

任何旧文档中与 Pi-first 主路由冲突的描述均已废弃，不得恢复。
