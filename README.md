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
runtime/<arch>/            架构相关基础 Runtime
runtime/agent/             DSH + Pi 统一依赖定义/锁
packaging/windows/         Windows staging、验证与 installer
.github/workflows/         正式 package、Runtime vendor、路径 contract
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
  -> artifact / installer
```

本地等价 staging：

```powershell
cmake -S . -B C:\b\MiaoDesk\x64 -A x64 -DCMAKE_INSTALL_PREFIX=C:\pkg\MiaoDesk\x64
cmake --build C:\b\MiaoDesk\x64 --config Release --target MiaoDesk MiaoDeskWallpaper MiaoDeskHarness --parallel
cmake --install C:\b\MiaoDesk\x64 --config Release --prefix C:\pkg\MiaoDesk\x64
.\packaging\windows\stage-x64.ps1 -Destination C:\pkg\MiaoDesk\x64
.\packaging\windows\verify-path-budget.ps1 -Root C:\pkg\MiaoDesk\x64
```

NSIS 直接消费同一 staging：

```powershell
makensis /DSTAGE_DIR="C:\pkg\MiaoDesk\x64" /DOUTPUT_FILE="MiaoDesk-x64-Setup.exe" packaging\windows\installer.nsi
```

构建目录必须位于源码树外。正式包不得依赖 Windows `LongPathsEnabled`、`subst`、symlink 或 Junction。

## Runtime V3

Pi 与 DeepSeek Harness 共用一棵生产依赖树：

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

DSH/Pi 的直接版本只定义在 `runtime/agent/package.json`，完整间接依赖由 `runtime/agent/package-lock.json` 固定。架构相关的 Node/Goz 由 `runtime/<arch>` 管理。最终用户机器不运行 `npm install` / `npx`，也不依赖系统 Node。

ARM64 当前仍消费旧固定 RuntimeBundle，迁移完成前不删除其现有离线依赖。

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
```

不复制 NuGet 的版本目录、`build/native`、x86、DLL、WinRT headers、`.targets` 或额外元数据。当前来源版本记录在 `THIRD-PARTY-NOTICES.md`；Git 本身固定实际参与构建的 SDK 文件内容。Microsoft Edge WebView2 Runtime 视为 Windows 系统组件，不重复打进仓库。

## AI 运行链

普通 AI / Desktop Agent 请求主路由：

```text
MiaoDesk -> Pi -> bundled Node -> configured Provider
```

DeepSeek Harness 使用同一产品配置，后台服务以 `--no-open` 启动，UI 由 MiaoDesk WebView2 承载。

## 最小验证

正式包只保留真正保护产品的检查：

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
