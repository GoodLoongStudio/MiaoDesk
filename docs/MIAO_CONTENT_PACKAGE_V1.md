# Miao Content Package v1 — External Content Contract

- 状态：Phase 3 / Slice B 正式技术契约
- 日期：2026-09-07
- 所属：`MiaoDesk Content Framework`
- 上位引擎：`Miao Scene Engine`
- 关联：`MIAODESK_CONTENT_FRAMEWORK.md`、`MIAO_SCENE_ENGINE.md`

## 1. 目标

这一阶段的目标不是继续增加某一种视觉能力，而是让 MiaoDesk 从“C++ 内置内容”进入“外部内容平台”。

第一个关键里程碑定义为：

> 不修改 MiaoDesk C++，只新增一个 `.mdwall` / `.mdwidget` 内容包，MiaoDesk 就能够发现、校验、加载并实例化新的 Scene 内容。

因此 Package / Serialization / Asset ingress 是 Miao Scene Engine 从对象模型走向真实用户内容的正式入口。

核心链路：

```text
.mdwall / .mdwidget
        ↓
manifest.json
        ↓
scene.json + parameters.json + assets
        ↓
MiaoContentPackage
        ↓
MiaoSceneSerializer
        ↓
SceneRuntimeDefinition
        ↓
MiaoSceneRuntimeModel::Validate
        ↓
Runtime Instance
        ↓
Render Graph / Renderer
```

注意：**JSON 只是 Scene Engine 对象模型的序列化格式，不是引擎本身。** 顺序始终是：

```text
C++ Object Model → Serialization Contract → Package Format
```

而不是先设计 JSON 再让 Runtime 迁就 JSON。

## 2. 一套 Package，两种产品 Profile

Wallpaper 与 Widget 共用同一个 Miao Content Package 核心格式：

```text
.mdwall    → ContentKind::Wallpaper
.mdwidget  → ContentKind::Widget
```

差异只体现在 `kind` / Runtime Profile / Desktop Host policy，不重新发明两套 Scene、Material、Parameter、Asset 系统。

第一阶段 Package 采用“目录即包”的开发格式，方便 AI Creator、热更新和调试：

```text
Aurora.mdwall/
├─ manifest.json
├─ scene.json
├─ parameters.json
├─ assets/
├─ shaders/
└─ preview/
```

未来可以增加归档/压缩传输格式，但解包后仍映射到同一 Package Root 和同一验证规则。

## 3. manifest.json v1

第一版 manifest 保持小而稳定：

```json
{
  "schema": 1,
  "id": "com.goodloong.aurora-minimal",
  "name": "Aurora Minimal",
  "author": "GoodLoong",
  "version": "1.0.0",
  "kind": "wallpaper",
  "runtime": "scene",
  "entry": "scene.json",
  "parameters": "parameters.json",
  "preview": "preview/thumbnail.jpg",
  "capabilities": ["clock.read", "audio.read"]
}
```

字段约束：

- `schema`：当前必须为 `1`；
- `id`：稳定内容 ID，不能依赖文件夹名；
- `name`：展示名；
- `author`：作者元数据，可为空；
- `version`：内容版本；
- `kind`：`wallpaper | widget`，必须与 `.mdwall | .mdwidget` 一致；
- `runtime`：`scene | web`；
- `entry`：Scene 时通常为 `scene.json`，Web 时为 HTML；
- `parameters`：可选 ParameterSchema；
- `preview`：可选预览资源；
- `capabilities`：内容声明需要的数据/系统能力，不直接授予 Win32 权限。

## 4. Package Root 是安全边界

任何 Package 声明的路径都必须是 Package Root 内安全相对路径。

允许：

```text
scene.json
assets/background.png
shaders/water.hlsl
preview/thumb.jpg
```

拒绝：

```text
../outside.txt
../../Windows/System32/...
C:\Users\...
\\server\share\...
```

加载器必须同时做：

1. lexical 相对路径检查；
2. canonical / weakly-canonical containment 检查；
3. 必需资源存在性检查；
4. 文件大小预算；
5. Package kind 与扩展名一致性检查。

后续 Asset System、Shader include、Script import 都必须复用这一安全根，不允许各自实现另一套路径规则。

## 5. scene.json 是 Runtime Object Model 的序列化

Scene 文件序列化以下核心对象：

```text
Scene
├─ Node[]
│  └─ Component[]
├─ Asset[]
├─ ShaderProgram[]
├─ Material[]
├─ Parameter[]
├─ InputChannel[]
└─ Binding[]
```

