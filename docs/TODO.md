# MiaoDesk 开发 Todo

- 更新：2026-09-27。
- 定位：主产品优化的唯一任务级执行清单；阶段路线见 [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md)。
- 目标：[PRODUCT_VISION.md](PRODUCT_VISION.md) 定义的漂亮、智能桌面；遵守 [DESIGN_BASELINE.md](DESIGN_BASELINE.md)。
- 本轮核对基点：`631cb0718b184e1e6e1052e0a3568d9b44ef16b7`。本轮核实了该 SHA 的远端 CI（见 BASE-02），并核对了仓库文档与实现；**仍未在本轮运行 Windows 产品**，因此一切真机项保持未勾选。
- 本机为 macOS，可跑的是：仓库门（含 mingw 交叉 `-fsyntax-only` 全量语法检查、文档引用三道门）与 `node tests/*.mjs` 源契约测试。这些能证明"语法没坏、契约还在"，不能证明链接通过或行为正确。
- 旧清单完整保存在 [历史快照](history/TODO_SNAPSHOT_2026-09-27.md)，旧 P0/P1/P2/P3/B 编号仅用于追溯。

## 1. 范围与执行规则

本清单覆盖搜索框、动态桌面、普通 AI 对话/内容创作、API 配置和独立 Harness 工作台。**本地 AI 是独立扩展架构**：DGX 部署、模型选型、推理服务器、模型路由和权重分发不阻塞这些任务。AI 流程用现有可用 Provider 验收，同时用可控测试服务覆盖错误分支。

保持现有 Native C++ / Win32 / Direct2D 技术基线；复用内容包、参数和场景运行时。大型 Timeline、Shader Editor、Node Graph、完整 3D 渲染器和 Wallpaper Engine 全功能对齐不进入本轮。

执行约定：

1. 下列任务框初始均为未完成，表示**本次优化/验收尚未闭环**，不表示对应功能不存在。每项的“现状”区分已有实现、待核验和拟新增行为。
2. 先复现或测量，再决定是否改代码。验收已满足时直接补齐证据并关闭任务，不重复实现。
3. 每项开工时登记负责人、目标 SHA 和设备；可用状态为待核验、待设计、开发中、待 Windows 验收、已完成、受外部条件阻塞。
4. 勾选完成必须附实现提交（若有）、适用检查结果、人工验收记录及遗留限制。只完成编码时保持未勾选。
5. 第一轮保护稳定性与可操作性；第二轮完善搜索、视觉和组件；第三轮完善 AI 与工作台。独立的代码核查、设计和测试准备可以提前做，发布收口仍依赖证据。
6. 发布阻塞项包括崩溃、数据丢失、错误应用内容、桌面图标无法操作、主要控件不可达，以及现有视觉/RC 契约规定的缺陷。其他美化项不自动升级成发布阻塞。
7. 使用现有测试与采集工具。只为真实状态转换、竞态、数据边界等补回归，不为单纯文案/间距调整堆叠源码字符串断言。
8. 优化前后必须使用相同场景、设备、内容与配置比较。测试错误、跳过和未执行不能记为通过；缺硬件时推进独立任务，保留真机项未完成。

## 2. 已有基础：继续验收，不重复建设

| 领域 | 当前已经存在的基础 | 本轮重点 |
| --- | --- | --- |
| 桌面宿主 | Shell Host、层级修复、PaintReady、壁纸停用检查、内置组件尺寸约束 | 生命周期、真实视觉与多屏验收 |
| 官方内容 | 三款 `.mdwidget`；MiaoCloud / NeonCity / MysticMoon 以 `scene.json` 运行 | 效果、数据交互与内容管理 |
| 性能 | 性能策略、刷新控制、全进程树采集及回归比较工具 | 参考机数字、热点与资源释放 |
| 内容管理 | 搜索筛选、真实缩略图、主要操作及 AI 创作入口 | 状态语义、操作连续性、包生命周期 |
| 创作预览 | 实时 Scene 预览、暂停/继续、重载、全屏、错误显示、静态回退 | 与桌面效果一致性、错误恢复与应用边界 |
| API 配置 | 聊天/图片配置、凭据存储、模型下拉、窗口滚动 | 真机可用性、连接诊断、配置生效 |
| 发布 | x64 / ARM64 / MSIX 流程、同一 SHA 检查、RC 证据校验 | 当前候选版本与物理设备签收 |

## 3. 执行批次与依赖

| 批次 | 任务 | 交付结果 | 退出条件 |
| --- | --- | --- | --- |
| 准备 | BASE-01～03 | 参考环境、当前构建状态、文档边界 | 后续任务有可复现起点；无法取得的证据明确标记 |
| 第一轮：稳定、可操作、可测量 | STAB-01～03、LAY-01～02、PERF-01～03 | 桌面生命周期记录、DPI 矩阵、性能基线 | 已发现的核心阻塞缺陷关闭，基线可重复 |
| 第二轮：搜索、视觉、组件与内容管理 | SEARCH-01～03、VIS-01～02、WIDGET-01～03、LIB-01～03 | 连贯的日常桌面体验 | 固定查询集、截图/录屏与操作用例通过，无资源回归 |
| 第三轮：AI、配置与工作台 | AI-01～02、CREATE-01～04、API-01～03、HAR-01～02 | 从配置到生成、预览、应用、恢复的完整路径 | 使用可用 Provider 通过端到端任务及错误分支 |
| 发布收口 | REL-01～03 | 同一候选版本的安装、视觉、性能与 CI 证据 | 当前发布范围内的阻塞项关闭，RC 证据验证通过 |

第一组建议实际领取：BASE-01、BASE-02、BASE-03；取得 Windows 参考环境后执行 STAB-01、LAY-01、PERF-01。先记录问题，再按影响范围拆修复提交。

## 4. 准备工作

### BASE-01 建立参考环境与固定验收样本

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有视觉与性能采集器；缺本轮参考机证据。依据 `WINDOWS_VISUAL_ACCEPTANCE.md`、`WINDOWS_PERFORMANCE_BASELINE.md`、`RC_KNOWN_LIMITATIONS.md`。
- **依赖 / 入口**：无；`packaging/windows/collect-visual-acceptance.ps1`、`packaging/windows/collect-performance-baseline.ps1`。
- **待办**：登记 OS、架构、CPU/GPU/内存、驱动、显示器拓扑和缩放；准备三款官方壁纸、三款组件、图片/视频/Web 样本；使用专用搜索测试文件与测试待办，避免采集个人数据。
- **交付 / 验收**：记录候选完整 SHA、包来源、配置及复现步骤；Windows x64 混合 DPI 双屏含竖屏作为 RC 参考环境；另列 ARM64 搜索框视觉验收设备。暂缺设备的场景明确留空，不能由 CI 截图替代。

### BASE-02 核实当前构建与最近 UI 改动

- [x] 完成本项；负责人：本轮；证据：`api-settings-scroll-todo-2026-09-26.md`、`preview-sandbox-todo-2026-09-26.md` 的 Build evidence 段（2026-09-27 核实）。

