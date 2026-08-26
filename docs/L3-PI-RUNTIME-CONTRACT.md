# TuringDesk Pi Runtime Contract

> 状态：**强制架构契约**  
> 日期：2026-08-26  
> 适用范围：TuringDesk Native 主线、图灵 AI、Desktop Control、构建、CI、打包、部署与验收  
> 上位产品基线：`docs/TURINGDESK-PRODUCT-BASELINE.md`  
> Desktop Composition 架构：`docs/DESKTOP_COMPOSITION_ARCHITECTURE.md`

## 1. 目的

本文固定图灵智能桌面 AI 的默认 Agent Runtime 为 **Pi**，并固定 AI 与桌面系统之间的控制边界。

Pi 负责 Agent Loop、上下文、Skills、Extensions 与通用工具；TuringDesk 负责桌面状态、权限、壁纸、Widget、多屏、性能和其他真正依赖产品内部状态的能力。

如果旧文档、旧注释、旧 CI 规则、旧脚本与本文冲突，以 `TURINGDESK-PRODUCT-BASELINE.md`、`DESKTOP_COMPOSITION_ARCHITECTURE.md` 和本文为准。

## 2. 唯一默认 AI 流程

```text
用户在 TuringDesk 发起 AI 请求
        ↓
TuringDesk Pi Runtime Host
        ↓
Bundled Node 24
        ↓
@earendil-works/pi-coding-agent
        ↓
Pi Agent Loop
        ├─ read / write / edit / grep / find / ls
        ├─ shell / PowerShell
        ├─ Skills / Extensions / Packages
        └─ TuringDesk Desktop Tools
        ↓
当前配置 Provider / Model / Base URL / API Key
```

Pi 是普通问答和桌面 Agent 请求的默认 Runtime。

只有 Pi Runtime、Node、Provider、Agent Session 或工具循环失败时，才允许回退：

```text
Pi 失败
  ↓
记录真实失败原因
  ↓
Direct Model Runtime
  ↓
当前配置 API
```

Direct Model 只负责轻量 fallback，不得重新成为桌面自动化路线。

## 3. Pi 版本与分发

正式 RuntimeBundle 使用并锁定：

```text
@earendil-works/pi-coding-agent
@earendil-works/pi-agent-core
@earendil-works/pi-ai
Bundled Node 24
```

约束：

- 最终用户不需要安装 Node、npm、Pi 或开发环境；
- Pi 生产依赖离线随 TuringDesk 分发；
- 正式构建和更新不得临时联网安装 Pi；
- Pi 更新必须经过版本锁、Windows CI、真实工具 E2E 后进入 `main`。

## 4. Provider-neutral

TuringDesk 统一管理：

- Provider；
- Model；
- Base URL；
- API Key；
- API 协议。

支持的 Pi Provider 协议至少包括：

```text
openai-completions
openai-responses
anthropic-messages
google-generative-ai
```

不得用 Provider 品牌决定是否绕过 Pi。

API Key 的唯一长期存储是 Windows Credential Manager。Pi 配置文件不得写明文 Key；当前凭据仅通过受控子进程环境注入。

## 5. Pi Host 与产品边界

TuringDesk Native 主程序不实现第二套 Agent Loop。

当前生产形态以 Pi RPC 为主，后续可迁移到更深的 Pi SDK Host，但必须保持同一产品边界：

```text
Native UI
  ↓
Pi Runtime
  ↓
Agent Loop
  ↓
Desktop Tools / Generic Tools
```

Native UI 负责：

- 输入和流式输出；
- Runtime 生命周期；
- Provider 配置桥接；
- 权限确认；
- 日志；
- TuringDesk Desktop Tool Host。

Pi 负责：

- Agent 规划；
- 工具循环；
- Session / Context；
- Skills / Extensions / Packages；
- 通用文件和 Shell 能力。

### 5.1 Conversation Panel UI contract

普通图灵 AI 的唯一生产展示面是 `TuringDesk.Native.ConversationPanel`。旧终端式 `TuringDesk.Native.L3CliWindow` 已退休，不得作为第二条 UI 路径恢复。

