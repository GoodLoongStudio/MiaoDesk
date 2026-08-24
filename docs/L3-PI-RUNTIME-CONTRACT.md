# TuringDesk L3 Pi Runtime Contract

> 状态：**强制架构契约**  
> 日期：2026-08-24  
> 适用范围：TuringDesk Native 主线、L3 AI、构建、CI、打包、部署与验收  
> 上位产品基线：`docs/TURINGDESK-PRODUCT-BASELINE.md`

## 1. 目的

本文固定 TuringDesk L3 AI 的默认 Agent Runtime 为 **Pi**，目标是让 TuringDesk 拥有一个轻量、可嵌入、Provider-neutral、可扩展的 Agent 核心，而不是把产品架构绑定在某个单一模型厂商或复杂的外部宿主协议上。

如果旧文档、旧注释、旧 CI 规则、旧脚本与本文冲突，以 `TURINGDESK-PRODUCT-BASELINE.md` 和本文为准；冲突内容应删除或改为历史说明，不得继续作为实现依据。

## 2. 唯一默认流程

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
        ├─ TuringDesk Shell Tool
        ├─ Skills / Extensions / Packages
        └─ TuringDesk Desktop Tools
        ↓
当前配置 Provider / Model / Base URL / API Key
```

**Pi 是普通 AI 请求和桌面 Agent 请求的默认 Runtime。**

轻量 Direct Model 只允许作为失败回退：

```text
Pi Runtime / Node / Provider / Agent Loop 失败
        ↓
记录失败原因
        ↓
Direct Model Runtime
        ↓
当前配置 API
```

不得把 Direct Model 重新定义成默认 Agent 路线。

## 3. Pi 版本与分发

TuringDesk 使用 Earendil Works 维护的正式 Pi 包：

```text
@earendil-works/pi-coding-agent
@earendil-works/pi-agent-core
@earendil-works/pi-ai
```

正式 RuntimeBundle 必须锁定明确版本并离线随 TuringDesk 分发。运行用户不需要自行安装 Node、npm 或 Pi。

当前约束：

- Node 由 TuringDesk RuntimeBundle 提供；
- Pi 依赖安装发生在 vendor CI，而不是最终用户机器；
- RuntimeBundle 中保存完整生产依赖；
- 正式构建和更新流程不得临时联网安装 Pi；
- Pi 更新必须通过版本锁、CI、E2E 后进入 `main`。

## 4. Provider 无品牌绑定

Pi Runtime 不按品牌决定主路由。

TuringDesk 统一配置：

- Provider；
- Model；
- Base URL；
- API Key；
- API 协议类型。

Pi 自定义 Provider 使用其正式支持的协议类型：

```text
openai-completions
openai-responses
anthropic-messages
google-generative-ai
```

TuringDesk 根据已探测出的 API 能力生成 Pi Provider 配置。Provider-specific 兼容参数可以存在，但不得把主 Runtime 绑定到某个品牌。

API Key 的唯一长期存储仍是 Windows Credential Manager。Pi 配置文件不得写入明文 Key；Pi 子进程通过受控环境变量读取当前凭据。

## 5. Pi Host 形态

TuringDesk Native 主程序不实现第二套 Agent Loop。

推荐顺序：

1. **Pi SDK Host**：Bundled Node 进程直接使用 `createAgentSession()` / `AgentSession`，适合注册 TuringDesk 自定义工具和权限层；
2. **Pi RPC Mode**：作为进程隔离和兼容模式，使用 LF-delimited JSONL over stdio；
3. 不使用 TUI 作为 TuringDesk 顶部 AI 的宿主界面。

Native UI 只负责：

- 输入与流式输出；
- Runtime 生命周期；
- Provider 配置桥接；
- 权限确认；
- 日志；
- TuringDesk 专属桌面工具。

Agent 规划、工具循环、上下文与 Skills/Extensions 由 Pi 管理。

## 6. 工具边界

### 6.1 通用能力

优先由 Pi 提供或通过 Pi SDK 标准工具接入：

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

文件创建、脚本执行、Git、压缩、CSV、PPT/Word/Excel 生成等通用任务，不应继续为每一种任务单独增加 C++ 业务工具。

### 6.2 Windows Shell

TuringDesk 不要求用户预装独立开发环境。

正式 Runtime 必须提供一个稳定的 Windows Shell 路径。推荐实现为：

```text
Pi Agent
  ↓
TuringDesk Shell Tool
  ↓
