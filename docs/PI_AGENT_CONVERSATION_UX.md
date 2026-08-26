# TuringDesk Pi Agent Conversation UX

> Status: Product and interaction baseline
>
> Scope: The primary end-user conversation surface for Pi-first Turing AI in TuringDesk.

## 1. Product idea

Pi should feel like a persistent desktop contact: a person-like conversation partner that can continue talking indefinitely, remember the current conversation context, and—when appropriate—operate the user's computer through TuringDesk's controlled native capabilities.

The experience must not look or behave like a terminal. The terminal/raw session remains an advanced/debug surface only.

The normal user mental model is:

> “I am chatting with someone who understands my desktop and can help me do things on it.”

This combines two modes in one continuous conversation:

1. **Conversation** — natural, ongoing chat.
2. **Action** — Pi may invoke approved TuringDesk tools to manipulate wallpaper, widgets, files, settings, automation, search, and other desktop capabilities.

The user should never need to switch mentally between “chat mode” and “agent mode”. Actions should appear as part of the conversation.

## 2. Primary interaction model

The Search Bar remains the lightweight entry point.

```text
Search Bar
    ↓ click AI / submit to Pi
Conversation Panel
    ↓
Persistent Pi session
    ↓
Pi response / tool calls / confirmations / results
```

The Conversation Panel can be opened and closed repeatedly without destroying the active Pi conversation.

Closing the panel means **hide the UI**, not “end the Pi session”.

The active session continues until one of these explicit events occurs:

- user chooses “New conversation” / “Clear conversation”;
- session expires under a defined retention policy;
- application exits and persistence policy does not restore it;
- a fatal agent/runtime reset requires a new session.

## 3. Infinite conversation model

“Infinite conversation” does not mean sending the entire raw transcript to the model forever. It means the user experiences one continuous relationship while the system manages context intelligently.

The conversation runtime should maintain:

- `conversationId`
- ordered message/event history
- latest Pi session/runtime handle
- current tool execution state
- compact conversation summary
- pinned user facts/preferences relevant to the conversation
- recent desktop context
- pending confirmations

When context becomes too large, older content should be summarized rather than simply discarded.

Recommended model:

```text
Recent messages (high fidelity)
+ conversation summary (compressed history)
+ current task/tool state
+ relevant desktop context
+ explicit user memory/context allowed by product policy
```

The UI still renders the full local history that TuringDesk chooses to retain, even when the model only receives a compressed working context.

## 4. Conversation panel behavior

The panel is a native glass floating surface that visually belongs to the Search Bar family.

Use the approved rendering baseline in:

- `docs/WINDOWS_LAYERED_DIRECT2D_UI_RENDERING.md`
- `docs/SEARCH_BAR_VISUAL_SPEC.md`

Recommended first implementation:

- width: ~560 logical px
- max height: ~680 logical px
- rounded corners: ~24 px
- opens below Search Bar when space permits
- opens above Search Bar when lower space is insufficient
- `Esc` hides/collapses the panel
- reopening restores the same conversation and scroll position

The panel should feel like a chat window, not a settings page or developer console.

## 5. Message/event types

The conversation timeline should use a small number of semantic event types.

### 5.1 UserMessage

Normal user text, voice-transcribed text, attachment prompt, or command expressed in natural language.

### 5.2 AssistantMessage

Pi's natural-language response.

Supports streaming display. The message bubble grows as tokens arrive.

### 5.3 ToolAction

A readable card that represents an action Pi is performing.

Never expose raw JSON/tool protocol in the normal UI.

Example:

```text
正在应用壁纸
Aurora Flow
● 准备资源
● 应用到桌面
○ 完成
```

### 5.4 ToolResult

Compact success/failure result card.

Example:

```text
✓ 壁纸已应用
Aurora Flow 已设置为桌面壁纸
```

### 5.5 ConfirmationRequest

Used when an action needs explicit consent.

Example:

```text
准备删除 12 个文件
此操作不可撤销

[取消]  [确认删除]
```

### 5.6 SystemNotice

Non-agent notices such as runtime reconnect, provider unavailable, tool permission failure, or restored session.

Keep these visually quieter than Pi messages.

## 6. “Pi is like an online friend” interaction principles

Pi should feel conversational before it feels mechanical.

### Do

- answer naturally before or while starting an action;
- explain what it is about to do in plain language when useful;
- continue the same topic across many turns;
- remember references like “that wallpaper”, “the files we just moved”, or “the widget from earlier” when the context is still valid;
- report progress in the timeline rather than opening unrelated windows;
- allow the user to interrupt, correct, or redirect while an action is running.

### Do not

- dump terminal output;
- show tool names such as `desktop_widget_update` to normal users;
- expose JSON arguments;
- force the user to create a new chat for each task;
- make every harmless desktop action require a modal confirmation;
- silently perform destructive/high-risk actions.

## 7. Pi-first architecture boundary

The chat UI is only a presentation layer.

It must not own desktop business logic.

```text
Conversation UI
    ↓
Pi conversation/controller layer
    ↓
Pi Agent / Harness
    ↓ tool calls
TuringDesk native tool bridge
    ↓
DesktopControlService
    ↓
WallpaperService / WidgetService / AutomationService / PerformanceService
```

This keeps the same desktop state available to:

- the normal UI;
- Pi;
- future editors;
- automation;
- other product surfaces.

Pi must not bypass `DesktopControlService` to mutate desktop domain stores directly.

## 8. Tool execution UX

A Pi action should be represented as a stateful timeline card.

