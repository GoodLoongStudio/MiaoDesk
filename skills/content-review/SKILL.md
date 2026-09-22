---
name: content-review
description: MiaoDesk 内容包安全与性能门禁。任何壁纸或组件内容包产出后必跑,逐项核查安全边界与性能上限,全部通过才允许交付。任一硬项不通过即判定不合格,不进入交付。
---

# 内容包安全与性能门禁

内容包产出后的最后一道关。**任一硬项不通过即判定不合格,不进入交付。** 本 skill 只做核查,不修改内容。

## 何时使用

- 任何 `.mdwall` / `.mdwidget` 包生成或修改完成后
- 准备把内容交付给用户之前

## 正面提示词

```text
作为门禁执行者,按以下方式核查:

1. 逐项过检查表,一项一项来,不跳项、不合并、不凭印象打勾。
2. 每一项都要落到具体证据:引用 manifest 的哪个字段、scene.json 的哪个节点、
   parameters.json 的哪个参数,而不是只说"看起来合规"。
3. 性能上限对照 runtime 的实际常量,不凭感觉估算:
   帧率 1~240、每 emitter 粒子 65536、每 scene 粒子 131072、
   渲染目标单维 16384、图片 25 MiB、视频 250 MiB。
4. 领域检查按 kind 只跑对应那一组,不跑另一组。
5. 结论只输出"可交付"或"不可交付"两种,并列出未通过项。
6. 内容被修改后,从头重跑完整门禁,不沿用上次的通过项。
```

## 反面提示词

```text
核查时绝不允许:
- 因为"看起来没问题"而跳过任一条检查项
- 把超标项四舍五入成达标
- 用"用户应该不会触发"为理由放过越界路径或越权能力
- 在未通过安全检查时先交付,再补性能优化
- 把 programmable material 当作“两个后端都能画”,或反过来把 D2D 对贴图 tint 的限制当作 bug
- 自行修改内容来让检查通过后,不重新跑完整门禁
- 把预览当作已应用,或跳过用户确认
- 报告"基本通过""大部分通过"等模糊结论;只报通过与不通过
```

## 安全检查(全部必须通过)

```text
[ ] 只产出 JSON 内容包;无 HTML / JavaScript / CSS / shell 命令 / 原生可执行文件
[ ] manifest.schema == 1,scene.json 的 schema == 1
[ ] id 形如 com.goodloong.<name>,未复用任何内置包 id
[ ] kind 与扩展名一致(.mdwall→wallpaper,.mdwidget→widget)
[ ] runtime 取值合法(scene / web)
[ ] 所有引用都是 Package Root 内的相对路径;无绝对路径,无 ../ 越界
[ ] 无 symlink / junction / reparse point 指向包外
[ ] capabilities 中每一项都被内容实际使用;无漏声明,无多声明
[ ] 未把 capability 当作 Win32 / 文件系统 / 注册表权限使用
[ ] 所有 binding 的 sourceId 都指向已存在参数;所有 parentId / rootNodeId 都指向已存在节点
[ ] 无悬空引用
```

## 性能检查(全部必须通过)

```text
[ ] 动画帧率在 1 ~ 240 之间(默认 60)
[ ] 每 emitter 粒子数 ≤ 65536
[ ] 每 scene 粒子总数 ≤ 131072
[ ] 渲染目标单维 ≤ 16384
[ ] 图片资源 ≤ 25 MiB,视频资源 ≤ 250 MiB
[ ] 壁纸:静态背景未每帧重绘
[ ] 组件:时钟按分钟边界、天气事件驱动、无变化不重绘
[ ] 渲染路径中无 Node / Pi / 在线请求
[ ] 不依赖系统 Node、全局 npm 或在线安装
[ ] 节点与组件数量已最小化,无堆叠凑数
[ ] 共享资源未按包重复打包
```

## 领域检查(按 kind 二选一)

```text
壁纸包:
[ ] profile 为 wallpaper
[ ] 未要求点击 / 拖拽 / 悬停命中 / 键盘焦点(只读指针位置的效果允许)
[ ] 未假设固定分辨率或显示器数量
[ ] 静态图 / 视频形态未硬套完整 scene.json 组件结构
[ ] 未混用 scene.ini 与 scene.json
[ ] 未生成 Script 组件或脚本资产
[ ] 未生成 Web 运行时内容
[ ] 未使用 input://pointer/down 或 input://event/pointer/click(壁纸不得关闭 click-through)
[ ] 未把 input://audio/beat 当电平用(它是单帧沿)
[ ] 音频/指针效果全部是声明式绑定,无脚本
[ ] 每个 spriteRenderer 只走一条到纹理的路:texture 或 materialId,没有两个都写
[ ] texture 指向一个真实存在的 Image 资产,不是别的类型
[ ] 若走 materialId:builtin 的 builtinName 恰为 solidColor(唯一的可用值)
[ ] 若走 programmable material:已确认接受“仅 D3D11 可渲染”,D2D 后端会拒绝加载
[ ] solidColor 的 color 未越界;它与 texture 同时存在时是染色,不是冲突
[ ] 贴图 sprite 的 tint 为白色(默认),或已确认只需在 D3D11 后端上运行
[ ] materialId 指向的 material 真实存在(不接受“取第一个 builtin”的兜底)
[ ] response 取值在闭集内(linear/square/cube/sqrt/smoothstep/elastic/threshold/invert),未发明新曲线
[ ] response / deadzone 未用在 bool / int 源上
[ ] deadzone 在 [0, 1) 内
[ ] 使用 elastic 时,下游若有界(opacity 等)已自行收敛
[ ] 静音或光标不在场时有可看的静态状态,不是黑屏
[ ] 未写 spatial:"3d"、未声明 lights[] / fog[]、未引用 mesh 资产(渲染器尚不存在)
[ ] 分类表述为"载体 × 运行时",未使用 Image/Video/Web/Scene 平面四分类
[ ] 引用的输入通道都在契约内(input://audio/{level,bass,lowmid,mid,highmid,treble,beat}、
    input://pointer/{x,y,inside}、input://event/pointer/{enter,leave}),未发明新通道

组件包:
[ ] profile 为 widget
[ ] geometry 使用归一化坐标(0..1),未用绝对像素
[ ] defaultWidth / defaultHeight 未接近 1.0
[ ] resize 为 false 时未提供可改尺寸的路径
[ ] 未实现 Shell 挂载 / z-order / Explorer recovery 逻辑
[ ] 未做成 click-through
[ ] 不依赖特定壁纸存在
```

## 输出格式

```text
安全:通过 / 不通过(列出未通过项)
性能:通过 / 不通过(列出未通过项)
领域:通过 / 不通过(列出未通过项)

结论:可交付 / 不可交付
```

**只有三项全部为"通过"时结论才是"可交付"。** 任何一项不通过,列出具体未通过项,回到对应 skill 修正后重跑完整门禁。

## 组合

```
content-package-basics → (wallpaper-content | widget-content) → content-review
```

本 skill 引用 basics 的全部契约,因此 basics 的违反会在此被捕获。
