# 创作 Agent · Pi 协议与能力探测记录（CCA-01）

- 建立：2026-09-27。执行者：Claude Code。
- 状态：**已用真实运行时完成探测**。结论中的 Provider 侧验证仍受"无可用视觉 Provider"限制，见第 6 节。
- 关联：[壁纸与组件专用创作 Agent 实施计划](CONTENT_CREATOR_AGENT_PLAN.md) CCA-01；[Pi Runtime Contract](L3-PI-RUNTIME-CONTRACT.md)。
- 这个文档的存在理由：CCA-01 要求"不假设当前文本 RPC 自动支持图片"。那不是假设问题——已经证实它会**静默失败**。

## 1. 探测对象与复现方式

| 项 | 值 |
| --- | --- |
| 锁定的 Pi 版本 | `@earendil-works/pi-coding-agent@0.83.0`（`runtime/agent/package.json`） |
| Pi 实际加载的 ai 层 | `pi-ai@0.83.0`（lock 的第 4545 行，嵌套于 pi-coding-agent 之下） |
| 随包 Node | `node-v24.19.0-win-x64.zip` / `-win-arm64.zip`（`runtime/x64/runtime-lock.json`） |
| 探测方式 | 在本机安装同一版本，以 `--mode rpc` 真实启动，Provider 指向本机 stub HTTP 服务 |
| 复现脚本 | `scripts/probe-pi-rpc-capability.mjs`（10 项断言，退出码 0/1） |

```bash
npm install --prefix /tmp/pi-probe @earendil-works/pi-coding-agent@0.83.0
PI_PACKAGE=/tmp/pi-probe/node_modules/@earendil-works/pi-coding-agent \
  node scripts/probe-pi-rpc-capability.mjs
```

它**不进 CI**：repo-hygiene 的 node 闸门都是"读仓库文本"的纯检查，而这个探测要装整个 Pi 依赖树。作为推送前的手动门更合适。

所有断言都对着 **Provider 实际收到的 HTTP 请求体**，不是对着 RPC 的返回值。理由见第 2 节。

## 2. 决定性结论：图片链路可用，但今天的声明会让它静默失效

`RpcCommand` 的 `prompt` 确实接受 `images: ImageContent[]`（`pi-coding-agent/dist/modes/rpc/rpc-types.d.ts`），形状是：

```ts
{ type: "image"; data: string; mimeType: string }
```

`data` 是**裸 base64**，不带 `data:` 前缀——由 `pi-ai/dist/api/openai-completions.js` 拼成 `data:${mimeType};base64,${data}` 放进 `image_url`。

真实跑一次，同一个 prompt、同一张 1×1 PNG，只改 `models.json` 的 `input`：

| `models.json` 的 `input` | Provider 收到的 user 消息 | RPC 返回 |
| --- | --- | --- |
| `["text","image"]` | `[{"type":"text",...},{"type":"image_url","image_url":{"url":"data:image/png;base64,iVBOR…"}}]` | `response{success:true}` + `agent_settled` 无 error |
| `["text"]`（**产品当前的写法**，`PiRuntime.cpp:455`） | `[{"type":"text",...},{"type":"text","text":"(image omitted: model does not support images)"}]` | `response{success:true}` + `agent_settled` **同样无 error** |

也就是说，`input` 不含 `image` 时，pi-ai 的 `downgradeUnsupportedImages()`（`transform-messages.js:19-37`）把图片换成一句占位文本，**RPC 层、事件层、stdout 全部正常**。base64 从不出现在请求里。

**这条决定了 CCA-09 的验收口径**：不能把"RPC 说成功"或"宿主发过图片"当成"模型看过图"。截图证据必须绑定 candidate digest，视觉评审失败时必须能区分"Provider 不支持视觉"和"宿主没有发出图片"，否则一次无声降级会被记成一次已完成的评审。

同时它限制了 CCA-01 不能顺手把 `input` 改成 `["text","image"]`：chat 能力是保守声明的（`PiRuntime.cpp:449-452` 的注释写明了理由——图片生成有自己的 Provider，不能因此把任意聊天端点都宣称为支持视觉）。**视觉声明必须是按 Profile 显式核验过的一项**，属于 CCA-09 的工作，在那之前保持 `["text"]`。

## 3. 会话与进程隔离：CCA-03 的设计前提已满足

