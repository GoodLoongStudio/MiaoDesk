# TuringDesk L3 Codex Runtime Contract

> 状态：**强制架构契约**  
> 日期：2026-08-23  
> 适用范围：TuringDesk Native 主线、L3 AI、构建、CI、打包、部署与验收  
> 上位产品基线：`docs/TURINGDESK-PRODUCT-BASELINE.md`

## 1. 目的

本文固定 TuringDesk L3 AI 的唯一运行流程，防止后续开发再次把 Codex CLI 从默认 L3 路由移除，或把 Direct Model 误改成主路由。

如果任何旧文档、旧注释、旧 CI 规则、旧脚本与本文冲突，以 `TURINGDESK-PRODUCT-BASELINE.md` 和本文为准；冲突文本应删除或改为历史说明，不得继续作为实现依据。

## 2. 唯一默认流程

```text
用户在 TuringDesk L3 发起 AI 请求
        ↓
Codex CLI `app-server --stdio`
        ↓
根据当前 Provider 的协议能力选择传输
        ├─ Responses API
        │     ↓
        │   当前配置 API
        │
        └─ OpenAI-compatible Chat Completions
              ↓
          Codex Relay
              ↓
          当前配置 API
```

**Codex CLI 是 L3 默认主路由。**

Direct Model 只允许作为失败回退：

```text
Codex CLI / Relay / 协议链失败
        ↓
记录失败原因
        ↓
Direct Model Runtime
        ↓
当前配置 API
```

不得把 Direct Model 重新定义成普通 L3 的默认路线。

## 3. Provider 无品牌绑定

L3 路由按 **API 协议能力** 判断，不按品牌判断。

正确判断维度：

- 当前模型；
- Base URL / endpoint；
- Responses API 是否可直接使用；
- Chat Completions 是否需要 Relay；
- API Key / Credential 是否可用。

禁止：

```text
if provider == DeepSeek then use Codex
if provider != DeepSeek then bypass Codex
```

DeepSeek、OpenAI-compatible 服务、本地兼容服务以及其他满足协议要求的 Provider 都必须遵循同一套 Codex-first 路由。

Provider-specific 参数兼容补丁可以存在，但不能改变主路由原则。

## 4. Codex 失败的定义

以下任一阶段失败，都必须视为 Codex 路径失败并进入 Direct API fallback：

1. 找不到 `codex.exe` / app-server；
2. 找不到需要的 `codex-relay.exe`；
3. Provider 配置无法映射到 Responses 或可桥接 Chat Completions；
4. `CODEX_HOME` 创建失败；
5. model catalog 生成失败；
6. Relay 进程启动失败；
7. Relay 监听端口未就绪；
8. Codex app-server 进程启动失败；
9. stdio 管道建立失败；
10. `initialize` 失败或超时；
11. `thread/start` 失败或没有 thread id；
12. `turn/start` 失败；
13. 模型 turn 异常退出、超时或返回 error；
14. Codex 进程意外退出。

Fallback 必须可见，不得静默伪装成 Codex 成功。

用户界面至少显示：

```text
[Runtime] Codex CLI · 主路由 · Relay/API
```

发生回退时显示：

```text
[Fallback] Codex CLI 失败，已切换 Direct API；原因已写入日志。
```

## 5. Direct Model fallback 边界

Direct Model fallback 的目的只有一个：**Codex 路径不可用时，保证用户仍然可以完成普通模型对话。**

它可以：

- 普通问答；
- 文本生成；
- 简单多轮对话；
- 直接调用用户当前配置的模型 API。

它不应成为第二套默认 Agent 架构，也不承担 Codex dynamic tools 的完整工具循环。

## 6. Native Tools

Codex CLI 可以通过 TuringDesk 注册的 Dynamic Tools 调用受控桌面能力。

工具实现属于 TuringDesk：

