# MiaoDesk Content Framework（妙喵内容框架）

- 状态：下一阶段开发契约
- 日期：2026-09-05
- 适用分支：`main`
- 上位基线：`DESIGN_BASELINE.md`、`DEVELOPMENT_ROADMAP.md`
- 目标：把 Wallpaper 与 Widget 从“硬编码内容”升级为“标准底层框架 + 可配置内容”，让官方、用户、AI 与未来 Creator 使用同一套内容模型和运行时。

## 1. 命名

这一阶段统一命名为：

```text
MiaoDesk Content Framework
妙喵内容框架
```

它是产品级总称，不叫 Wallpaper Editor、Widget Editor，也不叫 Scene Editor。

框架内部使用以下概念：

```text
MiaoDesk Content Framework
├─ Miao Content Runtime      内容运行时
├─ Miao Content Package      内容包
├─ Parameter System          参数系统
├─ Scene Runtime             原生场景运行时
├─ Capability Broker         能力代理
└─ Authoring / Creator       创作层（后续）
```

当前阶段首先建设 Runtime 与数据模型，不先建设大型可视化编辑器。

## 2. 核心目标

当前 Wallpaper 与 Widget 已经有稳定的 Windows Desktop Host、Shell attachment、z-order、multi-monitor 与 Native rendering 基础，但内容仍较多由产品代码直接定义。

下一阶段的核心变化是：

```text
以前：
MiaoDesk code
  ├─ 知道玻璃时钟怎么画
  ├─ 知道天气组件怎么画
  └─ 知道某张 Scene Wallpaper 怎么画

以后：
MiaoDesk Host
  ↓
Content Runtime
  ↓
Content Definition / Package
  ↓
Scene + Parameters + Assets + Capabilities
```

Wallpaper 和 Widget 不再自己定义“内容是什么”，它们主要负责“内容在哪里、以什么桌面语义运行”。

## 3. 总体架构

```text
                         MiaoDesk
                            │
                  ┌─────────┴─────────┐
                  │                   │
           Wallpaper Host        Widget Host
                  │                   │
                  └─────────┬─────────┘
                            │
                    Content Runtime
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
   Package System     Parameter System     Capability Broker
        │                   │                   │
        └───────────────────┼───────────────────┘
                            │
                       Scene Runtime
                            │
              Direct2D / DirectWrite / WIC
                    Media Foundation
                            │
                  Windows Desktop Surface
```

上层所有内容来源最终走同一套 Runtime：

```text
官方内容 ─┐
用户内容 ─┼→ Package / Definition → Content Runtime → Host
AI 内容 ──┤
Creator ──┘
```

原则：官方内容也必须尽量使用用户内容框架，避免形成“官方 Native hardcode / 用户另一套 Runtime”的双轨系统。

## 4. Wallpaper 与 Widget：统一内容，不统一宿主语义

Wallpaper 与 Widget 共用 Content Runtime、Package、Parameter、Scene、Asset 与 Capability 模型。

它们的差异留在 Host / Instance 层：

```text
Wallpaper
- 全屏或按显示器 Surface
- 默认 click-through
- wallpaper lifecycle
- fit / fill / per-monitor assignment

Widget
- 局部桌面 Surface
- 默认可交互
- x / y / width / height
- drag / optional resize
- widget lifecycle
```

因此不建立两套 renderer。

目标模型：

```text
Wallpaper Host ─┐
                ├→ Miao Content Runtime
Widget Host ────┘
```

## 5. Content Definition 与 Content Instance 必须分离

模板定义与桌面实例不是同一个对象。

### 5.1 ContentDefinition

负责描述内容本身：

```text
ContentDefinition
├─ id
├─ type: wallpaper | widget
├─ runtime: scene | web
├─ version
├─ scene / entry
├─ assets
├─ parameter schema
├─ capability declarations
├─ default geometry
└─ author / metadata
```

### 5.2 ContentInstance

负责用户当前桌面的实例状态：

```text
ContentInstance
├─ instanceId
├─ definitionId
├─ monitorId
├─ enabled
├─ geometry / fit mode
└─ parameterValues
```

Widget 实例：

```text
WidgetInstance
├─ definitionId
├─ monitorId
├─ x
├─ y
├─ width
├─ height
└─ parameterValues
```

Wallpaper 实例：

```text
WallpaperInstance
├─ definitionId
├─ monitorId
├─ fitMode
└─ parameterValues
```

同一个 Definition 可以在不同显示器上拥有不同 Instance 参数，不复制整个内容包。

## 6. Miao Content Package

统一定义内容包概念，Wallpaper 与 Widget 使用相同的核心结构。

建议目录：

