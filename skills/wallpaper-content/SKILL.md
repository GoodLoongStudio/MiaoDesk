---
name: wallpaper-content
description: 生成 MiaoDesk 壁纸内容包(.mdwall)。在 content-package-basics 之上叠加壁纸领域规则:wallpaper profile、click-through 语义、按载体×运行时选择壁纸路径、按需重绘。生成壁纸或修改壁纸内容时使用。
---

# 壁纸内容生成

在 `content-package-basics` 之上叠加壁纸领域规则。生成 `.mdwall` 壁纸包时使用。

## 何时使用

- 用户要求生成壁纸、动态壁纸、桌面背景类内容
- 修改已有 `.mdwall` 包的内容

## 正面提示词

```text
在 content-package-basics 的契约之上,壁纸包还遵守:

【profile 与扩展名】
- profile 为 wallpaper,包扩展名为 .mdwall。
- kind 为 wallpaper。

【分类:载体 × 运行时,不是一个平面列表】
MiaoDesk 的桌面内容 = 载体 × 运行时两个正交维度。

- 载体(ContentKind):Wallpaper / Widget。壁纸包取 Wallpaper。
- 运行时(ContentRuntimeKind):Scene / Web。壁纸包优先 Scene。

Scene 运行时的内部按"表现形态"分,这些是 Scene 的配置档,不是并列的顶层类型:
- 静态图    单图 + 可选缩放/色彩/视差
- 动态场景  组件 + 动画 + 绑定 + 粒子 + 后处理
- 视频      单轨 VideoRenderer 循环
- 3D 场景   当前未实现,不要生成

【3D:契约已有,渲染器没有 —— 绝对不要生成】
scene.json 里已经能写 spatial:"3d"、lights[]、fog[],产品也会正常校验通过,
但**还没有 3D 渲染器**,生成出来的内容预览和桌面上都看不到。
所以:
- 不要写 spatial:"3d"。
- 不要写 lights[] / fog[](它们在 3D 场景之外会被校验拒绝,但即使放进 3D 场景也没有意义)。
- 不要引用 mesh 类型资产(.obj / .fbx),同样没有加载器。
- 用户明确要求"3D 壁纸"时,明确说明暂不支持,并给一个 2D 等价方案
  (例如用多层贴图 + 视差模拟纵深),不要静默降级成别的东西。

选型规则:
- 用户只要一张会动的背景 → 动态场景。
- 用户给的是现成视频、且不要求叠加层 → 视频形态,只声明资源,
  不要强行套 scene.json 的完整组件结构。
- 用户给的是静态图 → 静态图形态。
- Web 运行时不属于你的产出范围,见反面提示词。

【交互语义】
- 壁纸 Surface 默认 click-through,桌面图标始终可点击。
- 允许"不抢输入"的指针效果:视差、追随、辉亮、扰动。
  这类效果只读指针位置,不得要求点击、拖拽、悬停命中或键盘焦点。
- 需要真正接收点击/拖拽的内容,应该做成组件(Widget),不是壁纸。
- 壁纸不属于任何组件;切换壁纸不销毁组件。

【音频响应(声明式绑定即可,无需脚本)】
可用通道,全部为 [0, 1] 浮点:
- input://audio/level     总音量
- input://audio/bass      低频 20-160 Hz
- input://audio/lowmid    中低 160-500 Hz
- input://audio/mid       中频 500-2000 Hz
- input://audio/highmid   中高 2000-5000 Hz
- input://audio/treble    高频 5000-16000 Hz
- input://audio/beat      节拍,单帧为真的沿(不是电平)
写法示例:一个 SpriteRenderer 的 scale 绑定 input://audio/bass,
就会随音乐低频胀缩。节拍通道配 InputRisingEdge 触发一次动画。
要求:
- 必须是声明式绑定,不得生成脚本。
- 不播放音频时必须有可看的静态或缓慢演化状态,不得全黑或静止死屏。
- 音频分析有约 20-40 ms 的建立时间,不要设计需要零延迟的效果。

【响应曲线(让音频/指针有手感,不需要脚本)】
默认绑定是线性的(value * scale + offset),这会让效果显得机械。
可在 binding 上指定 response,把输入先过一遍曲线再进 scale/offset:
- linear       默认,与旧行为完全一致
- square       t*t       低值时很闷,高值突然炸开(适合"只在鼓点上亮")
- cube          t*t*t     比 square 更极端
- sqrt          sqrt(t)   低值就抬起来,高值趋缓(适合"一直在轻微呼吸")
- smoothstep    3t^2-2t^3 两端平中间陡,最通用的"顺滑"
- elastic       欠阻尼弹簧,会过冲再回稳(适合追随光标、有弹性的缩放)
- threshold     >=0.5 输出 1,否则 0(把连续音量变成开关,做频闪)
- invert        1-t       反向
另有 deadzone(0 <= deadzone < 1):低于它的输入直接归零,
避免静音时频谱底噪让壁纸一直轻微抖动。
示例:一个跟随光标的层,response 用 elastic,鼠标停下时会轻轻回弹。
约束:
- response 与 deadzone 只对 float 源有效,不能用在 bool/int 通道上。
- elastic 会超过 1,下游若要求有界值(如 opacity)需自行收敛。

【指针交互(与 click-through 不互斥的那一层)】
- input://pointer/x、input://pointer/y   光标在所在显示器上的归一化位置
- input://pointer/inside                 光标是否在壁纸区域内
- input://event/pointer/enter、leave    进入 / 离开的沿
这四个通道**不影响桌面点击**:窗口依然是 click-through,只是宿主全局追踪光标。
所以视差、追随、辉亮、扰动这类"光标在哪就怎么动"的效果可以放心做。
- input://pointer/down、input://event/pointer/click 需要关闭 click-through,
  会真的抢走桌面交互。壁纸里**不要**用这两个;需要点击的内容应做成组件。

【分辨率与多显示器】
- 按主流 16:9 与 21:9 考虑,内容不假设单一显示器尺寸。
- 不假设固定显示器数量;多显示器启停不得让壁纸状态错乱。

【按需重绘】
- 静态背景不每帧重绘。
- 只有真正在动的层才参与逐帧更新。
- 壁纸停用必须是幂等状态:停用后不得被 reload / Shell repair 重新拉起。

【资源】
- 沙箱资源上限:图片 ≤ 25 MiB,视频 ≤ 250 MiB。
- 纹理与资产优先复用,不为细节重复打包同一资源。
```

