# Miao Scene Engine 开发路线

- 状态：Phase 3 GPU Runtime 执行计划
- 日期：2026-09-07
- 适用分支：`main`
- 上位基线：`DESIGN_BASELINE.md`、`DEVELOPMENT_ROADMAP.md`、`MIAODESK_CONTENT_FRAMEWORK.md`、`MIAO_SCENE_ENGINE.md`

本文把 MiaoDesk Content Framework 中已经进入代码的 Scene / GPU Runtime 继续拆成可执行里程碑。目标不是复制旧 Wallpaper Editor，而是建立一套可被 Wallpaper 与 Widget 共用、可参数化、可打包、可预览、可恢复的 Miao Scene Engine。

## 1. 当前起点

截至本路线建立时，主干已经具备：

```text
Miao Content Package (.mdwall)
  → manifest / scene / parameters / assets
  → MiaoSceneSerializer
  → SceneRuntimeDefinition
  → MiaoSceneRuntime
  → MiaoAssetDatabase
  → MiaoRenderGraph
  → D3D11 Scene Renderer
  → Wallpaper per-monitor surface
```

已进入真实运行路径的能力：

- D3D11 hardware + WARP fallback；
- SwapChain / Resize / Present；
- Render Graph 基础依赖排序与 cycle 检测；
- builtin 与 programmable material；
- Vertex / Pixel HLSL；
- `MiaoFrame` / `MiaoObject` / `MiaoParameters` constant buffers；
- package image → WIC → D3D11 SRV；
- `t0`、`t1`、`t8..t15` texture contract；
- ParameterValues → GPU parameter block；
- GPU device removed/reset 错误检测；
- programmable `.mdwall` 根据内容能力自动进入 D3D11 路径。

当前最大缺口是：Render Graph 已经有“多 Pass”的模型，但 Renderer 仍主要把 Scene 直接画到 Backbuffer。下一步必须先把中间 RenderTarget 做成真实资源，再继续 Post Process、Particle、Animation 等功能。

## 2. 总体顺序

```text
M0  合同冻结与回归基线
 ↓
M1  Offscreen RenderTarget + 真 Multi-Pass
 ↓
M2  Post Process Runtime
 ↓
M3  Animation Runtime
 ↓
M4  GPU Particle Runtime
 ↓
M5  Input / Audio Data Bus
 ↓
M6  Hot Reload / Preview Runtime
 ↓
M7  Renderer Process Sandbox
 ↓
M8  Widget Host 复用 Scene Engine
 ↓
M9  Dogfood / 性能 / 多显示器验收
 ↓
M10 AI / Creator 上层入口
```

任何里程碑都不能破坏现有桌面契约：

```text
Widget > Desktop Icons > Wallpaper
Wallpaper click-through
Widget interactive
per-monitor wallpaper instance
Explorer repair
DPI / monitor topology recovery
Wallpaper stop 幂等
```

## 3. M0 — 合同冻结与回归基线

### 目标

把已经进入代码的约定固定下来，避免后续每个模块重新定义一套资源/参数/Shader 规则。

### 工作项

1. `parameters.json` 作为当前包内参数文件规范名称；旧文档中的 `schema.json` 统一迁移为 `parameters.json` 表述。
2. 固定 Shader ABI v1：
   - `b0 = MiaoFrame`
   - `b1 = MiaoObject`
   - `b2 = MiaoParameters`
   - `t0 = input`
   - `t1 = mask`
   - `t8..t15 = package/user textures`
   - `s0 = linear sampler`
3. Compute Shader 只在模型中保留，不执行，直到资源/dispatch sandbox 完成。
4. Custom HLSL 保持 `MIAO_SCENE_ENGINE.md` 已确认的正式第一阶段能力；当前先在 `MiaoDeskWallpaper.exe` 内建立 fault boundary、Preview 与 fallback，后续 M7 再升级为独立 renderer process 隔离，不倒退能力模型。
5. `MiaoRenderGraph::SelfTest`、Shader Contract、自定义参数打包、texture path policy 继续作为最低回归门禁。

