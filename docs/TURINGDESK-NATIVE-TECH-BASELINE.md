# TuringDesk Native 技术路线基线

- 状态：**已确认**
- 日期：2026-08-23
- 适用范围：TuringDesk Native 主线
- 正式开发分支：`main`
- 产品基线：`docs/TURINGDESK-PRODUCT-BASELINE.md`
- L3 详细契约：`docs/L3-CODEX-RUNTIME-CONTRACT.md`
- 旧实现：`legacy/turingdesk-wpf/` 仅作历史参考

> 本文已替换 2026-08-21 版本中“L3 直接 WinHTTP、禁止本地代理、Codex 不进入普通 L3”的旧技术路线。旧路线不得恢复。

## 1. 三个核心能力

```text
TuringDesk
├─ A. 顶部统一入口：应用 / 文件 / L3 AI
├─ B. Wallpaper Engine 级桌面引擎
└─ C. DeepSeek Harness 高级工作台
```

Native 主线以 Windows 性能、稳定性、低常驻资源和清晰故障边界为优先目标。

## 2. 总体技术原则

1. 主桌面核心使用 C++23 / Win32。
2. Search UI 不使用 WPF、Electron、CEF、Qt 或 WebView。
3. L3 默认主路由固定为 **Codex CLI**。
4. OpenAI-compatible Chat Completions Provider 通过 **Codex Relay** 适配 Codex Responses 协议。
5. 原生 Responses Provider 可以由 Codex 直接连接，不强制经过 Relay。
6. Codex / Relay 失败时才回退 **Direct Model Runtime → 当前配置 API**。
7. Provider 路由按协议能力判断，不能绑定 DeepSeek 品牌。
8. L3 不自动启动 DeepSeek Harness；Harness 是独立高级工作台。
9. Web Runtime 只在 Harness 或 Web Wallpaper 等确有需要的场景按需启动。
10. 代码存在、编译通过、Mock/Loopback 通过都不等于产品完成；真实 Windows 设备可用才算完成。

## 3. Desktop Search / L1-L3

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

### 3.2 L1 应用搜索

TuringDesk 维护应用发现与排序，至少覆盖：

```text
Start Menu
App Paths
注册表应用信息
UWP / MSIX
常用系统程序
```

排序逐步支持 Exact / Prefix / Substring / Fuzzy / 使用频率 / 最近启动。

### 3.3 L2 文件搜索

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

L2 故障不得阻塞 L3 AI。

### 3.4 L3 唯一默认运行链

```text
用户请求
  ↓
TuringDesk L3 UI
  ↓
Codex CLI `app-server --stdio`
  ↓
  ├─ Responses Provider ─────────────→ 当前 API
  │
  └─ Chat Completions Provider
          ↓
      Codex Relay
          ↓
      当前 API
```

Codex CLI 是普通 AI 请求和桌面 Agent 请求的默认 Runtime。

如果 Codex / Relay / app-server / 协议协商任一阶段失败：

```text
记录失败
  ↓
Direct Model Runtime
  ↓
当前配置 API
```

Direct Model 是 **fallback**，不是默认主路由。

### 3.5 Provider 兼容原则

API 不限定为 DeepSeek。

判断依据只有：

- Base URL / endpoint；
- Responses API 能力；
- OpenAI-compatible Chat Completions 能力；
- Model；
- Credential。

允许为具体 Provider 做参数兼容，但禁止用 Provider 品牌决定是否绕过 Codex。

### 3.6 L3 Native Tools

```text
Codex CLI
  ↓ Dynamic Tools
TuringDesk NativeTools
  ↓
受控 Windows / 文件 / 桌面能力
```

工具由 TuringDesk 注册、校验、测试和审计。模型输出不能直接获得任意 PowerShell / CMD / Shell 权限，也不能绕过 Tool Result 声称动作成功。

### 3.7 L3 日志

路由日志：

```text
%LOCALAPPDATA%\TuringDesk\Logs\l3-runtime.log
```

Codex / Relay 详细日志：

```text
%LOCALAPPDATA%\TuringDesk\Logs\codex-runtime.log
```

日志必须能区分 binary、Relay、model catalog、initialize、thread/start、turn/start、timeout、exit code 和 Direct API fallback。

API Key / Token 禁止写入日志。

## 4. L3 与 L4 的边界

```text
L3 = Codex CLI → Relay/API → Direct API fallback
L4 = DeepSeek Harness WebUI
```

L3 与 L4 共享 Provider / Model / Base URL / API Key 配置，但 Runtime 生命周期互相独立。

必须遵守：

- Harness 关闭时 L1/L2/L3 继续可用；
- Codex 失败不能自动打开 Harness；
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
├─ Search L1
├─ Search L2
├─ L3 Router
├─ Settings
└─ Desktop coordination
```

### L3 请求时按需

```text
Codex\codex.exe app-server --stdio
CodexRelay\codex-relay.exe   # 仅需要协议桥时
```

### 其他按需

```text
TuringDeskWallpaper.exe
TuringDeskHarness.exe + Harness Node runtime
Web Wallpaper Host
Application Wallpaper EXE
```

## 8. Build / CI 架构约束

本地和云端共用同一个 L3 guard：

```text
scripts/verify-l3-runtime-contract.ps1
```

### 本地 CMake

`TuringDesk` target 必须依赖 `TuringDeskL3ContractCheck`，真正编译前先检查 Codex-first 架构。

### ARM64 一键部署

`DEPLOY-NATIVE-ARM64.cmd` 必须先执行同一 guard，再准备 RuntimeBundle / 获取已验证 Artifact。

### GitHub Actions

x64 源码验证和 ARM64 正式构建都必须在 Configure / Build 前执行同一 guard。

ARM64 Artifact 必须保留：

```text
Codex\codex.exe
CodexRelay\codex-relay.exe
```

不能出现“测试通过后为了压缩包体积又删除 Codex / Relay”的流程。

## 9. 完成标准

```text
代码存在        ≠ 完成
编译成功        ≠ 完成
CI 通过         ≠ 完成
Mock 通过       ≠ 完成
Loopback 通过   ≠ 完成
真实设备可用    = 完成
```

重点黑盒验收：

- L3 正常请求显示 `Codex CLI · 主路由 · Relay/API`；
- 故意破坏 Codex 后自动出现 Direct API fallback；
- 两份日志能指出失败层级；
- Wallpaper 真实嵌入 Explorer 并通过多显示器/睡眠/重启验证；
- Harness 后台静默启动，工作台只在用户明确打开时出现。

## 10. 最终边界

```text
TuringDesk Search
  = Native UI + goz + Codex CLI + optional Codex Relay + Direct API fallback

Wallpaper
  = Native Win32 + D3D11 / Media Foundation / WASAPI

Advanced Workbench
  = Official DeepSeek Harness + WebView2
```

任何旧文档中“普通 L3 禁止 Codex”“L3 只能 Direct WinHTTP”“L3 禁止本地 Relay”“Direct Model 是默认路由”的描述均已废弃，不得恢复。
