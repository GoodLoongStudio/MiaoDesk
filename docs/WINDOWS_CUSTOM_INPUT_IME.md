# Windows 自绘输入框 IME / TSF 实现规范

Status: reusable implementation note for MiaoDesk native Windows UI.

## 1. 命名与职责边界

共享框架统一叫 **Input**，具体界面仍保留自己的业务名称。

```text
InputImeAnchor
├─ Search profile
└─ Conversation profile
```

当前共享实现：

```text
src/native/include/miaodesk/InputImeAnchor.h
```

不要再创建 `SearchImeAnchor.h`。但也不要把 `SearchWindow`、`SearchEdit`、`Search` 业务逻辑机械改名成 Input；同理，Conversation 仍然是 Conversation。

原则：

> **Input 是输入基础设施；Search / Conversation 是使用该基础设施的具体 surface/profile。**

---

## 2. 为什么不能使用 1×1 EDIT proxy

历史错误模式：

```cpp
MoveWindow(edit, caret.x, caret.y, 1, 1, FALSE);

COMPOSITIONFORM composition{};
composition.dwStyle = CFS_POINT;
composition.ptCurrentPos = {0, 0};
```

这种做法对现代微软拼音不可靠，因为微软拼音主要由 TSF 驱动。TSF 可能同时读取：

- focused HWND；
- focused HWND 的真实矩形；
- Win32 thread caret；
- `GetGUIThreadInfo()` 暴露的 caret；
- focus / candidate / composition 创建时的几何状态。

因此，即使自绘 UI 看起来正确，只要真实 `EDIT` 是 1×1 或系统 caret 在错误位置，就可能出现：

- 拼音与视觉 caret 分离；
- 候选框跑到窗口左下角；
- 双光标；
- DPI / resize 后漂移。

---

## 3. 标准架构

```text
Custom visual input
├─ DirectWrite / Direct2D
│  ├─ background
│  ├─ committed text
│  ├─ IME composition text
│  ├─ selection
│  └─ visible caret
│
└─ Native EDIT
   ├─ 完整真实输入区域
   ├─ 拥有 keyboard focus
   ├─ 接收 WM_CHAR / WM_KEY* / WM_IME_*
   ├─ 发布真实 Win32 caret
   └─ 不负责可见绘制
```

核心原则：

> **视觉可以完全自绘，但 Windows 必须始终看到一个位置、尺寸、caret 都真实可信的原生输入控件。**

---

## 4. `InputImeAnchor.h` 的职责

共享框架负责：

- native `EDIT` 与可见输入区域同步；
- Win32 caret publication；
- `HideCaret()`；
- `CFS_FORCE_POSITION`；
- `CFS_EXCLUDE`；
- TSF / IMM32 兼容；
- deferred re-anchor；
- focus / key / IME / candidate 消息后的同步；
- busy / pending / geometry reentrancy guard。

共享发布入口：

```cpp
PublishInputImeAnchor(edit, caret, exclusionArea);
```

它统一执行：

```text
SetCaretPos
→ HideCaret
→ ImmSetCompositionWindow(CFS_FORCE_POSITION)
→ ImmSetCandidateWindow(CFS_EXCLUDE)
→ HideCaret
```

具体 surface 只负责计算自己的真实几何和 caret。

---

## 5. Search profile

Search 是当前已验证稳定的参考实现。

相关文件：

```text
src/native/include/miaodesk/InputImeAnchor.h
src/native/include/miaodesk/SearchWindow.h
src/native/src/ui/search/SearchWindow.cpp
```

Search profile 保留明确的 Search 名称：

```cpp
EnsureSearchImeGeometry(...)
AnchorSearchImeToVisibleCaret(...)
RequestSearchImeAnchor(...)
HandleSearchEditMessageBefore(...)
HandleSearchEditMessageAfter(...)
```

Search 当前可见输入矩形保持已验证参数：

```text
left   = 52
right  = 594
top    = 13
height = 30
```

Search 的 native EDIT 不绘制：

```cpp
if (message == WM_PAINT) {
    ValidateRect(hwnd, nullptr);
    return 0;
}
if (message == WM_ERASEBKGND) return 1;
```

Search 的业务名称、窗口类、搜索行为都不属于 Input 框架，不应改名。

---

## 6. Conversation profile

Conversation 使用同一套 InputImeAnchor 基础设施，但保留自己的布局、提交、附件、语音和聊天业务逻辑。

相关文件：

```text
src/native/include/miaodesk/InputImeAnchor.h
src/native/src/ui/ai/ConversationPanel.cpp
src/native/src/ui/ai/ConversationPanelInputOverlay.inc
```

Conversation profile：

```cpp
EnsureConversationImeGeometry(...)
ConversationNativeCaretPoint(...)
AnchorConversationImeToNativeCaret(...)
RequestConversationImeAnchor(...)
SyncConversationImeAnchor(...)
```

