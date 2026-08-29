# MiaoDesk

MiaoDesk 是 Windows 11 ARM64 原生 AI 桌面：桌面壁纸引擎 + 顶部搜索入口 + Pi Agent AI + DeepSeek Harness 高级工作台。

## Store Demo v0.1（妙喵）

面向 Windows Store 的首发 demo 包范围见：

- `docs/STORE_DEMO_V0.1_PLAN.md`
- `docs/STORE_DEMO_V0.1_ACCEPTANCE.md`

已实现的产品能力：

- 首启一键体验（动态壁纸 + 三款时钟）
- 无 API Key 演示模式（对话里可说「一键体验 / 换壁纸 / 加时钟」）
- 设置页「立即体验动态桌面」
- 高级工作台入口默认隐藏
- Pi Agent 固定工具白名单（有 Key 时）

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-store-demo-scope.ps1
```

## 当前正式平台

- Windows 11 ARM64
- C++23 / Native Win32
- 正式交付只从 `main` 构建
- ARM64 为正式打包与真实设备验收平台

正式原生目标：

- `MiaoDesk.exe` — Search / AI / 设置中心
- `MiaoDeskWallpaper.exe` — 桌面壁纸引擎
- `MiaoDeskHarness.exe` — DeepSeek Harness WebView2 宿主

## AI 固定运行链

普通 AI 和桌面 Agent 请求的默认主路由固定为：

```text
MiaoDesk
  ↓
Pi Runtime
  ↓
Bundled Node 24
  ↓
@earendil-works/pi-coding-agent --mode rpc
  ↓
当前配置 Provider / Model / Base URL / API Key
```

Pi / Node / Provider / Agent Loop 失败时，才回退：

```text
Direct Model Runtime → 当前配置 API
```

API 不绑定模型品牌，路由按 Provider 的协议能力判断。Pi 支持 MiaoDesk 需要的 OpenAI Chat Completions、OpenAI Responses、Anthropic Messages 和 Google Generative AI 等协议。

详细强制契约见：`docs/L3-PI-RUNTIME-CONTRACT.md`。

## Pi 工具能力

通用 Agent 能力由 Pi 管理：

```text
read / write / edit / grep / find / ls
shell
Skills
Extensions
Pi Packages
```

Windows 上，Pi 官方 `bash` 工具的 `shellPath` 被 MiaoDesk 配置为系统 Windows PowerShell，因此普通用户不需要额外安装 Git Bash。模型会被明确告知该工具后端使用 PowerShell 语法。

MiaoDesk 自己只保留真正属于桌面产品的专属工具，例如设置、壁纸、Scene、`.mdwall`、多屏和性能规则。

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
2. 验证 Pi-first AI 架构契约；
3. 校验并展开仓库固定的 RuntimeBundle；
4. 获取当前 `main` 已通过 CI 的 ARM64 原生构建；
5. 运行 Search / Wallpaper / Harness self-test；
6. 验证 Pi、Node 和 Harness Runtime；
7. 启动 MiaoDesk。

第三方运行时不会在用户机器现场通过 npm、Node 官网或 NuGet 下载。Node、Pi 完整生产依赖树、DeepSeek Harness 完整生产依赖树、goz 和编译所需 WebView2 SDK 都由 `runtime/arm64/` 的固定版本清单管理。

> Windows 11 自带/系统维护的 Microsoft Edge WebView2 Runtime 和 Windows PowerShell 视为操作系统组件；仓库内固定的是 WebView2 SDK 和 ARM64 static loader。

## DeepSeek Harness

MiaoDesk 不 fork、不魔改 DeepSeek Harness。仓库 vendoring 流程从官方 `@deepseek-ai/dsh` 固定版本生成完整离线生产依赖树。

后台服务启动必须禁止自动打开外部浏览器：

```text
Runtime\Node\node.exe
Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js web --host 127.0.0.1 --port 3080 --no-open
```

Harness UI 只在用户明确点击“打开 Harness 工作台”时由 `MiaoDeskHarness.exe` 的 WebView2 窗口显示。

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
Desktop\MiaoDesk-Logs\l3-runtime.log
Desktop\MiaoDesk-Logs\pi-runtime.log
Desktop\MiaoDesk-Logs\harness.log
Desktop\MiaoDesk-Logs\widget-runtime.log
```

API Key、Token 和其他凭据不得写入日志。

## 当前正式文档

- `docs/DOC-INDEX.md` — 全部文档索引与状态（先读这篇）
- `docs/MIAODESK-PRODUCT-BASELINE.md` — 唯一产品基线，按八条产品原则组织
- `docs/MIAODESK-NATIVE-TECH-BASELINE.md` — Native 技术基线
- `docs/L3-PI-RUNTIME-CONTRACT.md` — L3 强制架构契约
- `docs/STORE_DEMO_V0.1_PLAN.md` — 当前 demo 交付范围