```text
package/
├─ manifest.json
├─ scene.json
├─ schema.json
└─ assets/
   ├─ images/
   ├─ videos/
   └─ fonts/
```

Wallpaper 继续使用：

```text
.mdwall
```

Widget 新增：

```text
.mdwidget
```

两个扩展名可以有不同产品语义，但内部核心格式应尽可能共享。

示例 manifest：

```json
{
  "id": "com.example.glass-clock",
  "name": "My Glass Clock",
  "type": "widget",
  "version": "1.0.0",
  "runtime": "scene",
  "entry": "scene.json",
  "parameters": "schema.json"
}
```

## 7. Parameter System 是核心能力

参数系统不是 UI 附属功能，而是 Content Framework 的核心数据契约。

最小参数类型：

```text
bool
int
float
string
color
enum
image / asset reference
```

示例：

```json
{
  "parameters": {
    "backgroundOpacity": {
      "type": "float",
      "default": 0.55,
      "min": 0,
      "max": 1
    },
    "accentColor": {
      "type": "color",
      "default": "#FFFFFF"
    },
    "showTemperature": {
      "type": "bool",
      "default": true
    }
  }
}
```

同一份 ParameterSchema 同时服务：

```text
Settings UI
AI 修改
Creator
Preset 保存
Package 导入导出
版本迁移
```

设置 UI 不需要知道某个第三方内容具体有哪些选项，只根据 schema 生成相应控件。

AI 也优先修改参数值，而不是修改代码：

```text
“把时钟变成半透明蓝色”
  ↓
backgroundOpacity = 0.4
accentColor = #72A7FF
```

## 8. Native Scene Runtime

默认内容 Runtime 为 Native Scene Runtime。

第一阶段 Scene Node 保持小而稳定：

```text
Scene
├─ Group
├─ Rectangle
├─ RoundedRectangle
├─ Text
├─ Image
├─ Video
├─ Gradient
├─ Mask
└─ DataBinding
```

示例：

```json
{
  "nodes": [
    {
      "type": "roundedRect",
      "x": 0,
      "y": 0,
      "width": "100%",
      "height": "100%",
      "radius": 24,
      "fill": "$backgroundColor"
    },
    {
      "type": "text",
      "value": "{{time.hhmm}}",
      "x": "50%",
      "y": "45%",
      "fontSize": 52,
      "align": "center"
    }
  ]
}
```

运行路径：

```text
scene.json
    ↓
Miao Scene Runtime
    ↓
Direct2D / DirectWrite / WIC / Media Foundation
    ↓
Wallpaper / Widget Surface
```

目标是让官方内容和用户内容走相同 Native 性能路径。

## 9. Data Binding

Scene 不直接调用系统 API，而通过声明式数据绑定获取动态数据。

第一批建议数据源：

```text
time.*
weather.*
tasks.*
```

后续可以扩展：

```text
system.cpu
system.memory
battery
calendar
music
network
```

示例：

```text
{{time.hhmm}}
{{weather.temperature}}
{{tasks.today}}
```

Data Binding 必须与 Capability Broker 一起设计，避免第三方内容绕过宿主访问系统资源。

## 10. Capability Broker

用户内容不得默认获得任意：

```text
filesystem
registry
shell
process
network
native DLL
Windows API
```

需要系统能力时，通过声明式 capability 请求：

```json
{
  "capabilities": [
    "clock",
    "weather"
  ]
}
```

运行时：

```text
Content
   ↓
Capability Broker
   ↓
MiaoDesk Service
   ↓
Windows / Network / User Data
```

Package 声明能力，MiaoDesk 决定是否支持、是否需要用户授权以及提供什么受限数据。

## 11. Widget Geometry 也必须配置化

旧的三款内置 Widget 使用固定 Preset 尺寸；新 Content Framework 需要把 geometry policy 放入 Definition。

示例：

```json
{
  "geometry": {
    "defaultWidth": 0.30,
    "defaultHeight": 0.30,
    "resize": true,
    "minWidth": 0.15,
    "minHeight": 0.15,
    "maxWidth": 0.60,
    "maxHeight": 0.60,
    "aspectRatio": "free"
  }
}
```

允许：

```text
resize = false
aspectRatio = 1.0
aspectRatio = free
```

因此“是否允许 resize”属于内容模板策略，不再由整个 Widget 系统统一写死。

## 12. Animation

动画优先采用声明式模型。

第一批属性：

```text
opacity
position
scale
rotation
color
gradientOffset
```

示例：

```json
{
  "animation": {
    "target": "background",
    "property": "rotation",
    "from": 0,
    "to": 360,
    "duration": 30000,
    "loop": true,
    "easing": "linear"
  }
}
```

