# MiaoDesk 专业版能力与证据台账（PRO-01）

- 状态：PRO-01 交付物；与总体规划第 3/4/5 节同源，不另建目标任务。
- 建立：2026-10-03；实施：自动推进会话；签收：待产品所有者与 Windows 验收者。
- 核对基点：`d02812d63a2dbf480c4cb00faffb4f69a38b3bc4`（= 建立时 HEAD，工作区仅有文档改动）。
- 状态管理：[持续开发面板](../CONTINUOUS_DEVELOPMENT_BOARD.md) 第 14 节；任务规格：[TODO](TODO.md) 第 11 节。

## 1. 这份台账怎么读

每个能力登记五项支持状态。五项是**独立**的：一项能力可以"模型能写、运行时不画"，这正是总体规划第 5.1 节要求分开的原因。

| 状态 | 含义 | 由什么证明 |
| --- | --- | --- |
| 声明 | schema/校验器接受这个写法 | 解析器与 validator 代码 |
| 运行 | 桌面宿主真的执行它 | 渲染/运行时代码 |
| 预览 | 预览与创作反馈真的展示它 | 预览与证据采集代码 |
| AI | 创作工具/Skill 知道并能正确使用它 | 工具 registry、Skill、能力目录 |
| 真机 | 在 Windows 实机签收 | 真机证据（本轮**全部未取证**） |

本机可跑的门：`scripts/verify-windows-syntax.sh`（mingw 交叉 `-fsyntax-only`，0 真实错误）与
`scripts/run-pure-logic-tests.sh`（31 个纯逻辑目标真实编译并运行，全部通过）在建立本台账前各跑过一次。
这不覆盖任何 Windows 专属路径，因此"真机"一列本轮一律为未取证。

## 2. Scene 壁纸能力台账

Scene 组件共 10 类（`src/include/miaodesk/MiaoSceneModel.h:33-44`）。下表"声明/运行(D2D)/运行(D3D11)"分别表示解析接受、D2D 绘制、D3D11 绘制。

| 能力 | 声明 | 运行 D2D | 运行 D3D11 | 预览 | AI 教学 | 缺口与下一步 |
| --- | --- | --- | --- | --- | --- | --- |
| `transform` | 是 | 是 | 是 | 是 | 部分 | 两后端一致；`cornerRadius` 只有 D2D 一份实现（`MiaoSceneD2DRenderer.cpp` 的 `maxCornerRadius` 计算，:657-661） |
| `spriteRenderer` | 是 | 是（多实例） | **仅第一个**（`MiaoSceneD3D11Renderer.cpp` 的 `FindRenderable` 只返回首个 `ComponentKind::SpriteRenderer`，:92-99） | 是 | 是 | D3D11 多精灵、D3D11 `cornerRadius` 缺失 |
| `textRenderer` | 是 | 是（`{{data.path}}` 模板） | **否** | 是 | 部分 | D3D11 无文字；长文本/换行/对齐规则未成文 |
| `videoRenderer` | 是 | **否** | **否** | 否 | 是（被教成"单轨循环"） | 声明即通过校验但零像素；视频只有独立壁纸路径 `VideoWallpaperPlayer.cpp`。WALL-01 前必须给出"拒绝或实现"的明确结论 |
| `material` | 是 | 否 | 否 | 否 | 否 | 材质是顶层资源，本 kind 无数据 |
| `particleSystem` | 是 | 否（分析型粒子除外） | 是（模拟型） | 是 | 部分 | 该 kind 只用于把包路由到 D3D11（`IndependentWallpaperHost.cpp` 的 `CanonicalSceneUsesGpu`，:55-77） |
| `animator` | 是 | 否 | 否 | 否 | 否 | 动画数据在顶层 `animations[]`，该 kind 无数据 |
| `script` | 是 | 否 | 否 | 否 | 是（被教成"禁止"） | 无加载器/执行器；`userAuthored` 解析后从不参与任何判断 |
| `inputBinding` | 是 | 否 | 否 | 否 | 否 | 绑定是顶层 `bindings[]` |
| `custom` | 是 | 否 | 否 | 否 | 否 | 捕获性 kind，无行为 |