旧的聊天框逻辑不得重新出现：

```cpp
MoveWindow(state.input, caret.x, caret.y, 1, 1, FALSE);
composition.dwStyle = CFS_POINT;
candidate.dwStyle = CFS_CANDIDATEPOS;
```

现有兼容入口 `PositionConversationInputProxyAnchored()` 只允许委托：

```cpp
input_ime_detail::SyncConversationImeAnchor(state.input);
```

它不再拥有自己的 IME 几何算法。

---

## 7. Composition 与双光标

Conversation 会自己绘制 `GCS_COMPSTR`，因此可见文本可能是：

```text
committed + composition
```

但此时原生 EDIT 的 `EM_GETSEL` 通常仍停留在 committed insertion point。

如果 DirectWrite caret 仍只使用 `EM_GETSEL`，就会出现：

```text
|ni'hao
```

也就是用户看到的“拼音前面多一根光标”。

正确规则：

- 没有 composition：visual caret = `EM_GETSEL`；
- 有 composition：visual caret = 可见 composition 末端；
- native / TSF caret 同样需要加上 composition 的实际文本宽度；
- candidate anchor 使用同一个最终 caret。

当前共享实现通过：

```cpp
ReadImeCompositionText(edit)
MeasureEditTextWidth(...)
ConversationNativeCaretPoint(...)
```

把系统 caret 推到自绘拼音末尾，从而让：

```text
DirectWrite caret
Win32 caret
TSF caret
candidate anchor
```

描述同一个位置。

---

## 8. Conversation 输入矩形必须和视觉层一致

不能让 native EDIT 覆盖附件、麦克风或发送按钮区域。

Conversation 的 visible text right 由当前布局推导：

```text
sendLeft = client.right - 74
mic      = sendLeft - 8 - 34
attach   = mic - 4 - 34
textRight = attach - 8
```

因此标准偏移为：

```text
textRight = client.right - 162
```

`ConversationEditRect()` 必须与这个可见文本区域保持一致，而不是使用另一套近似宽度。

---

## 9. Focus / resize / IME 消息同步顺序

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

Focus 时：

```text
1. 先恢复完整 native EDIT geometry
2. 让 focus transaction 继续
3. PostMessage deferred anchor
4. 发布真实 Win32 caret
5. 更新 composition / candidate geometry
```

不要在所有 IME 回调里同步递归调用 `ImmSet*`。部分输入法会因为 setter 再触发通知，因此必须使用 pending / busy guard。

---

## 10. DPI 规则

以下必须来自同一 DPI 坐标体系：

- visible text rectangle；
- DirectWrite layout；
- native EDIT rectangle；
- caret；
- candidate exclusion rectangle。

Conversation 使用项目统一的 DPI scaling，再在 `EDIT` client 坐标中发布 caret。

禁止通过 `+8px`、`-20px` 等魔法偏移修复 IME。

---

## 11. 验证脚本与 CI

共享框架 contract：

```text
scripts/verify-input-ime-contract.ps1
```

Search surface contract：

```text
scripts/verify-search-input-contract.ps1
```

Search contract 会继续验证 Search UI 自身，并调用共享 Input contract。

独立 CI：

```text
.github/workflows/input-ime-contract.yml
```

只要修改共享 `InputImeAnchor.h`、Search 输入 surface 或 Conversation 输入 surface，就必须重新验证两套 profile，避免共享框架修改只测试其中一个调用方。

---

## 12. 新输入框接入规则

以后 Widget Editor、Scene Editor、设置中的自绘文本框等，不要重新实现 IME。

正确方式：

```text
新 Surface
  ↓
定义自己的 VisibleTextRect / caret calculation
  ↓
接入 InputImeAnchor shared framework
  ↓
DirectWrite 负责视觉
Native EDIT 负责 Windows 输入语义
```

如果新 surface 需要专属行为，应增加明确的 profile，例如：

```text
WidgetEditor profile
SceneEditor profile
```

而不是把业务逻辑塞进 generic Input framework。

---

## 13. 最低验收清单

每个接入 InputImeAnchor 的自绘输入框至少验证：

1. 微软拼音输入 `nihao` / `ni'hao`；
2. 拼音末端只有一根视觉 caret；
3. 候选框紧贴正确输入位置；
4. 上屏后 caret 位于 committed text 末端；
5. 光标移动到字符串中间继续输入；
6. Backspace / Delete；
7. 中英文切换；
8. 窗口移动和 resize；
9. 100% / 125% / 150% / 200% DPI；
10. 多显示器跨屏；
11. 微软拼音候选翻页；
12. 搜狗等第三方 IME 基本行为；
13. Search 与 Conversation 同时打开时互不污染；
14. native EDIT 永远不绘制第二份文字或 caret。

任何一项失败，都不应通过增加固定坐标偏移解决；应回到 InputImeAnchor 的 HWND / caret / TSF geometry 一致性检查。