PowerShell / cmd / bundled compatible shell
```

Shell Tool 必须支持：

- cwd；
- stdout / stderr；
- exit code；
- timeout；
- cancellation；
- 危险操作权限确认；
- 不记录 API Key / Token。

如果采用 Pi 自带 `bash` 工具，则 RuntimeBundle 必须同时提供兼容 Bash，不能把 Git Bash 作为用户先决条件。

### 6.3 TuringDesk 专属工具

只保留真正属于产品的能力，例如：

- 打开设置中心；
- 应用/切换桌面；
- 创建、校验 `.tdwall`；
- 场景、播放列表、多屏与性能策略；
- TuringDesk 通知与桌面状态。

这些工具通过 Pi SDK `customTools` 或 Extension 注册，不得重新实现完整 Agent Runtime。

## 7. 权限与安全

Pi 本身保持轻量，TuringDesk 必须在宿主层补齐桌面产品需要的权限策略。

最低要求：

- 读文件可默认允许在用户工作区内执行；
- 写文件、删除、覆盖、执行程序、系统设置修改必须经过风险分类；
- 高风险 Shell 命令需要用户确认；
- API Key 绝不出现在提示词、工具参数、日志和会话文件中；
- 子进程环境只注入当前请求需要的凭据；
- 权限拒绝必须作为 Tool Result 返回给 Agent，而不是伪装成功。

## 8. Session / Skills / Extensions

Pi 的 Session、Skills、Extensions 和 Pi Packages 是正式扩展机制。

TuringDesk 使用独立目录：

```text
%LOCALAPPDATA%\TuringDesk\PiAgent\
```

建议结构：

```text
PiAgent\
├─ models.json
├─ settings.json
├─ sessions\
├─ skills\
├─ extensions\
└─ packages\
```

TuringDesk 管理的 Provider 配置与用户自定义 Skills/Extensions 必须分层保存；更新 Provider 时不能覆盖用户自己的 Skills、Extensions 或 Session。

## 9. 与 DeepSeek Harness 的边界

DeepSeek Harness 是高级工作台，不是 L3 fallback，也不是 L3 transport。

```text
L3：Pi Runtime → 当前 API → Direct Model fallback
高级工作台：DeepSeek Harness WebUI
```

Pi 失败不能自动打开 Harness。

Harness 后台和 UI 生命周期独立；后台启动不得自动弹浏览器或工作台。

Pi 与 Harness 可以共用 Provider / Model / Base URL / API Key 配置，但运行时互相独立。

## 10. 日志契约

统一日志目录：

```text
Windows Desktop known folder\TuringDesk-Logs\
```

L3 路由日志：

```text
l3-runtime.log
```

Pi 详细日志：

```text
pi-runtime.log
```

至少记录：

- Pi Host 路径；
- Node 路径与版本；
- Pi 版本；
- Provider id / model；
- 安全处理后的 endpoint；
- session 创建；
- prompt 开始/完成；
- tool call 名称与完成状态；
- process exit code；
- timeout / cancellation；
- Direct Model fallback 原因。

禁止记录 API Key、Credential 内容、Bearer Token。

## 11. 构建与 CI 契约

本地 CMake、x64 验证、ARM64 正式构建和一键部署必须执行同一个架构 guard：

```text
scripts/verify-l3-runtime-contract.ps1
```

Guard 必须验证：

- `L3CliWindow` 默认使用 `PiRuntime`；
- `PiRuntime.cpp` 编译进 `TuringDesk`；
- Direct Model 只作为 fallback；
- 当前设计文档均为 Pi-first；
- RuntimeBundle 锁定 Pi；
- 正式构建不依赖在线 npm install；
- Harness 仍保持独立；
- 旧 Runtime 代码、Relay、旧 RuntimeBundle 目录和旧 guard 不得重新进入主线。

ARM64 CI 至少验证：

1. Bundled Node 可执行；
2. Pi CLI/SDK 可加载并输出版本；
3. Pi 自定义 Provider 配置可被解析；
4. Pi Agent 对模拟 OpenAI-compatible 上游完成真实工具循环 E2E；
5. 文件读写工具真实产生结果；
6. Shell Tool 能执行并返回 stdout / stderr / exit code；
7. Direct Model fallback 仍可用；
8. Goz 与 Harness 原有测试继续通过；
9. Artifact 包含 Pi Runtime 与全部生产依赖。

## 12. 完成标准

```text
代码存在        ≠ 完成
编译成功        ≠ 完成
CI 通过         ≠ 完成
Mock 通过       ≠ 完成
真实设备可用    = 完成
```

真实 Windows ARM64 至少验收：

```text
帮我在桌面创建一个 txt 文件
帮我读取并修改这个文件
帮我运行一个 PowerShell 命令并返回结果
帮我生成一个不依赖 Office 的 PPTX
```

必须看到真实文件/命令结果，不能只根据模型文字判断成功。

## 13. 最终边界

```text
TuringDesk Search / AI
  = Native UI + goz + Pi Agent Runtime + Direct Model fallback

Wallpaper
  = Native Win32 + D3D11 / Media Foundation / WASAPI

Advanced Workbench
  = Official DeepSeek Harness + WebView2
```

TuringDesk 的 Agent 核心从此以 **Pi-first、Provider-neutral、Host-controlled permissions** 为正式方向。