其他壁纸能力：

| 能力 | 结论 | 代码入口 | 缺口 |
| --- | --- | --- | --- |
| 关键帧动画 | 已实现（轨道/时间线/触发、4 种缓动、3 种循环） | `MiaoSceneRuntime.cpp:269-467`、`MiaoSceneRuntimeModel.h:195-215` | 无状态机/混合；Skill 未教 |
| 有界响应曲线 | 已实现 8 条 + deadzone | `MiaoSceneRuntimeModel.h:35-48` | Skill 已教，一致 |
| 分析型粒子 | 已实现（Sparkle / CometTrail / PetalFall，双后端） | `MiaoAnalyticParticleField.cpp` | 无风/力/碰撞 |
| 模拟型粒子 | 已实现，**仅 D3D11** | `MiaoParticleRuntime.cpp:43-181` | D2D 无此路径；无重力/吸引子/湍流 |
| 后处理 | 已实现 8 效果、真实多 Pass、Bloom 真分支，**仅 D3D11** | `MiaoPostProcessCompiler.cpp:38-178` | 无折射/色调/水体；**D2D 静默忽略** `postProcesses` |
| 自定义 HLSL | 已实现且**对内容包开放**（D3D11 vs/ps） | `MiaoShaderContract.cpp:8-85`、`MiaoSceneD3D11Renderer.cpp:821-924` | 与 ADV-04"先隔离后开放 AI Shader"冲突；`userAuthored` 不门禁 |
| 音频输入 | 已实现（WASAPI loopback → 5 频段 + 电平 + 节拍边沿） | `MiaoWallpaperAudioTap.cpp`、`MiaoInputBus.h:207-302` | 16 段频谱算得出但从未发布；HLSL `MiaoAudio*` 常量全为 0 |
| 指针输入 | 已实现（归属、归一化、事件边沿） | `IndependentWallpaperHost.cpp:363-423` | D3D11 走私有直读路径，未经归一化 |
| 事件/脉冲 | `input://event/pulse` 无生产者 | `MiaoSceneFrameScheduler.cpp:141` | 声明即通过 |
| 媒体输入 | 无此通道 | — | `input://media/*` 不存在 |
| 多显示器 | 已实现（每显示器一个 host 槽位） | `IndependentWallpaperHost.cpp:505-536` | 真机未验 |
| 画幅适配 | D2D 已实现 authored-canvas cover（按背景图尺寸推导） | `MiaoSceneD2DRenderer.cpp:532-587` | D3D11 是另一套坐标模型；`fitMode` 无渲染器读取 |
| 数据绑定源 | 仅 `Parameter` / `Input` 两种 | `MiaoSceneRuntimeModel.h:19-22` | 无 clock/media 源 kind |
| 3D 空间 | 声明 + 校验 + 序列化，渲染器明确拒绝 | `MiaoSceneD2DRenderer.cpp:386-392`、`MiaoSceneD3D11Renderer.cpp:940-945` | S6 建设项 |

## 3. 组件能力台账

