# TuringDesk Pi Agent UI Migration Contract

> Status: normative migration rule
>
> Scope: replacement of the old terminal-style Pi surface by the Conversation Panel.
>
> This document supersedes any older requirement that the terminal/raw Pi UI remain as a product-facing advanced workbench.

## 1. Migration decision

The old terminal-style Pi UI is not a compatibility target.

The product migration is:

```text
old terminal-style Pi UI
        ↓
Conversation Panel reaches functional replacement
        ↓
normal Pi entry points switch to Conversation Panel
        ↓
old terminal UI code/path is physically removed
```

TuringDesk must not keep two normal Pi user experiences alive merely for compatibility.

The Conversation Panel becomes the single normal end-user Pi surface.

## 2. What is removed

Once the Conversation Panel has replaced the old path, remove product-facing terminal presentation code and entry points that exist only to render Pi as a console/raw session.

This includes, where no other product dependency remains:

- the old terminal-style Pi window;
- Search Bar routing that launches the terminal UI;
- terminal-specific buttons, layout and user-facing copy;
- compatibility code whose only purpose is to preserve the old Pi UI;
- duplicated session ownership created only for the terminal surface.

Do not keep the old UI hidden behind the new UI.

## 3. What must remain

Removing the old UI must not remove Pi runtime or observability.

Keep the following capabilities:

- bundled Pi runtime;
- persistent Pi session/controller;
- native tool bridge;
- provider/model/API-key configuration;
- cancellation and retry support;
- tool execution state;
- runtime diagnostics;
- raw logs required for debugging;
- structured activity events used by the Conversation Panel.

Presentation is replaced; agent/runtime capability is retained.

## 4. Logging and observability

The new Conversation Panel must preserve a strong diagnostic trail even though normal users no longer see terminal output.

At minimum retain:

```text
pi-runtime.log
```

and add/retain structured records for:

- Pi process/session start and stop;
- configured provider/model/protocol (never log the API key);
- capability/extension registration;
- user turn start/end identifiers;
- tool start/update/result;
- timeout;
- retry;
- cancellation;
- provider/runtime error category;
- malformed RPC/event payloads;
- Conversation Panel event translation failures.

Recommended separation:

```text
pi-runtime.log          // raw runtime/process/RPC diagnostics
pi-activity.log         // structured agent/tool/activity lifecycle
conversation-ui.log     // presentation/event binding failures only
```

Logs are diagnostic infrastructure. They are not rendered verbatim into the normal chat timeline.

## 5. User-visible event translation

Raw runtime information must be translated before display.

Example:

```text
raw log:
tool=desktop_widget_update task=abc rpc=... status=pending

normal UI:
正在调整桌面小组件…
```

The normal Conversation Panel shows observable action state, not JSON, stdout/stderr or private reasoning.

See:

- `docs/PI_AGENT_ACTIVITY_FEEDBACK.md`
- `docs/PI_AGENT_CONVERSATION_UX.md`

## 6. Development transition rule

During implementation, the old terminal UI may exist briefly only while the Conversation Panel is incomplete.

Do not add new product behavior to it.

Any new Pi UX work must go into:

```text
src/native/src/ui/ai/
```

with conversation/session logic separated from rendering.

The old terminal surface is considered migration-only from this point forward.

## 7. Deletion gate

The old UI can be physically deleted when the new Conversation Panel proves all of the following on ARM64 Windows:

1. opens from the Search Bar AI entry;
2. sends a user message to the persistent Pi session;
3. streams Pi text back into one AssistantMessage;
4. shows visible activity while Pi is working;
5. displays tool execution/result status;
6. preserves the conversation when the panel is hidden/reopened;
7. supports cancellation/error display;
8. writes usable runtime/activity logs;
9. preserves provider/model/API-key behavior;
10. does not require the terminal window for any normal Pi task.

After that gate, delete the old terminal presentation rather than hiding it.

## 8. Core rule

> **Keep the logs and runtime. Replace the terminal UI.**

The Conversation Panel is the product surface; raw terminal output is debugging data.