任何被 AI、动画、Binding、Script 或热更新引用的对象必须使用稳定 ID，例如：

```text
scene://main
node://background
component://background/sprite
asset://background
material://background
shader://water
param://intensity
input://audio/bass
binding://audio-glow
```

禁止把数组下标当长期引用契约。

## 6. Parameter 与 Binding

`parameters.json` 是用户/AI 可配置面的正式 schema，而不是每个 Widget/Wallpaper 手写设置 UI。

例如：

```json
{
  "schema": 1,
  "parameters": [
    {
      "id": "param://intensity",
      "type": "float",
      "default": 0.7,
      "min": 0.0,
      "max": 2.0,
      "step": 0.01
    }
  ]
}
```

Parameter、Input、Animation、Script、AI Patch 最终都通过 Property System 修改 Runtime，不直接操作 Renderer。

```text
Parameter ─┐
Input ─────┤
Animation ─┤
Script ────┼→ Property System → Runtime
AI Patch ──┘
```

## 7. Asset System

Package 中所有运行资源最终都进入统一 Asset Database：

```text
AssetId
AssetType
RelativePath
ContentHash
Dependencies
RuntimeMetadata
```

初始类型：Image / Video / Audio / Font / Shader / Script / Binary；后续加入 Mesh、Compute data 等。

目标：

```text
修改一个资源
    ↓
hash/dependency 变化
    ↓
只重新导入受影响资源
    ↓
Hot Reload Scene
```

AI Creator 修改一个 Material、HLSL、图片或参数时，不需要重新加载整个桌面进程。

## 8. 加载阶段

正式加载过程固定为：

```text
Discover Package
    ↓
Load manifest
    ↓
Validate package root / paths / limits
    ↓
Read entry + parameter schema
    ↓
Deserialize
    ↓
Validate stable IDs / types / references
    ↓
Resolve assets + dependencies
    ↓
Create SceneRuntimeDefinition
    ↓
Create Runtime Instance
```

任何一步失败都返回可诊断错误，不允许“半加载”后直接污染当前桌面实例。

## 9. Definition / Instance / RuntimeState 继续严格分离

```text
Package / SceneDefinition
  内容作者定义的资源、节点、材质、参数和能力

ContentInstance
  monitor / geometry / parameter values / enabled

SceneRuntimeState
  GPU resources / dirty state / animation time / provider state / errors / PaintReady
```

Package 文件不能保存 GPU handle、当前 HWND、当前 D3D resource 等 RuntimeState。

## 10. 与 HLSL 的关系

HLSL 只是 Programmable Material 的一种 Asset/Shader 能力，不是 Package 或 Scene Engine 的中心。

一个 Package 可以完全不包含 HLSL：

```text
Image + Text + Builtin Material
```

也可以包含用户/AI 自定义 Shader：

```text
Scene + Material + shaders/water.hlsl
```

Package 层只负责安全加载、资源解析和契约校验；Shader 的编译和 GPU fault policy 属于 Miao Scene Engine Render Runtime。

## 11. Slice B 施工顺序

### B.1 Package ingress（现在开始）

- `MiaoContentPackageManifest`
- `.mdwall/.mdwidget` kind validation
- manifest loader
- package-root path sandbox
- entry / parameters / preview size and existence validation
- capability declaration parsing
- raw entry/parameter source load
- self-test

### B.2 Scene serialization

- `MiaoSceneSerializer`
- `scene.json → SceneRuntimeDefinition`
- PropertyValue typed serialization
- Node / Component / Asset / Shader / Material / Binding reference validation
- serializer round-trip test

### B.3 Asset database

- hash
- dependency graph
- import/cache metadata
- dirty detection
- hot reload invalidation

### B.4 First external-content milestone

至少一个官方示例 `.mdwall` 在不修改 Runtime C++ 的前提下完成：

```text
Package Discover
→ Load
→ Deserialize
→ Validate
→ Runtime Instance
→ Render
→ Desktop Surface
```

完成这一里程碑后，再进入 D3D11 Render Core / Render Graph 的完整施工。

## 12. 当前施工状态

本契约落库的同时开始 B.1：新增通用 `MiaoContentPackage`，作为旧 `WallpaperPackage` 之外的新 Content Framework 入口。旧包实现暂时保留兼容，不把新 Runtime 继续塞进旧 wallpaper-specific parser。

目标是逐步形成：

```text
旧 WallpaperPackage         → compatibility path
MiaoContentPackage          → canonical Content Framework path
```

新功能只扩展 canonical path。
