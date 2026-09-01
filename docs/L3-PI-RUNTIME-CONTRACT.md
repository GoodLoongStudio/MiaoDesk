# MiaoDesk Pi Runtime Contract

Status: normative runtime contract.

本文件只固定长期有效的 AI Runtime 与 Desktop Control 边界，不记录历史迁移脚本或一次性 CI 方案。

## 1. 默认 AI 路由

普通妙喵 AI / Desktop Agent 请求默认走：

```text
Conversation Panel
    ↓
Pi Runtime
    ↓
Bundled Node
    ↓
@earendil-works/pi-coding-agent
    ↓
current Provider / Model / Base URL / API Key
```

Pi 负责 Agent loop、context、generic tools、skills/extensions；MiaoDesk 负责真正依赖产品内部状态的 Desktop tools。

Direct Model 只能在 Pi Runtime 真实失败时作为轻量 fallback，不能重新成为桌面自动化主路线。

## 2. Runtime 分发

唯一 Agent 依赖定义：

```text
runtime/agent/package.json
runtime/agent/package-lock.json
```

完整 transitive graph 只由 `package-lock.json` 固定。

架构相关基础 Runtime：

```text
runtime/x64/runtime-lock.json
runtime/arm64/runtime-lock.json
```

它们只管理该架构的 Node/Goz archive 和 SHA-256，不重复定义 DSH/Pi。

正式 staging 使用 `npm ci` 从已提交 lock 物化 `Runtime/Agent`。最终用户机器不得运行 `npm install` / `npx`，也不依赖系统 Node/npm。

## 3. Provider-neutral

MiaoDesk 统一管理 Provider、Model、Base URL、API protocol 和 API Key。Provider branding 不得决定是否绕过 Pi，endpoint/protocol semantics 优先。

API Key 长期存储使用 Windows Credential Manager。不得写入 prompt、session、普通配置文件或日志；子进程只通过受控环境获得当前凭据。

## 4. UI 与 Harness 边界

普通 AI 的 canonical UI 是 Conversation Panel。旧终端式 L3 UI 不得恢复成第二套产品入口或第二套 runtime strategy。

```text
Conversation Panel -> Pi Runtime -> Provider
```

DeepSeek Harness 是独立高级工作台：

```text
MiaoDeskHarness.exe
    ↓
Runtime/Node/node.exe
    ↓
Runtime/Agent/.../@deepseek-ai/dsh
```

Pi 失败不能自动打开 Harness；两者可以共享 Provider/Model/Base URL/API Key 配置，但生命周期独立。

## 5. Tool ownership

通用能力归 Pi：

```text
read / write / edit / grep / find / ls
shell / PowerShell
skills / extensions / packages
```

不要为普通文件、脚本、Git、JSON/CSV 等任务继续增加一套 C++ Agent tool。

MiaoDesk Native tools 只暴露依赖内部产品状态的能力，例如 settings、wallpaper state/package、Widget state 和 desktop preview。实际 tool allowlist 以当前 Pi launch/runtime code 为准，不在文档复制第二份易漂移的完整列表。

## 6. Desktop mutation 安全边界

AI 读取状态与生成 preview 可以自动执行；真实桌面 mutation 必须经过产品宿主的校验和用户确认边界。

长期规则：

1. 重要修改前读取真实 Desktop state；
2. 使用稳定 wallpaper / monitor / widget ID；
3. Widget geometry 使用 monitor-relative normalized coordinates；
4. Tool Result 必须返回真实执行结果，模型不能自行宣称成功；
5. AI 不直接修改 private INI/store 作为公共控制接口；
6. Settings、Editor、AI 最终调用同一 Desktop Control path；
7. 更广泛的编辑能力在开放前应有 transaction/undo strategy。

## 7. Desktop Composition

```text
Desktop Composition
├─ Wallpaper Layer
├─ Widget Layer
└─ Control Layer (Settings / Editor / AI)
```

Widget 是独立产品对象：换壁纸不能删除 Widget。AI 是 Desktop Control 的 client，不拥有 Wallpaper/Widget persistence。

## 8. 安全

最低要求：Credential 不进入日志/prompt/session/tool args；Native tool 只执行注册能力；tool worker 支持 timeout/cancellation；高风险 shell/write/delete/system operation 经过相应确认；权限拒绝作为真实 tool result 返回；AI 生成 package 在 Apply 前必须验证。

## 9. Runtime state

Pi 用户状态位于：

```text
%LOCALAPPDATA%\MiaoDesk\PiAgent\
```

用户 session/skills/extensions 与产品 bundled runtime 分离，产品升级不得覆盖用户内容。

Runtime 日志由当前 `RuntimeLogPaths` 统一管理。日志可以记录版本、provider/model、安全 endpoint、tool 状态、timeout、exit code、fallback 原因；禁止记录 API Key/Bearer token。

## 10. 正式验证

不再维护只检查源码 marker 的 L3/PowerShell contract 脚本。正式 package 流程至少验证：

- C++ x64 build/install
- bundled Node 可运行
- DSH CLI `--help`
- Pi CLI `--version`
- `MiaoDesk.exe --self-test`
- `MiaoDeskWallpaper.exe --self-test`
- `MiaoDeskHarness.exe --self-test`
- moved-install Harness Web smoke
- stock-Windows path budget

代码存在或 mock 通过不等于真实用户流程完成。涉及 Agent/tool 行为的改动还需要在真实 Windows 环境验证 provider request、tool execution、Desktop state/preview/apply 边界。

## 11. 禁止回归

不得重新引入：

```text
system Node/npm requirement
package-time unpinned npm install
a second per-architecture Pi/DSH dependency tree
Direct Model as primary desktop Agent
AI direct persistence mutation
old terminal AI product surface
Credential in plain-text config/logs
```