### 完成标准

- 文档与代码命名一致；
- x64 Build 通过；
- Path Layout Contract 通过；
- 不新增第二套 Scene schema。

## 4. M1 — Offscreen RenderTarget + 真 Multi-Pass

这是当前立即执行的里程碑。

### 目标

把：

```text
Scene → Backbuffer
```

升级为：

```text
Scene → SceneColor(offscreen)
      → Composite / Post
      → Backbuffer
      → Present
```

### 4.1 Render Resource 描述

Render Graph 资源需要具备至少：

```text
id
external
persistent
format
size policy
usage
```

v1 先固定：

- Color resource；
- BGRA8 UNORM；
- surface-relative size；
- scale 1.0 / 0.5 / 0.25；
- render-target + shader-resource usage。

后续再扩 HDR / depth / UAV，不在第一步一起做。

### 4.2 D3D11 RenderTarget 生命周期

建立 Engine-owned offscreen target：

```text
ID3D11Texture2D
ID3D11RenderTargetView
ID3D11ShaderResourceView
```

规则：

- 创建随 Device；
- Resize 随 Host Surface；
- device reset 时释放重建；
- pass 读写切换前解除 SRV/RTV 冲突绑定；
- Engine-owned 与 Host-owned backbuffer 明确分离。

### 4.3 Pass 执行器

Renderer 不再硬编码“遇到 Scene2D/Programmable 就 Draw”。建立按 RenderPassKind 执行的调度入口：

```text
Scene2D / Programmable
PostProcess
Composite
Present
```

第一阶段真正跑通：

```text
programmable scene
  writes SceneColor

composite
  reads SceneColor
  writes Backbuffer

present
  reads Backbuffer
```

### M1 完成标准

- Scene 不直接依赖 backbuffer 才能绘制；
- SceneColor 可作为 SRV 被下一 Pass 读取；
- Resize 后 SceneColor 自动重建；
- Render Graph 顺序真正决定实际执行顺序；
- ShaderPulse 经 offscreen + composite 后视觉保持正确；
- x64 编译 / smoke 通过；
- 无 D3D11 simultaneous input/output binding warning。

## 5. M2 — Post Process Runtime

### 目标

允许 Scene 输出经过一至多个后处理 Pass，而不是把 effect 写死进主 Scene shader。

第一批 Engine-owned effect：

```text
Copy
ColorMatrix
Blur
Bloom-lite
Vignette
Noise
```

先提供 builtin effect，不先开放 Post Process Editor。

Graph 示例：

```text
SceneColor
  ↓
BlurA
  ↓
BlurB
  ↓
Composite
  ↓
Backbuffer
```

完成标准：至少一个官方 `.mdwall` 通过两个以上真实 offscreen pass 工作。

## 6. M3 — Animation Runtime

### 目标

把内容动画从 hardcoded C++ / HLSL 时间逻辑中抽离为 Runtime 合同。

第一批：

```text
opacity
position
scale
rotation
color
float parameter
```

需要：

- Timeline clock；
- easing；
- loop / ping-pong；
- event-triggered animation；
- property dirty propagation；
- Wallpaper continuous 与 Widget event-driven 两种调度策略。

完成标准：动画内容不要求作者写 C++；Widget 无动画时不进入无意义高频重绘。

## 7. M4 — GPU Particle Runtime

### 目标

支持天气、星空、尘埃、粒子流等动态桌面内容，但保持资源预算可控。

顺序：

1. CPU-authored particle definition；
2. D3D11 instanced rendering；
3. bounded particle count；
4. GPU update/compute 仅在 sandbox/resource contract 成熟后开启。

首版不做大型 Particle Editor。

## 8. M5 — Input / Audio Data Bus

统一 Frame/Input Provider，不让 Scene 直接访问 Win32 或音频设备。