| 能力 | 结论 | 代码入口 | 缺口与下一步 |
| --- | --- | --- | --- |
| 内容包尺寸 | 每个包**一个固定尺寸**（归一化分数），`resize:false` | `MiaoContentDefinitionLoader.cpp:510-581` | 尺寸族、切尺寸保留配置缺失（WPRO-01） |
| 布局系统 | **缺失**，节点只带绝对 `position/scale` | `MiaoSceneModel.h` | 无行列/对齐/间距/溢出规则 |
| 三个内容组件（时钟/天气/待办） | 真实数据 | `assets/widgets/*.mdwidget`、`ContentWidgetHost.cpp:604-648` | 待办仅暴露 4 个槽位（`kPublishedTaskSlots=4`） |
| 时钟刷新 | 对齐分钟边界 | `NativeWidgetHost.cpp:656-664` | 秒针样式没有单独的刷新档 |
| 天气 | 真实网络（Open-Meteo + IP 定位）、缓存 + 90s 重试 | `NativeWeatherService.cpp:217-355` | 无显式 loading 态 |
| **原生 `native:today-tasks` 预设** | **假数据**：硬编码 `L"3"`、`L"1 / 3 完成"` 与三条固定待办，从不读 `TodayTaskStore` | `NativeWidgetPainter.h` 的 `PaintTodayTasks` 内的字面量（:391-418） | 违反"真实数据"硬门；本轮登记为缺陷 D-2 |
| 交互 | 只有拖动；无点击/完成/计时/媒体控制/右键/键盘 | `NativeWidgetHost.cpp:813-858` | WPRO-03 |
| Provider 抽象 | **部分**：三个互不相同的快照结构，无统一接口/时间戳/loading/取消 | `MiaoContentDataBinding.h:16-19`、`NativeWeatherData.h:14-27`、`TodayTaskStore.h:18-25` | WPRO-04 |
| 能力 broker | 已实现，闭集 3 个（`time.*/weather.*/tasks.*`） | `MiaoContentDataBinding.cpp:96-117` | 无撤销/取消；无 media/agenda/timer/focus/photos |
| Action 注册表 | **缺失** | — | WPRO-03 |
| 无障碍 | **缺失**（窗口 `WS_EX_NOACTIVATE`，无焦点/键盘/UIA） | `NativeWidgetHost.cpp:916-921` | WPRO-06 |
| 多实例 | 内容包支持；原生预设按显示器刻意单例 | `DesktopWidgetStore.cpp:134-141`、`WidgetService.cpp:134-135` | — |
| 显示器分配 | 已实现 + 健康遥测 | `WidgetService.cpp:167-180,223-352` | 真机未验 |
| 吸附对齐 | **缺失** | `NativeWidgetHost.cpp:746-762` | 仅新建时有粗粒度避让 |

## 4. 创作（Creator / AI）能力台账

| 能力 | 结论 | 代码入口 | 缺口 |
| --- | --- | --- | --- |
| 8 个创作工具 | 7 个 Executable，`CreatorToolName::ImageGenerate` 显式不可用 | `CreatorToolWorker.cpp` 的 `AvailabilityOf`（:150-171） | 不可用理由是显式的（不是默认分支） |
| 工具归属/阶段门 | 已实现且顺序刻意（归属先于阶段与参数） | `CreatorToolRegistry.cpp:122-218` | — |
| 候选摘要/台账/回执 | 已实现 | `ContentCandidateDigest.cpp`、`ContentCandidateLedger.cpp`、`ContentCandidateReceipt.cpp` | — |
| 有界修复 | 已实现（2 轮 / 10 分钟 / 相同错误即停） | `CreationRepairPlanner.cpp` | — |
| 渲染取证判定 | 已实现（纯逻辑，6 类用例） | `RenderEvidence.cpp:19-124` | — |
| **宿主取证采集** | **有缺陷**：`sample.digest` 恒为空 → 每次都被判"第 0 帧绑定的候选摘要与当前候选不一致" | `src/app/main.cpp:464,494` | 本轮登记为缺陷 D-1，已修 |
| 取证 artifact 落盘 | **未实现**：样本结构有 `byteCount`/`artifactPath` 字段（`RenderEvidence.h:48-49`），宿主采集 `src/app/main.cpp` 的 `CollectEvidence` 从不给它们赋值 | `src/app/main.cpp` 的 `CollectEvidence`（:429-502） | CCA-08 后半 |
| 能力自述 | 只列 8 个工具名，不描述内容能力 | `CreatorToolWorker.cpp:119-138` | CAP-01 |
| Skill 覆盖面 | 覆盖 3/10 组件 kind、0 个资产类型；所述规则逐条与代码相符 | `skills/*/SKILL.md` | 无门禁发现"真实能力从未被教"（CAP-02） |
| 预览 | 创作者与组件库预览用真实 D2D 场景渲染器 | `ContentCreatorDialog.cpp:1123`、`ContentWidgetPreviewRenderer.cpp:156` | 旧 AI 壁纸沙箱路径仍是 CSS 近似（`GeneratedDesktopPreview.cpp:253-269`） |
| 系统提示词 | 创作者会话**没有**专用 systemPrompt，回落聊天提示词；`ExecutableCreatorToolNames()` 无生产调用方 | `PiLaunchProfile.cpp:56-69`、`CreatorToolWorker.cpp:182-191` | CCA-04 声称的机制未接线 |

