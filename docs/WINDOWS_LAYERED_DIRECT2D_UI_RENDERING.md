# MiaoDesk Windows Layered Direct2D UI Rendering Guide

Status: approved implementation baseline.

适用于 Search Bar、悬浮工具条、玻璃面板、assistant bubble 等需要透明圆角和原生 Win32 交互的中小型 floating surface。

当前参考实现：

```text
src/ui/search/SearchWindow.cpp
```

## 1. 唯一可见边界原则

MiaoDesk 浮动透明 UI 的可见轮廓只允许一个 owner：Direct2D 绘制的 per-pixel alpha surface。

标准路径：

```text
WS_EX_LAYERED HWND
    ↓
32-bit top-down DIB section
    ↓
ID2D1DCRenderTarget
    ↓
PREMULTIPLIED alpha
    ↓
Direct2D geometry / DirectWrite text
    ↓
UpdateLayeredWindow(..., ULW_ALPHA)
    ↓
Windows compositor
```

不要同时用 `CreateRoundRectRgn` / `SetWindowRgn`、DWM window shaping 和 Direct2D 去定义同一个圆角轮廓。多个 geometry owner 会造成锯齿、脏边、裁剪和 DPI 漂移。

## 2. Window 与 backing surface

Top-level window 使用：

```cpp
WS_EX_TOOLWINDOW | WS_EX_LAYERED
```

Backing surface 使用 32-bit top-down DIB：

```cpp
bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
bitmapInfo.bmiHeader.biBitCount = 32;
bitmapInfo.bmiHeader.biCompression = BI_RGB;
```

每次 redraw 前清成全透明，不能保留上一帧 alpha 数据。

Direct2D target 使用 `ID2D1DCRenderTarget`，pixel format 使用：

```cpp
D2D1::PixelFormat(
    DXGI_FORMAT_B8G8R8A8_UNORM,
    D2D1_ALPHA_MODE_PREMULTIPLIED)
```

`UpdateLayeredWindow` + `AC_SRC_ALPHA` 要求 premultiplied alpha；不要混用 straight alpha。

## 3. Anti-aliasing 与 geometry

Geometry：

```cpp
D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
```

透明 surface 上的文字默认使用：

```cpp
D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE
```

ClearType 假设更接近不透明 RGB surface，在透明 layered window 上容易产生彩边。

Fill、outline、highlight、focus glow 都应从同一 base rect/radius 推导。状态变化只改变视觉层，不改变 silhouette geometry。

## 4. Present

```cpp
BLENDFUNCTION blend{};
blend.BlendOp = AC_SRC_OVER;
blend.SourceConstantAlpha = 255;
blend.AlphaFormat = AC_SRC_ALPHA;

UpdateLayeredWindow(..., &blend, ULW_ALPHA);
```

要求：

- `SourceConstantAlpha = 255`
- `AlphaFormat = AC_SRC_ALPHA`
- `ULW_ALPHA`
- source pixels 已 premultiplied

## 5. 输入与视觉必须分离

Visible text/caret 由 DirectWrite/Direct2D 绘制；Native `EDIT` 只负责 Windows 输入语义、focus、clipboard、IME 和 voice typing。

**不要使用 1×1 EDIT proxy。** 现代微软拼音/TSF 会读取 focused HWND、真实矩形和 Win32 caret。Native `EDIT` 必须覆盖真实输入区域，只是禁止它绘制第二份文字或 caret。

统一输入规则见：

```text
docs/WINDOWS_CUSTOM_INPUT_IME.md
src/include/miaodesk/InputImeAnchor.h
```

因此正确模型是：

```text
Direct2D / DirectWrite
    visible background / text / caret

Native EDIT
    real input geometry / focus / IME / clipboard
    no visible painting
```

## 6. Text / caret / DPI

Text 和 caret 必须共享同一个 DirectWrite layout origin。不要用独立 hard-coded offset 估算 caret。

Window size、DIB size、geometry、icon、text size、hit-test、IME anchor 必须使用一致的 DPI scale。不能只缩放 HWND 而保留旧像素 geometry。

## 7. 性能与生命周期

- 尺寸不变时复用 DIB/DC/render target。
- 只在 resize 或 D2D target invalid 时重建 surface。
- 只在 query/focus/hover/caret/result/resize/display 等真实 state change 时 redraw。
- 静态 UI 不运行持续 60 FPS loop。
- 删除 DIB 前先从 HDC 恢复原 bitmap，再 `DeleteObject` / `DeleteDC`。

## 8. 验收

仓库不再维护只检查源码 marker 的 Search rendering contract 脚本。修改 layered rendering 后应通过正式 C++ build，并在真实 Windows 上验证：

- transparent rounded edge 无脏边/白边
- hover/focus 不改变 silhouette
- 文字无明显彩边
- Native EDIT 不绘制第二份内容
- 中文 IME/candidate/caret 正常
- 100% / 125% / 150% / 200% DPI
- 多显示器移动后 geometry 正确
- surface redraw 没有旧 alpha 残影

## 9. 适用范围

适合需要高控制、非矩形 per-pixel transparency 的小中型 Native floating surface。

大型复杂窗口如果需要大量标准控件、复杂 accessibility tree、滚动布局和文本编辑，应单独评估 retained UI framework；不要把这套 layered renderer 当成所有 UI 的默认答案。

核心规则：

> **一个可见边缘只能有一个 owner。**
