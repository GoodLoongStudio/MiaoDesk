# Windows 自绘输入框 IME / TSF 实现规范

Status: reusable implementation note for MiaoDesk native Windows UI.

## 目的

MiaoDesk 的搜索框、聊天框、编辑器、Widget 属性输入等界面经常采用“视觉完全自绘，但仍需要系统级中文输入法”的模式。

这类控件最容易出现的问题不是文字绘制，而是 **Windows 输入法认为的 caret / HWND 几何信息与界面实际绘制位置不一致**。典型症状包括：

- 微软拼音的拼音组合串出现在输入框左下角、窗口边缘或错误坐标；
- 候选词窗口与视觉 caret 相隔很远；
- DPI、窗口移动、窗口缩放后位置漂移；
- IMM32 手动定位看起来正确，但微软拼音仍然忽略；
- 自绘 caret 正确，TSF 看到的系统 caret 却仍在别处。

本规范记录 MiaoDesk 搜索框中已经验证过的正确方案，并作为后续所有 Windows 自绘输入控件的默认实现方式。

---

## 核心结论

### 不要使用 1×1 `EDIT` 跟随视觉 caret 的代理方案

错误模式类似：

```cpp
POINT caret = VisualCaretPoint();
MoveWindow(edit, caret.x, caret.y, 1, 1, FALSE);

COMPOSITIONFORM composition{};
composition.dwStyle = CFS_POINT;
composition.ptCurrentPos = {0, 0};
ImmSetCompositionWindow(himc, &composition);
```

这种方案对旧式 IMM32 输入法可能偶尔可用，但对现代微软拼音不可靠。

原因是微软拼音主要由 TSF 驱动。TSF 不只读取 `ImmSetCompositionWindow()`，还可能读取：

- 当前拥有输入焦点的真实 HWND；
- 该 HWND 的真实窗口矩形；
- Win32 thread caret 的矩形；
- `GetGUIThreadInfo()` 暴露的 caret 信息；
- focus / composition 创建阶段采样到的几何信息。

如果焦点 `EDIT` 只有 1×1，即使视觉上把它移动到正确位置，TSF 仍可能得到无效或退化的输入区域。

---

## MiaoDesk 推荐架构

```text
Custom visual input
├─ DirectWrite / Direct2D
│  ├─ background
│  ├─ committed text
│  ├─ IME composition text
│  ├─ selection
│  └─ visible caret
│
└─ Native EDIT (input infrastructure only)
   ├─ full real text rectangle
   ├─ owns keyboard focus
   ├─ receives WM_CHAR / WM_KEY*
   ├─ receives WM_IME_*
   ├─ publishes real Win32 caret geometry
   └─ never paints itself visibly
```

关键原则：

> **视觉层可以完全自绘，但 Windows 必须始终看到一个尺寸、位置、caret 都真实可信的原生输入控件。**

---

## 1. 原生 `EDIT` 必须保持完整输入区域大小

不要把原生输入控件缩成 1×1。

例如：

```cpp
RECT textRect = VisibleTextRect();
SetWindowPos(
    edit,
    nullptr,
    textRect.left,
    textRect.top,
    textRect.right - textRect.left,
    textRect.bottom - textRect.top,
    SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
```

窗口移动、DPI 变化、父窗口 resize 后都必须重新同步。

### 为什么

微软拼音 / TSF 会在 focus 与 candidate/composition 创建期间采样 focused HWND 的几何信息。

完整尺寸的 `EDIT` 可以让 Windows 正确理解：

- 当前输入区域在哪里；
- 当前输入区域有多大；
- 候选框应该避开什么区域。

---

## 2. 原生 `EDIT` 只负责输入，不负责视觉

原生 `EDIT` 可以存在于真实位置，但必须抑制它自己的绘制，以避免与 DirectWrite 重叠。

推荐在 subclass proc 中处理：

```cpp
if (message == WM_PAINT) {
    ValidateRect(hwnd, nullptr);
    return 0;
}

if (message == WM_ERASEBKGND) {
    return 1;
}
```

视觉文字、placeholder、selection、composition、caret 全部由 MiaoDesk 自己绘制。

这样可以同时获得：

- 自定义 UI；
- 正确的 Windows keyboard focus；
- 正确的 TSF / IME 行为。

---

## 3. 自绘 caret 和系统 caret 必须使用同一个文本布局结果

视觉 caret 不能靠固定字符宽度、GDI 粗略估算或魔法偏移。

优先使用当前实际绘制所使用的 DirectWrite `IDWriteTextLayout`：

