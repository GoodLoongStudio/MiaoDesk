---
name: content-package-basics
description: MiaoDesk 内容包契约底座。生成任何 .mdwall 壁纸包或 .mdwidget 组件包之前必用,定义 manifest 字段、稳定 ID、Package Root 边界、capability 声明、ParameterSchema 与 binding 规则。壁纸与组件 skill 的共同基础。
---

# MiaoDesk 内容包契约底座

定义什么是合法的 MiaoDesk 内容包。所有内容生成任务的第一步,`wallpaper-content` 与 `widget-content` 都建立在它之上。

## 何时使用

- 开始生成任何 `.mdwall` / `.mdwidget` 包时
- 修改已有包的 manifest、parameters 或 bindings 时
- 不确定某个字段是否合法时

## 正面提示词

```text
生成 MiaoDesk 内容包时严格遵守以下契约。

【manifest.json】
- schema 必须为 1。
- id 是稳定内容 ID,形如 com.goodloong.<name>;身份只来自 id,不依赖文件夹名。
  文件夹改名、包在托管目录内移动或重排,都不改变身份。
- kind 必须与扩展名一致:.mdwall → wallpaper,.mdwidget → widget。
- runtime 只能是 scene 或 web。
- entry:scene 时为 scene.json。
- parameters:可选,指向 parameters.json。
- preview:可选,指向包内预览图。
- author / version / name 如实填写,version 用语义化版本(如 1.0.0)。
- capabilities:只声明内容真正需要的数据能力,如 clock.read / weather.read / audio.read。

【Package Root 安全边界】
- 所有文件必须位于 Package Root 之内,只使用包内相对路径。
- 不得使用 ../ 越界到 root 之外。
- 不得使用 symlink / junction / reparse point 指向包外。
- 内置包只读;新包不得复用任何内置包的 id。

【parameters.json】
- schema 为 1,parameters 是数组。
- 每个参数:id 形如 param://<name>,稳定且唯一;key 是代码引用名;
  type 用受支持的类型;default 给出默认值。
- color 类型用归一化 [r, g, b, a] 数组,分量在 0..1。
- float 类型必须给 min / max / step,且 default 落在范围内。
- 只把用户真正会想调的项暴露为参数;内部常量、调试值、实现细节不做成参数。
- 默认值必须是"开箱即好看"的值,不要求用户先调一遍。
- 参数数量保持精简,优先少量高价值参数。

【bindings(scene.json 内)】
- sourceKind 为 parameter 时,sourceId 必须指向 parameters.json 中已存在的参数 id。
- 时间类数据用受护栏保护的 time.* 绑定。
- 需要读取外部数据时,同步在 manifest.capabilities 声明对应能力;
  漏声明即视为越权。
- scale / offset 用于把数据映射到目标区间,保持单调可预期。
- 需要非线性手感时用 response(闭集 8 条曲线,见 wallpaper-content);
  它只对 float 源有效,且不得引入任何脚本。
- 非时间类数据不得驱动逐帧更新。

【scene.json】
- 顶层字段:schema(1)· id(scene://<name>)· kind · profile · rootNodeId ·
  nodes[] · assets[] · shaders[] · materials[] · inputs[] · bindings[] · animations[]。
- 每个 node 有唯一 id(node://<name>)、name、parentId、enabled、components[]。
- rootNodeId 必须指向存在的 node;parentId 为空表示根。
- asset 只引用包内相对路径。
- material 优先引用 builtin 模型(builtinName);builtin 无法表达时才自定义 shader。
- 保持节点与组件数量最小化:能用一个节点表达的不要拆成多个。

【sprite 怎么拿到一张图:两种途径,选一种】
一个 spriteRenderer 要到"画出图像"这一步,只有两条路,共用同一个纹理寄存器(t0),
所以**只能选一条**;两条都写会被拒绝,报错点名组件。

1. 组件自带的 `texture` 属性(assetReference),指向一个 `AssetType::Image` 资产。
   - **不需要 material**,也不用声明 `materials[]` —— 仓库里唯一填满的壁纸
     MiaoCloud 就是这种:5 个图层 sprite 各有 texture,`materials` 是空数组。
   - 这是图片图层该用的写法,优先级高于 shader 路线。
2. `materialId` 指向一个 material。
   - builtin material:**只有 `builtinName: "solidColor"` 一种能用**。
     别的 builtin 名一律被拒 —— 不会静默忽略,而是明确报错。
   - programmable material(`model: "programmable"` + `pixelShaderId`):
     **只有 D3D11 后端能画**。D2D 后端没有 shader 路径,会拒绝加载整个包。
     用它就要接受"这份内容在 D2D 后端上不可见"。
   - `solidColor` 的 `color` 属性会给贴图染色,它和 `texture` 可以同时存在。

【`tint` 在两个后端上不一样,这不是 bug】
- D3D11:tint 是 shader 常量,对贴图免费,染成什么颜色都行。
- D2D:`ID2D1BitmapBrush` 没有颜色成员,一条 pass 内无法给位图染色,
  所以**贴图 sprite 上非白色 tint 会被明确拒绝**;白色(默认)不受影响。
- 结论:想让一个包在两个后端都能加载,贴图 sprite 的 tint 保持默认白色,
  要染色就走 `solidColor` 的 `color` —— 那条路两个后端都通。

【`materialId` 写错名字没有兜底】
`materialId` 指向一个不存在的 material 是硬错误(报错点名该 id),
不会被"取场景里第一个 builtin material"这种兜底悄悄替换。
```

## 反面提示词

```text
绝不允许:
- 产出 schema 不等于 1 的 manifest 或 scene.json
- 用文件夹名当身份,或复用内置包已占用的 id
- kind 与扩展名不一致,或 runtime 取 scene / web 以外的值
- 引用 Package Root 之外的绝对路径,或用 ../ 越界
- 使用 symlink / junction / reparse point 指向包外
- 在 manifest 声明内容并未使用的能力
- 暴露内部常量、调试值、实现细节作为参数
- 给出越界默认值,或缺 min/max/step 的无界 float
- 出现悬空 parentId / rootNodeId / sourceId(指向不存在的对象)
- 用自定义 shader 表达一个 builtin material 就能做到的效果
- 在 spriteRenderer 上同时写 programmable material 和 texture(两者都要 t0)
- 给贴图 sprite 写非白色 tint(在 D2D 后端会被拒;要染色就用 solidColor 的 color)
- 把 `builtinName` 写成 solidColor 以外的值(只有这一个是可用的)
- 写 programmable material,却假定内容在 D2D 后端上也能显示
- 无意义地堆叠节点、组件或 pass 来"增加细节"
- 输出 HTML / JavaScript / CSS / shell 命令 / 原生可执行文件
```

## 输入 / 输出

- **输入**:用户意图(自然语言)、目标 kind(wallpaper / widget)
- **输出**:`manifest.json` + `parameters.json` + `scene.json` 草案,以及包目录结构

## 组合

被 `wallpaper-content` 与 `widget-content` 引用;`content-review` 会复核本 skill 的每一条约束。
