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

## 自动化边界

GitHub Actions 只负责检查、编译、测试、打包和固定第三方 RuntimeBundle；不得通过 workflow 自动改产品源码、设计基线或架构 Guard 后再 push `main`。

当前长期保留的 workflow：

- ARM64 正式构建与端到端验证；
- x64 源码兼容性验证；
- PowerShell 语法与当前基线检查；
- ARM64 RuntimeBundle vendoring。

本地 CMake 和 GitHub Actions 共用：

```powershell
scripts/verify-l3-runtime-contract.ps1
scripts/verify-codex-jsonl-wire.ps1
scripts/verify-runtime-log-paths.ps1
scripts/verify-arm64-runtime-bundle.ps1
```

这些 guard 只验证当前代码，不修改源码。

## 一键测试

在 Windows 11 ARM64 上同步 `main` 后，只运行：

```text
DEPLOY-NATIVE-ARM64.cmd
```

脚本会：

1. `git pull --ff-only` 同步 `main`；
2. 验证 L3 Codex-first 架构契约；
3. 校验并展开仓库固定的 RuntimeBundle；
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

RuntimeBundle 的生成、版本和目录约定见 `runtime/arm64/README.md`。

## Runtime diagnostics

所有 Native 运行日志统一写入用户实际 Windows Desktop known folder：

```text
Desktop\TuringDesk-Logs\l3-runtime.log
Desktop\TuringDesk-Logs\codex-runtime.log
Desktop\TuringDesk-Logs\harness.log
```

API Key、Token 和其他凭据不得写入日志。

## 当前正式文档

- `docs/TURINGDESK-PRODUCT-BASELINE.md` — 唯一产品基线
- `docs/TURINGDESK-NATIVE-TECH-BASELINE.md` — Native 技术基线
- `docs/L3-CODEX-RUNTIME-CONTRACT.md` — L3 强制架构契约
