# Miao Scene Engine — Native GPU Scene Runtime

- 状态：Phase 3 场景引擎核心技术契约
- 日期：2026-09-05
- 所属：`MiaoDesk Content Framework`
- 上位基线：`DESIGN_BASELINE.md`、`DEVELOPMENT_ROADMAP.md`
- 关联：`MIAODESK_CONTENT_FRAMEWORK.md`

> 本文细化 Content Framework 中 Scene / GPU / Shader / Particle / Animation / Runtime Profile。若早期“简单 Scene Runtime”描述与本文冲突，以本文的 Scene Engine 子系统契约为准；项目级取舍仍以上位基线为准。

## 1. 命名与目标

正式命名：**Miao Scene Engine（妙喵场景引擎）**。

它不是 Wallpaper Editor 或 Widget Editor，而是 Wallpaper 与 Widget 共用的可编程内容引擎。目标是承载 Wallpaper Engine 级动态桌面效果，同时天然适合 AI 生成内容。

核心产品原则：

```text
用户的想象力决定内容上限；Runtime 不用“用户不会编程”限制表达能力。
AI Creator 负责生成/解释/修改 Scene、HLSL、Particle、Animation、Script。
MiaoDesk Runtime 负责安全、性能、资源、生命周期、Preview 与失败恢复。
```

因此 **Custom HLSL 从第一阶段就是正式能力**。不要求用户手写，但允许用户和 AI 直接创作 HLSL。

## 2. 能力目标

Miao Scene Engine 最终应能表达：

```text
Image / Video / Text / Vector
2D / 2.5D Layer + Parallax
Custom HLSL Shader
Water / Rain / Refraction / Noise / Aurora / Glow / Distortion / Raymarching
GPU Particle
Timeline + Procedural + Event Animation
Mouse Reactive
Audio Reactive
Weather / System / Media driven scene
Post Process
Future Mesh / 3D / Compute / Simulation
```

我们借鉴成熟动态壁纸产品已经验证过的能力边界，但使用自己的对象模型、Package、ABI、安全模型和 AI Authoring 路线，不复制其内部格式或实现。

## 3. 总体架构

```text
MiaoDesk Content Framework
│
├─ Content Package / Definition / Instance
├─ Parameter System
├─ Capability Broker
│
└─ Miao Scene Engine
   ├─ Scene System
   ├─ Property System
   ├─ Asset System
   ├─ Material / Shader System
   ├─ Particle System
   ├─ Animation System
   ├─ Script System
   ├─ Input Bus
   ├─ Render Graph
   └─ Runtime Scheduler
             │
             ▼
      Rendering Backends
      ├─ D3D11 / DXGI
      ├─ Direct2D / DirectWrite
      ├─ WIC
      └─ Media Foundation
             │
      ┌──────┴──────┐
      ▼             ▼
Wallpaper Host   Widget Host
```

Direct2D 不再等同于 Scene Runtime。高阶场景以 D3D11/DXGI GPU pipeline 为核心；Direct2D/DirectWrite 负责高质量 2D/Text 等子能力。

## 4. 四层边界

```text
Content Model
  Scene / Node / Component / Property / Asset

Creative Runtime
  Material / Shader / Particle / Animation / Script / Input

Render Runtime
  RenderGraph / Pass / RenderTarget / GPU Resource / D3D11 / Direct2D / Media

Desktop Runtime
  Wallpaper Host / Widget Host / DesktopShellHost / Monitor / DPI / Power / Recovery
```

边界必须单向：Shader 不直接访问 Win32；Scene 不直接 SetParent；Wallpaper Host 不理解具体 HLSL 语义。

## 5. 第一版冻结的核心抽象

第一版冻结 10 个核心抽象：

1. `Scene`
2. `Node`
3. `Component`
4. `Property`
5. `Asset`
6. `Material / ShaderProgram`
7. `ParticleSystem`
8. `Animation`
9. `Script`
10. `RenderGraph`