- **现状 / 依据**：9/26 已实现 API 页面滚动和预览控件；两个专项 TODO 的构建验收已勾选。本轮以 `gh`/公开 API 核实了目标 SHA 的远端 CI。
- **依赖 / 入口**：可独立执行；`.github/workflows/`、`tests/api-settings-scroll.mjs`、`tests/content-creator-modes.mjs`、`src/tests/SceneD2DRenderer.cpp`。
- **待办**：逐项核实目标 SHA 的 Repo Hygiene、Windows x64 Build 和相关打包；核实预览 D2D 测试实际执行；记录 success/failure/skipped/cancelled，修复真实失败。
- **交付 / 验收**：形成 SHA→工作流链接→结论表；更新 `api-settings-scroll-todo-2026-09-26.md`、`preview-sandbox-todo-2026-09-26.md` 的证据。构建通过与真机通过分开记录，正式全链收口由 REL-02 负责。

- **本轮结论**：SHA `9dc2f288` 的 7 个工作流全部 `completed`，逐 job、逐 step 核对后**除 1 步外全部 success**。Repo Hygiene #209、Windows x64 Build #515、x64 Package #288、x64 MSIX #389、ARM64 Package #240、Path Layout Contract #548。
  - 惟一的非 success 是 x64 Build 的 `Upload Content widget lifecycle diagnostics`，条件为 `if: failure()`：被跳过恰好说明前面的生命周期测试通过。已按"skipped ≠ 通过"单独核对条件，未记为失败，也未当成漏跑。
  - 预览 D2D 测试确认真实执行并通过：`Render a textured sprite through the real D2D backend`、`Verify binding response curves`、`Verify scene runtime binding end to end`。
- **遗留限制**：以上只是构建与源码契约通过，**不等于真机通过**。滚动、预览、全屏的设备侧表现仍属 REL-02 / REL-03 与相关第二轮、第三轮任务的验收范围，本机为 macOS，无法关闭。

### BASE-03 校正当前技术文档与实现的边界

- [x] 完成本项（文档契约部分）；负责人：本轮；证据：本节的"本轮改动"清单 + `AI_GENERATED_DESKTOP_SANDBOX.md` 顶部 Corrected 段。

- **现状 / 依据**：旧清单存在多次追加形成的过期状态；`AI_GENERATED_DESKTOP_SANDBOX.md` 仍写 AI 不能生成/预览组件，与当前 Content Creator 不一致。
- **依赖 / 入口**：无；相关契约、`ContentCreatorBridge.cpp`、`ContentCreatorDialog.cpp`、`GeneratedDesktopPreview.cpp`、`DesktopWidgetTools.cpp`。
- **待办**：区分旧壁纸预览工具、现有组件只读工具与新内容包创作流程；明确各自能力、校验与应用边界；核对尺寸保护、官方包迁移、ARM64 流程和性能采集的当前状态。
- **交付 / 验收**：当前契约不再出现互相矛盾的支持范围；保留旧记录可追溯。文档明确“可生成候选组件包”不意味着 AI 可绕过正式 API 修改现有组件状态。

- **本轮改动**（三处同一句过期断言的副本，代码与文档都有）：
  1. `docs/AI_GENERATED_DESKTOP_SANDBOX.md` — 撤回"AI cannot preview, generate, or apply widgets"，改为三张面对照表（Pi 工具面只读 / Content Creator 生成内容包 / 正式 API 才可变现役状态）。
  2. `src/ai/pi/PiNativeToolsExtension.cpp` — 系统提示改为"工具面对组件只读"，并指向用户主动发起的 `AI 制作组件` 创作流；保留"永远不要输出 widget HTML/CSS/JavaScript"与 PREVIEW-FIRST 两条硬规则。
  3. `src/ai/pi/PiRuntime.cpp` — 此处原文自相矛盾（同一段里先说“不要去生成组件”，隔四行又要求“必须生成 `.mdwidget`”），已改为一致表述；同时修掉"写完包后用 `wallpaper_validate_package` 校验"——该走 `WallpaperPackage::Validate`，只认旧版 Web `.mdwall` 的 `entry` 必须为 HTML，用它校验 `.mdwidget` 必然误判。
- **核对结论**：`WidgetService::Update` 对内置 Native preset 的宽高保护真实存在；`desktop_widget_list` / `wallpaper_state_get` 确为只读，且 worker allowlist 以退出码 26 拒绝产品状态变更。
- **遗留限制**：① 该宽高保护此前无任何回归覆盖，已补 `tests/widget-preset-geometry-guard.mjs`；② 官方包迁移与 ARM64 流程只在 CI/文档层核对，未跑真机；③ 性能采集现状依赖参考机，属 BASE-01，未关闭。

## 5. 第一轮：桌面稳定性、布局与性能

### STAB-01 壁纸与组件状态转换回归

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有停用、reload、层级及健康检查；需端到端验收。依据设计基线的壁纸/组件独立性。
- **依赖 / 入口**：BASE-01、BASE-02；`WallpaperService.cpp`、`WidgetService.cpp`、`DesktopControlService.cpp`、`verify-widget-visibility.ps1`。
- **待办**：覆盖首次启动、退出重启、各类壁纸切换、连续启停与 reload；停用壁纸时保留三组件；重复相同操作检查幂等；记录持久化状态与实际窗口是否一致。
- **交付 / 验收**：至少连续 20 次切换/启停，无重复 Surface、错误复活、孤立窗口或配置丢失；桌面未被组件覆盖处仍能点击、框选、右键。只对失败分支补必要回归。

### STAB-02 系统生命周期与多屏恢复

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有 Shell repair；恢复效果待真实环境确认。依据开发路线与 Windows 视觉验收。
- **依赖 / 入口**：BASE-01、STAB-01；`src/desktop/shell/`、`src/desktop/wallpaper/monitor/`。
- **待办**：分别覆盖 Explorer 重启、锁屏解锁、休眠恢复、主屏切换、副屏断开重连、分辨率/方向变化；恢复前后采集视觉证据，记录恢复耗时与每屏分配。
- **交付 / 验收**：每种场景至少重复 3 次；层级、启停和交互符合原状态；缺失显示器上的组件有可恢复的可见位置；无窗口滞留屏外。设备不支持的场景标记未覆盖。

### STAB-03 内容失败的隔离与恢复

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：具备独立宿主与诊断；具体错误路径需核验。依据设计基线的 Wallpaper/Widget 故障隔离。
- **依赖 / 入口**：STAB-01；`NativeWidgetHost.cpp`、`ContentWidgetHost.cpp`、`WallpaperService.cpp`、Web/Video 运行时。
- **待办**：用测试包覆盖缺资产、损坏包、Web 加载失败、视频打不开、渲染失败；验证用户可停用/更换失败内容，诊断能定位包和失败阶段。
- **交付 / 验收**：单份失败内容不阻断其余组件、壁纸或搜索；无无限重启/错误弹窗循环；失败操作不把上次可用配置替换成坏配置。

### LAY-01 统一 DPI 与窗口可达性

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：创作窗口适配与 API 滚动已实现。依据 Native UI、输入法和设计基线。
- **依赖 / 入口**：BASE-01、BASE-02；`src/ui/settings/`、`src/ui/wallpaper/`、`ContentCreatorDialog.cpp`、`ConversationPanel.cpp`。
- **待办**：覆盖 1366×768、1920×1080 和可用高分屏，100%/150%/200% 缩放；测最小窗口、最大化、跨屏与 DPI 改变；统一最小尺寸、滚动范围和弹窗落点规则。
- **交付 / 验收**：主要操作和可编辑字段均能到达，无文字/按钮重叠或屏外弹窗；扩大窗口后滚动偏移正确收敛；拖动滚动条与滚轮时背景和子控件同步。