```cpp
layout->HitTestTextPosition(
    caretIndex,
    trailing,
    &hitX,
    &hitY,
    &metrics);
```

由此得到：

```text
caretX = textRect.left + hitX
caretY = textRect.top  + hitY
```

同一个坐标结果用于：

1. DirectWrite 可视 caret；
2. Win32 caret；
3. IME composition anchor；
4. candidate exclusion rectangle。

**不要让这四套位置分别计算。**

---

## 4. 必须发布真实 Win32 caret

仅仅绘制一个蓝色/黑色 caret 是不够的。

TSF 需要看到真实的 Windows caret：

```cpp
SetCaretPos(localCaretX, localCaretY);
HideCaret(edit);
```

这里 `localCaretX / localCaretY` 是相对于 `EDIT` 客户区的坐标，而不是父窗口坐标或屏幕坐标。

`HideCaret()` 只是避免系统 caret 被用户看到，不会取消其几何意义。

### 坐标转换

如果 DirectWrite layout 使用的是父窗口坐标：

```cpp
const int localCaretX = visualCaretX - textRect.left;
const int localCaretY = visualCaretY - textRect.top;
```

必须确保所有坐标最终转换到 focused `EDIT` 的 client coordinate system 后再调用 `SetCaretPos()` / `ImmSet*()`。

---

## 5. IMM32 仍然保留，但作为兼容层

现代微软拼音主要依赖 TSF，但 IMM32 定位仍应该同步。

推荐：

```cpp
COMPOSITIONFORM composition{};
composition.dwStyle = CFS_FORCE_POSITION;
composition.ptCurrentPos = {localCaretX, localCaretY};
ImmSetCompositionWindow(context, &composition);
```

候选框推荐使用排除区域：

```cpp
CANDIDATEFORM candidate{};
candidate.dwIndex = 0;
candidate.dwStyle = CFS_EXCLUDE;
candidate.ptCurrentPos = {localCaretX, inputHeight};
candidate.rcArea = {
    0,
    0,
    inputWidth,
    inputHeight
};
ImmSetCandidateWindow(context, &candidate);
```

`CFS_EXCLUDE` 通常比“随便指定一个候选框点”更适合自绘输入框，因为它告诉 Windows：候选窗不要盖住这个真实输入区域。

---

## 6. IME composition 文本与 committed text 要分开

推荐维护：

```cpp
std::wstring committed;
std::wstring composition;
```

视觉内容可以是：

```cpp
visible = committed + composition;
```

但需要明确区分两类 caret：

### Visual caret

用于 DirectWrite 界面绘制，可以位于当前 composition 尾部。

### Native / TSF caret

用于 Windows 输入基础设施，必须映射到 focused `EDIT` 当前真实输入位置。

不要简单地因为视觉上显示了 `committed + composition`，就把 1×1 HWND 移到 composition 尾端。

正确做法仍然是：

- `EDIT` 保持完整矩形；
- caret 在 `EDIT` 内部更新。

---

## 7. 必须在这些事件后重新同步 IME anchor

至少覆盖：

```text
WM_SETFOCUS
WM_KEYUP
WM_CHAR
WM_IME_STARTCOMPOSITION
WM_IME_COMPOSITION
WM_IME_ENDCOMPOSITION
WM_INPUTLANGCHANGE
WM_SIZE / DPI / layout change
```

对 `WM_IME_NOTIFY`：

```text
IMN_OPENCANDIDATE
IMN_CHANGECANDIDATE
```

也建议重新定位。

### 不要在所有 IME 消息内部同步递归调用 `ImmSet*`

部分输入法会因为 setter 再触发通知。

MiaoDesk 搜索框使用的成熟方案是：

```text
IME / focus event
   ↓
PostMessage(custom deferred anchor message)
   ↓
next message turn
   ↓
SetCaretPos + ImmSetCompositionWindow + ImmSetCandidateWindow
```

并使用 busy / pending guard 避免反馈循环。

---

## 8. Focus 时几何必须先正确，再让 TSF 采样

特别注意 `WM_SETFOCUS`。

顺序推荐：

```text
1. Ensure native EDIT full geometry
2. Let focus transaction continue
3. Defer IME anchor update
4. Publish real caret
5. Publish composition/candidate position
```

如果 focus 发生时 `EDIT` 仍然是 1×1，随后再调整，很可能已经错过 TSF 最重要的一次几何采样。

---

## 9. DPI / resize 的规则

以下所有数据必须来自同一 DPI 标准：

- visual text rectangle；
- DirectWrite text format；
- `EDIT` rectangle；
- caret position；
- candidate exclusion rectangle。

禁止出现：

