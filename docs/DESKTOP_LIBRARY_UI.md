# TuringDesk Desktop Library UI

Status: authoritative product UI contract for the native desktop library/settings shell.
Date: 2026-08-25

## 1. Reference boundary

TuringDesk uses the public Lively WinUI layout as a behavioral and information-architecture reference only. Lively is GPL-3.0; TuringDesk remains an independent MIT/C++23 implementation. Do not copy XAML, styles, assets, icons, comments, or implementation code into TuringDesk.

Reference revision studied: `rocksdanister/lively@c1036feb664960722e34bf4309042c247d6a909d`.

Primary UI references:

- `Lively.UI.WinUI/Views/MainWindow.xaml`
- `Lively.UI.WinUI/Views/Pages/LibraryView.xaml`

Wallpaper Engine remains a product-capability benchmark, not a UI implementation source.

## 2. Product shell

The old `TabControl + left ListView + right detail pane` layout is deprecated.

The desktop library converges on:

```text
┌─────────────────────────────────────────────────────────────┐
│ 图灵智能桌面          [ 搜索桌面……………… ]          ＋ 添加 │  48px shell header
├─────────────────────────────────────────────────────────────┤
│ 桌面库  小组件  播放列表  多屏  应用规则  性能  图灵 AI     │  lightweight navigation
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ┌──────────────┐ ┌──────────────┐ ┌──────────────┐         │
│   │ thumbnail    │ │ thumbnail    │ │ thumbnail    │         │
│   │              │ │              │ │              │         │
│   │ title        │ │ title        │ │ title        │         │
│   │ description  │ │ description  │ │ description  │         │
│   └──────────────┘ └──────────────┘ └──────────────┘         │
│                                                             │
│                   responsive card wall                      │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│ 状态              [目标显示器 ▾] [收藏] [删除] [应用到桌面] │ command bar
└─────────────────────────────────────────────────────────────┘
```

## 3. Installed wallpaper page

### 3.1 Card wall

- full-width responsive wrapping grid;
- preferred card size follows the mature Lively density: approximately `272 x 153` logical pixels;
- real stored thumbnail when available;
- generated visual placeholder for built-in Scene/Web resources when no thumbnail exists;
- title and short description are part of the card, not a separate permanent right panel;
- single click selects;
- double click applies;
- selected card uses the system accent as a restrained bottom indicator/border;
- favorite/unavailable/recent state is visible on-card;
- right-click/more actions provide Apply, Favorite, Remove where valid.

### 3.2 Search and add

- search is in the top shell, not below a page title;
- `+ 添加` is a single entry point for file/package/Web import;
- file drag/drop is accepted over the library and should provide an obvious drop state;
- `.tdwall`, image, video and local HTML import remain first-class.

### 3.3 Bottom command bar

The large permanent detail pane is removed. Selection-specific actions live in a compact bottom bar:

- selected wallpaper title/type/status;
- target monitor / current layout selector;
- Favorite;
- Remove when allowed;
- primary `应用到桌面` action.

## 4. Widgets page

Widgets are a first-class sibling of the wallpaper library, not a hidden diagnostics page.

```text
小组件

[ Clock preview ] [ Weather/Web preview ] [ + New ]
[ title/status  ] [ title/status       ]

bottom command bar:
目标显示器 / runtime state / 启用停用 / 编辑 / 删除
```

V1 requirements:

- card grid rather than a raw LISTBOX;
- visible enabled/disabled state;
- friendly display name, never raw `\\?\DISPLAY#...` identifiers in normal UI;
- create clock;
- enable/disable;
- remove;
- runtime health will later show Configured / HWND / WebView / Visible rather than treating `enabled=true` as proof of rendering.

V2 adds drag/move/resize and Inspector.

## 5. Navigation

Top-level native navigation:

- 桌面库
- 小组件
- 播放列表
- 多屏
- 应用规则
- 性能
- 图灵 AI

`桌面库` and `小组件` are local pages in the desktop library shell. Existing automation/settings pages remain reachable through the navigation callback until they are visually migrated into the same shell.

## 6. Window behavior

- must fit the monitor work area and stay above the taskbar;
- minimum size should still preserve at least two wallpaper cards where practical;
- DPI-aware card metrics;
- resize reflows the grid rather than leaving a fixed detail-column layout;
- normal desktop library window should feel like a content browser, not a configuration dialog.

## 7. Completion gate

This UI is not complete because the controls exist. It is complete only when a real Windows ARM64 user can:

```text
open 图灵智能桌面
 -> immediately see a visual wallpaper card wall
 -> search
 -> select any visible card
 -> choose monitor
 -> apply
 -> switch to 小组件
 -> create/select/enable/disable/remove a Widget from visual cards
 -> resize the window without broken layout or taskbar overlap
```

The visible workflow is part of the product acceptance gate, not optional polish.