### LAY-02 键盘、焦点与中文输入一致性

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有 Native 输入与 IME 契约；跨窗口一致性待验证。依据 `WINDOWS_CUSTOM_INPUT_IME.md`。
- **依赖 / 入口**：LAY-01；`SearchWindow.cpp`、对话输入实现、设置与创作窗口。
- **待办**：检查 Tab/Shift+Tab、Enter、Esc、焦点可见性、文本选择和剪贴板；覆盖中文候选框、组合输入、长文本；区分弹窗关闭、全屏退出与取消任务。
- **交付 / 验收**：键盘可完成搜索、配置、预览和应用；IME 确认文字不会误触发送/应用；光标与显示文字对齐；关闭子窗口后焦点回到合理位置。

- **本轮核验（2026-09-27，未勾选：组合输入与候选框需真机 + 微软拼音）**：
  - **文档说 Search 用共享 IME anchor，代码没有。** `docs/WINDOWS_CUSTOM_INPUT_IME.md` §4 把 `src/ui/search/SearchWindow.cpp` 列为 `InputImeAnchor`  Search profile；`grep InputImeAnchor src/ui/search/SearchWindow.cpp` 零命中——连 `#include` 都没有，也没有任何 `ImmSet*` / `WM_IME_*` / `SetCaretPos`。全仓 Search surface 的 IME 处理量为 0。
  - 且 Search 用的是该文档 §2 **明令禁止**的 1×1 proxy：`SearchWindow.cpp:293` 建 EDIT 为 `(kEditLeft, kInputProxyY, 1, 1)`，`:797` 再 `MoveWindow(..., 1, 1, FALSE)` 把它保持住。
  - **而修好它的代码已经写好了，只是没人接。** `InputImeAnchor.h` 里有整套未使用的 Search profile：`EnsureSearchImeGeometry` / `AnchorSearchImeToVisibleCaret` / `RequestSearchImeAnchor` / `SearchImeAnchorBusyScope` / deferred 消息。对齐关系也全部吻合：`kSearchEditControlId = 100` 就是 SearchWindow 的 `kSearchEditId`；父窗口类 `MiaoDesk.Native.SearchWindow` 一致（所以 `IsMiaoDeskSearchEdit` 判得中）；`kSearchEditLeft = 52` 等于 `kEditLeft`，`kSearchEditRight = 594` 等于 `kEditRight`。
  - 头文件里 `kSearchEditTop = 13`、`kSearchEditHeight = 30` 与 SearchWindow 的 `kInputProxyY = 27` 冲突 —— 但 56 高的 bar 里居中 30 的输入行正是 `(56-30)/2 = 13`，所以 **13/30 才是真实输入矩形，27+1×1 是 proxy 位**。两者不是都"对"，是后者从未被替换掉。
  - **为什么本轮没顺手接上**：把 EDIT 从 1×1 撑到 542×30 会盖住自绘输入区，必须先让 EDIT 不绘制（透明/`WM_CTLCOLOR` 一类），再加 `WM_SETFOCUS / WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION / WM_IME_ENDCOMPOSITION / WM_INPUTLANGCHANGE / WM_SIZE` 的路由与 deferred 消息回填。这些改动本机无法目视与输入验证，赌在产品的**唯一主入口**上不合算。建议下一轮在 Windows 上按这个顺序接：先接 `EnsureSearchImeGeometry` 确认自绘不被遮，再接 anchor 与消息路由。
  - **顺手要修的文档**：真接上之后，`WINDOWS_CUSTOM_INPUT_IME.md` §4 才算成立；在那之前该节描述的 Search 行为与现状不符，读它的人会以为中文输入已经是好的。

### PERF-01 建立可比较的全进程性能基线

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：采集和比较工具已存在，缺本轮参考数字。依据 `WINDOWS_PERFORMANCE_BASELINE.md`。
- **依赖 / 入口**：BASE-01；`collect-performance-baseline.ps1`、`PerformanceService.cpp`。
- **待办**：按 desktop-only、wallpaper、widgets-3、ai-idle 四场景，稳定后每场景采样至少 30 秒、重复 3 次；固定壁纸、FPS、组件、分辨率、供电和 Provider；记录完整进程树。
- **交付 / 验收**：保存 CPU、工作集、私有内存、句柄、线程、进程数及可用 GPU 指标的平均/p95/峰值；GPU 缺测写缺测。根据结果登记本机资源预算，禁止凭空宣称低占用。

### PERF-02 按热点优化刷新与播放调度

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有性能策略和 Scene 调度；只处理测得的无效工作。依据开发路线的按需刷新原则。
- **依赖 / 入口**：PERF-01；`WallpaperPerformancePolicy.cpp`、`MiaoSceneFrameScheduler.cpp`、`NativeWidgetHost.cpp`、预览定时器。
- **待办**：测静态内容与未变化组件的重绘次数；检查时钟实际显示粒度、天气/待办数据事件；分开 compositor 重呈现与内容重绘；核实隐藏/暂停预览以及已配置的壁纸节能策略。
- **交付 / 验收**：展示优化前后数据和适用内容；未改变的数据不触发无意义内容重绘；暂停/恢复时间正确；动画无新跳帧、音画不同步或交互延迟。不把时钟的秒级样式强制降成分钟刷新。

### PERF-03 资源生命周期与长时运行

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：缺反复使用和长时运行的对比记录；这是核验任务，不预先认定存在泄漏。
- **依赖 / 入口**：PERF-01、STAB-01；窗口、预览、图片缓存、Pi/Node、WebView2 与 Harness 的创建/销毁路径。
- **待办**：连续 30 次打开关闭管理页/预览/对话/工作台；20 次内容切换；进行至少 2 小时桌面运行；检查退出后的句柄、线程、子进程和临时目录生命周期。
- **交付 / 验收**：区分有界缓存与持续增长；预热后的多轮曲线无未解释的单调增长；临时资源按策略清理，保留的后台进程有明确用途与释放条件。

## 6. 第二轮：搜索、视觉、组件与管理

### SEARCH-01 固定查询集与排序质量

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：应用搜索、goz 文件搜索已接通，缺命中质量数据。依据产品愿景“更快更准”。
- **依赖 / 入口**：BASE-01；`AppSearch.cpp`、`GozSearch.cpp`、`SearchWindow.cpp`。
- **待办**：建立至少 30 条测试查询，覆盖应用全名/简称、中文、大小写、空格、同名文件、文件名与路径；明确现有匹配能力，记录实际前 3 项、遗漏与误命中；据样本调整排序和去重。
- **交付 / 验收**：必需精确匹配样本全部命中；模糊/简称 Top-3 首轮目标 ≥90%，样本和分母固定。拼音等新能力先记录需求与样本，不默认扩大检索范围。

### SEARCH-02 输入响应、异步结果与索引异常

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有异步文件搜索和状态字段；速度、竞态待量化。依据产品愿景与 Native 性能原则。
- **依赖 / 入口**：SEARCH-01、PERF-01；`SearchWindow.cpp`、`GozSearch.cpp`。
- **待办**：区分索引未就绪、搜索中、无结果和查询失败；检查快速输入、清空、旧请求晚返回；测按键到首批结果与最终结果的 p50/p95，冷/热状态分别统计。
- **交付 / 验收**：旧结果不能覆盖新查询，输入不被阻塞；参考机暖态首轮目标为应用首批结果 p95 ≤100ms、已就绪本地文件索引 p95 ≤300ms；首测后可调整目标，但需记录理由，不能把未就绪样本静默剔除。