外围再定义 `InputBus` 和 `RuntimeProfile`。后续功能优先通过新增 Component / Asset / Property / Render Pass 扩展，而不是增加新的特殊 Host。

## 6. Scene / Node / Component

采用组合模型，而不是大量继承型 Node。

```text
Node
├─ id
├─ name
├─ parent / children
├─ enabled
└─ components[]
```

例如：

```text
Node: Character
├─ Transform
├─ SpriteRenderer
├─ Material
├─ Animator
├─ MouseReactive
└─ Script

Node: Rain
├─ Transform
├─ ParticleSystem
├─ Material
└─ AudioReactive
```

### Stable ID

任何会被 AI、Binding、Animation、Patch 引用的对象必须使用稳定 ID：

```text
scene://main
node://rain
component://rain/particle
asset://shader/rain-refraction
shader://rain-refraction
```

禁止把 `children[3]` 一类数组位置当长期契约。

## 7. Property System

所有可变状态都暴露成统一 Property，例如：

```text
Transform.position
Sprite.opacity
Material.glowIntensity
Material.waveSpeed
Particle.spawnRate
Text.fontSize
Video.playbackRate
```

Parameter、Animation、Data Binding、Script、AI Patch 都只通过 Property 修改内容：

```text
Parameter ─┐
Animation ─┤
Binding ───┼→ Property System → Scene Runtime
Script ────┤
AI Patch ──┘
```

例如“雨再大一点”最终可以只是 `property://rain.spawnRate = 240`，AI 不需要知道 Renderer 内部实现。

## 8. Asset System

资源独立于 Scene Tree：

```text
Asset
├─ id
├─ type
├─ source
├─ hash
├─ dependencies
└─ runtime metadata
```

初始类型：Image / Video / Audio / Font / Shader / Script / Binary；模型为后续 Mesh 等类型预留。

资源进入统一导入与缓存流程：

```text
PNG/JPG/WebP → TextureAsset
MP4/WebM     → VideoAsset
HLSL         → ShaderAsset
Script       → ScriptAsset
```

AI 修改一个 Shader 时，只重新编译和热加载受影响依赖，不重建整个 Package。

## 9. Material / Shader System

统一关系：

```text
Renderable → Material → ShaderProgram
```

Material 负责 ShaderProgram、Textures、Parameter block、Blend/Render state、Sampler。

`ShaderProgram` 第一版对象模型即表示 Vertex / Pixel / Compute。**v1 Runtime 优先执行 Vertex + Pixel，Compute 先进入对象模型，等 dispatch/UAV/资源配额和隔离规则完成后再开放执行。**

### 9.1 Custom HLSL 第一阶段开放

MiaoDesk 不把作者限制在官方 Blur / Glow / Ripple 列表。用户或 AI 可以直接提供 HLSL Shader Asset。

```text
Natural Language / User Code
        ↓
AI Creator / Source Author
        ↓
Custom HLSL + Parameters
        ↓
Validate → Compile → Preview → Apply
```

### 9.2 Miao Shader Contract v1

自由写 HLSL，但必须通过稳定 ABI 与引擎交互。第一版保留寄存器：

```text
b0  MiaoFrame
b1  MiaoObject
b2  Material / Parameter block

t0  primary input texture
t1  mask texture
t8..t15 user/package textures

s0  standard linear sampler
```

`MiaoFrame` 至少提供：time、deltaTime、resolution、mousePosition、mouseVelocity、audioVolume、audioBass、audioMid、audioTreble。

`MiaoObject` 至少提供：world transform、object color、object size、object opacity。

Parameter System 生成的值进入 b2 或等价 material constant storage。

### 9.3 创作自由与系统权限分离

Shader 可以自由做 Raymarching、SDF、Noise、Fractal、Water、Refraction、Distortion、Procedural Sky、Audio Visualizer 等 GPU 计算；但不能直接访问 filesystem、network、registry、Win32、Shell 或启动进程。

外部数据只能：

```text
Capability Provider → Input Bus / Property → GPU constant → Shader
```