第一批：

```text
time
resolution
mouse position / velocity
visibility / pause state
audio volume / bass / mid / treble
```

后续系统数据仍通过 Capability Broker。

完成标准：暂停/锁屏/不可见时能够降低或停止高频更新；Widget 能保持事件驱动。

## 9. M6 — Hot Reload / Preview Runtime

目标链：

```text
package files changed
  → validate
  → build shadow runtime
  → preview
  → atomic swap
  → old runtime release
```

要求：

- scene / parameter / shader / texture 热重载；
- 编译失败保留上一帧可用 runtime；
- error diagnostics 可返回 Settings/Creator/AI Preview；
- 不允许失败包直接污染正式 ContentInstance。

## 10. M7 — Renderer Process Sandbox

这是对已经开放的 Custom HLSL/未来 Script 的隔离升级，不是开放 Custom HLSL 的前置条件。

目标：

```text
MiaoDeskWallpaper.exe
  ↓ narrow IPC
MiaoSceneRenderer.exe
  ↓
D3D11 / HLSL / package assets
```

隔离：

- shader compile crash；
- GPU device crash/recovery；
- package parser/resource abuse；
- future compute/custom effect execution。

Capability Broker 仍在可信宿主侧，Renderer 只接收批准后的数据快照/资源句柄。

注意：当前正式基线仍是三个 EXE。真正新增 `MiaoSceneRenderer.exe` 前，必须先更新项目级基线与 packaging contract，不能在实现中偷偷增加第四个正式进程。

完成标准：Renderer 崩溃不能带走 MiaoDesk 主 UI，Wallpaper Host 可回退并重启 renderer。

## 11. M8 — Widget Host 复用 Scene Engine

目标不是给 Widget 再写一套 Renderer，而是复用：

```text
Package
Parameter
SceneRuntime
Binding
Animation
RenderGraph
Asset
```

Widget Profile 只增加：

```text
interactive surface
pointer events
geometry policy
event-driven scheduler
actions
```

第一份 dogfood：`GlassClock.mdwidget`。

完成标准：GlassClock 不再依赖专用 Painter 才能存在，并且 CPU/内存不劣于当前 Native preset 基线。

## 12. M9 — Dogfood 与性能验收

必须至少覆盖：

- ShaderPulse / 一个正式 GPU wallpaper；
- 一个普通 Native Scene wallpaper；
- GlassClock.mdwidget；
- 横屏 + 竖屏；
- 96 / 120 / 144+ DPI；
- 双显示器不同内容；
- Explorer restart；
- sleep / resume；
- GPU device reset fallback；
- Wallpaper disable 后 Widget 独立存活。

建立指标：

```text
idle CPU
active GPU
working set
GPU memory
handle count
frame time
reload latency
```

## 13. M10 — AI / Creator

Runtime 稳定后再把大规模产品化创作体验接上；但 AI/用户生成 Custom HLSL 的能力本身已经属于 Scene Engine 第一阶段合同。

AI 可以生成/修改：

```text
manifest
scene
parameters
bindings
HLSL
particle / animation definition
```

所有持久桌面变更仍走：

```text
Generate / Patch
→ Validate
→ Compile
→ Preview
→ User Apply
```

Creator 也是同一套底层协议，不再拥有私有 Scene 格式。

## 14. 每一步的提交与验收纪律

开发按“小闭环”推进，不一次把 M1-M10 混在一个提交里。

每个子步骤：

```text
contract/model
→ implementation
→ self-test / smoke
→ x64 build
→ package smoke（到稳定点时）
→ real Windows visual validation（涉及桌面显示时）
```

CI 被后续 push 自动取消不算失败；判断以当前 HEAD 对应的最新 run 为准。

如果一个步骤需要改变既有桌面层级、Shell ownership 或安装布局，先停在该步骤重新评审，不为 Scene Engine 强行改 Host 基线。
