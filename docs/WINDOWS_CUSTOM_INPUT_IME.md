# Windows 自绘输入框 IME / TSF 实现规范

Status: current implementation note for MiaoDesk native Windows UI.

## 1. 职责边界

共享输入基础设施使用 `InputImeAnchor`，Search 与 Conversation 只负责各自的可见矩形、caret 和业务行为。

当前共享实现：

```text
src/include/miaodesk/InputImeAnchor.h
```

不要为每个 surface 复制一套 IME anchor。`Input` 是基础设施，`Search` / `Conversation` 是使用它的业务 surface。

## 2. 不使用 1×1 EDIT proxy

现代微软拼音主要由 TSF 驱动。Windows 可能同时读取 focused HWND、真实窗口矩形、thread caret、`GetGUIThreadInfo()` caret 以及 composition/candidate 几何。

因此自绘输入框必须保持一个真实、完整的 Native `EDIT` 作为 Windows 输入语义载体：

```text
Custom visual input
├─ DirectWrite / Direct2D
│  ├─ committed text
│  ├─ composition text
│  ├─ selection
│  └─ visible caret
└─ Native EDIT
   ├─ 覆盖真实输入区域
   ├─ 持有 keyboard focus
   ├─ 接收 WM_CHAR / WM_KEY* / WM_IME_*
   ├─ 发布真实 Win32 caret
   └─ 不负责可见绘制
```

禁止重新使用 `MoveWindow(edit, caret.x, caret.y, 1, 1, FALSE)` 一类 1×1 proxy 方案。

## 3. InputImeAnchor 契约

共享入口：

```cpp
PublishInputImeAnchor(edit, caret, exclusionArea);
```

它统一负责：

```text
SetCaretPos
→ HideCaret
→ ImmSetCompositionWindow(CFS_FORCE_POSITION)
→ ImmSetCandidateWindow(CFS_EXCLUDE)
→ HideCaret
```

并处理 focus、IME 通知、deferred re-anchor 与 reentrancy guard。

## 4. Search profile

当前参考实现：

```text
src/include/miaodesk/InputImeAnchor.h
src/include/miaodesk/SearchWindow.h
src/ui/search/SearchWindow.cpp
```

Search 保留自己的业务命名与布局。Native `EDIT` 只承担输入语义，不绘制第二份文本或 caret。

## 5. Conversation profile

当前实现：

```text
src/include/miaodesk/InputImeAnchor.h
src/ui/ai/ConversationPanel.cpp
src/ui/ai/ConversationPanelInputOverlay.inc
```

Conversation 使用同一套 anchor 基础设施，同时保留自己的附件、语音、发送、聊天与布局逻辑。

Composition 存在时，视觉 caret 与系统 caret 都必须位于可见 composition 末端，而不是只使用 committed text 的 `EM_GETSEL` 位置，否则会出现双光标。

## 6. 几何与 DPI

以下数据必须处于同一 DPI / client 坐标体系：

- visible text rectangle
- DirectWrite layout
- Native `EDIT` rectangle
- caret
- candidate exclusion rectangle

Native `EDIT` 不得覆盖附件、麦克风或发送按钮。禁止使用固定 `+8px`、`-20px` 等魔法偏移修复 IME；应修正 HWND / caret / TSF geometry 的一致性。

## 7. 必须同步的消息

至少覆盖：

```text
WM_SETFOCUS
WM_KEYUP
WM_CHAR
WM_IME_STARTCOMPOSITION
WM_IME_COMPOSITION
WM_IME_ENDCOMPOSITION
WM_INPUTLANGCHANGE
WM_SIZE
IMN_OPENCANDIDATE
IMN_CHANGECANDIDATE
```

Focus 顺序应保持：恢复真实 Native `EDIT` geometry → 完成 focus transaction → deferred anchor → 发布 Win32 caret → 更新 composition/candidate geometry。

## 8. 验证

仓库不再维护独立 IME contract PowerShell/workflow；不要重新创建只检查源码 marker 的测试脚本。

修改 `InputImeAnchor`、Search 输入 surface 或 Conversation 输入 surface 时，至少完成：

1. 正式 C++ x64 构建；
2. 微软拼音 `nihao` / `ni'hao`；
3. composition 末端只有一根可见 caret；
4. 候选框贴合真实输入位置；
5. 上屏、光标中间插入、Backspace/Delete；
6. 中英文切换；
7. 窗口 move/resize；
8. 100% / 125% / 150% / 200% DPI；
9. 多显示器跨屏；
10. Search 与 Conversation 同时打开互不污染；
11. 至少一个第三方 IME 基本输入。

这类行为依赖真实 Windows IME/TSF，不能用源码文本 contract 代替真实交互验证。

## 9. 新 Native 输入 surface

如果未来增加新的自绘文本输入 surface，应：

```text
定义 VisibleTextRect / caret calculation
→ 使用 InputImeAnchor
→ DirectWrite 负责视觉
→ Native EDIT 负责 Windows 输入语义
```

只有真实共享的基础逻辑进入 Input 层，业务行为继续留在具体 surface。当前不为已删除的 Wallpaper/Scene/Widget Editor 预留输入架构。