| 探测项 | 结果 | 对 CCA-03 的意义 |
| --- | --- | --- |
| 同一 `PI_CODING_AGENT_DIR` 起两个进程 | 各自拿到不同 `sessionId`，两个进程都活着 | 没有跨进程互斥锁把第二个挡在外面；"聊天 + 一个创作"可以各持一个按需进程 |
| `new_session` RPC | `success:true`、`cancelled:false` | 换作品后可重置上下文，不必杀进程 |
| `--no-session` | **不创建 session 目录** | 宿主必须自己保存作品状态（brief / 候选 / 操作记录），不能假设 Pi 替产品持久化 |
| `abort` 中途发起 | `success:true`，本轮仍走到 `agent_settled` | 取消是协作式的：旧轮次会正常收尾，迟到消息要靠 epoch 挡掉 |
| `PI_CODING_AGENT_DIR` 被扩展读到 | 扩展内 `process.env.PI_CODING_AGENT_DIR` 指向隔离目录 | 配置隔离可用环境变量完成，不需要改 Pi |
| cwd | 继承自宿主 `CreateProcessW` 的 lpCurrentDirectory | 创作可用自己的工作目录；当前 PiRuntime 统一传 DesktopDirectory |
| `--session-dir` / `PI_CODING_AGENT_SESSION_DIR` | CLI help 与 `main.js:493-497` 均支持 | 需要真正保存 Pi 会话时的第二个隔离维度 |

**不需要为创作改 Pi。** 计划 3.2 节的"如锁定版本 Pi 提供已验证的多会话能力可提交替代设计"不触发——这里验证的是**多进程**隔离，比单进程多会话更强，且不用在 Pi 内部维护两份状态策略。

## 4. `--tools` 确实约束扩展注册的工具（CCA-04 的前提）

这是 CCA-04"用创作专属 allowlist 替代通用文件/shell 权限"能不能成立的关键，所以让扩展自己在 `session_start` 上报 `pi.getActiveTools()`：

| 启动参数 | 扩展实际拿到的工具 |
| --- | --- |
| `--tools read,probe_creator_only,probe_second` | `["probe_creator_only","probe_second","read"]` |
| `--tools read` | `["read"]` |
| 省略 `--tools` | `["bash","edit","read","write","probe_creator_only","probe_second"]` |

`--tools` 同时作用于 built-in 与 extension/custom 工具（`cli/args.js:246-248` 的 help 文本，此处已由运行结果证实）。**所以创作会话的通用 `bash/read/edit/write/grep/find/ls` 可以被整体摘掉**，需要的包读写改为受约束工具——这正是不新增第二套通用工具而仍能完整制作的前提。

一个坑记在这里：探测用的扩展 import 了 `typebox`，Node 从扩展所在目录逐级向上找 `node_modules`。工作目录放在系统临时目录下时扩展加载失败，**而那个失败在 RPC 层同样是静默的**（stderr 空、`session_start` 不上报、`getActiveTools()` 无从问起）。所以探测脚本把默认工作目录落在装了 Pi 的那棵树里。宿主的扩展由 `EnsurePiNativeToolsExtension` 写进 `paths::PiAgentRoot()/extensions/`，同样依赖这条解析规则。

## 5. CLI 参数逐项核对

宿主拼出的命令行（`src/ai/pi/PiRuntime.cpp:498-502`）当前用的每个 flag 都在锁定版本中存在，没有一个是失效参数：

```
--mode rpc  --no-session  --approve  --provider  --model
--no-extensions  --extension  --tools  --append-system-prompt
```

`--approve` 是"Trust project-local files for this run"，与 `defaultProjectTrust:"always"`（settings.json）方向一致。

## 6. 未验证项与阻塞

1. **真实视觉 Provider 未见证**。本机没有配置支持视觉的 Provider，第 2 节的 `["text","image"]` 一行是 **stub Provider 收到的请求体**，证明的是"宿主声明的能力确实变成了请求内容"，不是"某个真实模型真的理解这张图"。计划要求的"用合成测试图核验支持视觉的已配置 Provider"仍未做。
2. **`resolveImageApiKey` 之外的图片输入路径未测**。`creator_image_generate` 是另一条链（本机无可用图片服务）。
3. **Windows 进程清理未测**。`TerminateProcess` 的退出码、句柄残留、多次开关后的进程增长，只有 Windows 能给证据。
4. **Provider 对图像数/分辨率/请求大小的限制未测**。计划 6.3 说"以实际支持为准，适用更小者"，这条等真实 Provider。

## 7. 给下一步的结论

- **不用升级 Pi**，0.83.0 的 RPC 已覆盖 prompt+images / abort / new_session / get_state；也不要顺手更新 `runtime/agent/package-lock.json`。
- **CCA-03 按"每个运行时一个进程"实现**：分开 `PI_CODING_AGENT_DIR`、`--session-dir`、cwd、`--tools` 与扩展文件路径即可，不需要 Pi 侧改动。
- **CCA-09 的图片链路先决条件**：先在 Profile 上显式核验目标 Provider 支持视觉，再把 `input` 写成 `["text","image"]`；`PiRuntime` 需要一个"视觉已核验"的来源，而不是依赖模型名猜。
- **`PiRuntime::BuildProviderSetup` 的 signature** 必须把视觉声明纳进去，否则同一进程里改了声明不会重启会话，会继续用旧的 `models.json` 跑。
