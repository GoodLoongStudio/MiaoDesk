# MiaoDesk Desktop Domain Architecture

Status: normative architecture contract.

本文件只描述当前有效的模块边界，不记录历史迁移过程。物理源码布局见 `NATIVE_SOURCE_LAYOUT.md`。

## 1. 进程边界

```text
MiaoDesk.exe
├─ App / Search / Settings UI
├─ AI / Pi Runtime
└─ DesktopControlService client

MiaoDeskWallpaper.exe
├─ Desktop Shell
├─ Wallpaper
├─ Widgets
├─ Automation
└─ Performance

MiaoDeskHarness.exe
└─ DeepSeek Harness host
```

这是小进程架构，不是微服务。进程数量保持少，模块 ownership 保持明确。

当前实现根：

```text
src/
├─ app/
├─ ai/
│  ├─ a2ui/
│  ├─ agent/
│  ├─ pi/
│  └─ tools/
├─ desktop/
│  ├─ automation/
│  ├─ control/
│  ├─ demo/
│  ├─ performance/
│  ├─ preview/
│  ├─ shell/
│  ├─ wallpaper/
│  └─ widgets/
├─ harness/
├─ search/
├─ ui/
└─ include/miaodesk/
```

共享 C++ 接口放在 `src/include/miaodesk/`；只属于单一实现单元的头文件应优先与实现放在同一领域目录，新代码不要为了方便继续扩大公共 include 面。

## 2. 依赖方向

产品调用应沿一个方向流动：

```text
UI / Pi / future editor
        ↓
adapter / controller
        ↓
DesktopControlService
        ↓
domain service
        ↓
persistence / runtime / renderer
        ↓
DesktopShellHost（仅需要 Windows desktop surface 时）
```

禁止新增以下直接依赖：

```text
UI -> wallpaper.ini / private persistence
UI -> WorkerW / Progman / runtime HWND enumeration
UI -> WebView2 child process internals
Pi -> wallpaper.ini / private Widget store
Pi -> runtime HWND enumeration
Renderer -> AI runtime
Wallpaper -> Pi runtime
```

旧实现如果仍需要兼容 bridge，只允许 bridge 收口已有路径，不能继续扩张新的旁路。

## 3. Domain ownership

### Desktop Shell — `src/desktop/shell/`

负责：

- Progman / WorkerW / Windows 11 Raised Desktop discovery
- Explorer restart recovery
- surface attachment / z-order
- desktop surface 所需的显示器拓扑
- 只读 surface telemetry

`DesktopShellHost` 拥有 mutation；`DesktopSurfaceTelemetry` 只读，不得获得 `SetParent` / `SetWindowPos` / repair ownership。

### Wallpaper — `src/desktop/wallpaper/`

负责：

- wallpaper state / package validation
- library / package / import / apply
- Image / Video / Web / Scene lifecycle
- per-monitor assignment
- scaling / render / Web runtime

`library/`、`monitor/`、`render/`、`web/`、`runtime/` 是实际职责边界。`legacy/` 仍包含历史 engine implementation；新功能不得继续堆进 legacy。

### Widgets — `src/desktop/widgets/`

负责：

- Widget persistence
- normalized geometry
- preset/source management
- runtime lifecycle
- surface/runtime health

`DesktopWidgetStore` 是内部 persistence，不是产品 API。UI/AI 应通过 `WidgetService`、`DesktopWidgetController` 或 `DesktopControlService`。

### Automation — `src/desktop/automation/`

负责：

- playlists
- schedules
- profiles
- application rules
- runtime evaluation

`AutomationService` 拥有 persistence/evaluation。当前 Win32 automation UI 仍通过 `ui/automation/AutomationUiAdapter` compatibility boundary，不得直接获得 persistence ownership。

### Performance — `src/desktop/performance/`

负责 fullscreen/maximized/battery/remote/lock/idle 等 policy 输入和 Normal/Throttle/Pause/Stop 决策。

`PerformanceService` 拥有 persisted policy；UI 通过 `PerformanceUiAdapter` 调用。

### AI — `src/ai/`

负责：

- Pi Runtime
- Provider / Model state
- native tools
- Agent orchestration
- A2UI parsing

AI 是 Desktop domain 的 client，不拥有 wallpaper / Widget persistence，也不直接枚举 runtime HWND。

### UI — `src/ui/`

负责：

- render
- user input
- navigation
- preview / inspector
- 调用 controller/service
- 显示 state / error

UI 不拥有 domain rules。

### Search — `src/search/` + `src/ui/search/`

`src/search/` 负责应用/文件搜索能力；`src/ui/search/` 只负责 Search surface 与输入交互。

### Harness — `src/harness/`

负责独立 DeepSeek Harness host 生命周期、配置 bridge 与 bundled runtime bootstrap；不负责桌面 domain persistence。

## 4. Desktop Control

当前共享 facade：`DesktopControlService`。

其职责是把 UI/Pi 请求翻译成 domain service 调用，并提供一致的 desktop snapshot/state。客户端不能因为 facade 缺一个方法就绕过它直接修改 persistence。

典型调用：

```text
Pi JSON arguments
    ↓
AI adapter
    ↓
DesktopControlService
    ↓
WallpaperService / WidgetService / ...
    ↓
DesktopControlResult / DesktopSnapshot
```

新 Widget UI 同样遵循：

```text
Win32 action
    ↓
DesktopWidgetController
    ↓
DesktopControlService
    ↓
WidgetService
```

## 5. 当前 compatibility boundaries

兼容层只在确实承担行为替换时保留。

当前仍有价值的例子：

- `WallpaperEngineProduction.cpp`：对 legacy wallpaper implementation 做 production adapter/macro substitution；不能当空壳删除。
- `WallpaperAutomationWindowProduction.cpp`：把旧 `WallpaperAutomationStore` 调用替换成 `AutomationUiAdapter`；在旧 UI 仍存在时有真实作用。

没有逻辑、只 `#include` 另一个 `.cpp` 的 production wrapper 不应保留。Wallpaper Library 已直接编译 `WallpaperLibraryWindowV2.cpp`。

## 6. Source / Header rule

- 领域实现放在对应 `src/<domain>/...`。
- 跨多个 target/domain 的稳定接口才放 `src/include/miaodesk/`。
- 新 target-local header 与 `.cpp` 就近放置。
- 不允许重新引入 `src/native/src` 或第二层 source root。
- 不允许为了“架构感”创建无调用价值的 facade/helper/library。

## 7. 完成标准

架构重构是否完成看行为边界，不看文件名：

```text
Pi creates Widget -> UI sees the same Widget
UI moves Widget -> Pi reads the same updated geometry
UI/Pi read the same DesktopSnapshot/runtime health
UI applies wallpaper -> Pi reads the same current state
Pi applies wallpaper -> UI reflects the same current state
Automation UI edits policy -> runtime consumes the same persisted state
Explorer restarts -> desktop runtime recovers without UI/AI special handling
```

Domain service 拥有状态转换；客户端只表达 intent。