### SEARCH-03 搜索到 AI 的连续操作

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：AI 入口已存在；需验证文本、焦点和返回路径。依据三个界面的产品定义与搜索视觉契约。
- **依赖 / 入口**：LAY-02、SEARCH-02；`SearchWindow.cpp`、`ConversationPanel.cpp`。
- **待办**：检查 Alt+Space、方向键选中、Enter 打开、Esc 返回；明确进入 AI 时的查询传递与发送时机；退出对话后保留合理的搜索状态；核实语音输入入口。
- **交付 / 验收**：输入不丢失、不重复发送；应用启动与 AI 发送不混淆；连续搜索→对话→返回流程可用键盘完成，仍符合已批准搜索框外观。

### VIS-01 统一玻璃视觉与交互状态

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有批准的玻璃 UI 与搜索框参考。依据 `MIAODESK_GLASS_UI_DESIGN_LANGUAGE.md`、`SEARCH_BAR_VISUAL_SPEC.md`。
- **依赖 / 入口**：LAY-01、PERF-01；搜索框、对话、组件与管理/创作页面的现有样式定义。
- **待办**：逐页核对字体层级、间距、圆角、图标、按钮语义及默认/悬停/焦点/禁用状态；复用现有公共样式；修复可见差异，避免为统一样式重写全部 UI。
- **交付 / 验收**：同类控件含义和状态一致；亮/暗/高细节背景下文字可读；搜索框边缘无白边鼓包、双轮廓或光标错位；按既有契约补 ARM64 实机参考图对照。

### VIS-02 官方壁纸的构图与动效质量

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：三款官方包已走 Scene；需要验证迁移后真实视觉。依据内容框架与产品愿景。
- **依赖 / 入口**：STAB-02、PERF-01；`assets/wallpapers/`、Scene Runtime/渲染器。
- **待办**：按 MiaoCloud / NeonCity / MysticMoon 分别录制动效；检查 16:9、16:10、竖屏裁切及图标区可读性；检查循环接缝、速度、透明叠加、音频/指针效果（仅对声明支持的包）。
- **交付 / 验收**：每包有代表性截图和至少一个完整动效周期的录屏；无资产缺失、突跳或错误拉伸；调整不破坏既有动画保真检查和资源预算。

### WIDGET-01 拖动、位置恢复与交互区域

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有直接拖动与位置持久化、预设尺寸保护。依据设计基线 Widget geometry 与交互契约。
- **依赖 / 入口**：STAB-02、LAY-02；`WidgetService.cpp`、`DesktopWidgetController.cpp`、组件 Host 与实例存储。
- **待办**：核查拖动阈值与点击区域，保证按钮/任务点击不会误拖；检查拖动结束写入、取消拖动和应用重启；确认跨屏坐标及屏幕移除后的恢复；内容组件尺寸遵守 Definition。
- **交付 / 验收**：反复拖动/重启位置正确且可见；内置预设拖动只修改位置，不绕过尺寸规则；不新增移动模式或第二套拖动 Surface。

### WIDGET-02 天气的数据状态与刷新

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：天气服务、数据绑定和组件已有实现；失败状态及刷新体验待核验。
- **依赖 / 入口**：VIS-01、PERF-01；`NativeWeatherService.cpp`、`WeatherGlass.mdwidget`、组件数据发布路径。
- **待办**：覆盖首次加载、断网、超时、地点无效、恢复联网及旧数据；明确更新时间和旧数据提示；检查温度、单位、长地点名和多语言文本布局；避免数据不变仍持续重绘。
- **交付 / 验收**：旧值不会伪装为实时数据，失败不留永久转圈；恢复后自动刷新或提供可用重试；数据/布局更新不影响桌面流畅度。

### WIDGET-03 待办闭环与时钟边界

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：TodayTaskStore、任务编辑器及三个内容包已存在；待真实交互和时间边界验证。
- **依赖 / 入口**：WIDGET-01、VIS-01；`TodayTaskStore.cpp`、`TodayTaskEditorDialog.cpp`、`TodayTaskContentProvider.cpp`、时钟数据绑定。
- **待办**：覆盖任务新增、编辑、完成、删除、空态、长文本和重启持久化；核验多个实例数据关系；检查时钟跨分钟/日期、时区变化与休眠恢复。
- **交付 / 验收**：操作后显示与持久化一致，计数/进度正确；写入失败不显示假成功；时间显示及时且无变化时不过度重绘。

### LIB-01 库页面状态与连续操作

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：筛选、搜索、缩略图和主操作已实现；仍需明确选中/使用中的语义。依据 Settings 管理职责。
- **依赖 / 入口**：LAY-01、VIS-01；`WallpaperLibraryWindowV2.cpp`、`ContentPackageManagerDialog.cpp`、组件管理页面。
- **待办**：区分已安装、当前选中、正在使用及加载失败；切换分类/筛选后保留可解释的选择与滚动位置；无结果时可清除筛选；应用时显示目标显示器和状态反馈。
- **交付 / 验收**：用至少 100 条测试内容核查浏览与缩略图加载；用户能判断“哪张正在用、将改哪块屏幕”；不存在加载造成的明显输入停顿，损坏缩略图有稳定占位。

- **本轮核验（2026-09-27，未勾选：100 条内容浏览与选中/滚动保持需真机）**：
  - **收藏是"能写不能读"**：写侧齐全——`favoriteButton`、右键菜单 `kMenuFavorite`、`:1163` 调 `SetFavorite`、`:1072`/`:1378` 切"收藏/取消收藏"标签、`:870` 渲染收藏态。但 `WallpaperLibrary::Favorites()` 全仓只有声明、定义和 self-test 三处，**没有任何 UI 调用**，壁纸筛选 8 个片里也没有"收藏"。于是用户可以标收藏，却没有任何地方只看收藏。
  - 这与 CREATE-04 记的 `RecentlyUsed()` 是同一形态：派生视图已在 API 里、数据也在维护，只差一个 filter。两者都等同一个前置——筛选条改为可换行或做宽度钳制（现在 `chipX += chipW + S(8)` 不换行）。修好布局后这两个视图各加一片即可，数据侧无需再动。

### LIB-02 包导入、替换与删除的恢复性

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有托管包与生命周期服务。依据 `CONTENT_PACKAGE_LIFECYCLE.md`、`MIAO_CONTENT_PACKAGE_V1.md`。
- **依赖 / 入口**：STAB-03、LIB-01；`DesktopContentPackageLifecycle.cpp`、`src/content/package/`。
- **待办**：覆盖导入后删除原文件、同 ID 升级、内置只读保护、Unicode 路径、损坏包、使用中的内容删除；检查运行实例、库状态与磁盘一致性，失败时保留上一可用版本。
- **交付 / 验收**：包身份来自 manifest ID；原下载路径变化不影响运行；失败不残留半安装状态；删除/替换的后果在操作前清楚，状态最终与实际内容一致。

