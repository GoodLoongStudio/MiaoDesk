---
name: widget-content
description: 生成 MiaoDesk 小组件内容包(.mdwidget)。在 content-package-basics 之上叠加组件领域规则:widget profile、geometry policy(归一化尺寸/宽高比/是否可缩放)、桌面层级契约、按需重绘策略。生成组件或修改组件内容时使用。
---

# 小组件内容生成

在 `content-package-basics` 之上叠加组件领域规则。生成 `.mdwidget` 组件包时使用。

## 何时使用

- 用户要求生成小组件、桌面组件、widget 类内容
- 修改已有 `.mdwidget` 包的内容

## 正面提示词

```text
在 content-package-basics 的契约之上,组件包还遵守:

【profile 与扩展名】
- profile 为 widget,包扩展名为 .mdwidget。
- kind 为 widget。

【geometry policy(manifest 内)】
- 在 manifest 中声明 geometry:
  defaultWidth / defaultHeight / resize / aspectRatio。
- 尺寸使用 monitor-relative normalized 坐标,取值 0..1,不用绝对像素。
- 参考既有内置组件的量级:0.22 ~ 0.30 区间,不要做到接近 1.0 的全屏尺寸。
- **一个包只有一档尺寸**:没有 small/medium/large 尺寸族,也没有"切换尺寸"这一说。
  需要不同尺寸就交付不同的包。切尺寸保留配置属于后续工作,当前做不到 —— 别承诺。
- `resize` 现在是**惰性字段**:写 true 校验得过,但产品里没有任何入口能改变一个组件的
  尺寸(拖动只改 x/y;通用 Update 能带上宽高,却没有任何调用方那么做)。所以把它当 false 处理:
  预留的最小/最大约束是正确的做法,但不要指望用户在桌面上拖边缘。
- `aspectRatio` 声明宽高比;不需要固定比例时写 `"free"` 或留空。
  **但它当前没有被任何渲染器或宿主编译出来**:校验只拿 `defaultWidth / defaultHeight`
  (归一化之比)去比声明值,于是 0.30 / 0.30 = 1.0 一路绿灯;而宿主要的像素盒是
  "归一化宽 × 屏幕宽"与"归一化高 × 屏幕高",16:9 屏幕上实际是 1.78:1。
  也就是说:**这个字段现在不会给你一个遵循该比例的像素盒**。写它不会报错,也不会生效。
  组合当前的实际比例是 `(归一化宽 / 归一化高) × (屏幕宽 / 屏幕高)` —— 想要一个接近
  16:9 的方块,就照这个式子反推 defaultWidth / defaultHeight,而不是写 `aspectRatio`。
  真要让它落到像素上(信箱化留边)属于 WPRO-01 的实施轮。

【桌面层级契约】
- 层级自上而下:Widget(可交互)> Desktop Icons > Wallpaper(click-through)。
- 组件默认可交互,直接接收鼠标输入,不进入 click-through 状态。
- 组件始终位于 Desktop Icons 之上、普通应用窗口之下。
- 桌面图标在组件未覆盖区域必须始终可点击、可框选、可右键。
- 换壁纸不销毁组件;停用壁纸后组件仍正常显示。
- 组件故障不得拖垮壁纸,壁纸故障不得拖垮组件。
- Shell 挂载、z-order、Explorer recovery 只由 DesktopShellHost 统一处理,
  组件内容不实现第二套。

【尺寸所有权】
- 尺寸策略由 ContentDefinition 的 geometry 拥有;实例记录的是 x / y / enabled 与所属显示器。
- 内置(native preset)组件的宽高由 preset 拥有,通用 Update 带不同宽高会被直接拒绝。
- 用户能改变的是**位置**。所以布局要按"固定尺寸的卡片"来设计,而不是"会被拖大的面板"。

【按需重绘】
- 时钟类:按分钟边界刷新,不逐帧。
- 天气类:事件驱动,数据变化才重绘。
- 任务 / 列表类:内容无变化不重绘。
- 不为"看起来在动"而加入无意义的逐帧动画。

【资源】
- 组件常驻桌面,常驻内存 / CPU / 句柄必须维持在低基线。
- 不为单个组件打包可复用的共享资源副本。
```

## 反面提示词

```text
绝不允许:
- 声明固定像素尺寸,或接近 1.0 的全屏/遮挡整个桌面的尺寸
- 设计需要 Shell 挂载、z-order 所有权或 Explorer recovery 的组件逻辑
- 让组件依赖某张特定壁纸存在,或反之
- 每帧无条件重绘(时钟必须按分钟边界)
- 绕过 geometry policy 直接写死 width/height,或让任意调用方改尺寸
- 在组件里引入需要常驻全屏的渲染目标
- 把组件做成 click-through(click-through 只属于壁纸 Surface)
- 用无意义的逐帧动画掩盖内容不更新
```

## 输入 / 输出

- **输入**:组件意图(自然语言)、期望尺寸量级、是否需要可缩放
- **输出**:`.mdwidget` 内容包(`manifest.json` 含 geometry + `parameters.json` + `scene.json`)

## 组合

```
content-package-basics → widget-content → content-review
```

需要参数可调时,在 basics 的 `parameters.json` 规则内扩展;需要时钟 / 天气 / 音频数据时,在 basics 的 `bindings` 与 `capabilities` 规则内扩展,并同步声明能力。