## 5. DESK-01/03/04/05 核验结论（本轮）

| ID | 结论 | 证据 |
| --- | --- | --- |
| DESK-01 | 三官方壁纸均以 `scene.json` 为入口且能通过 load→deserialize→validate→initialize；正式运行、视觉、多屏与性能仍待真机 | `BuiltinWallpaperPackages.cpp` 测试、`assets/wallpapers/*/manifest.json` |
| DESK-03 | 指针归属/归一化/事件边沿接线存在，且声明即关闭 click-through | `IndependentWallpaperHost.cpp:363-423`、`PointerAttribution.cpp` 测试 |
| DESK-04 | WASAPI 采集→FFT→频段→InputBus 接线存在；设备切换/恢复待真机 | `MiaoWallpaperAudioTap.cpp`、`AudioIngress.cpp`/`InputBusPublisher.cpp` 测试 |
| DESK-05 | Web 音频推帧接线存在（页面侧 shim 与 C++ 信封有对等测试） | `WallpaperWebAudioBridge.js:62-133`、`WebAudioEnvelopeParity.mjs` |
| CCA 旧状态 | 13/15 项为"部分实现"，`CCA-00/CCA-12` 未完成；文档自述与代码一致 | `docs/CONTENT_CREATOR_AGENT_PLAN.md` 逐条与本轮代码核对 |

## 6. 本轮登记缺陷

| ID | 缺陷 | 影响 | 处置 |
| --- | --- | --- | --- |
| D-1 | 取证样本摘要恒为空，`CreatorToolName::PreviewEvidence` 在真机必然失败，却自述可执行 | 创作者拿不到真实渲染证据；证据链断在最后一环 | 本会话修复并补纯逻辑测试 |
| D-2 | `native:today-tasks` 预设显示硬编码假待办 | 用户看到不存在的任务；违反"宣称的数据来源真实可用" | 已登记，WPRO-04 前处理 |
| D-3 | 未知 capability ID 只做字符集校验，`audio.read` 之类能通过并静默无效 | 虚构能力比缺失更难发现 | CAP-01 关闭 |
| D-4 | 声明了但零像素的组件 kind（`videoRenderer`/`material`/`particleSystem`/`animator`/`script`/`inputBinding`/`custom`）能通过全部校验 | 坏包看起来合法 | CAP-01 显式标注 + 校验器拒绝/告警 |
| D-5 | D2D 静默忽略 `postProcesses` 与模拟粒子 | 作者以为生效 | CAP-01 后端矩阵 + 明确降级说明 |

## 7. 交接

- 固定样本、参考机、预算与度量协议：PRO-02/PRO-03，需人工选样与 Windows 实机，未启动。
- 下一步（按依赖可自动推进）：CAP-01 能力目录，直接关闭 D-3/D-4/D-5，并让 Skill/工具/校验共用同一份目录。
- 每轮补齐本表时，必须写证据 SHA 与本机跑了哪些门；不得只改结论不改证据。