- **本轮核验（2026-09-27，未勾选：端到端磁盘/运行实例一致性仍需真机）**：
  - "内置只读保护"**已实现**：`MiaoContentPackageUninstall.cpp:149` 直接拒绝 Built-in 包，另有两道归属校验（只接受 UserManaged、必须在托管根内）。
  - "使用中的内容删除"**已实现且已被 CI 覆盖**：被桌面实例引用时卸载被拒（提示"仍被桌面实例引用"），该路径由 `MiaoDeskContentWebReplacementTest` 在 `windows-x64-build.yml` 里真实构建并运行。此前提过的"CI-only 跳过"只是 `run-pure-logic-tests.sh` 的本地行为，不是没跑。
  - "失败不残留半安装状态"**已实现**：安装走 staging → 备份 → rename，失败回滚；卸载以"rename 进 .trash"为逻辑卸载点，物理清理尽力而为，锁文件不会留下半删除态。
  - **新发现缺口（待补）**：Built-in 拒绝这条分支本身**无任何测试**——上面 CI 覆盖的是"被实例引用"和域名不匹配等分支。建议补一条：对 Built-in 包调用 `UninstallContentPackage` 必须失败且包仍在。本机无法编译运行 C++ 测试，未代写。

### LIB-03 参数配置与预览/应用一致性

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有参数模型、组件配置与预览；需检验用户设置真实生效。依据 Content Framework。
- **依赖 / 入口**：LIB-02、LAY-02；`ContentWidgetSettingsDialog.cpp`、`ContentWidgetPreviewRenderer.cpp`、参数校验与实例存储。
- **待办**：检查参数名称、默认值、单位、范围和恢复默认；覆盖取消、保存、重新打开、多实例不同值；对后端不支持的参数给出明确原因。
- **交付 / 验收**：预览和正式实例使用一致的有效参数；取消不改持久状态；非法值无法提交；一个实例的外观修改不意外覆盖其他实例。

- **本轮核验（2026-09-27，未勾选：打开/取消/多实例一致性需真机）**：
  - **修掉两个校验缺口**（都在 `ContentWidgetSettingsDialog.cpp` 的同一段解析里）：
    1. `Int` 分支缺 `ERANGE` 判断。`std::wcstoll` 溢出时是**钳位**而不是报错，且 `end` 仍指向串尾，所以原来唯一的 `*end != L'\0'` 检查照样放行——输入 `99999999999999999999` 会把 `9223372036854775807` 提交进去。`Float` 分支早有等价判断（`!isfinite`），存储解码器也有这条检查，只有这个调用点漏了。已实测确认该行为（`errno==ERANGE` 且 `*end=='\0'`）。
    2. `step` 在 schema 里是真的、每个出厂包都声明、表单还把"step N"当约束显示给用户，但**没有任何地方校验值**，`step:0.01` 上填 `0.313` 会被照常提交。已补 `OffStepMessage()`，按 minimum（无则 0）锚定网格，容差随步进缩放。选**拒绝**而不是悄悄取整——悄悄四舍五入会让人以为存的是自己输的值。加了这条之前先核过：5 个包的 8 个带 step 参数的默认值**全部在网格上**，所以不会把产品自带的默认值判非法。
  - 两个函数的实际行为都不是靠读源码断言的：`OffStepMessage` 是纯 double/字符串逻辑，把**真实字节**抽出来编译执行了 14 个用例（含负数 minimum、step 为 0、无 step 等边界）；`wcstoll` 的钳位行为也单独跑过。新增 `tests/widget-parameter-validation.mjs`，除存在性与覆盖断言外，还钉住那个"出厂默认值必须在网格上"的安全性质——否则以后某个包发了网格外默认值，用户会无法原样保存。
  - **审计查出但未动（留待真机或单独决定）**：
    - 预览与活实例的 DPI/alpha 管线不同：预览硬编码 96 DPI、按主屏推宽高比、`ALPHA_MODE_IGNORE` 且清成不透明底色；活实例用 `GetDpiForWindow` + `ALPHA_MODE_PREMULTIPLIED` + 透明。同参数不同光栅化，150% 多屏下卡片与组件会不一致。
    - 预览的缓存失效只看 `manifest.json` 与 entry，活宿主遍历包里每个文件；只改 `parameters.json` 的原地重装会让桌面 1 秒内刷新而库卡片仍显示旧值（库窗口在弹窗返回后调 `Reset()` 挡住了常规路径，挡不住包管理器那条）。
    - `fields` 只建一次而 `ReloadValues` 会替换 `state.snapshot`（含 definition），弹窗打开期间包变更会让字段提示过期。fail-safe，但用户看到的是过期提示加一句"未知参数"。

## 7. 第三轮：AI、创作、配置与工作台

### AI-01 对话活动状态与结果反馈

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有 Pi 对话与活动契约。依据 `PI_AGENT_ACTIVITY_FEEDBACK.md`、`PI_AGENT_CONVERSATION_UX.md`。
- **依赖 / 入口**：LAY-02、BASE-03；`ConversationPanel.cpp`、相关 `.inc`、`PiRuntime.cpp`。
- **待办**：核查发送、等待服务、执行工具、等待用户、完成、失败和取消的事件映射；工具名使用用户可理解的描述；长等待展示真实状态，结果给出可用入口。
- **交付 / 验收**：可控延迟/失败服务下不出现“已结束仍执行中”或无反馈等待；展示可观察动作，不展示隐藏推理；错误保留输入与已完成结果。

- **本轮核验（2026-09-27，未勾选：事件映射的端到端时序仍需真机与可控故障服务）**：
  - 已修一个真缺陷：`FriendlyToolName` 缺 `desktop_preview_wallpaper` / `desktop_preview_examples` 两条映射，而这两个正是壁纸主线要走的核心工具，导致活动框直接显示 `正在执行：desktop_preview_wallpaper` —— 正是本条验收禁止的"工具名不可理解"。已补中文映射。
  - 新增 `tests/tool-friendly-name-coverage.mjs`：从 `PiRuntime.cpp` 的 allowlist 反解出 agent 可调用的全部 19 个工具，逐个要求必须有可读名。以后往 allowlist 加工具忘记加映射会立刻变红（做过变异测试）。这条测试的价值在于**防再犯**，而不是记录当时那一次。
  - 剩余：可控延迟/故障服务下的时序（"已结束仍执行中"、无反馈等待）、错误时保留输入与已完成结果，需真机 + 可控服务，未关闭。

### AI-02 取消、重试与重复操作保护

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：需核验现有取消链和失败恢复；并非断言全部缺失。依据 Pi runtime 与活动反馈契约。
- **依赖 / 入口**：AI-01；`L3Agent.cpp`、`PiRuntime.cpp`、`NativeToolIsolation.cpp`、对话事件处理。
- **待办**：覆盖生成中取消、工具执行中取消、断网、超时和快速重试；忽略旧请求迟到事件；区分可终止工作与已提交操作，重试不得自动重复提交。
- **交付 / 验收**：取消有明确反馈且不启动后续步骤；无法立即终止的工具状态真实可见；重试保留上下文，不重复生成实例、导入或应用内容；现有桌面保持稳定。