Suggested states:

```text
Queued
Running
WaitingForConfirmation
Succeeded
Failed
Cancelled
```

Long-running actions should update the same card instead of adding noisy repeated messages.

The user should be able to continue chatting while a tool is running unless the action fundamentally requires serialized interaction.

If the user says “stop”, “算了”, “取消”, or otherwise clearly cancels the task, the UI/controller should attempt to cancel the active Pi/tool execution and update the card to `Cancelled`.

## 9. Permission and safety model

The product should distinguish actions by risk rather than asking permission for everything.

### Low-risk / usually no confirmation

Examples:

- query/search
- list wallpapers/widgets
- apply an already-installed wallpaper
- show/hide a widget
- open a file/folder/application
- change non-destructive TuringDesk preferences

### Medium-risk / contextual confirmation may be appropriate

Examples:

- moving many files
- modifying automation rules
- replacing a user's desktop configuration
- bulk widget changes

### High-risk / explicit confirmation required

Examples:

- delete files
- destructive overwrite
- uninstall/remove packages
- execute an action that could cause data loss
- security-sensitive system changes

Confirmation should live inside the conversation timeline, not as an unrelated Windows dialog when avoidable.

## 10. Interruptibility and correction

The key difference between a useful agent and a one-shot command runner is that the conversation remains alive while actions happen.

Example:

```text
User: 把桌面截图整理到一个文件夹里
Pi: 好，我先扫描桌面的截图文件。
[Tool card: scanning]
User: 等一下，今天的截图不要动
Pi: 好，我会排除今天创建的截图。
[Tool card updates task criteria]
```

The controller should preserve enough execution context to let Pi revise or cancel a task whenever the underlying tool supports it.

## 11. Input behavior

The bottom composer should support:

- keyboard input;
- Chinese IME;
- paste;
- multiline text;
- Windows voice typing;
- future file/image attachments.

Suggested shortcuts:

- `Enter` — send
- `Shift + Enter` — newline
- `Esc` — hide panel
- `Alt + Space` — focus/open Search Bar

The Search Bar and Conversation Panel may share the same active conversation.

A Search Bar query routed to Pi should become a `UserMessage` in the current conversation rather than launching a detached terminal session.

## 12. Streaming response

Pi responses should stream into one AssistantMessage bubble.

Do not create one bubble per token/chunk.

During streaming:

- show a subtle activity indicator;
- continuously update the message content;
- auto-scroll only when the user is already near the bottom;
- if the user scrolls upward, do not fight their scroll position.

## 13. Conversation persistence

The first production version should restore the current conversation after the panel is closed/reopened.

Recommended later persistence:

- current conversation restored across TuringDesk restart;
- local conversation history stored under the TuringDesk application data directory;
- secrets/API keys must never be serialized into transcript history;
- tool results should store user-readable summaries, not sensitive raw runtime payloads.

A separate “History” UI can be added later. It is not required for the first chat-panel milestone.

## 14. Advanced workbench

The existing advanced/raw Pi/Harness interface remains useful for debugging and power users.

It should not be the default user experience.

Normal flow:

```text
Conversation Panel
    ↓
Pi
```

Advanced flow:

```text
Conversation Panel
    ↓ “Open advanced workbench”
Raw Pi/Harness session, logs, tool traces
```

The advanced workbench must observe the same provider/model/API-key configuration as the normal conversation surface.

## 15. First implementation milestone

The first useful version should stay intentionally small.

Required:

1. Search Bar AI action opens the Conversation Panel.
2. One persistent Pi conversation remains alive while panel opens/closes.
3. User and Pi messages render as chat bubbles.
4. Pi replies stream into the UI.
5. Existing Pi tool calls continue to work.
6. Tool calls render as readable status cards instead of terminal text.
7. At least one confirmation-card flow is supported.
8. `Esc` hides the panel without killing the conversation.
9. Reopen restores messages and active session state.
10. Advanced workbench remains available but is no longer the default Pi surface.

Not required for the first milestone:

- multiple named conversations;
- cloud sync;
- complex rich-text editing;
- reactions;
- elaborate avatars;
- multi-agent UI;
- full attachment management.

## 16. Acceptance scenarios

### Continuous chat

```text
User: 你在吗？
Pi: 在，怎么了？
User: 帮我换个舒服一点的壁纸
Pi: 可以。我先看看你现在的桌面配置。
[ToolAction]
Pi: 我建议 Aurora Flow，要直接换吗？
User: 换吧
[ToolAction → success]
User: 这个还不错，再亮一点有没有？
Pi: 有，我再给你换一个更明亮的。
```

This entire sequence must stay in one conversation.

### UI close/reopen

1. Start talking to Pi.
2. Close/collapse the conversation panel.
3. Continue using the desktop.
4. Reopen Pi.
5. Previous messages are still present.
6. Pi still understands references to the current conversation.

### Computer action

1. Ask Pi to perform a supported desktop action.
2. Pi explains the action naturally.
3. Tool status appears as a card.
4. The tool runs through TuringDesk's approved service boundary.
5. Result appears in the same conversation.

### Destructive action

1. Ask Pi to delete files.
2. Pi identifies the target.
3. A ConfirmationRequest card is shown.
4. No destructive tool call executes before confirmation.
5. User can cancel and continue chatting normally.

## 17. Core product rule

The normal Turing AI experience should feel like:

> **A conversation with a capable desktop companion, not a command line attached to an LLM.**

The terminal is an implementation/debugging detail. The conversation is the product.