开放 HLSL 不等于开放电脑权限。

## 10. Render Graph

复杂场景不能长期依赖 `for node -> Draw()`。建立 **Miao Render Graph**：

```text
Scene
 ↓
Compile Render Graph
 ↓
Background Pass
 ↓
Object / Video Pass
 ↓
Particle Pass
 ↓
Effect Pass
 ↓
Post Process
 ↓
Composite
 ↓
Desktop Surface
```

例如：

```text
Video → Water Shader → RT_A → Distortion → RT_B
     → Particles → Bloom → ColorGrade → Final Surface
```

Render Graph 负责 Pass 依赖、RenderTarget 生命周期/复用、Dirty/Skip 策略和后续 GPU profiling。

## 11. Particle System

粒子采用可组合模块：

```text
ParticleSystem
├─ Emitter[]
├─ Initializer[]
├─ Operator[]
├─ Renderer[]
└─ ControlPoint[]
```

第一批 Operator 可包含 Spawn、RandomPosition、RandomVelocity、Gravity、Wind、Noise、Drag、Attraction、Repulsion、Vortex、Color/Size/RotationOverLife、Fade、AudioScale、MouseForce。

目标实现为 GPU Particle；CPU 只负责低频控制和场景管理。

## 12. Animation System

统一支持：

```text
Timeline / Keyframe
Procedural Modifier
State / Event
```

Procedural 可表达 `sin(time)`、noise、pulse、oscillate、random；Event 可表达 MouseEnter、AudioBeat、WeatherChange 等触发。Animation 最终仍然修改 Property，不直接耦合 Renderer。

## 13. Input Bus

统一输入通道：

```text
frame.*
mouse.*
audio.*
display.*
system.*
weather.*
media.*
user.*
```

第一批核心输入：frame.time/deltaTime、mouse.position/velocity/click、audio.volume/bass/mid/treble、display.width/height。

Property、Shader、Particle、Animation、Script 使用同一 Input Bus，避免各系统建立私有数据接口。

## 14. Script System

HLSL 解决“怎么画”，Script 解决“什么时候做什么”。例如夜间切换状态、点击触发动画、天气变化、随机流星。

第一刀不实现完整脚本 VM，但对象模型从 v1 保留 Script Component / Asset。未来脚本只能通过 Miao Scene API 操作 Property/Event/Capability，禁止 Node/child_process、任意 filesystem、registry、native DLL、shell。

## 15. Wallpaper / Widget 只在 Runtime Profile 分叉

Scene、Shader、Material、Particle、Animation、Asset、Property、Input 全部共用。

Wallpaper Profile：per-monitor surface、pointer observe、click-through、允许 continuous animation、受 FPS/电源/全屏策略控制。Wallpaper 可以观察全局鼠标做视差/波纹，但 Surface 本身仍 click-through。

Widget Profile：local rect、pointer interactive、默认 event-driven、continuous animation 需要 opt-in、geometry 由 manifest policy 决定。Widget 默认不 60 FPS 常驻刷新。

## 16. Definition / Instance / RuntimeState 分离

```text
SceneDefinition
  节点 / 组件 / Shader / Asset / defaults

ContentInstance
  monitor / geometry / parameters / enabled

SceneRuntimeState
  GPU resources / render graph / animation time / dirty state / provider state / compile errors / paintReady
```

RuntimeState 不写回 Package 成为创作内容。

## 17. Shader 编译、Preview 与失败恢复

AI/用户生成 Shader 的统一路径：

```text
HLSL Source → Contract validation → Compile → Offscreen Preview
            → Runtime/GPU budget check → User Apply → Desktop Instance
```

编译器不提供任意 filesystem include handler；Package 内依赖由 Asset System 显式解析。

Runtime 必须处理 compile failure、device removed、render timeout、resource allocation failure、scene failure。

第一阶段先在现有 `MiaoDeskWallpaper.exe` 内建立清晰 Scene Runtime fault boundary 和 fallback。长期是否新增独立 `MiaoSceneRenderer.exe` 作为 GPU/Script sandbox，需要先更新当前“三个正式 EXE”的项目基线，不在本次骨架开发中偷偷增加第四个正式进程。