- **本轮核验（2026-09-27，只修了 D1，其余未动）**：
  - **已修 D1（"已结束仍执行中"）**：`PiActivityEvent` 不带 turn id，而 `PiRuntime::Stop()` 只 `request_stop()` 不 join worker——所以取消前刚发出的工具事件仍会被 post 出来，把已清掉的活动卡片重新点亮，用户同时看到"在线"和"正在执行"。generation 计数器只挡住了 delta/done，没挡住活动事件。已在对话面与创作面都给活动事件加"本轮仍在进行"的门，取消/完成后迟到的 `ToolStarted/ToolFinished` 不再复活卡片，迟到的 `resultText` 也不会再把已结束那轮的候选换掉。
  - **审计确认安全的**：重复提交在对话面和创作面都不成立（`busy` 闩在 AskAsync 之前同步置位，Enter 重复和按钮双击都过同一处）；确认卡双击安全；Direct Model 回退通道无工具不可能提交；被取消的 native tool 结果在 `finally { rm(work) }` 里连同 output 一起删掉，宿主不会采纳；preview tool 的 worker allowlist 与实际调用集合一致；`InstallGeneratedPackage` 幂等。
  - **查出但未动的**（都需要动设计或需真机，逐条记在这里免得丢）：
    1. `/retry` 把上一条**原始 prompt 原样重发**进同一个 Pi session（session 复用，agent 内存历史里还有上一轮已提交的 tool 记录），且 `lastPrompt` 只在 `/new` 清。提交后遇传输失败 → 提示可 /retry → 重试会再跑一次 `ppt_create` 生成第二个文件。全仓没有任何"这一轮已经提交过哪些工具"的账本。要满足"重试不重复提交"，需要 per-turn 提交账本或重试前重置 session。
    2. `Stop()` 不终止 Pi 进程；`CleanupProcess()`（唯一 `TerminateProcess`）只在析构和换 provider 时调。 acutely 的是 preview 工具：`CreateWallpaperPreview` 写完沙盒会**阻塞式** `SendMessageTimeoutW` 通知主进程，主进程侧 `SearchPreviewBridgeProc` 无任何 turn 门，会直接建出带"应用/拒绝"按钮的可见沙盒窗。于是取消之后桌面上可能留着一个用户被告知"已停止"的轮次的预览窗，点一下应用就能改桌面。
    3. 创作面**没有任何取消手段**：生成中只有"新对话"和关窗，两者都会通过共享 `gPiRuntime` 反过来取消对话面正在进行的轮次（D4）；发送按钮也没有 Esc/Enter（prompt EDIT 未子类化）。"取消有明确反馈"在这个面上按构造就做不到。
    4. `AskAsync` 里 `worker_ = std::jthread(...)` 的移动赋值会 join 旧 worker，而调用线程就是**UI 线程**；旧 worker 若卡在 `ReadLine` 的 1000ms 轮询或 `WaitForSingleObject(2000)`，整个界面会阻塞最多约 2 秒。
    5. `AskAsync` 对"已在忙"只回一句 `Pi Runtime 正忙`，调用方（创作面）把它当成"本轮生成已完成"提示给用户——请求根本没发出去。

### CREATE-01 创作流程与上一可用结果保留

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：壁纸/组件共用创作界面，已有预设、生成、重新生成与应用入口。依据产品愿景与内容框架。
- **依赖 / 入口**：AI-01、LIB-02；`ContentCreatorDialog.cpp`、`ContentCreatorBridge.cpp`。
- **待办**：明确描述→生成→校验→预览→应用的界面状态与按钮启用条件；核验重新生成失败时能否回看上一结果，缺失时补单次恢复能力；保留描述与用户参数。
- **交付 / 验收**：两种模式使用一致操作规则；生成失败不清掉上次可用候选或更改桌面；不会对未通过校验的结果显示可应用。无需引入完整版本历史系统。

- **本轮核验（2026-09-27，未勾选：界面流程与"保留描述/参数"仍需真机）**：
  - "重新生成失败时能否回看上一结果"**已满足**：`SetGeneratedPackage` 在校验失败时提前 return，不会清空 `generatedPackage`，所以上一版候选与其预览会保留。
  - 但这里藏着一个真缺陷：失败那轮的结算提示只说"未检测到有效内容包路径"，而上一版候选的预览和"应用"按钮仍然有效 —— 用户读完提示再点应用，装上去的是**上一轮**的产物，且无从得知。已修：新增 `generatedPackageIsCurrentRound`，本轮未产出包时提示改为"本轮未产出可用内容包 · 当前预览仍是上一版候选，应用会使用它"。改动 16 行，已在 `tests/content-creator-modes.mjs` 补 4 条契约断言，并做过变异测试（撤掉该分支即变红）。
  - 剩余：完整界面状态机、按钮启用条件、描述与参数的保留仍需真机走查，未关闭。

### CREATE-02 预览控件与资源释放验收

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：播放/暂停、重载、全屏、错误状态与静态回退已实现。依据 `preview-sandbox-todo-2026-09-26.md`。
- **依赖 / 入口**：BASE-02、LAY-01、PERF-01；`ContentCreatorDialog.cpp`、`tests/content-creator-modes.mjs`。
- **待办**：核验暂停后时钟连续性、重载重新读盘、Space/R/Esc、全屏尺寸变化；检查失败后重载可用、静态回退明确标识、隐藏/关闭后的刷新与资源释放。
- **交付 / 验收**：两种创作模式均通过；暂停不偷偷推进预览时间；全屏退出恢复原窗口；失败可恢复，关闭无残留定时刷新。不把源码契约检查当作真实控件验收。

### CREATE-03 预览能力与桌面实际效果对齐

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：创作实时预览使用 D2D，正式内容可能有不同后端；D2D 贴图 tint 有已知限制。依据 Scene/Package 契约与 RC 限制。
- **依赖 / 入口**：CREATE-02、VIS-02、LIB-03；D2D/D3D11 渲染器、包校验与预览桥接。
- **待办**：建立同包/同参数/同时间点的预览与桌面比较样本；覆盖字体、贴图、透明、裁切、动画、数据绑定；无法实时支持的后端效果清楚标记，禁止静默展示不等价结果。
- **交付 / 验收**：支持的能力视觉一致，必要的宿主尺寸差异有解释；静态预览不会被称为实时效果；不为消除提示而强行在本轮实现 3D 或完整后端对齐。

### CREATE-04 显式应用、失败恢复与生成质量

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有应用入口与包校验；端到端质量和恢复需核验。依据 Content Framework、包生命周期及预览/应用隔离原则。
- **依赖 / 入口**：CREATE-01～03、AI-02、LIB-02；创作桥接、校验器与 DesktopControlService。
- **待办**：用固定至少 10 条壁纸、10 条组件描述验证生成→校验→预览→应用；覆盖无效包、用户取消、重复点击、应用失败；设计并补齐必要的“恢复应用前内容”入口，范围限本次应用。
- **交付 / 验收**：分别报告生成成功率、校验通过率、需求符合度、视觉问题及延迟，保留失败样本；预览不修改桌面，只有显式应用提交；失败保留原状态，成功后能恢复原内容。不得用单次成功宣称普遍质量达标。

- **本轮核验（2026-09-27，未勾选：10+10 条端到端与视觉质量需真机）**：
  - **修掉一个会直接废掉"恢复原内容"的数据缺陷**：`WallpaperService::ApplyLibraryItem` 与 `AssignLibraryItemToMonitor` 从不调用 `WallpaperLibrary::MarkUsed`。`WallpaperEngine` 自己持有 library 并在自己的应用路径上打时间戳，但**创作器"应用到桌面"、内容管理器、按显示器分配全走 WallpaperService**——于是这些路径应用的壁纸，`lastUsedUnixSeconds` 永远停在迁移时写入的值上。
  - 为什么这条致命：`WallpaperLibrary::RecentlyUsed()` 已经存在、有 self-test、有公开 API，却**没有任何 UI 调用它**。本来看不出问题，一旦按本条要求补"恢复应用前内容"入口把视图接出来，排序会是一个忽略了大半真实使用记录的陈旧时间戳——视图越像功能，越骗人。已修：两个应用路径都补上 `MarkLibraryItemUsed`，且定位为**簿记**（打不上时间戳绝不能反过来让已成功的应用失败）。
  - 顺手把结构钉住：`ApplyLibraryItem` 改为 switch 之后单一出口，新增"任何成功都不得从 switch 内直接 return"的断言（`tests/library-usage-stamping.mjs`），否则以后有人加分支提前 return，这个 stamping 又会被静默绕过。
  - **未做（有意）**：加"最近使用"筛选片。筛选条是单行 `chipX += chipW + S(8)`、不换行也不做宽度钳制，加第 9 片在窄窗口必然溢出，而本机无法目视验证布局。数据侧已修好，视图侧留给能在 Windows 上验收的一轮。

