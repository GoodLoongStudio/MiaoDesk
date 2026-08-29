# MiaoDesk Pi Agent Activity Feedback Contract

> Status: Product and interaction baseline
>
> Companion specification to `docs/PI_AGENT_CONVERSATION_UX.md`.

## 1. Core rule

Every meaningful Pi action that affects waiting time, the computer, a tool, or task progress must produce user-visible feedback in the Conversation Panel.

The user should never have to wonder whether Pi is frozen, thinking, waiting, executing, retrying, or finished.

At the same time, MiaoDesk must not expose private chain-of-thought or hidden model reasoning. The UI shows **observable work state and action intent**, not internal reasoning tokens.

The product rule is:

> **Show what Pi is doing, not how Pi privately reasons.**

## 2. Required visible activity states

The UI should be able to represent at least these semantic states:

- `Understanding` — interpreting the user's request.
- `Planning` — preparing the next user-visible task steps when planning is useful.
- `Searching` — searching apps, files, settings, wallpapers, widgets, or other indexed data.
- `Inspecting` — reading current desktop/application state.
- `PreparingTool` — preparing an approved MiaoDesk tool operation.
- `RunningTool` — an external/native action is executing.
- `WaitingForSystem` — waiting for Windows, a process, filesystem operation, network request, or runtime response.
- `WaitingForUser` — blocked on confirmation, choice, missing information, or permission.
- `Retrying` — retrying after a recoverable failure.
- `SummarizingResult` — converting tool/runtime results into a human-readable response.
- `Succeeded` — action completed.
- `Failed` — action failed and the user can see a concise reason.
- `Cancelled` — action was cancelled by the user or agent.

These labels are semantic states. The Chinese UI copy should remain natural rather than mechanically exposing enum names.

Examples:

```text
正在理解你的需求…
正在检查当前桌面…
正在搜索截图文件…
正在应用壁纸…
正在等待 Windows 完成操作…
刚才没有成功，正在重试…
正在整理结果…
```

## 3. Every external action must be surfaced

Any tool call or externally observable operation must create or update a timeline activity item.

Examples include:

- searching applications or files;
- enumerating desktop state;
- reading wallpaper/widget configuration;
- applying wallpaper;
- creating, showing, hiding, moving, or removing widgets;
- changing automation/performance settings;
- opening files, folders, or applications;
- moving/copying/deleting files;
- launching a process;
- downloading/preparing a resource;
- waiting for Pi/Harness/native-tool RPC;
- retrying a failed action;
- waiting for confirmation.

The normal user UI must never silently execute these and then suddenly jump to the final answer.

## 4. Update one card instead of flooding the chat

A multi-step operation should normally use one stateful activity card that evolves in place.

Example:

```text
整理桌面截图

✓ 扫描桌面
✓ 找到 47 个截图文件
● 创建目标文件夹
○ 移动文件 23 / 47
○ 整理结果

48%
```

Then:

```text
整理桌面截图

✓ 扫描桌面
✓ 找到 47 个截图文件
✓ 创建目标文件夹
✓ 移动 47 个文件
✓ 完成

桌面/截图整理_2026-08-26
[打开文件夹]
```

Do not create dozens of separate chat bubbles for low-level progress updates.

## 5. Conversation and activity can coexist

Pi may speak naturally while an activity card is present.

Example:

```text
Pi: 好，我先看看桌面上有哪些截图。

[正在扫描桌面截图…]

User: 今天的别动。

Pi: 好，我会排除今天创建的截图。

[activity card updates its criteria/progress]
```

The user must be able to interrupt, correct, or cancel while Pi is working whenever the underlying operation supports it.

## 6. Thinking/activity indicator behavior

When Pi has received a message but has not yet produced visible text or a tool action, show a lightweight activity indicator immediately.

Recommended copy rotates only when the semantic state actually changes:

```text
正在理解你的需求…
正在查看相关信息…
正在准备下一步…
```

Do not fabricate fake progress percentages and do not cycle arbitrary messages merely to appear busy.

