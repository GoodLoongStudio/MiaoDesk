# TuringDesk

TuringDesk 是 Windows 11 ARM64 原生 AI 桌面：桌面壁纸引擎 + 顶部搜索入口 + Codex CLI AI + DeepSeek Harness 高级工作台。

## 当前正式平台

- Windows 11 ARM64
- C++23 / Native Win32
- 正式交付只从 `main` 构建
- ARM64 为正式打包与真实设备验收平台

正式原生目标：

- `TuringDesk.exe` — Search / L1-L3 / 设置中心
- `TuringDeskWallpaper.exe` — 桌面壁纸引擎
- `TuringDeskHarness.exe` — DeepSeek Harness WebView2 宿主

旧 C# / .NET / WPF 实现已冻结在 `legacy/turingdesk-wpf/`，只用于历史参考。

## L3 AI 固定运行链

普通 L3 AI 请求的默认主路由固定为：

```text
TuringDesk L3
  ↓
Codex CLI `app-server --stdio`
  ↓
  ├─ Responses API → 当前配置 API
  └─ Chat Completions → Codex Relay → 当前配置 API
```

Codex CLI / Relay / app-server / 协议链失败时，才回退：

```text
Direct Model Runtime → 当前配置 API
```

API 不绑定 DeepSeek 品牌，路由按 Provider 的协议能力判断。

详细强制契约见：`docs/L3-CODEX-RUNTIME-CONTRACT.md`。

## 本地与云端构建保护

本地 CMake 和 GitHub Actions 共用：

```powershell
scripts/verify-l3-runtime-contract.ps1
```

这个 guard 会阻止以下回归：

- Codex CLI 被移出 L3；
- Direct API 被重新改成默认主路由；
- `CodexRuntime.cpp` 不再参与 TuringDesk 编译；
- Codex / Relay 被从 ARM64 Artifact 删除；
- 日志链路被移除；
- 主路由被硬编码到某个 Provider 品牌。

## 一键测试

在 Windows 11 ARM64 上同步 `main` 后，只运行：

```text
DEPLOY-NATIVE-ARM64.cmd
```

脚本会：

1. `git pull --ff-only` 同步 `main`；
2. 先验证 L3 Codex-first 架构契约；
3. 从仓库自身 `runtime/arm64/` 校验并展开固定 RuntimeBundle；
4. 获取当前 `main` 已通过 CI 的 ARM64 原生构建；
5. 运行 Search / Wallpaper / Harness self-test；
6. 验证 Codex CLI、Codex Relay 和 Harness Runtime；
7. 启动 TuringDesk。

第三方运行时不会在用户机器现场通过 npm、Node 官网、NuGet 或 Codex Release 下载。Node、DeepSeek Harness 完整生产依赖树、goz、Codex Relay、Codex ARM64 和编译所需 WebView2 SDK 都由 `runtime/arm64/` 的固定版本清单管理。

> Windows 11 自带/系统维护的 Microsoft Edge WebView2 Runtime 视为操作系统组件；仓库内固定的是 WebView2 SDK 和 ARM64 static loader。

## DeepSeek Harness

TuringDesk 不 fork、不魔改 DeepSeek Harness。仓库 vendoring 流程从官方 `@deepseek-ai/dsh` 固定版本生成完整离线生产依赖树。

后台服务启动必须禁止自动打开外部浏览器：

```text
Runtime\Node\node.exe
Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js web --host 127.0.0.1 --port 3080 --no-open
```

Harness UI 只在用户明确点击“打开 Harness 工作台”时由 `TuringDeskHarness.exe` 的 WebView2 窗口显示。

不会回退到系统 Node、全局 npm、`npx` 或在线安装。

## RuntimeBundle

版本锁：`runtime/arm64/runtime-lock.json`

完整性检查：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-arm64-runtime-bundle.ps1
```

L3 架构检查：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-l3-runtime-contract.ps1
```

RuntimeBundle 的生成、版本和目录约定见 `runtime/arm64/README.md`。

## 日志

L3 路由：

```text
%LOCALAPPDATA%\TuringDesk\Logs\l3-runtime.log
```

Codex / Relay 详情：

```text
%LOCALAPPDATA%\TuringDesk\Logs\codex-runtime.log
```

日志不得记录 API Key 或 Token。

## 文档

当前产品基线：`docs/TURINGDESK-PRODUCT-BASELINE.md`  
L3 强制架构契约：`docs/L3-CODEX-RUNTIME-CONTRACT.md`  
Native 技术基线：`docs/TURINGDESK-NATIVE-TECH-BASELINE.md`

## Runtime diagnostics

All native runtime logs are written to the user's actual Windows Desktop known folder:

```text
Desktop\TuringDesk-Logs\l3-runtime.log
Desktop\TuringDesk-Logs\codex-runtime.log
Desktop\TuringDesk-Logs\harness.log
```

API keys and credentials must never be written to these logs.
