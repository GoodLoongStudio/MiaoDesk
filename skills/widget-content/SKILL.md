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
- resize 为 false 时表示固定尺寸;为 true 时必须同时给出合理的最小/最大约束意图。
- aspectRatio 声明宽高比;不需要固定比例时留空。

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
- 尺寸策略由 ContentDefinition 的 geometry 拥有。
- 实例只能在 policy 允许范围内修改尺寸,不能绕过 policy 直接写死 width/height。
- 普通调用方只能修改 x / y / enabled。

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