Once Pi begins streaming an AssistantMessage, the generic thinking indicator should disappear unless a separate tool/activity is still running.

## 7. Tool status cards

Tool cards should expose user-meaningful information, not protocol details.

Bad:

```text
desktop_widget_update
args={...}
RPC pending
```

Good:

```text
正在调整桌面时钟

✓ 读取当前小组件
● 更新显示状态
○ 保存桌面状态
```

Raw tool names, JSON arguments, stdout/stderr, RPC IDs, and internal Pi traces belong only in the advanced workbench/debug logs.

## 8. Waiting must be explicit

If Pi is blocked, the UI should say what category of wait is happening.

Examples:

```text
正在等待 Windows 完成文件移动…
正在等待文件索引服务响应…
正在等待妙喵桌面应用新的壁纸…
需要你的确认才能继续。
```

A long silent spinner is not acceptable.

For unusually long operations, the card may show elapsed time, current step, and a Cancel action when cancellation is supported.

## 9. Success, failure, retry, and cancellation

Every started user-visible action must end in a terminal state.

### Success

Show what actually changed:

```text
✓ 已应用壁纸
妙喵云境 已设为当前桌面壁纸
```

### Failure

Explain the useful reason and next action without dumping technical logs:

```text
应用壁纸失败
桌面宿主暂时没有响应。
[重试]
```

### Retry

Retry must be visible:

```text
第一次没有成功，正在重试… 2 / 3
```

### Cancellation

Cancellation must also close the activity lifecycle:

```text
已取消整理桌面截图
没有继续移动剩余文件。
```

## 10. Confirmation is an activity state

Destructive or sensitive work should transition the existing task card to `WaitingForUser` instead of opening an unrelated modal when avoidable.

Example:

```text
清理下载目录

✓ 找到 12 个可删除文件
! 等待你的确认

此操作不可撤销
[取消] [确认删除]
```

No destructive tool call may execute before confirmation.

## 11. Event model

The Conversation controller should treat Pi output as a stream of semantic conversation events rather than terminal text.

Recommended event shape:

```text
ConversationEvent
├─ eventId
├─ conversationId
├─ timestamp
├─ type
├─ taskId?          // groups updates for one task/card
├─ state?
├─ title?
├─ detail?
├─ progress?        // only when real progress exists
├─ actions?         // cancel / retry / confirm / open result
└─ payloadRef?      // internal reference, not rendered raw
```

Suggested event types:

```text
UserMessage
AssistantMessageStarted
AssistantMessageDelta
AssistantMessageCompleted
ActivityStarted
ActivityUpdated
ActivityCompleted
ConfirmationRequested
ConfirmationResolved
SystemNotice
```

The UI should update an existing card when `taskId` matches instead of appending duplicate cards.

## 12. Architecture boundary

Visible activity events belong to the conversation/controller layer. Desktop business logic still belongs to the domain services.

```text
Pi Agent / Harness
      ↓ events + tool intent
Conversation Controller
      ├─ emits user-visible activity state
      ↓ tool call
MiaoDesk native tool bridge
      ↓
DesktopControlService
      ↓
WallpaperService / WidgetService / AutomationService / PerformanceService
```

Domain services should return structured operation results/progress where practical; they should not own chat UI strings or draw cards.

## 13. Acceptance gate

A Pi conversation/action flow is not UX-complete unless all of the following are true:

- a visible state appears quickly after the user submits a request;
- every external action is represented in the timeline;
- long operations expose real progress or at least the current semantic step;
- waits are explained;
- retries are visible;
- confirmation blocks are visible and actionable;
- success/failure/cancel always closes the task lifecycle;
- the user can continue chatting while work proceeds when technically possible;
- raw chain-of-thought, JSON, RPC protocol, terminal output, and internal logs are not exposed in the normal UI.

## 14. Product sentence

> **Pi should behave like a capable online friend who tells you what they are doing while helping with your computer, instead of silently operating in the background.**