第一阶段不建设：

```text
Shader Editor
Particle Editor
Node Graph
大型 Timeline Editor
```

先验证 Runtime 的声明式动画契约。

## 13. Scene Runtime 与 Web Runtime

最终允许两种内容 Runtime。

### 13.1 Scene Runtime

默认、推荐：

```text
JSON + assets
→ Native Scene Runtime
→ Direct2D / DirectWrite / WIC / Media Foundation
```

特点：

- 低常驻开销；
- 安全边界清晰；
- 易于参数化；
- AI 容易生成；
- Store 与内容审核更容易控制。

### 13.2 Web Runtime

用于 Native Scene 无法覆盖的高级内容：

```text
HTML / CSS / JS
→ isolated WebView2
```

默认限制：

```text
No Node
No arbitrary filesystem
No native DLL
No registry
No shell
```

系统能力仍通过 Capability Broker。

Web Runtime 不得成为所有用户内容的默认实现，也不能重新把 WebView2 带回所有常驻 Widget 路径。

## 14. Authoring 路线：Runtime First

开发顺序必须避免再次出现“先做巨大 Editor，底层模型持续变化导致 Editor 重写”的问题。

阶段：

```text
Stage 1  Content Runtime
Stage 2  Parameter Editor / Package tooling
Stage 3  Visual Creator
```

Stage 1 可以先允许开发者直接编辑：

```text
manifest.json
scene.json
schema.json
assets/
```

然后导入 MiaoDesk 验证。

Stage 2 提供基于 ParameterSchema 的设置、预览、打包与校验工具。

Stage 3 才建设视觉创作体验，例如添加文本、图片、矩形、绑定参数、实时 Preview。

## 15. AI 与 Creator 的统一边界

AI、Creator、第三方作者不应该拥有不同的底层协议。

统一路径：

```text
Creator ─┐
AI ──────┼→ Definition / Parameters / Scene
用户文件 ┘
              ↓
            Preview
              ↓
         用户确认 / Save
              ↓
         Package / Instance
              ↓
         Content Runtime
```

AI 默认修改声明式内容与参数，不直接注入 Native code、shell command 或任意脚本。

对影响桌面持久状态的操作继续保留 Preview / explicit commit 边界。

## 16. 官方内容必须 Dogfood 用户框架

Content Framework 成立的关键验收不是“第三方 JSON 能加载”，而是官方内容也能逐步迁移进去。

第一批验证对象：

```text
GlassClock.mdwidget
一个 Native Scene Wallpaper .mdwall
```

后续迁移：

```text
TodayTasks.mdwidget
WeatherGlass.mdwidget
更多官方 Wallpaper
```

如果官方内容仍大量依赖 hardcoded C++ 特例，而用户内容走另一套 Runtime，则框架不算完成。

## 17. 第一阶段开发任务

第一阶段只做能够验证内容平台成立的最小闭环：

```text
1. ContentDefinition + ContentInstance
2. ParameterSchema + ParameterValues
3. .mdwidget / .mdwall shared package contract
4. Native Scene Runtime MVP
5. Data Binding MVP（time 优先）
6. Capability Broker 最小接口
7. Package validator / loader
8. Preview / reload path
9. GlassClock 迁移为 .mdwidget
10. 一个 Scene Wallpaper 迁移为新 .mdwall runtime
```

其中 GlassClock 是第一批 Widget dogfood，Scene Wallpaper 是第一批 Wallpaper dogfood。

## 18. 第一阶段明确不做

```text
完整 Visual Editor
Timeline / Keyframe Editor
Shader Editor
Particle Editor
Node Graph
任意 Native plugin / DLL 扩展
默认 unrestricted JavaScript
第三方内容直接访问 Windows API
官方内容与用户内容两套 renderer
```

这些能力如果未来需要，必须建立在 Content Runtime 已稳定的前提下。

## 19. 完成标准

第一阶段完成必须满足：

```text
用户可从包加载一个非硬编码 Widget
→ ParameterSchema 自动产生可修改参数
→ 修改参数立即反映到 Preview / Instance
→ Scene Runtime 使用 Native renderer 正确绘制
→ Widget 可在真实桌面拖动并保持现有 Shell/z-order 契约
→ 同一 Definition 可创建不同参数的 Instance
→ Package 不获得未声明系统权限
→ GlassClock 官方实现可以通过同一框架运行
→ 至少一个 Wallpaper 也通过同一框架运行
→ x64 正式 staging/package smoke 通过
→ 真实 Windows 多显示器 / DPI 验证通过
```

只有当官方内容和用户内容共享 Package、Parameter、Scene 与 Runtime 时，才能认为“配置化与参数化”真正完成。