当前迁移期间允许保留 `L3CliWindow` 的兼容文件名/函数名作为内部 ABI/构建 shim，但它们不得重新拥有旧终端视觉或独立 Runtime 策略。新的 UI 代码以 `ConversationPanel.h` 为 canonical include；legacy header 只允许继续缩减。

禁止恢复：

```text
TuringDesk.Native.L3CliWindow window class
Consolas terminal presentation
AI window-local ModelSettingsWindow entry
separate terminal transcript/input product surface
UI-specific provider routing that bypasses Pi-first
```

Conversation Panel 可以演进视觉、布局、富文本和工具结果展示，但运行时路由仍必须保持：

```text
Conversation Panel
  -> Pi Runtime
  -> current Provider / Model / Base URL / API Key
  -> Direct Model only on real Pi failure
```

这项 UI 清理不代表 M4 完成；M4 仍必须在 M3 真实 Windows gate 关闭后按完整产品 shell parity 规则推进。

## 6. 工具边界

### 6.1 通用能力归 Pi

```text
read
write
edit
grep
find
ls
shell / PowerShell
Skills
Extensions
Pi Packages
```

文件、脚本、Git、压缩、CSV/JSON、文档处理等通用任务，不得继续为每一种业务单独增加一套 C++ Agent Tool。

### 6.2 TuringDesk Desktop Tools

TuringDesk 只暴露依赖产品内部状态的能力。

当前第一阶段白名单：

```text
settings_open
wallpaper_create_web_package
wallpaper_validate_package
wallpaper_state_get
wallpaper_apply_web_package
desktop_widget_create_web
desktop_widget_update
desktop_widget_remove
desktop_widget_list
```

含义：

- `settings_open`：打开图灵智能桌面设置；
- `wallpaper_create_web_package`：生成并校验 Web `.tdwall`；
- `wallpaper_validate_package`：校验 `.tdwall`；
- `wallpaper_state_get`：读取真实当前桌面状态；
- `wallpaper_apply_web_package`：实际应用已校验 Web `.tdwall`；
- `desktop_widget_create_web`：创建持久桌面 Widget；
- `desktop_widget_update`：移动、缩放、启停、改样式/HTML；
- `desktop_widget_remove`：删除 Widget；
- `desktop_widget_list`：读取 Widget ID、目标显示器和布局。

这些是 **Desktop Control API 的第一阶段桥接工具**，不是最终 API 形状。

后续壁纸 Scene 参数、播放列表、多屏、性能策略、Widget 属性、编辑器属性都必须逐步收口到同一个版本化 Desktop Control API。

## 7. AI Desktop Control 强制规则

Settings、Editor 和 AI 最终必须操作同一套状态模型：

```text
Settings Center -----------┐
Scene / Widget Editor -----+--> Desktop Control API --> Desktop Runtime
Pi Agent ------------------┘
```

AI 桌面操作必须满足：

1. 重要修改前先读状态；
2. 所有 mutation 必须校验；
3. 使用稳定 wallpaper / monitor / widget ID；
4. Widget 布局使用显示器相对的 normalized geometry；
5. Tool Result 必须返回真实执行结果，模型不能自行宣称成功；
6. 更广泛的 Scene/Widget 编辑在开放给 AI 前必须补 transaction 与 undo/redo；
7. AI 不得把直接修改私有 INI/内部文件作为长期公共控制接口；
8. 手工 UI、Editor、AI 不允许长期维护三套互不一致的修改逻辑。

## 8. Wallpaper 与 Widget 的关系

Desktop Composition 正式分层：

```text
Desktop Composition
├─ Wallpaper Layer: Image / Video / Web / Scene
├─ Widget Layer: persistent monitor-relative surfaces
└─ Control Layer: Settings / Editor / AI
```

Widget 是一等公民，不属于某张壁纸的临时附属物：

- 换壁纸不得删除 Widget；
- Widget 可以独立创建、移动、缩放、启停；
- Widget v1 使用隔离本地 WebView2 surface；
- Widget v1 默认 click-through，不得挡住桌面图标；
- 后续支持 Native Text、Clock、Calendar、Image、System、Media、Data-bound Widget；
- `.tdwall` 与未来 `.tdwidget` 应共享安全 package core。

