# MiaoDesk Widget Product Model

Status: normative product contract. The `_M3` filename is retained only for stable existing links.

## User meaning

Widget 是附着在 Windows 桌面的独立信息卡。普通用户不需要理解 HWND、renderer process、normalized coordinates 或 z-order 实现细节。

当前内置 showcase Widgets 使用 Native Direct2D surface：

- `玻璃时钟`
- `今日待办`
- `玻璃天气`

复杂 Web 内容可以使用隔离 WebView2 Widget，但不是默认常驻 showcase 路径。

## Product behavior

当前 Widget 管理面向普通用户只暴露必要操作：

```text
add fixed format
drag to reposition
hide / show
delete
refresh runtime state
```

固定 preset 拥有默认逻辑尺寸。新建时使用 collision-aware 自动放置；用户拖动后通过 `WidgetService::Update` 持久化 normalized `x/y`。

Resize、数字坐标编辑和 monitor reassignment 只有在有明确产品需求时再开放，不为“编辑器完整”预先增加复杂度。

## Wallpaper independence

Widget 与 Wallpaper 是两个独立状态域：

- 换壁纸不能删除 Widget；
- Wallpaper disabled 不能等价于 Widget disabled；
- Wallpaper host 暂停 Web wallpaper 不能自动暂停 Native Widget；
- 共享 Performance policy 可以在策略明确要求时同时 Pause/Stop 两个域。

目标视觉顺序：

```text
desktop icons
Widget
MiaoDesk wallpaper
```

Desktop attachment/z-order mutation 由 `DesktopShellHost` 统一拥有，Widget domain 不重新实现 WorkerW/Progman ownership。

## Controller / persistence boundary

`DesktopWidgetController` 与 `WidgetService` 是产品调用边界。`DesktopWidgetStore` 是内部 persistence，不是 UI/AI 公共 API。

```text
UI / Pi
  ↓
DesktopControlService / DesktopWidgetController
  ↓
WidgetService
  ↓
persistence + runtime
```

## Runtime expectations

Enabled 只表示配置状态，不等于真实渲染成功。产品状态需要能够区分：

- process/window 是否存在
- parent/style/visibility 是否有效
- target monitor / geometry 是否有效
- Web Widget lifecycle 是否 ready
- z-order 是否正确

具体见 `WIDGET_RUNTIME_HEALTH_M3.md` 和 `WIDGET_PLACEMENT_HEALTH_M3.md`。

## Real Windows validation

仓库不再维护独立 Widget acceptance executable 或源码 marker contract 脚本。涉及 Widget runtime/layering/drag 的改动需要在真实 Windows 上验证：

1. 三个内置 preset 均可创建并明显区分；
2. 初始位置不重叠；
3. drag 后位置持久化；
4. Settings/Search 打开时 Widget 不被错误隐藏；
5. Widget 位于 wallpaper 上、desktop icons 下；
6. Explorer restart 后恢复；
7. display reconnect/change 后恢复到有效 monitor/geometry；
8. enabled Widget 的 runtime health 反映真实 surface，而不是只返回 enabled=true。

代码编译成功不替代真实桌面可见行为验证。