### API-01 配置页面分组与滚动可用性

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：独立聊天/图片字段及滚动已实现。依据 Settings 设计基线与专项滚动 TODO。
- **依赖 / 入口**：LAY-01、LAY-02、BASE-02；`DesktopAiSettingsPage.cpp`、`tests/api-settings-scroll.mjs`。
- **待办**：核验字段分组、继承关系和简洁帮助文案；检查滚轮、Shift+滚轮、水平滚轮、拖动滚动条和焦点定位；在短窗口确认 Image API Key 与底部动作可达。
- **交付 / 验收**：小窗口和高 DPI 下可完成两类服务配置；无需滚动时滚动条消失；滚动不造成模型下拉/密钥/状态框与背景错位。

### API-02 模型检测与连接失败诊断

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：模型检测下拉已加入；连通与实际可用需区分。依据 Provider 配置职责。
- **依赖 / 入口**：API-01；`DesktopAiSettingsPage.cpp`、API profile 与 Provider 请求路径。
- **待办**：检测结果不覆盖手填模型，刷新保留仍有效选择；区分 DNS/网络、鉴权、接口路径、模型不存在、限流及响应格式错误；轻量探测与实际调用分别给出结论。
- **交付 / 验收**：用户能判断改哪个字段或稍后重试；检测不到列表时仍可手填；模型列表成功不能被显示成聊天/图片生成必然可用；错误展示不泄露 Key。

### API-03 保存、生效与凭据生命周期

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有 Credential Manager、会话签名及配置重载；需验证完整行为。依据 Pi runtime 契约与本地隐私数据约定。
- **依赖 / 入口**：API-02、AI-02；`ApiRuntimeProfile.h`、`PiRuntime.cpp`、`L3Agent.cpp`、配置 UI。
- **待办**：覆盖聊天/图片分别换端点、模型和 Key，清空与恢复继承，取消编辑、保存重启；定义正在生成时切换配置的生效时机；检查错误日志、配置和诊断导出。
- **交付 / 验收**：后续请求使用用户保存的目标；运行中任务不静默切换服务；取消不生效，保存结果明确；Key 不进入明文配置、日志或测试证据。

- **本轮核验（2026-09-27，未勾选：换端点/模型后的真实生效时机仍需真机）**：
  - **修掉两个真 bug（同一根因）**：`/key <key>` 永远失败、`/clear-key` 永远假成功。`ModelCredentialGuard.h` 把 `CredWriteW` 重定向为 `CredWriteGuard`，而后者对退役的 `MiaoDesk/ModelApiKey` 目标直接拒绝（`ERROR_ACCESS_DENIED`）——于是写必失败；但读侧同样被重定向，所以 `LoadApiKey` 是好的，呈现"能读不能写"。`CredDeleteW` 根本没有被重定向，删的是一个没人写的名字，拿到 `ERROR_NOT_FOUND`，调用方把它当"已删除"，真实凭据照旧生效。已改为**重定向到 API 配置中心默认 Profile**，与读侧对称（blob 格式不变，仍是裸 wchar_t，所以三个读取方行为一致）。顺带把顺序不变量写进测试：`ApiRuntimeProfile.h` 必须在 Cred 宏之前展开，否则它自己的退役删除会递归。
  - **核对为安全**：Key 落点只有 Credential Manager；`api-profiles.ini` / `model-settings.json` / Pi `models.json` / Harness `settings.yaml` 全部只存引用与非密元信息；设置页用 `ES_PASSWORD` + 掩码；WinHTTP trace 虽然默认编译进来，但 URL 在 `?`/`#` 处截断、只记 header/body 长度，不含 Key。
  - **新发现缺口（待决策，未动）**：
    1. ~~`BaseUrl` 字段若被粘进带 query 的密钥，会被原样写进纯明文 `PiAgent\models.json` 与 `Harness\DshHome\settings.yaml`，并在配置列表回显；而真正发请求时 `WinHttpCrackUrl` 只取 UrlPath，query 在链路上被丢掉——即"落盘了但没用到"。~~ **已修**：新增 `UrlCarriesSecret()`，`ValidateDraft` 对 `baseUrl` / `imageBaseUrl` 都拒收带 userinfo 或密钥名 query 参数的地址，并提示 Key 该填哪个字段。选**拒绝**而不是静默改写，是因为两条链路行为不一致（Direct Model 丢 query、Harness `JoinApiUrl` 保留），改写会动 Harness。该函数是纯字符串逻辑、不依赖 Windows API，所以直接把真实字节抽出来编译执行了 17 个用例——**这一步抓出了我自己写错的一处**（用"@ 后不能有 /"判断 userinfo，结果任何带路径的正常 URL 都判定失败），已修。回归只由 `tests/settings-url-secret-guard.mjs` 做存在性与覆盖断言，它证明不了解析正确。
    2. 上游错误原文（聊天 300 字节 / 探测 220 字节）会进可见 UI 和 `Desktop\MiaoDesk-Logs\l3-runtime.log`，与 `docs/L3-PI-RUNTIME-CONTRACT.md:128`"禁止记录 API Key/Bearer token"的规定相冲。代码不会把自己的 Key 放进去，但那是第三方文本，未被过滤。
    3. `tests/image-provider-probe.mjs` 把解析出的 Key 打到 stdout；当前夹具是占位符所以无害，但机制存在。
    - 另有一个无害发现：`MIAODESK_PI_CREDENTIAL_GUARD` 宏（CMake 与语法门里都有）在 `src/` 中无人引用，属死配置。

### HAR-01 工作台启动、重连与配置

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：独立宿主、随包运行时与配置桥接已存在。依据产品愿景与 Harness 职责。
- **依赖 / 入口**：API-03；`src/harness/`。
- **待办**：覆盖首次打开、冷启动、重复打开、服务超时/退出、端口占用与重新连接；给启动过程和失败以可理解反馈；明确与普通 AI 对话的入口用途。
- **交付 / 验收**：不会重复启动服务或留下永久空白页；失败有可行重试；配置一致，用户未打开时不进入桌面每帧路径。

### HAR-02 工作台关闭与桌面隔离

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有独立进程结构；关闭窗口后的后台行为和资源仍需核实。
- **依赖 / 入口**：HAR-01、PERF-03；`HarnessHost.cpp`、`HarnessProcessManager.cpp`。
- **待办**：明确关闭窗口、退出整个产品、任务运行中关闭的不同语义；验证必要会话保留与无用进程释放；覆盖工作台失败时继续搜索、拖动组件、切换壁纸。
- **交付 / 验收**：后台行为与界面提示一致；重复开关不持续增加资源；工作台故障不拖垮桌面与搜索。

## 8. 发布收口与持续回归

