# MiaoDesk

MiaoDesk 是 Windows 原生 AI 桌面：动态壁纸引擎、顶部搜索入口、Pi Agent 和 DeepSeek Harness 工作台。

## 产品进程

正式构建只有三个 Native 目标：

- `MiaoDesk.exe` — 唯一用户入口，负责 Search / AI / 设置
- `MiaoDeskWallpaper.exe` — 独立桌面/壁纸进程
- `MiaoDeskHarness.exe` — 独立 DeepSeek Harness 宿主

诊断 acceptance executable 不属于正式 build graph，也不进入用户包。

## 仓库结构

```text
src/native/                Native 产品代码
assets/                    产品资源与壁纸包
third_party/webview2/      最小编译期 WebView2 SDK
runtime/<arch>/            固定的架构运行时基础包/版本锁
packaging/windows/         Windows 正式 staging 与包验证
packaging/nsis/            NSIS installer
.github/workflows/         正式构建、Runtime vendor、路径 contract
docs/                      当前产品/技术文档
```

`runtime/` 只存运行时；编译 SDK 不允许放回 `runtime/<arch>`。

## Windows x64 正式打包

GitHub Actions：`.github/workflows/package-windows-x64.yml`

正式链只有一条：

```text
external short CMake build
  -> cmake --install
  -> packaging/windows/stage-x64.ps1
  -> Runtime/Agent single dependency graph
  -> path budget
  -> production self-tests
  -> moved-install DSH Web smoke
  -> artifact
```

本地等价 staging：

```powershell
cmake -S . -B C:\b\MiaoDesk\x64 -A x64 -DCMAKE_INSTALL_PREFIX=C:\pkg\MiaoDesk\x64
cmake --build C:\b\MiaoDesk\x64 --config Release --target MiaoDesk MiaoDeskWallpaper MiaoDeskHarness --parallel
cmake --install C:\b\MiaoDesk\x64 --config Release --prefix C:\pkg\MiaoDesk\x64
.\packaging\windows\stage-x64.ps1 -Destination C:\pkg\MiaoDesk\x64
.\packaging\windows\verify-path-budget.ps1 -Root C:\pkg\MiaoDesk\x64
```

构建目录必须位于源码树外。正式包不得依赖 Windows `LongPathsEnabled`。

## Runtime V3

x64 的 Pi 与 DeepSeek Harness 共用一棵生产依赖树：

```text
Runtime/
  Node/
    node.exe
  Agent/
    package.json
    package-lock.json
    node_modules/
      @deepseek-ai/dsh
      @earendil-works/pi-coding-agent
Goz/
  goz.exe
  gozd.exe
```

DSH 和 Pi 版本由 `runtime/x64/runtime-lock.json` 固定；最终用户机器不运行 `npm install` / `npx`，也不依赖系统 Node。

ARM64 当前仍消费固定 RuntimeBundle，后续迁移到同一 Runtime V3 staging 模型。

## WebView2

仓库只保留 MiaoDesk 实际参与编译的最小 SDK：

```text
third_party/webview2/
  include/
    WebView2.h
    WebView2EnvironmentOptions.h
  lib/
    x64/WebView2LoaderStatic.lib
    arm64/WebView2LoaderStatic.lib
  LICENSE.txt
  NOTICE.txt
  manifest.json
```

版本只记录在 `manifest.json`，不再复制 NuGet 的版本目录、`build/native`、x86、DLL、WinRT headers 或 `.targets`。Microsoft Edge WebView2 Runtime 视为 Windows 系统组件，不重复打进仓库。

## AI 运行链

普通 AI / Desktop Agent 请求主路由：

```text
MiaoDesk -> Pi -> bundled Node -> configured Provider
```

DeepSeek Harness 使用同一产品配置，后台服务以 `--no-open` 启动，UI 由 MiaoDesk WebView2 承载。

## 最小验证

正式包保留真正保护产品的检查：

- `MiaoDesk.exe --self-test`
- `MiaoDeskWallpaper.exe --self-test`
- `MiaoDeskHarness.exe --self-test`
- Pi CLI `--version`
- DSH CLI `--help`
- DSH Web moved-install smoke
- 85 字符安装根假设下 projected path `<= 248`

历史 preview / acceptance / evidence 脚本不属于正式工程结构。

## 文档

- `docs/MIAODESK-PRODUCT-BASELINE.md` — 产品基线
- `docs/MIAODESK-NATIVE-TECH-BASELINE.md` — Native 技术基线
- `docs/DESKTOP_COMPOSITION_ARCHITECTURE.md` — 桌面组合架构
- `docs/DESKTOP_DOMAIN_ARCHITECTURE.md` — Desktop domain 边界
- `docs/L3-PI-RUNTIME-CONTRACT.md` — Pi / AI runtime 契约
- `docs/PATH_LAYOUT_CONTRACT.md` — 路径与安装布局约束
- `docs/REPOSITORY_RUNTIME_V3_REFACTOR_PLAN.md` — Runtime V3 重构规划

## 第三方许可

见 `THIRD-PARTY-NOTICES.md`。