```text
DirectWrite: logical 96-DPI px
EDIT: physical px
IME anchor: manually multiplied DPI px
```

这会导致 125%、150%、200% 缩放时越来越偏。

如果窗口本身是 per-monitor DPI aware，则建议所有 UI geometry 都先通过项目统一的 `Px(state, value)` 生成，再直接使用对应 client pixel coordinates。

---

## 10. 已知错误模式

### 错误 A：1×1 proxy EDIT

```cpp
MoveWindow(edit, caret.x, caret.y, 1, 1, FALSE);
```

不要使用。

### 错误 B：只调用 `ImmSetCompositionWindow`

对现代微软拼音不够。

### 错误 C：只画一个自定义 caret

Windows 不知道这个 caret 存在。

### 错误 D：用固定字符宽度计算 caret

中文、英文、emoji、不同字体、fallback glyph 都会偏。

### 错误 E：父窗口坐标直接传给 `SetCaretPos`

`SetCaretPos()` 使用拥有 caret 的窗口客户区坐标。

### 错误 F：靠 `+8px / -12px` 魔法数修拼音位置

不同 DPI、字体、输入法都会再次坏掉。

### 错误 G：focus 后才把输入控件从 1×1 恢复

TSF 可能已经采样了错误 geometry。

---

## MiaoDesk 当前参考实现

### 正确参考：顶部搜索框

主要文件：

```text
src/native/include/miaodesk/SearchImeAnchor.h
src/native/src/ui/search/SearchWindow.cpp
```

其核心已经包含：

- full-size hidden native EDIT；
- suppressed native painting；
- real caret measurement；
- `SetCaretPos()`；
- `CFS_FORCE_POSITION`；
- `CFS_EXCLUDE`；
- deferred IME anchoring；
- TSF/IMM32 compatibility handling。

### 需要迁移的旧模式：聊天框

相关文件：

```text
src/native/src/ui/ai/ConversationPanelInputOverlay.inc
```

历史实现使用：

```cpp
MoveWindow(state.input, caret.x, caret.y, 1, 1, FALSE);
```

这类实现应迁移到本规范描述的 full-size native EDIT 模式，而不是继续通过偏移量修补。

---

## 推荐后续抽象

当搜索框和聊天框都验证稳定后，建议提取一个共享组件，例如：

```text
NativeImeAnchorHost
或
CustomTextInputImeHost
```

建议职责：

```cpp
struct CustomTextInputImeGeometry {
    RECT inputRect;
    POINT caretClient;
};

class CustomTextInputImeHost {
public:
    void Attach(HWND edit);
    void SetGeometry(const CustomTextInputImeGeometry& geometry);
    void SyncNow();
    void RequestSync();
    void OnInputMessage(UINT message, WPARAM wParam, LPARAM lParam);
};
```

共享层只处理：

- native input HWND geometry；
- Win32 caret；
- TSF-compatible geometry；
- IMM32 composition/candidate anchor；
- deferred sync；
- focus / DPI / resize handling。

业务 UI 自己负责：

- DirectWrite layout；
- visual caret；
- composition style；
- placeholder；
- selection；
- send / submit behavior。

这样后续 Widget 编辑器、属性面板、桌面搜索、AI Chat、Scene Editor 都可以复用同一套基础设施。

---

## 验收测试

任何新的自绘文本输入控件都至少验证：

1. 微软拼音输入 `nihao`，composition 紧贴视觉 caret；
2. candidate bar 出现在输入框下方且不覆盖当前行；
3. 输入若干英文后再切中文，anchor 跟随文本末端；
4. 在文本中间移动 caret 后输入中文，anchor 跟随插入点；
5. 125% / 150% / 200% DPI；
6. 主副显示器不同 DPI；
7. 窗口移动到另一显示器；
8. 窗口 resize；
9. 搜狗 / 微软拼音 / 微软五笔至少做基本兼容检查；
10. composition 开始、更新、提交、取消后无残留拼音文本；
11. candidate 打开时窗口移动/resize 不产生漂移；
12. 原生 `EDIT` 本身从不穿透自绘 UI 显示出来。

---

## 规则总结

以后在 MiaoDesk 中实现 Windows 自绘输入框，默认遵循：

```text
完整尺寸 native EDIT
        +
隐藏 native painting
        +
DirectWrite authoritative layout
        +
visual caret == Win32 caret geometry
        +
SetCaretPos
        +
IMM32 compatibility anchor
        +
deferred TSF-safe resync
```

不要再使用：

```text
1×1 EDIT + MoveWindow 跟随自绘 caret
```

作为正式输入法定位方案。