```text
Codex CLI
   ↓ dynamic tool call
TuringDesk NativeTools
   ↓
受控 Windows / 文件 / 桌面能力
```

模型不得直接获得任意 Shell 权限；动作成功必须以 Tool Result 为准。

## 7. 与 DeepSeek Harness 的边界

DeepSeek Harness 是高级工作台，不是 L3 fallback，也不是 L3 transport。

```text
L3：Codex CLI → Relay/API → Direct API fallback
L4：DeepSeek Harness WebUI
```

L3 的 Codex 失败不能自动启动 Harness。

Harness 后台和 UI 生命周期独立；后台启动不得自动弹浏览器或工作台。

## 8. 日志契约

所有 L3 路由和 Codex 失败必须可诊断。

### 8.1 路由日志

```text
%LOCALAPPDATA%\TuringDesk\Logs\l3-runtime.log
```

至少记录：

- 请求选择 Codex 主路由；
- Provider id；
- Model；
- 安全处理后的 endpoint；
- Codex 成功；
- Codex 失败原因；
- Direct API fallback 启动；
- Direct API fallback 成功/失败；
- 用户取消请求。

### 8.2 Codex 详细日志

```text
%LOCALAPPDATA%\TuringDesk\Logs\codex-runtime.log
```

至少记录：

- Codex binary 路径；
- model catalog；
- Relay 启动与 readiness；
- app-server 启动；
- initialize；
- thread/start；
- turn/start；
- process exit code；
- timeout；
- Codex / Relay stderr。

**API Key、Credential 内容、Bearer Token 不得写入日志。**

## 9. 本地构建契约

本地 CMake 构建必须在编译 `TuringDesk.exe` 前执行：

```text
scripts/verify-l3-runtime-contract.ps1
```

CMake 中 `TuringDesk` 必须依赖 `TuringDeskL3ContractCheck`。

因此无论开发者使用命令行 CMake 还是 Visual Studio 生成的工程，只要真正构建 `TuringDesk`，都不能绕过 L3 架构检查。

一键 ARM64 部署入口 `DEPLOY-NATIVE-ARM64.cmd` 也必须先运行同一契约检查。

## 10. 云端 CI 契约

x64 源码验证和 ARM64 正式构建都必须在 Configure/Build 前显式执行同一个 guard：

```text
scripts/verify-l3-runtime-contract.ps1
```

ARM64 CI 还必须验证：

1. `codex.exe --version`；
2. `codex app-server --help`；
3. Relay Responses → Chat Completions 桥接；
4. Codex app-server → Relay → 模拟上游端到端；
5. 最终 Artifact 保留 `Codex/codex.exe`；
6. 最终 Artifact 保留 `CodexRelay/codex-relay.exe`。

不得出现“CI 测完 Codex 后，在上传 Artifact 前把 Codex/Relay 删除”的流程。

## 11. 构建 Guard 必须阻止的回归

构建应直接失败，如果出现任一情况：

- `L3CliWindow` 不再引用 `CodexRuntime`；
- Codex 不再是主路由；
- Direct API fallback 被删除；
- Direct API 被改成默认主路由；
- `CodexRuntime.cpp` 不再编译进 TuringDesk；
- `NativeTools.cpp` 被从 Codex 工具链移除；
- 主路由被硬编码到某个品牌 Provider；
- 路由日志或 Codex 日志被移除；
- ARM64 Artifact 删除 Codex 或 Relay；
- 旧“普通 L3 禁止 Codex/Relay”的规则重新出现。

## 12. 验收标准

CI 通过只是必要条件，不等于真实完成。

真实 Windows ARM64 验收至少要看到：

正常路径：

```text
[Runtime] Codex CLI · 主路由 · Relay/API
AI  <模型真实回答>
```

故意破坏 Codex 后：

```text
[Fallback] Codex CLI 失败，已切换 Direct API
AI  <Direct API 真实回答>
```

同时两份日志能够明确指出失败发生在哪一层。