## 18. 性能与电源策略

视觉自由不能等价于 GPU 永久满载。Scheduler 至少支持 Low/Medium/High/Ultra、15/30/60 FPS bounded cap、无 dirty/continuous 时停帧、全屏降级/暂停、锁屏停止、电池模式降质。

Render Graph、Particle、Shader 后续都应输出 GPU 时间和资源统计，让 AI Creator 能提示“这个效果很重”并主动优化。

## 19. 序列化原则

顺序固定：

```text
C++ Runtime Object Model → Serialization Contract → scene/material/shader metadata → .mdwall/.mdwidget
```

不能先拍脑袋设计 JSON 再让 Runtime 迁就 JSON。`.mdwall` 与 `.mdwidget` 共用 Scene Engine 模型，仅通过 ContentKind / RuntimeProfile 和宿主策略区分。

## 20. AI Creator 的位置

AI 在 Renderer 外面：

```text
User → AI Creator → Scene Patch / HLSL / Particle / Animation / Parameters
                    ↓
                 Validate
                    ↓
                 Hot Reload
                    ↓
                  Preview
                    ↓
                   Apply
```

AI 不参与每帧渲染；生成后的 Scene 必须能够离线持续运行。

## 21. 实施切片

### Slice A — Object Model + Shader Contract（立即开始）

建立平台无关 C++ 基础：Scene、Node、Component、Property、Asset、ShaderDefinition、stable-id validation、Miao Shader Contract v1，并进入 `MiaoDeskCore` 编译。

### Slice B — Serialization / Package

实现 scene loader/writer、schema validation、`.mdwall/.mdwidget` manifest mapping、Asset database、hash/dependency tracking。

### Slice C — D3D11 Render Core

实现 SceneRuntime、RenderContext、RenderGraph MVP、Texture/RenderTarget、Sprite/full-screen quad、Direct2D text/UI interop。

### Slice D — Custom HLSL Runtime

实现 HLSL compile、Shader Contract binding、Vertex/Pixel Shader、Material parameter block、compile diagnostics、hot reload。

### Slice E — Preview / Recovery

实现 offscreen preview、Apply boundary、shader failure fallback、device-removed recovery、performance telemetry。

### Slice F — Animation / Particle / Input

实现 Property animation、mouse/audio Input Bus、GPU particle MVP、post-process pass。

### Slice G — Dogfood

至少迁移一个官方 GPU Scene Wallpaper 与 `GlassClock.mdwidget`，证明 Wallpaper/Widget 使用同一引擎。

## 22. 第一阶段“不做”的真正含义

第一阶段不先建设大型 Visual Scene Editor、Shader Graph Editor、Particle Editor、完整 Timeline Editor、完整 3D Editor；**这不等于限制 Runtime**。

第一阶段明确允许：用户直接写 HLSL、AI 生成/修改 HLSL、AI 生成/修改 Scene、Particle、Animation 配置、开发者直接编辑 Package source。

Creator UI 后续只是底层能力的一种可视化入口。

## 23. 验收原则

第一阶段最终应证明：

1. Custom HLSL 能通过稳定 Contract 编译、Preview、Apply；
2. Shader 参数可由 Parameter System 驱动；
3. Mouse/Audio 至少一个 Input Channel 能驱动 Shader 或 Property；
4. 一个 Scene 支持多个 Render Pass；
5. GPU/Shader 失败不会破坏桌面核心；
6. Wallpaper Host 与 Widget Host 能调用同一个 Scene Engine；
7. 官方内容开始 dogfood；
8. AI 能通过自然语言生成/修改 Scene 与 HLSL，而不进入每帧 Runtime。

长期目标不是复刻现有壁纸编辑器，而是建立 **AI-native programmable desktop platform**：底层保持接近游戏引擎的表达自由度，上层让用户主要通过想象和自然语言完成创作。