## 9. 权限与安全

最低要求：

- API Key、Token 不得出现在提示词、工具参数、日志和 Session 文件；
- Native Tool worker 只接受白名单工具；
- Native Tool 必须隔离执行、支持 timeout/cancellation；
- 写文件、删除、覆盖、执行程序、系统设置修改按风险分类；
- 高风险 Shell 操作需要用户确认；
- 权限拒绝必须作为真实 Tool Result 返回；
- Widget 数据源、网络能力和未来交互能力必须权限化；
- AI 生成 `.tdwall` / `.tdwidget` 必须标记 provenance 并验证后才能应用。

## 10. Session / Skills / Extensions

TuringDesk Pi 目录：

```text
%LOCALAPPDATA%\TuringDesk\PiAgent\
```

结构：

```text
PiAgent\
├─ models.json
├─ settings.json
├─ sessions\
├─ skills\
├─ extensions\
└─ packages\
```

Provider 配置、TuringDesk 自带扩展和用户自定义 Skills/Extensions 必须分层，更新不得覆盖用户内容。

## 11. 与高级工作台的边界

```text
普通 AI
  = Pi Runtime → 当前 API → Direct Model fallback

高级工作台
  = DeepSeek Harness WebUI
```

Pi 失败不得自动打开高级工作台。两者可共享 Provider / Model / Base URL / API Key 设置，但 Runtime 生命周期互相独立。

## 12. 日志契约

统一日志目录：

```text
Windows Desktop known folder\TuringDesk-Logs\
```

```text
l3-runtime.log   # AI 路由
pi-runtime.log   # Pi / Tool / Native worker
```

至少记录：Node/Pi 版本、Provider/model、安全 endpoint、session、prompt、tool 名称与状态、timeout/cancellation、process exit code、fallback 原因。

禁止记录 Credential 内容、API Key、Bearer Token。

## 13. 构建与 CI 契约

所有 Windows 构建继续执行：

```text
scripts/verify-l3-runtime-contract.ps1
scripts/verify-windows-powershell-compat.ps1
```

Guard 必须防止：

- Pi-first 被旧 Runtime 替换；
- Direct Model 重新成为主路由；
- Native Desktop Tool 白名单回退到旧的三工具状态；
- Widget/Desktop Control 工具从 Pi Extension 或 Native worker 中意外消失；
- 通用 C++ Agent Tools 重新暴露；
- 旧 Codex/Relay 架构重新进入主线；
- 旧终端 AI UI class、Consolas 展示和 window-local AI 设置入口重新进入生产路径。

ARM64 CI 至少持续验证：

1. Bundled Node / Pi 可加载；
2. RPC `get_state` readiness handshake；
3. Provider loopback 真正收到请求；
4. Pi built-in `write` 与 Windows Shell 真执行；
5. TuringDesk Native Tool 真执行并产生 `.tdwall`；
6. Desktop Control/Widget Tool 保持在白名单和扩展注册表中；
7. Native wallpaper self-tests；
8. goz MFT/USN integration；
9. 高级工作台 smoke；
10. ARM64 artifact 完整上传。

## 14. 完成标准

```text
代码存在        ≠ 完成
编译成功        ≠ 完成
CI 通过         ≠ 完成
Mock 通过       ≠ 完成
真实 Windows 用户流程通过 = 完成
```

桌面控制最低真实验收逐步扩展为：

```text
读取当前桌面状态
创建一个动态 Web 壁纸并真正应用
在桌面右上角创建一个时钟 Widget
把 Widget 移到另一位置并缩放
删除 Widget
切换壁纸后确认 Widget 布局仍然存在
```

## 15. 最终边界

```text
TuringDesk Search / AI
  = Native UI + goz + Pi Agent Runtime + Direct Model fallback

Desktop Composition
  = Wallpaper Layer + Widget Layer + Desktop Control API
  = Native Win32 / D3D11 / Direct2D / Media Foundation / WASAPI / isolated WebView2

Advanced Workbench
  = Official DeepSeek Harness + WebView2
```

正式方向固定为：**Pi-first、Provider-neutral、Desktop-Control-unified、Host-controlled permissions**。
