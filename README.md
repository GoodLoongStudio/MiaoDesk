# MiaoDesk

MiaoDesk 是一个漂亮的智能桌面。用户感知到三个界面：顶部搜索框（应用搜索 / 文件搜索 / 进入 AI 聊天）、动态桌面（动态壁纸与动态小组件）、以及 DeepSeek Harness 专业工作台。所有功能都对照这三者设计。

产品愿景完整定义见 `docs/PRODUCT_VISION.md`。

## 产品进程

正式构建只有三个 Native 目标：

- `MiaoDesk.exe` — 唯一用户入口，负责 Search / AI / 设置
- `MiaoDeskWallpaper.exe` — 独立桌面/壁纸/Widget 进程
- `MiaoDeskHarness.exe` — 独立 DeepSeek Harness 宿主

诊断 acceptance executable 不属于正式 build graph，也不进入用户包。

## 当前产品原则

Wallpaper、Widgets、Search Bar 属于常驻桌面路径，**Native C++ 与性能优先**。

- Wallpaper：载体取壁纸层，运行时 Scene（内部再分静态图 / 动态场景 / 视频）优先；Web 内容按需启动 WebView2。
- Widgets：三款内置组件全部 Native C++ / Direct2D；Web Widget（WebView2 承载组件）路径已整体移除。
- Windows Shell：Progman / WorkerW / Raised Desktop / Explorer recovery 统一由 `DesktopShellHost` 管理。
- 旧 Wallpaper / Scene / Widget Editor、Inspector、Timeline 和 Wallpaper Engine parity 路线已退出当前设计。

当前产品愿景与唯一设计/开发基线：

- `docs/PRODUCT_VISION.md` — 产品是什么、用户感受到什么
- `docs/DESIGN_BASELINE.md` — 唯一设计基线
- `docs/DEVELOPMENT_ROADMAP.md` — 唯一开发路线

## 仓库结构

```text
src/                       Native C++ 产品代码，按领域直接分组
assets/                    产品资源与壁纸包
config/                    随包只读产品默认值（缺失时使用编译期回退）
third_party/webview2/      最小编译期 WebView2 SDK
runtime/agent/             DSH + Pi 唯一依赖定义/锁
runtime/x64/               x64 Node/Goz 基础 Runtime
runtime/arm64/             ARM64 Node/Goz 基础 Runtime
packaging/windows/         Windows staging、验证与 installer
.github/workflows/         正式 package、Runtime vendor、路径 contract
docs/                      当前基线与必要技术契约
skills/                    AI 内容创作 skill 集（生成壁纸/组件内容包）
```

`src/` 不再额外套 `native/src`。`runtime/<arch>` 只存真正与 CPU 架构相关的基础 Runtime；DSH/Pi 不允许再按架构复制一份。

## Windows x64 正式打包

GitHub Actions：`.github/workflows/package-windows-x64.yml`

普通 Native 源码提交由 `.github/workflows/windows-x64-build.yml` 做快速
configure/build；只有影响发行布局、Runtime、CMake、配置或随包资源的改动
才自动运行完整 package workflow。正式包也可通过 `workflow_dispatch` 手动触发。

正式链只有一条：

```text
external short CMake build
  -> cmake --install
  -> packaging/windows/stage.ps1 -Architecture x64
  -> AI/ single dependency graph
  -> path budget
  -> production self-tests
  -> Native Widget PaintReady smoke
  -> moved-install DSH Web smoke
  -> NSIS installer
  -> install/uninstall smoke
  -> artifacts
```

本地等价 staging：

```powershell
cmake -S . -B C:\b\MiaoDesk\x64 -A x64 -DCMAKE_INSTALL_PREFIX=C:\pkg\MiaoDesk\x64
cmake --build C:\b\MiaoDesk\x64 --config Release --target MiaoDesk MiaoDeskWallpaper MiaoDeskHarness --parallel
cmake --install C:\b\MiaoDesk\x64 --config Release --prefix C:\pkg\MiaoDesk\x64
.\packaging\windows\stage.ps1 -Destination C:\pkg\MiaoDesk\x64 -Architecture x64
.\packaging\windows\verify-path-budget.ps1 -Root C:\pkg\MiaoDesk\x64
```

NSIS 直接消费同一 staging：

```powershell
makensis /DSTAGE_DIR="C:\pkg\MiaoDesk\x64" /DOUTPUT_FILE="MiaoDesk-x64-Setup.exe" packaging\windows\installer.nsi
```

正式 package workflow 会同时上传原始 staging 和单文件
`MiaoDesk-x64-Setup.exe`，并对安装器执行静默安装/卸载 smoke test。

构建目录必须位于源码树外。正式包不得依赖 Windows `LongPathsEnabled`、`subst`、symlink 或 Junction。

## Runtime V3

Pi 与 DeepSeek Harness 在 x64/ARM64 上共用同一份依赖定义和完整 lock：

```text
runtime/
  agent/
    package.json
    package-lock.json
  x64/
    node/
    goz/
    runtime-lock.json
  arm64/
    node/
    goz/
    runtime-lock.json
```

最终用户 staging 为：

```text
Runtime/
  Node/
    node.exe
AI/
  package.json
  package-lock.json
  node_modules/
    @deepseek-ai/dsh
    pi/                  # @earendil-works/pi-coding-agent 的短物理目录
Goz/
  goz.exe
  gozd.exe
```

DSH/Pi 的直接版本只定义在 `runtime/agent/package.json`，完整间接依赖只由 `runtime/agent/package-lock.json` 固定。`runtime/<arch>/runtime-lock.json` 只描述该架构的 Node/Goz archive 与 SHA-256。最终用户机器不运行 `npm install` / `npx`，也不依赖系统 Node。

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
- Native Widget `PaintReady`
- Pi CLI `--version`
- DSH CLI `--help`
- DSH Web moved-install smoke
- 85 字符安装根假设下 projected path `<= 248`

历史 preview / acceptance / evidence / updater 脚本不属于正式工程结构。

## 文档

- `docs/DESIGN_BASELINE.md` — 当前唯一设计基线
- `docs/DEVELOPMENT_ROADMAP.md` — 当前唯一开发基线与路线
- `docs/DOC-INDEX.md` — 当前有效技术文档索引
- `docs/LOCAL_AI_ARCHITECTURE.md` — 本地 AI 架构（DGX Spark + 开源模型，含按任务切换模型）
- `docs/LOCAL_AI_DEPLOYMENT.md` — 本地 AI 部署手册
- `docs/NATIVE_SOURCE_LAYOUT.md` — 当前源码布局
- `docs/DESKTOP_DOMAIN_ARCHITECTURE.md` — Desktop domain 边界
- `docs/L3-PI-RUNTIME-CONTRACT.md` — Pi / AI runtime 契约
- `docs/PATH_LAYOUT_CONTRACT.md` — 路径与安装布局约束

## 第三方许可

见 `THIRD-PARTY-NOTICES.md`。