## 反面提示词

```text
绝不允许:
- 在壁纸里要求点击、拖拽、悬停命中或键盘焦点 —— 需要这些就做成组件
- 使用 input://pointer/down 或 input://event/pointer/click —— 这会关掉 click-through 抢走桌面点击
- 把 input://audio/beat 当电平用(它是单帧沿,当电平会每帧都触发)
- 设计需要零延迟的音频反应
- 生成 HTML、CSS、JavaScript、shell 命令或任何可执行代码
- 生成 Script 组件内容,或引用任何脚本资产
- 生成 Web 运行时内容(内嵌浏览器是给用户手工/第三方内容用的,不是你的产出通道)
- 假设固定分辨率或固定显示器数量
- 生成每帧全屏重绘的静态内容
- 让壁纸依赖某个组件存在,或切换壁纸时影响组件
- 把纯视频壁纸硬塞进完整 scene.json 组件结构
- 把 Web 当成壁纸的默认承载层
- 混用旧 scene.ini 与新 scene.json 描述同一个包
- 突破 25 MiB 图片 / 250 MiB 视频上限
- 写 spatial:"3d"、lights[]、fog[] 或引用 mesh 资产 —— 渲染器尚不存在
- 用户要求 3D 时静默降级成 2D;必须明说暂不支持并给 2D 替代方案
- 使用 "Image / Video / Web / Scene 四类" 这种平面分类描述壁纸
```

## 输入 / 输出

- **输入**:壁纸意图(自然语言)、目标载体(壁纸)、目标形态(静态图 / 动态场景 / 视频)
- **输出**:`.mdwall` 内容包(`manifest.json` + 视形态的 `scene.json` / 资源声明)

## 组合

```
content-package-basics → wallpaper-content → content-review
```

需要参数可调时,在 basics 的 `parameters.json` 规则内扩展;需要数据绑定时,在 basics 的 `bindings` 规则内扩展。

## 分类依据

分类表述与 `docs/WALLPAPER_ENGINE_BENCHMARK.md` §6 保持一致。
能力差距与未实现形态(3D、Script、Web 音频)见该文档 §7,不要生成尚未实现的能力。