### REL-01 安装、升级与退出卸载

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有 installer 与 moved-install smoke，仍需实际用户环境签收。依据正式打包链和 RC 限制。
- **依赖 / 入口**：BASE-02、STAB-01；`packaging/windows/`。
- **待办**：在干净 x64 Windows 环境安装、首次运行、重启、升级、退出和卸载；覆盖 Unicode/带空格路径、登录启动设置及既有移动安装流程；按现有策略核对用户数据保留。
- **交付 / 验收**：不依赖系统 Node/npm、源码目录或开发机配置；三正式 EXE 与所需资源完整；升级不丢配置与内容，卸载无意外残留产品进程。不为测试随意删除用户内容或凭据。

- **本轮核验（2026-09-27，未勾选：需干净 x64 环境实装）**：
  - **三个正式目标里有一个会永久丢掉文件搜索，且未登记为已知限制。** 文件搜索走 client/server：`goz.exe` 只查询，真正读 NTFS MFT/USN 索引的 `gozd.exe` 必须以**提权的 Windows 服务**运行（`packaging/windows/installer.nsi:42` 的注释就是这条）。EXE 安装器做了：`installer.nsi:106` 调 `gozd.exe install`，`:110` 失败即 `Abort`。**MSIX 工作流完全没有这一动作**——`grep gozd|Goz|service .github/workflows/package-windows-x64-msix.yml` 零命中，而它确实通过 `stage.ps1:42-43` 把 `Goz\goz.exe` 和 `Goz\gozd.exe` 都放进去了。二进制在、服务没装，所以 MSIX 装完后 `GozSearch::Available()` 一路为 false。
  - **后果不是白屏，是静默少一个主功能**：搜索框的三件事变成两件。UI 侧降级是诚实且做得好的——`SearchWindow.cpp:907` 会给一行"文件搜索未连接 / 当前仍可搜索应用；按 Enter 可交给妙喵 AI"，不会假装有结果。所以问题不在欺骗用户，在于**搜索框 headline 能力在 MSIX 上等于没有，而 `RC_KNOWN_LIMITATIONS.md` 里只字未提**。
  - **待确认的平台问题（本机无法回答）**：MSIX 在 Win11 上可以带 full-trust service（`desktop6:Service` 一类扩展），所以"MSIX 装不了服务"未必成立。需要分清是**有意不为**（MSIX 定位为受限目标）还是**漏做**。两种结论的下一步完全不同：有意则补进 `RC_KNOWN_LIMITATIONS.md` 并说明不进 RC 范围；漏做则补安装步骤。
  - 建议：在补之前，MSIX 这一目标不应被视为"三个 headline 能力齐全"来验收。

### REL-02 候选 SHA 的正式流水线闭环

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有同 SHA 发布检查，不代表当前提交已通过。依据 `RC_KNOWN_LIMITATIONS.md`。
- **依赖 / 入口**：候选范围内修复合入后；`scripts/verify-rc-ci.mjs`、`.github/workflows/rc-same-sha-gate.yml`。
- **待办**：冻结候选完整 SHA；检查 x64 Build、x64 Package、x64 MSIX、Repo Hygiene、ARM64 Package；保留 run 链接与产物身份，缺失的工作流按正式流程运行。
- **交付 / 验收**：五项均为该 SHA 的 success；不同提交的通过结果不能拼接，skipped/cancelled 不算通过。先审定发布范围，不把每个非阻塞美化项都强制绑进 RC。

### REL-03 真实视觉、性能与版本签收

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：已有证据结构与校验器；尚待物理设备结果。依据视觉/性能契约及 RC 规则。
- **依赖 / 入口**：REL-01、REL-02，以及发布范围内相关任务；`verify-rc-evidence.ps1`、`rc-manual-signoff.template.json`。
- **待办**：按同一参考机收集 initial、explorer-restart、sleep-resume 截图/JSON 和四性能场景；人工检查多屏 DPI、竖屏、alpha、层级、交互与图标可操作性；绑定完整候选 SHA。
- **交付 / 验收**：证据校验通过、人工检查逐项真实签收、已发现阻塞问题关闭；视觉采集器通过不能替代人眼判定。全部满足后才提升版本/标记 RC 完成，已知限制写入发行说明。

## 9. 执行与证据模板

每项任务开始时复制以下记录到对应 Issue/PR 或任务日志，完成后在本清单关联记录。负责人和日期由领取任务时填写，不预填虚假承诺。

```text
任务 ID / 标题：
负责人 / 状态：
依据与目标行为：
复现步骤 / 输入样本：
依赖 / 硬件条件：
候选 SHA / 包来源 / 设备与配置：
实施改动（无需改代码则说明）：
自动检查：命令 / 结果 / 链接
Windows 人工验收：步骤 / 结果 / 截图或录屏位置
性能前后对比（相关时）：
尚未覆盖 / 已知限制：
完成日期 / 实现提交或 PR：
```

测试证据放在机器本地或受控制品存储；仓库只记录脱敏摘要和可访问的引用。遵守 `LOCAL_PRIVATE_DATA.md`，不要提交凭据、私人桌面截图、个人搜索路径或机器私有配置。

### 已有验证入口

在仓库根目录执行与改动相关的检查；以下命令是可复用入口，不表示每个任务都要跑全部检查。

```sh
node tests/api-settings-scroll.mjs
node tests/content-creator-modes.mjs
bash scripts/verify-doc-code-citations.sh
bash scripts/verify-doc-symbols-exist.sh
```

Windows 视觉/性能示例（每次先手动将产品切到对应场景；`-Scenario` 仅给测量命名，不会替你启停功能）：

```powershell
powershell -ExecutionPolicy Bypass -File packaging/windows/collect-visual-acceptance.ps1 -OutputDirectory C:/MiaoDesk-RC-Evidence/visual/initial
powershell -ExecutionPolicy Bypass -File packaging/windows/collect-performance-baseline.ps1 -Scenario desktop-only -DurationSeconds 30 -OutputDirectory C:/MiaoDesk-RC-Evidence/performance
```

按同一方式补 wallpaper、widgets-3、ai-idle 三场景；多轮开发测量使用不同目录避免覆盖，最终 RC 目录只装选定候选版本的完整结果。Explorer 重启与休眠恢复后的视觉记录分别放入约定目录。

```powershell
powershell -ExecutionPolicy Bypass -File packaging/windows/verify-rc-evidence.ps1 -EvidenceRoot C:/MiaoDesk-RC-Evidence
```

`manual-signoff.json` 必须在人工操作后填写真实结果。完整结构和同机/SHA 约束见 [RC_KNOWN_LIMITATIONS.md](RC_KNOWN_LIMITATIONS.md)。

## 10. 独立扩展与本轮边界

本地 AI 的架构与部署继续在 [LOCAL_AI_ARCHITECTURE.md](LOCAL_AI_ARCHITECTURE.md)、[LOCAL_AI_DEPLOYMENT.md](LOCAL_AI_DEPLOYMENT.md) 维护，旧任务过程见历史快照。本轮不承诺它们的部署完成时间，也不以模型 A/B、DGX 可用性或本地图像服务作为主产品任务的依赖。

通用 Provider 兼容、API 凭据、对话状态、图片请求、内容生成与应用属于主产品，仍在上述任务验收范围内。若某问题只在特定本地服务实现上出现，先定位接口责任，再登记到对应架构，避免混淆两条工作线。

本轮不新建大型编辑器、通用脚本解释器、内容市场或完整 3D 引擎；已有技术契约保留，不自动转换为当前待开发任务。与本轮体验/可靠性无关的历史技术债继续在历史记录中追溯，实际触及时再建立有验收标准的新任务。
