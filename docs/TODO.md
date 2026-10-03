# MiaoDesk 详细验收与问题库

> 当前执行顺序、优先级与自动推进资格统一看 [CONTINUOUS_DEVELOPMENT_BOARD.md](CONTINUOUS_DEVELOPMENT_BOARD.md)。本文件继续保留细粒度验收、诊断、真机条件和历史证据，不再作为“下一项做什么”的唯一队列。


- 更新：2026-10-03；第 1～10 节保留基础版详细验收，第 11 节扩展专业版任务。
- 定位：任务规格与验收证据库；状态和顺序唯一来源为 [CONTINUOUS_DEVELOPMENT_BOARD.md](CONTINUOUS_DEVELOPMENT_BOARD.md)，阶段路线见 [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md)。
- 目标：[PRODUCT_VISION.md](PRODUCT_VISION.md) 定义的漂亮、智能桌面；遵守 [DESIGN_BASELINE.md](DESIGN_BASELINE.md)。
- 历史基础版核对基点（2026-09-27，非当前 HEAD）：`631cb0718b184e1e6e1052e0a3568d9b44ef16b7`。本轮核实了该 SHA 的远端 CI（见 BASE-02），并核对了仓库文档与实现；**仍未在本轮运行 Windows 产品**，因此一切真机项保持未勾选。
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
  - **本条结论已于当日自我作废——我曾判错一次，记录在此以免再犯。** 下面是初稿写下的错误判断："Search 用共享 IME anchor 的代码不存在"、"`InputImeAnchor.h` 里整套 Search profile 没人接"、"§4 与现状不符，读它的人会以为中文输入已经是好的"。**这些全是错的。**
  - **错在哪、真实情况是什么**：机制不在 `SearchWindow.cpp` 里。`InputImeAnchorBridge` 在静态初始化阶段就对本线程装好了 `WH_CALLWNDPROC` + `WH_CALLWNDPROCRET`，而 `SearchWindow.h` include 了 `InputImeAnchor.h`——MiaoDesk.exe 的 UI 线程一直带着它。`HandleSearchEditMessageAfter` 对 `WM_SETFOCUS / WM_KEYUP / WM_CHAR / WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION / WM_IME_ENDCOMPOSITION / WM_INPUTLANGCHANGE` 一律 `HideCaret` + `RequestSearchImeAnchor`；`WM_WINDOWPOSCHANGED` 还会先 `EnsureSearchImeGeometry` 再按条件锚定。**所以 `WINDOWS_CUSTOM_INPUT_IME.md` §4 是对的，搜索框的中文组合/候选定位本来就接好了，该节不需要改。**
  - **我犯错的形状（比结论本身重要）**：只在 `SearchWindow.cpp` 里 grep `ImmSet*` / `WM_IME_*` / `SetCaretPos`，零命中就断言"全仓 Search surface 的 IME 处理量为 0"。我甚至**根本没有**跑过"全仓"范围的 grep——被 grep 的文件只有一个。当实现方式是线程钩子、宏或模板基类时，"调用方文件里没有符号"这个推断完全不成立。这也正是同一轮里我在 `WallpaperLibraryWindowV2` / `ModelCredentialGuard` 上躲过的那类坑，却在 Search 上踩了。
  - 我已经把这条假限制写进了 `docs/RC_KNOWN_LIMITATIONS.md`（现已删除）。把没发生的限制记成"已知"，和漏记真限制一样会误导验收——RC 文档的职责就是把产品缺陷和"待签字的证据"分开，两者混起来就失效了。
  - **这条里唯一真实的部分**：§2 禁止的 1×1 proxy 确实存在（`SearchWindow.cpp:293` 创建处仍是 `(kEditLeft, kInputProxyY, 1, 1)`），且 `WM_SIZE` 里那条本来还在反复把它塞回去——那一半已修，见下。因为它被钩子在首个 `WM_WINDOWPOSCHANGED` 时纠正回真实矩形，所以是瞬时的，不是持续故障；创建处为何不动，理由见下。

- **本轮已修（2026-09-27，八项；后六项其实不小：整套"壁纸自动化"、整个 AI 创作窗、待办编辑窗、包管理器与 Skill 浏览窗此前键盘上都用不了（Tab 无效或 Esc 是死的）；自绘控件走到了也看不见焦点；而 API 配置页的 Tab 序与版面序是两套，一页来回折返三次）**：
  1. **LAY-2-1 的 WM_SIZE 那条已修**：`SearchWindow` 的 `WM_SIZE` 里 `MoveWindow(edit_, kEditLeft, kInputProxyY, 1, 1, FALSE)` 换成 `input_ime_detail::EnsureSearchImeGeometry(edit_)`。原写法不只是违反 §2——它自己打自己：`MoveWindow` 会发 `WM_WINDOWPOSCHANGED`，而 `WH_CALLWNDPROCRET` 钩子对每个 `WM_WINDOWPOSCHANGED` 都会用同一个 `EnsureSearchImeGeometry` 把真实矩形装回去，于是每次展开/收起和每次按键触发的 resize 都白抖一轮，中间还夹着一个候选框锚在 1×1 上的窗口。
  - **未动创建处**：创建时该 EDIT 仍用 `kInputProxyY` + 1×1 起步（`SearchWindow.cpp:293`，同样被钩子纠正）。那是初始化路径，改它无法验证自绘是否被遮，留到能上真机那轮跟 IME 一起收口。
  2. **LAY-2-3 已修**：组件设置对话框的弹窗循环本来就调 `IsDialogMessageW`，而它按 `VK_ESCAPE` 找 id 为 `IDCANCEL` 的控件、找不到就 beep。原对话框只有 kCloseId，所以 Esc 什么都做不了（另外还得靠 Alt+F4）。已加 `case IDCANCEL:` 复用同一个关闭动作。
  3. **LAY-2-4 已修：自动化窗与规则窗此前一个字段都到不了，Esc 也关不掉。** `WallpaperAutomationWindow`（Profile / Playlist / Schedule 三组 + 7 个"星期"复选）和内嵌的 `WallpaperApplicationRulesWindow`（EXE、触发、动作、优先级）都是 `WS_OVERLAPPED*` + 满窗 `WS_TABSTOP` 控件，但它们不是 `WallpaperEngine::Run()` 服务的那一个窗口——那个泵只把 `IsDialogMessageW` 给了库窗口，所以这两个窗的 Tab 毫无作用，纯键盘用户在**两组设置**里都进不去。而它们连 Esc 都没处理过（无 `WM_KEYDOWN`、无 `IDCANCEL`），关窗只剩"关闭"按钮和 Alt+F4。
  - 修法：泵改成遍历三个界面（库 + 自动化 + 规则），各自用 `IsWindow` 兜住再交给对话框管理器；给两个窗补 `VK_ESCAPE` 与 `IDCANCEL` 两条命令路径。`WallpaperAutomationWindow` 还加了 `RulesWindow()` 供泵使用。
  - **为什么两个窗都要补 `IDCANCEL`**：`IsDialogMessageW` 对 Esc 只会投递 `WM_COMMAND / IDCANCEL`（不同于它在没有该控件时什么都不做），所以 Win32 侧的两条入口是 `WM_KEYDOWN` 和 `IDCANCEL`；两条都接才不吃亏——前者不依赖对话框管理器是否派发，后者兼容任何将来改成真对话框的写法。
  - **顺带修掉的是关窗丢键盘**：这两个窗都是"关窗即隐藏"（要留状态给下次打开），而刚被点过的"关闭"按钮正持有焦点——隐藏持有焦点的窗口会让 Windows 把焦点交给 Z 序里的下一个窗口，经常就是桌面，用户下一次按键直接进了别的应用程序，且毫无提示。新增 `src/include/miaodesk/SurfaceKeyboardFocus.h`：`HideSurface` 先记 `GetFocus() == surface`，隐藏后**仅在原本持有焦点时**把键盘交还打开我们的那个界面，并优先落到它第一个可见可用的 Tab 停靠点（直接给顶层窗口焦点只是让键盘停在框架上，Tab 无处可去）。
  - **LAY-2-12 已修（同一天，同一个窗）：六个动作键点了没反应，一个字都不说。** 这个窗**全文件没有一个 `EnableWindow`**，每个按钮从画出来那一刻就是活的；所以"列表里没有东西"不是点击到不了的状态，而是**这个窗在自动化库为空时的默认状态**——也就是每个新装用户的第一次打开。可下面六个 handler 全都是在"取不到选中项"时直接 `return;`：`ApplySelectedProfile`（`if (!id || !applyDecision) return;`）、`DeleteSelectedProfile`、`DeletePlaylist`、`DeleteSchedule`（都是 `if (!id) return;`）、`AddPlaylistEntry`、`RemovePlaylistEntry`。在空的 Profile 列表上点"应用"，和在窗口空白处点一下完全无法区分。
    - 修法：六处各给状态行 + `MB_ICONERROR`。文案**点名是哪个列表**（Profile / Playlist / Schedule / 左侧壁纸库 / 右侧条目），因为这一个窗里有三个下拉和两个列表框，只说"没有选中"等于没说。
    - 之所以没有给这些按钮加 `EnableWindow` 门：门要跟着每次 `RebuildProfiles/RebuildPlaylists/RebuildSchedules` 一起刷新，漏一处就是同一个 bug；而空状态下的即时反馈顺带告诉用户"这里现在是空的、该先建一个"，门只会让按钮看起来是坏的。
    - 测试 `tests/automation-actions-report-empty-selection.mjs` 把"这个窗没有 `EnableWindow`"本身钉成断言：哪天加了门，文件会明确要求重新分析而不是让人把这条断言删掉了事（10 个变异全红，含逐个把六个守卫退回裸 `return;`）。判定逻辑与库窗那条 `every-library-action-talks-back.mjs` 同源，见 LIB-01 的记录。
  - **LAY-2-13 已修（同一天，同一族缺陷的第 11～13 例）：另外三个界面上还有七处"点了没反应，一个字都不说"。** 这次是**派主动作**去全仓扫出来的，而不是逐个文件读——七个里有六个是我自己前两轮漏掉的，其中两个连我写的测试都漏了。逐个记下来，因为每一例的"为什么守卫其实可达"都不一样，而它们都是同一个错误推理：旁边有个相关的门，就以为守卫不可达。
    1. `ContentCreatorDialog::SendPrompt` 空 prompt。**生成**按钮**根本不可能是 disabled**——`SetBusy` 在两个分支上都 `EnableWindow(send, TRUE)`，因为它要兼当"停止"（禁用掉这个面在生成中就完全没有取消手段了）。而 prompt 框只有 cue banner，所以"还没打字就点生成"是第一个动作。原来 `if (text.empty()) return;` 什么都不写——这个文件里**一个 `SetStatus` / `MessageBeep` 都没有**，唯一的发声面是 `resultNote`。已改为写 note + `SetFocus(prompt)`，并按组件/壁纸分别说"先描述你想做的**组件/壁纸**"。
    2. 同文件 `ToggleFullscreenPreview`：预览pane 点了没反应。**全屏按钮**的门是 `(previewLive || previewBitmap)`，而 pane 的门只是"`generatedPackage` 非空"——两者不一致，而 `SetGeneratedPackage` 完全可以带着 `· 预览加载失败` / `· 无预览资源` 成功发布。于是 pane 可点、两个预览标志都为假、点击落空。已改为把原因写进 note（有 `previewRenderError` 就连错误一起给）。
    3. 同文件：**重新生成**在**回合进行中**被强制点亮。`SetGeneratedPackage` 里 `UpdatePreviewChrome();` 紧接着一句无条件的 `EnableWindow(preview, TRUE);`——而 `UpdatePreviewChrome` 刚刚才算好并应用了真正的条件 `!lastUserPrompt.empty() && !busy`。偏偏 `SetGeneratedPackage` 会由 activity 事件触发，而 activity 事件**可以在回合中间到**。于是从"某个 activity 事件带了包路径"到"本回合结束消息"这段窗口里，重新生成是亮的而 `busy` 为真，点击命中 `Regenerate()` 的 `if (busy || ...) return;`，无声。已删掉那句覆盖（`SetBusy(false)` 自己会再跑一次 `UpdatePreviewChrome`，所以覆盖本来什么都没买到）。
    4. `WallpaperApplicationRulesWindow::DeleteRule`：`if (selectedRuleId.empty()) return;`。这个窗口同样**没有任何 `EnableWindow`**，而 `NewRule()` 的第一句就是 `selectedRuleId.clear()`——所以"新建 → 删除"是一条直接的未拦路径，空规则表的机器上打开窗口也是。同一个文件里 `SaveRule` 对"EXE 没填"是有状态行的，只有 删除 不说。已补。
    5-7. `TodayTaskEditorDialog` 的 `EditSelected` / `ToggleSelected` / `DeleteSelected`：三个按钮**没有一个被 `EnableWindow` 管过**，而且**没有 `LVN_ITEMCHANGED` 处理**——所以点列表下方空白处取消选中，按钮依旧是亮的，然后点击被丢弃。`Save()` 对每个失败都写状态，只有这三条不写。各补状态行 + beep，并点名是哪个按钮（编辑 / 完成恢复 / 删除）。
    - 顺手修掉**我自己那条不变量里的一个假阴性**：`automation-actions-report-empty-selection.mjs` 的"这个分支有没有说话"判断原来读的是守卫**之后** 200 个字符,于是**下一条分支**里的 `SetStatus`（失败路径的）能让一个什么都没说的守卫通过。上面第 5-7 例从来没有被它抓到过，也是同一原因。改成真正抽出**守卫自己那个分支**（还要区分 `if (c) return;` 和 `if (c) { ... }`：无花括号时向后找第一个 `{` 会落到后面某个语句的块上，正是要问的那个反馈）。修好之后它立刻抓出了 `ActivatePlaylist` / `NextPlaylist`——这两条也在本族里（播放列表下拉为空时点"设为默认"/"下一张"，同样无声），一起补了。
    - 测试 `tests/ungated-action-surfaces-report.mjs` 覆盖以上全部，6 个变异全红。它**不能**覆盖会话面发送键的空输入（那是第四例同类），原因写在文件注释里：那一个是设计取舍（提示层与面板职责划分），不是漏掉的反馈，不该由不变量替产品做决定。
  - 未验证项：这条与本项其他条目一样，Tab 序、下拉框展开时 Esc 只收列表不关窗、以及"关闭后焦点回到设置中心"都需要真机走查。测试 `automation-window-keyboard-conformance.mjs` 已用 13 个变异逐个验红。

  4. **LAY-2-5 已修：AI 创作窗此前键盘上也完全不可用。** 它是 modeless，创建它的 `SearchWindow` 与派发它消息的 `SearchWindow::RunMessageLoop` 在同进程同线程但**不同编译单元**——所以那条泵只有 `TranslateMessage / DispatchMessage`，整个创作窗（prompt、5 个预设、转写、Skill 列表、应用按钮，全 `WS_TABSTOP`）Tab 一概无效。这条此前被记在"仍未修"里，理由只写了"同理，没有 `IsDialogMessageW`"。
  - 修法：`ContentCreatorDialog.cpp` 增 `g_openCreatorWindows`（创建后 push，`WM_DESTROY` 里、`delete state` **之前** erase），以 `creator::DialogManagedCreatorWindows()` 暴露给泵；`SearchWindow::RunMessageLoop` 改为遍历这些 HWND、各自 `IsWindow` 兜住后交给对话框管理器。
  - **关键取舍：全屏预览窗被排除在对话管理器之外。** 该模式自己吃掉所有按键（Esc 退出、空格播放/暂停、R 重载）。`IsDialogMessageW` 会把 `VK_ESCAPE` 吞掉并投递 `WM_COMMAND / IDCANCEL`，而创作窗不处理 `IDCANCEL`——顺手接上对话框管理器会让**全屏预览的 Esc 静默失效**。过滤器按 `previewFullscreenActive` 排除，测试钉住这条。
  - **搜索框仍然故意不加**，但这次把边界写准了：不再是"这个文件里不能出现 `IsDialogMessageW`"（创作窗就在同一条泵里），而是"这条泵绝不把 `hwnd_` / `edit_` 交出去"。测试按这个口径改。
  - 未验证项：Tab 序、Enter 落进行（prompt 是 `ES_WANTRETURN` 多行）、全屏预览下 Esc/空格/R 仍需真机。测试 `creator-window-keyboard-conformance.mjs` 用 13 个变异验红。
  5. **LAY-2-6 已修：待办编辑窗的 Esc 是死的（同一个文件里下一层却是好的）。** `TodayTaskEditorDialog.cpp` 有两个嵌套泵、都调 `IsDialogMessageW`，所以 Tab 两边都通。但**条目对话框**（新增/编辑待办）的"取消"按钮 id 就是 `IDCANCEL`，Esc 能用；而**承载它的编辑窗**（列表 + 新增/编辑/完成/恢复/删除/关闭）一个 `IDCANCEL` 都没有——对话框管理器按 Esc 时会去找这个 id，找不到就把按键丢掉。同一个文件、只差一层，一层认 Esc、一层不认。
  - 入口：组件设置对话框的"编辑今日待办"，即调用点 `ContentWidgetSettingsDialog.cpp:365` 上的 `ShowTodayTaskEditorDialog`。
  - 顺带核对过、结论是**不要动**的两处（每处都记在这里，免得下一轮再重新判断一遍）：两个嵌套泵里的 `if (result == 0) PostQuitMessage(...)` 是对的——嵌套泵若私吞 WM_QUIT，外层循环会在进程已被要求退出后继续跑，所以 quit 必须重新投递；`EnableWindow(owner, FALSE/TRUE)` 在每条退出路径上都配对，包括 quit 路径，所以编辑窗消失后主窗不会被永久禁用。
  - 未验证项：真机按 Esc 关闭编辑窗后，焦点应落在组件设置窗上——那条路径本来就有（`EnableWindow(owner, TRUE)` + `SetForegroundWindow(owner)`）。测试 `today-task-editor-escape.mjs` 用 9 个变异验红。
  6. **LAY-2-7 已修：包管理器与 Skill 浏览器的 Esc 也是死的。** 这两个窗各有自己的嵌套泵、都调 `IsDialogMessageW`，所以 Tab 通，但都没处理 `IDCANCEL`——对话框管理器按 Esc 会去找这个 id，找不到就把按键丢掉。两处都已补上与"关闭"按钮、`WM_CLOSE` 完全相同的 `DestroyWindow(hwnd)`。
  - **LAY-2-11 已修：整条 NavigateCallback 是死代码，而它看起来是活的。** 我本来要给"库页面 AI 导航是个死路"做跨进程修复，查下去发现**它根本不是死路**：`SetPage(Page::AI)` 当场就在库里显示 API 配置页。真正的问题是那条回调路径从头到尾没有执行过——`WallpaperLibraryWindowV2` 把 `navigateCallback` 存下来就再没用过，`SectionForNav` 定义了也没人调用；而 `WallpaperEngine` 老老实实给它传了一个带 Playlists / Displays / Performance / AI 四个分支的 lambda，其中 AI 分支还是个 MessageBox，告诉用户"AI 模型配置位于 MiaoDesk 设置中心"。
    - **为什么这条比"有个回调没用到"严重**：它从外部完全看不出是死的。我读了引擎那个分支，就据此断定 AI 导航是个"有指针没路径"的缺陷，并设计了一整套跨进程方案去修一个不会发生的行为。已整条删除（回调 typedef、Show 参数、成员与赋值、`SectionForNav`、`WallpaperSettingsSection` 枚举、引擎那个 lambda），并把 `tests/no-dead-ui-callbacks.mjs` 作为不变量留下：扫 UI 实现里每个 `std::function` 成员，要求它在本 TU 内真的被调用过；同时禁止那个 typedef/枚举/Mapper 回来。
    - 顺手又删一个同族的：`ActivityCard::terminal`——每次工具开始都被写成 false，没有任何代码读它，"卡片是否会结束"这件事实际由 `ClearActivityCard()`（整个卡片重置）负责。留着一个没人读的状态字段，会让读代码的人以为卡片有"终态"这个属性。已删，并在同一测试里禁止它回来。
  - 那个不变量本身也修了一处：最初按"文件"扫，而对话面板是 6 个 `.inc` 被一个 `.cpp` include 的——`.inc` 里声明的回调在别的 `.inc` 里调用，按文件扫会把活成员报成死的。已改为**按编译单元扫**（把 `.cpp` 和它 include 的 `.inc` 拼起来）。
  - 顺带记下但**没有动**的一个设计问题：API 配置页在壁纸进程（`DesktopAiSettingsPage`）和 MiaoDesk 设置中心各有一份。这不是缺陷而是重复，要怎么合需要你定。
  - **本轮把这条从"逐个窗修"升级成一条不变量**：新增 `tests/esc-answers-every-dialog-surface.mjs`，把仓库里所有按键走对话框管理器的界面登记成一张表（PUMPS / OWN_PUMP / SERVED），要求每个界面要么答 `IDCANCEL`、要么在豁免表里给出理由，并校验"实际含 `IsDialogMessageW(` 的文件集合"与登记表一致——以后谁新写一个泵忘了决定 Esc，这里立刻红，不用再靠第六次发现。
  - **两个豁免，理由都写进测试里而不是只写在注释里**：① 设置中心（壁纸库）——它 `WM_CLOSE` 是 `SW_HIDE` 不是销毁，而它托管的 AI/API 页每次显示都 `LoadProfiles()`，未保存的填写会被重载冲掉；Esc 正是在文本字段里最容易被随手按到的那个键，所以它里 Esc 必须保持"什么都不做"（X/Alt+F4 照旧可关）。测试顺带钉住这个前提本身：库窗一旦改成销毁、或 API 页一旦不再重载，豁免当场失效、要求重新决定而不是默认继承。② AI 创作窗——全屏预览下 Esc 已有绑定，且该窗已整体退出对话框管理器；非全屏时 Esc 属于预览交互，不该用一次误按丢掉一整份已生成的包。
  7. **LAY-2-8 已修：键盘能走到了，但看不见焦点在哪。** 这一项的前面几条把 Tab 打通了，于是暴露出下一层问题——**owner-draw（自绘）控件不会自己画焦点框**：EDIT 有光标、列表框有选中态、标准按钮有焦点框，自绘的什么都没有，只有 `DRAWITEMSTRUCT.itemState` 里的 `ODS_FOCUS` 一个信号，代码不画就没有。
  - **API 配置页两条自绘路径全都没画**：`DesktopAiSettingsPage.cpp` 的 `DrawActionButton`（新建/保存/删除/设为默认/显示密钥/复制/探测模型，全部 `WS_TABSTOP`）和 `DrawProfileItem`（Profile 列表框）。也就是说键盘Tab过去一片自绘按钮，只有一个"按下"态，看不出 Enter 会打在谁身上。两处都补上了焦点框。
  - **创作窗的预览面板也没画，而且漏在更要紧的那条分支上**：`DrawPreviewPane` 有两条绘制路径——实时预览（`previewLive`，画完 `FrameRect` 就 `return`）和占位提示。原先只可能（其实并没有）在尾部画，所以**预览正在显示时焦点提示消失**，而那正是用户围着刚生成的内容转的时候。现在两条分支都画。
  - 顺带核对：壁纸库的三条自绘路径（`DrawPrimaryButton` / `DrawNavButton` / `DrawContentFilterButton`）和创作窗的 `DrawPrimaryAction` / `DrawPresetChip` **本来就有** `DrawFocusRect`，未改。风格统一沿用它们既有的 `if (itemState & ODS_FOCUS) { RECT focus = rcItem; InflateRect(&focus, -S(n), -S(n)); DrawFocusRect(dc, &focus); }`。
  - **并把这条也变成不变量**：`tests/owner-drawn-focus-cue.mjs` 登记了 8 条自绘路径，每条都要求"测试 `ODS_FOCUS` + 真的调 `DrawFocusRect`"，且**按绘制路径计数**——`DrawPreviewPane` 登记为 2 条路径，只在末尾画一个框照样红。第一版这个检查写错了（正反向二选一的正则，被另一条分支的内容满足），是变异测试把它揪出来的：删掉尾部那个框时测试仍然是绿的。
  7. **LAY-2-10 已修：壁纸高级设置窗也是同一形态（第四个被漏掉的界面）。** `WallpaperApp::ShowAdvancedSettings()` 内联创建的 `MiaoDesk.Native.WallpaperSettings`（从库页面"显示器/性能"进入）有约 20 个 `WS_TABSTOP`——场景/布局/缩放/焦点 X/Y/帧率/全屏与最大化动作/循环/静音/音量/倍速/三个 seek 按钮——而 `Run()` 的 surfaces 数组里只有另外三个。**它被漏掉恰恰是因为形状**：另外三个都是有 `Window()` 访问器的对象，它是同一个 TU 里的裸 HWND 成员，所以逐行读那个数组时不会想到它。已加入数组，并补 `IDCANCEL`。
    - **我第一版写错了，测试当场抓住**：让它 `DestroyWindow`，而这个窗的"关闭"按钮和 `WM_CLOSE` 都是 `SW_HIDE`（要留着控件状态给下次打开）——等于给同一个意图加了第二个动作，而且**只在按 Esc 时发生**，用户完全无从得知。现已把三条关闭路径（Esc / 关闭按钮 / WM_CLOSE）全部改走 `SurfaceKeyboardFocus.h` 的 `HideSurface`。
    - 顺带修掉同一个"关窗丢键盘"：这个窗同样是隐藏持有焦点的窗口，焦点原本会落到 Z 序里的下一个窗口（经常是桌面）。三条路径现在都把键盘交还给库窗（它是唯一打开这个窗的界面）。
    - 未验证项：真机上从库页面进入该窗、Tab 走完全部字段、Esc 关闭后焦点落点。`tests/advanced-settings-window-keyboard.mjs` 5 个变异全红。

- **本轮新增（2026-09-27，按用户提出的四个问题逐条查证后实现，均未勾选：需真机确认）**：
  - **查证结论先记下**（四个问题分别是什么现状）：① 壁纸列表预览——图片壁纸是真的（WIC 解码 + cover-fit + 路径缓存），视频壁纸只有**一帧**壳缩略图，Web 壁纸没有缩略图，**Scene 壁纸完全没有 shader/粒子渲染**（只有包里声明的 preview 资源，否则是"按 scene-id 染色的纯色 + 斜网格线"占位卡）。② 组件列表预览——Content 组件是**真渲染**（`ContentWidgetPreviewRenderer` 解析同一个包、同一份有效参数、同一份宿主数据），但**每次重绘只画一帧、无动画**；原生预设一次性 D2D 渲染。③ AI 生成时的预览——Scene 包走 `StartLivePreview`，**反而是实时动画**（带播放/暂停/重载/全屏）。所以同一个组件在列表里比在创作窗里更糙。
  - **CREATE-1-1 已修：创作窗此前完全没有进度反馈。** 它收到了和对话窗同一路 `PiActivityEvent`，但 handler 只做一件事：`InspectForGeneratedPackage(event->resultText)`——把每个活动事件都当"找内容包路径"用，一条都没显示。一个数分钟的回合里用户只看到按钮变成"停止"和一句静态的"AI 正在生成内容包…"，无法区分 Pi 在思考、在改文件、还是卡死了。这直接违反 `PI_AGENT_ACTIVITY_FEEDBACK.md` §1。
    - 已加 `ShowActivity(PiActivityEvent)`：把标题下的 `note`（原本只在建窗时设一次的静态）当活动行用，覆盖 `PiActivityKind` 全部 8 个语义状态，工具名走 `ToolDisplayNames.h`（与对话窗同一张表，不让两个界面用两套名字）；`SetBusy` 起停一个 1 秒定时器，在行尾追加"· 已等待 N 秒"（≥3 秒才显示）。**不造假进度**：这条流水线不知道一个需求要几次工具调用，活动契约明令禁止编造百分比，唯一诚实的定量只有耗时。空闲时恢复原文案（因此建窗时把 `note` 原文抓下来存着）。
  - **CREATE-1-2 已修：校验器的判决此前既不给用户、也不给模型。** `SetGeneratedPackage` 的行为是 `if (!inspected.success || info.kind != ExpectedKind()) return;`——`MiaoContentModel` 那些精确消息（`Content parameter is below minimum: <key>` 等 30+ 条，见 `src/content/model/MiaoContentModel.cpp`）被整个丢掉，用户只看到一句"未检测到有效内容包路径"，而**模型什么都没有收到**；
  - 系统提示（`PiRuntime.cpp:526-528` 的 `systemPrompt` 组装处）还明确告诉它 scene/.mdwidget 包由宿主校验，所以按构造它永远听不到回音；模型唯一的自纠通道是 `content-review` 那份散文清单。
    - 现在：`lastValidationError` 记下校验器原文 → 写进转写区（`[内容包未通过校验]` 块，含路径和原文）→ `BuildPrompt` 在下一次请求里以"【上轮生成的包未通过校验，必须先修掉这一条】/校验器原文：…/不要改变用户需求"的形式回灌给模型。`ResetSession` 清空（新会话不能继承一个已经不在的包的报错）。
    - **为什么整段原样引用而不是概括**：概括会丢掉字段名，而字段名恰恰是模型唯一能据此定位修改的东西。
  - **CREATE-1-3 已修：校验通过的包现在自动入库。** 原先必须手点"加入壁纸库/加入组件库"；同一个已经校验过的包晚一次点击才进库，用户若在此期间关窗，产物只存在于模型的沙箱里、直接丢失。已改为校验成功即入库，并把该按钮置灰（否则是一个点了不干事的按钮）。
    - **刻意停在"入库"这一步**：`AI_GENERATED_DESKTOP_SANDBOX.md` §1 的硬规则是"只有显式 Apply 能跨越提交边界"，所以"应用到桌面"仍是用户的一次点击。库是应用托管存储，不是桌面状态。
    - 顺手把 `InstallToLibrary` 从 `InstallGeneratedPackage` 里拆出来，让自动路径和按钮走同一个函数——否则"已自动入库"会和按钮做的是两件事。
  - **CREATE-1-4 已修：重新生成时，上一版预览被当成当前预览用。** `SendPrompt` 有意不拆掉预览（拆了就是一整轮空白），但也没说它是不是新的：上一版 Scene 还在动、标签还是原话，等新包落地时和旧的那份**看不出区别**。唯一提示过用户的地方是失败结算那一句"当前预览仍是上一版候选"——一条几分钟前读过的状态行。现在：预览标题在忙时改成"生成中（上一版预览）"，并且**在两条绘制分支上都打角标**（实时 Scene 那一支是会 early return 的，也正是重新生成壁纸时真正走的那支；只在一处打等于没打）。
  - 未验证项：活动行文案在真机上是否够醒目、"已等待 N 秒"节奏、自动入库后到"应用"之间的状态是否说得清、以及"上一版预览"角标在浅色/深色预览上是否可读。测试 `creator-progress-and-retry-loop.mjs` 16 个变异全红（其中 3 个第一版是绿的——单次出现的检查在有 3 处调用点时会漏、`if (false)` 包裹的调用文本还在、库按钮那条只跑了自己的测试而没跑 `content-creator-modes.mjs`）。
  - **上一条挂起的"生成的 Scene 壁纸应用不了"本轮已修（2026-09-27 当日，goal：有问题就修不等催）**。这不是 AI 创作通路独有的问题，而是**整个 Content Scene 类别**：`ApplyLibraryItem` 的全局入口只能写入"内置 scene key / image / video / web 源"，Content Scene 包没有可写的东西，于是报"请在目标显示器上分配该壁纸"——而"全局 / 当前布局"正是库页面默认选中的目标，所以整类壁纸（AI 生成的 + 任何导入的 .mdwall scene 包）在默认路径上必然失败。
    - 修法：不新增渲染能力，改用**已经支持 Content Scene 的那条机制**——按显示器分配（它通过 content resolver 解析 `content:<id>`）。`DesktopControlService::ApplyLibraryItem` 失败时先问 `WallpaperService::NeedsPerMonitorApply`，是则枚举真实拓扑、逐屏分配。
    - **关键的一点：必须同时把 `Layout` 切成 `independent`。** 分配表只在 `StartIndependent` 里被消费，Span/Clone/PrimaryOnly 下引擎渲染的是全局选择——不切布局就是"写进去了、桌面没变"却报成功，正是本仓已经修过三次的"假成功"。Web 通路早就这么做（`WallpaperWebRuntimeCoordinator.cpp` 的 `PersistMonitorWeb`），这里是照着它做，不是发明。
    - **切布局失败必须报失败**，不能把即将声明的成功发出去（`!layoutSwitched` → return {false, ...}）。
    - `NeedsPerMonitorApply` 的判定刻意收得很窄：只有 Scene 且是 `content:` id 且解析不到内置 runtime key 时才需要逐屏。内置 Scene / 图片 / 视频 / Web 全部仍走原全局路径，原行为不变。
    - 未验证项：多屏hot-plug、以及"全局应用"把原本各屏不同的壁纸统一掉这一行为是否符合预期（这正是全局应用的字面语义，但它是行为改变，需确认）。测试 `content-scene-global-apply.mjs` 10 个变异全红。
  - 已把 17 个 tab stop 的创建顺序改成与 `Layout()` 的摆放顺序一致：`新增配置 → Profile 列表 → 名称 → 服务类型 → Base URL → API Key → ◉/复制 → 模型/探测模型 → 图片接口三项 → 测试连接/保存/设为默认/删除`。只移动语句位置，不动任何逻辑——创建顺序除了 Tab 序之外不影响别的（各 `state.x = ...` 互相独立，`SendMessageW` 填充在全部建完后）。
  - 顺带核对：**组件设置对话框本来就是对的**（参数按定义顺序在建窗循环里逐行生成，`y` 递增），所以它不在这条里；也正因如此，只有 API 配置页一个页面需要改，不是全局问题。
  - 新测试 `tests/api-page-tab-order.mjs` 把"创建序 == 摆放序"钉住：解析 `CreatePage` 的创建序列与 `Layout()` 的 `place(...)` 序列，深比较，并要求两组集合相同（否则有控件没被摆放、会卡在建窗时的 10×10 尺寸上）；另外要求 `Layout()` 里每一次 `SetWindowPos` 都带 `SWP_NOZORDER`（否则重排一次 Tab 序就变），并要求泵的 surfaces 数组里仍有库窗口（这点必须在 `Run()` 里查，不能在文件里查——`libraryWindow_.Window()` 还作为自动化窗的焦点归还目标出现在 `ShowAutomation`，全文 grep 会在页面已不被泵服务时也放行）。


- **仍未修的核心项**：
  - 附带：壁纸库搜索框（`WallpaperLibraryWindowV2.cpp:1944`）只有 `EN_CHANGE` 一条路径，没有 Enter 提交。
  - **为什么搜索框没在同一改动里加**：它有自定义输入处理（Enter 执行选中项），`IsDialogMessageW` 会抢先，所以那一面需单独判断——可能要靠 `DLGC_WANTALLKEYS` 一类豁免，而不是直接加。测试已把这条边界钉住：搜索框那条泵若被顺手加上 `IsDialogMessageW` 会立刻红。

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
  - **这条本轮也修掉了（2026-09-27 当日）**：壁纸筛选条从 8 片加到 10 片，新增"收藏"（`item.favorite`）和"最近使用"（`lastUsedUnixSeconds > 0`，并按新到旧排）。数据侧一行没改——`SetFavorite` 和 `MarkUsed` 的写入路径本来就在（星标按钮 + 右键菜单；每次应用打时间戳），缺的只有读出来的一面。之所以现在能加，正是因为上一条已经把 Chip 行改成可换行。
  - **排序只在该视图内做，不碰"全部"**：在 `RefreshWallpapers` 里用 `wallpaperFilterIndex == 9` 守着 `std::stable_sort`，否则会给从未要求按时序看的用户把每张壁纸的位置都搬一遍。
  - **顺带修掉一个由新 Chip 放大的错误空态**：网格原来只有一句"桌面库为空。使用右上角'添加'导入壁纸。"——库明明有壁纸、只是当前筛选/搜索没命中时也这么说，等于把用户支去导入他本来就有的文件。"收藏"对新用户必然是空的，这句话从吹毛求疵变成一进页面就骗人。现在分三种：库真空 → 导入提示；筛选未命中 → "当前筛选下没有壁纸。点「全部」查看库里的所有壁纸。"（**先判断搜索框再判断筛选片**，否则空搜索时让用户去点"全部"是错的）；搜索未命中 → "没有匹配「<关键词>」的壁纸。换个关键词，或清空搜索。"
  - 未验证项：第 9、10 片 Chip 的位置与两行 Chip 的视觉节奏、以及"最近使用"的排序体感，需真机。测试 `library-favourite-and-recent-views.mjs` 8 个变异全红。
  - **这个前置本轮修掉了（2026-09-27 当日）**：筛选条原来就是"不换行"本身，而且**现在就已经溢出了**——窗口自己的最小跟踪宽度是 `S(820)`（`WM_GETMINMAXINFO`），减掉 `S(208)` 侧栏和页边距后 Chip 行可用 588 逻辑像素，而 8 个壁纸 Chip 需要 590、8 个组件 Chip 需要 610。也就是说在最窄合法窗口下，最后一个 Chip 有一截在窗口外面，且每个 DPI 都这样（两边同时缩放，所以这是逻辑像素问题，不是缩放问题）。计划里想加的第 9 片（收藏）会把需求推到 666。
  - 修法：把几何抽成纯函数 `src/include/miaodesk/LibraryFilterChipLayout.h` 的 `ResolveFilterChipLayout`，由布局调用；放不下就换行，并且**band 高度按行数算**（`chipBandH` 替代原来固定的 `S(48)`），把下面的网格整体下移同样的量。没有选择"压缩 Chip 宽度"——10 个带中文的自绘 Chip 挤在一行里只会变成截断文字。
  - **没有新增渲染能力、也没有改桌面路径**，纯粹布局。
  - 未验证项：换行后两行 Chip 的视觉节奏、以及真实 DPI 下的位置，需真机。测试 `tests/library-filter-chip-layout.mjs` 是**把真实头文件编译后跑起来**验的（不是在文本上断言）：19 个用例覆盖最小宽度到比一个 Chip 还窄的退化为止，断言"不出界 / 行连续 / 不重叠 / 贪心不无故换行 / band 高度等于行数推出的值 / usedWidth 等于最宽行"，并用 12 个变异逐个验红。
  - 写这个测试过程中我自己的测试错了两次（SPAN_FOR 的键和 harness 打印的用例名不一致，导致 16 个用例被静默跳过 12 个；以及把 `used` 当 `usedWidth` 用），头文件本身没问题——正因为是跑真代码，这两次都很快露出来了。
  - **又修一条"核心动作静默失败"（2026-09-27 当日）**：`ApplySelected` 的守卫原来写作 `if (!selected || SourceMissing(*selected)) { log; return; }`——**没有状态行、没有响声**，而它正下方那条 `applied == false` 的失败路径两者都有。之所以看着无害，是因为另两个入口都拦住了：`applyButton` 被 `EnableWindow(FALSE)`、右键菜单 `kMenuApply` 被 `MF_GRAYED`。但**双击卡片没有拦住**，而资源缺失的卡片脸上就印着"· 不可用"——于是用户双击它，是整个产品最核心的动作上得到"什么都没发生"。现在拆成两个分支并各自给出可执行的反馈：未选中 → "先选择一个桌面，再点应用到桌面。"；资源缺失 → ""<标题>"的资源已不存在，无法应用；移除后重新导入。"（说"移除后重新导入"而不只说"不存在"，是因为卡片已经告诉过他不存在了，缺的是下一步）。测试 `tests/library-apply-failure-is-visible.mjs` 同时还钉住**可达性**本身：双击路径不得出现 `SourceMissing`/`usable` 门，否则这次修复就成了不可达代码；5 个变异全红。
  - 同一条双击分支上顺带修掉的不一致：它是唯一一处改了选中态却不调 `UpdateFooter()` 的地方（widget 分支上面两行就调了）。双击的第二次点击可能落在另一张卡片上，而第一次点击已经把页脚刷新给了那一张——于是页脚标题和按钮态与用户正看着的行不符。
  - **再修两条同一形状的"静默丢点击"（2026-09-27 当日）**，都是同一个错误推理：旁边有个看起来相关的门，就以为守卫不可达。
    1. `ImportWeb` 的 `if (url.empty()) return;`——"添加 Web"按钮**从头到尾没有任何门**：它跟着整条 web bar 显隐（`ShowWindow(webConfirm, SW_SHOW)` 是无条件的），而 bar 一打开就 `SetFocus(webUrl)`，字段里只有 cue banner。也就是说**打开 bar 然后直接点按钮**就是这个守卫的第一现场，而它什么都不说：没有状态行、没有响声。现在给状态行 + `MB_ICONERROR` + `SetFocus(webUrl)` 把键盘还回字段。状态文案说的是"HTTPS 或本地 HTML"，因为 `WebWallpaperProcessSet::IsSupportedSource` 两种都收（远端 HTTPS、存在的本地 HTML），而菜单项写着"添加 HTTPS Web 地址…"、cue banner 写着 https://example.com——两处都低报了能力。
    2. 顺手把这条"守卫可达性"变成不变量 `tests/every-library-action-talks-back.mjs`，因为它不是这次修完就没的问题：两次都是"我觉得我的守卫到不了"。它从**派发表本身**反解出全部 12 个动作处理器，只承认**有条件**的门（`EnableWindow(...)` 与 `ShowWindow(x, cond ? SW_SHOW : SW_HIDE)`；`ShowWindow(webConfirm, SW_SHOW)` 这种无条件显隐不算），于是按钮门只有"页面可见性"一层的（收藏、移出库、小组件启停/删除）额外要求 enablement 必须绑到选中态；id→HWND 的映射也一并验，否则"这个控件有门吗"根本问不出来。**9 个变异全红**，其中包括我第一版测试的两个漏洞：`[\s\S]*?` 从守卫懒匹配到**下一条分支**的 beep，让没有 beep 的分支通过；以及空 signature 的 `bodyOf` 直接返回整个 slice，同样是这个后果。
    - 这条不变量有意**不**覆盖"门存在但门错了条件"的情况（例如把 `EnableWindow(favoriteButton, selected ? ...)` 改成 `EnableWindow(favoriteButton, installed ? ...)`）——那要求文本上比较守卫条件与门条件，而两边的变量名不一样（`!current` vs `widget`）。这是这份测试的已知边界，写在这里免得下次误以为它管这个。

  - **再修一条"假成功"，而且比静默失败更糟（2026-09-27 当日）**：上一轮我把 `applied`/`failure` 引进来，是为了修 `DesktopControl` 那条路径的假成功；但**引擎那条路径根本没接上**——`ApplyCallback` 返回 `void`，`ApplySelected` 里就是 `applyCallback(*selected, targetId);`，`applied` 保持初值 `true`。于是引擎在 `ApplyLibraryItem` 里 bail out 时：设 `libraryError_`、记日志、`RefreshSettings()`、return，而库窗口照样走完 `MarkUsed` → `RefreshWallpapers` → `SetStatus(L"已应用到桌面：" + title)`。用户被告知成功，壁纸被记了"最近使用"，真实原因写进 `libraryError_`——这个字段**全仓只有一个读取点**：高级设置窗的诊断文本。也就是说失败原因在另一个窗口、另一个页面、用户没在看的地方。原来的注释甚至把这个写成了设计："the status line below is therefore not evidence of success on this path -- the engine's own window is"——把缺陷当规格。`AI_GENERATED_DESKTOP_SANDBOX.md` 记这种形状为 false success，并注明本仓已栽过三次；这是第四次，而且这次连"引擎自己的窗口"都不成立。
    - 修法：`ApplyCallback` 改为返回 `std::wstring`（空 = 成功），引擎四个 bail-out 全部返回**用户能动手的中文理由**（Web 激活失败 / 显示器分配失败 / Scene 无可用运行时 / 类型不可运行，后者两种都把"未修改当前桌面"说清楚，否则用户会以为是改坏了），成功尾显式 `return {};`（非 void 函数漏 return 是 UB，不是空串）。`libraryError_` 仍然照设——它是引擎自己的诊断面，但它从此**不能**替代告诉点了按钮的人。
    - 顺带修掉这条链的第二截：`MarkUsed` 的守卫对引擎路径同样失效，"最近使用"视图（本条上面刚加的）会把一个**应用失败**的壁纸排到真正用过的前面。现在 `applied` 由回调结果推出，守卫重新生效。
    - **顺带删掉一个死重载（同一天）**：`WallpaperLibraryWindow` 有一个不带 targets 的 `Show` 重载，挂 `GlobalApplyCallback`，**全仓零调用**。它不是无害的便利接口——上一轮改 `ApplyCallback` 返回类型时，这个死副本也得跟着改：一次签名改动伸进了永不执行的代码，而下一个人读头文件会以为存在"只支持全局应用"的调用方式。现在只有一个 `Show`，没有 target 的调用方传空 vector。测试把"只准有一个 Show 重载"钉住（变异：把死重载加回来 → 红）。
    - 测试 `tests/library-apply-reports-outcome.mjs` 7 个变异全红，含"把 `applied = true` 写回回调分支"和"某个 bail-out 改成裸 `return;`"（裸 return 等于空串，等于成功，是把假成功往下一层搬）。同时**改写**了上一轮的 `library-apply-reports-result.mjs`：它原来钉住 void 回调那句注释，现在整个前提反了，留着就是一条会主动阻挠修复的测试。

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
    - ~~预览的缓存失效只看 `manifest.json` 与 entry，活宿主遍历包里每个文件；只改 `parameters.json` 的原地重装会让桌面 1 秒内刷新而库卡片仍显示旧值（库窗口在弹窗返回后调 `Reset()` 挡住了常规路径，挡不住包管理器那条）。~~ **已修**：预览的 `PackageStamp` 改为与活宿主**逐字相同**的递归策略。两个函数同名、不同 TU，名字不会告诉你它们是否一致——所以新增 `tests/preview-host-stamp-parity.mjs`，把两边函数体归一化参数名后直接比对字符串，任何一侧下次漂移都会立刻红。顺带删掉 `EnsureScene` 的 `definition` 参数：它只被旧的两文件签名用到，留着会让人以为预览是按调用方传入的 definition 校验的（实际每次从 catalog 重新解析）。
    - `fields` 只建一次而 `ReloadValues` 会替换 `state.snapshot`（含 definition），弹窗打开期间包变更会让字段提示过期。fail-safe，但用户看到的是过期提示加一句"未知参数"。
    - **审计报过但核对为正确、不要改**：`ResetDefaults` / `Apply` 的 `state.changed = true; if (ReloadValues(state)) SetStatus(...)` 曾被指"先标成功再刷新，刷新失败也会显示已恢复"。实际不会——`ReloadValues` 自己失败时就 `SetStatus(result.message)` 返回 false，所以用户看到的要么是确认、要么是重载错误，不会看到假的成功。`changed` 也不是"有未保存编辑"，它是**对话框返回值**（`return state.changed;`），调用方据此刷新组件，所以重置后必须为 true。改这里会把对的代码改坏。

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
    1. ~~`/retry` 把上一条**原始 prompt 原样重发**进同一个 Pi session（session 复用，agent 内存历史里还有上一轮已提交的 tool 记录），且 `lastPrompt` 只在 `/new` 清。提交后遇传输失败 → 提示可 /retry → 重试会再跑一次 `ppt_create` 生成第二个文件。全仓没有任何"这一轮已经提交过哪些工具"的账本。~~ **已修掉不需要账本的那两半**：① `StopTurn` 现在也清 `lastPrompt`——取消是明确的"别做这个"，取消后还能 /retry 就是把已提交的工作原样重放；② `FinishTurn` 在"本轮跑过工具"时**不再邀请** /retry，改成明说"重试会把已完成的动作再做一次，请直接重新描述需求"。新增 `turnRanTool` 标记（轮次开始时清、`UpdateToolStart` 里置），只可能在一轮进行中被置位——已核实活动事件流被 `state->busy` 门住、而日志轮询那条路被 overlay 吞掉计时器、生产环境不可达，所以不会被上一轮的残留污染。**仍未修的核心**：用户手打 /retry 依旧会重放，真正的修法还是 per-turn 提交账本或重試前重置 session，那要动设计。
    2. **预览窗口会在取消后照样弹出来（这半已修）**：`Stop()` 不终止 Pi 进程，`CleanupProcess()`（唯一 `TerminateProcess`）只在析构和换 provider 时调。而 `CreateWallpaperPreview` 写完沙盒后**阻塞式** `SendMessageTimeoutW` 通知主进程，主进程侧 `SearchPreviewBridgeProc` 原本**没有任何 turn 判断**，收到就 `ShowPreviewWindow` —— 于是取消之后桌面上可能留着一个用户被告知"已停止。"的轮次的预览窗，点一下"应用"就能改桌面。已加门：runtime 不忙就整个吞掉这次 COPYDATA。选**共享 runtime 的 Busy()** 而不是面板自己的 busy 标志，因为创作窗驱动的是同一个 runtime，按面板判断会把创作面合法产生的预览也误杀。代价是正常结束后才到的迟到预览也不弹窗了——弹在轮次结束之后本来就是困惑的，而 transcript 里的 artifact 路径照旧传达结果。**未修的部分**：`Stop()` 依然不终止进程。`CreateWallpaperPreview` 写完沙盒会**阻塞式** `SendMessageTimeoutW` 通知主进程，主进程侧 `SearchPreviewBridgeProc` 无任何 turn 门，会直接建出带"应用/拒绝"按钮的可见沙盒窗。于是取消之后桌面上可能留着一个用户被告知"已停止"的轮次的预览窗，点一下应用就能改桌面。
    3. ~~创作面**没有任何取消手段**~~：**已修**。原来 `SetBusy` 把发送按钮禁掉并改叫"生成中…"，生成中只剩"新对话"和关窗，两者都会通过共享 `gPiRuntime` 反过来取消对话面正在进行的轮次。现在发送按钮在忙时保持可用并显示"停止"，点击停当前轮，与对话面行为一致；`stopRequested` 让 `kRequestDone` 报"本轮已按你的要求停止"而不是那句会骗人的"本轮请求结束"。提示里明确写了"已经开始执行的操作可能已经完成，不会被撤销"——因为 `PiRuntime::Stop()` 只请求停止不终止 worker，光说"已停止"会让人以为副作用也停了。prompt EDIT 仍未子类化，Esc 在这面上依旧无效。
    4. `AskAsync` 里 `worker_ = std::jthread(...)` 的移动赋值会 join 旧 worker，而调用线程就是**UI 线程**；旧 worker 若卡在 `ReadLine` 的 1000ms 轮询或 `WaitForSingleObject(2000)`，整个界面会阻塞最多约 2 秒。
    5. ~~`AskAsync` 对"已在忙"只回一句 `Pi Runtime 正忙`，调用方（创作面）把它当成"本轮生成已完成"提示给用户——请求根本没发出去。~~ **已修**：`kRequestDone` 识别该拒绝并改为"没有发出请求：妙喵正在处理对话窗口里的任务。等那边结束，或先在对话里停止，再试一次。"。这根因是创作面与对话面**共用一个 `PiRuntime` 却各有一个 busy 闩**，所以面板在跑时创作面的闩是空的、请求能进到 `AskAsync` 才被拒。测试把两边的字符串钉在一起（`PiRuntime` 发什么 / 创作面认什么），改名会立刻红，不会静默退化成继续说"已完成"。

### CREATE-01 创作流程与上一可用结果保留

方案 2 的专项实现统一按 [壁纸与组件专用创作 Agent 实施计划](CONTENT_CREATOR_AGENT_PLAN.md) 的 CCA-00～14 推进：独立创作会话、受约束工具、结构化候选、自动校验修复与视觉反馈。本处 CREATE-01～04 保留产品级验收与已有进展，不重置状态；AI-01～02、LIB-02/03 和 PERF-03 的复用关系见专项计划第 10 节。本地 AI 不作为依赖。

- [ ] 完成本项；负责人：待领取；证据：待补。

- **现状 / 依据**：壁纸/组件共用创作界面，已有预设、生成、重新生成与应用入口。依据产品愿景与内容框架。
- **依赖 / 入口**：AI-01、LIB-02；`ContentCreatorDialog.cpp`、`ContentCreatorBridge.cpp`。
- **待办**：明确描述→生成→校验→预览→应用的界面状态与按钮启用条件；核验重新生成失败时能否回看上一结果，缺失时补单次恢复能力；保留描述与用户参数。
- **交付 / 验收**：两种模式使用一致操作规则；生成失败不清掉上次可用候选或更改桌面；不会对未通过校验的结果显示可应用。无需引入完整版本历史系统。

- **本轮补记（2026-09-28，仍未勾选）**：CREATE-01 待办里的两条已推进到"判定层有可执行测试、界面挂载仍缺"。
  1. "保留描述与用户参数" —— 新增 `CreationDraftStore`（`src/include/miaodesk/CreationDraftStore.h` + `src/desktop/control/CreationDraftStore.cpp`）：关窗再打开要接上的字段（需求正文每一项 + 上一有效候选摘要/版本号 + 取消标记 + epoch/turnId）与**五种**恢复结论（接上继续 / 只显示不自动继续 / 必须再问并说清缺哪一项 / 没有草稿 / 接不上且**绝不当成新草稿**）。`src/tests/CreationDraftStoreTest.cpp` 59 条，本机实跑。**挂载点仍缺**：`SetDraftPersistHook` 至今只有一个测试在调（见专项计划 §11.5 第 2 条）。
  2. "补单次恢复能力" —— 应用事务的一次恢复判定已由 `ContentApplyRecovery` 承担（`src/tests/ContentApplyRecoveryTest.cpp` 49 条），四种真相各自独立：落地了**不**撤销、没落地写回精确前态、已被另一份候选接管则一个字都不写、读不出现状就停。宿主持盘读目标与写回仍未接。
  两条都不构成"本项完成"：它们只让规则在本机可判，真机上的窗口行为与桌面读写仍未验。

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

- **本轮核验（2026-09-27，未勾选：DNS/网络、鉴权、路径、限流等分类的端到端区分仍需可控故障服务）**：
  - **修掉一条真会泄露 Key 的路径（同时踩中本项"错误展示不泄露 Key"与 API-03"凭据不进明文配置"两条验收）**：`L3Agent::ProbeModels` 的错误消息把服务端响应体**原样**拼进去——`Utf8ToWide(response.body.substr(0, 220))` 然后 `detail += L" " + shortBody`。这条消息有**两个去向**：设置页状态行，以及 `profile.lastMessage`——后者由 `TestConnection` 经 `WriteIni` 写进**明文** profile INI。也就是说，一个把请求头回显回来的网关（这种网关存在：调试代理和错误页会把拿到的 `Authorization` 原样返回）就足以让 Key 先进屏幕、再进明文文件。两条验收此前都只是靠"没有服务端会反射请求"这个假设成立。
    - 修法：新增 `src/include/miaodesk/SecretRedaction.h`（纯扫描器，不依赖 `<regex>`，为的是能真正编译运行、规则集中可读）。`SummarizeRemoteBody` 先压平成单行 → 脱敏 → 截断；`RedactSecrets` 保留**字段名**（知道是哪个字段泄了才有下一步），只隐藏值。覆盖的形状：`Authorization: Bearer …`、JSON `"api_key":"…"`、查询串 `?api_key=…&x=1`、以及没有任何字段名的裸 `sk-…`。
    - **先截断再脱敏**，顺序是刻意的：反过来就是对 2 MB 的响应体跑一遍扫描再丢掉。
    - 压平不只是为了好看：`lastMessage` 是 `WritePrivateProfileString` 写进 INI 的值，带换行的值能在读回它的文件里伪造键。
    - 裸 `sk-` 识别要求**词边界**：不加的话 `risk-assessment` 会从自己第三个字母起匹配到 `sk-`，整个词被脱敏——把普通错误文本改坏，而那个词根本不是密钥。第一版测试没有这条用例，变异到它的时候是绿的；补上之后才红。
    - 测试 `tests/probe-error-body-is-redacted.mjs`：**把真实头文件编译并运行**（31 个用例），再钉住 `L3Agent.cpp` 的接线（先截断后脱敏、`result.message` 不得直接来自 `response.body`），并对全仓 `src/` 扫一遍"响应体进消息且没走脱敏器"的其它位置——现在只有这一处。6 个变异全红。
  - 未验证项：真实故障服务下端到端的分类（DNS / 鉴权 / 路径 / 限流）、以及探测成功但聊天或图片生成实际不可用的区分，需可控故障服务与真机。
  - **同一条记录的缺口 2，本轮把两半都收了（2026-09-27 当日）**：记录里写的是"上游错误原文（聊天 300 字节 / 探测 220 字节）会进可见 UI 和 `l3-runtime.log`，与 `L3-PI-RUNTIME-CONTRACT.md:128` 相冲。代码不会把自己的 Key 放进去，但那是第三方文本，未被过滤。"现在**探测那半**由上面的 `SummarizeRemoteBody` 兜住；**聊天那半**也修了——`L3Agent.cpp` 的 `模型请求失败：HTTP … · Endpoint=<path>` 同样把 path 和正文都过一遍脱敏（path 也要脱，是因为用户若把凭据粘进 Base URL 的**路径**，这行会按名回显）。
  - **缺口里还藏着第三处，本轮补掉**：`UrlCarriesSecret` 只拒 userinfo 和**查询串**里的密钥名，**路径里的凭据它看不见**。而 `baseUrl` 会被原样写进 `api-profiles.ini`、明文 `PiAgent\models.json`（`PiRuntime::ConfigurePiAgent` 直接 `EscapeJson(setup.baseUrl)`）和 Harness `settings.yaml`——三个明文文件。已把该函数搬到 `src/include/miaodesk/ApiUrlSecretPolicy.h`（纯字符串逻辑，不依赖 Windows API），新增"路径段像粘进来的凭据"判定（`sk-`/`pk-`/`rk-` 前缀 + 全 opaque 字符，**刻意窄**，否则"高熵且长"这种启发式会把正常部署名和 webhook 路径一起拒掉）。
  - **为什么搬进头文件**：这个函数原本在 `DesktopAiSettingsPage.cpp` 的匿名 namespace 里。写它那一轮**确实**把真实字节抽出来编译执行了 17 个用例并当场抓到一个自己写错的 bug，但**没留下测试**，记录里也写明了"回归只由 settings-url-secret-guard.mjs 做存在性与覆盖断言，它证明不了解析正确"——于是这段逻辑在裸跑，这次一改就会踩空。现在 `tests/api-url-secret-policy.mjs` 编译并执行 40 个用例（含 12 个**必须放行**的正常 URL，防的是把用户正常输入拒掉），并钉住：头文件必须保持纯净、设置页必须 include、两个字段的守卫必须既存在**又不是死的**（`if (false && UrlCarriesSecret(...))` 这种把文字留下、把行为去掉的形状会红）。
  - **执行这一步立刻抓到一个真缺口**：查询名表里**从来没有 `api_token`**（有 `api_key`、`apikey`、`access_token`，唯独没有 `api_token`）。原测试只做文本存在性断言，所以十四轮都没人发现。现已补齐 `api_token`/`x-api-key`/`secret_key`/连字符形式。
  - **删掉 `tests/settings-url-secret-guard.mjs`**：它的每一条断言都被新测试覆盖且更严，而它在函数搬走之后是**错的**（去设置页里找一个已经不在那里的函数）。留着一个会主动误导人的测试比没有更糟。
  - 未验证项：真实故障服务下端到端的分类（DNS / 鉴权 / 路径 / 限流）、以及探测成功但聊天或图片生成实际不可用的区分，需可控故障服务与真机。

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

S1 基础版先完成既有可靠性验收。2026-10-03 起，3D、灯光、形变和受限程序化能力正式进入专业版 S6，不再排除于总体目标；任务见第 11 节。大型编辑器和内容市场不作为交付前提，脚本表达优先采用有界行为。


## 11. 专业级桌面建设任务（2026-10-03）

依据：[总体规划](PROFESSIONAL_DESKTOP_PLAN.md)。**以下是任务规格，不是第二份完成状态表**；领取、进度、阻塞和 Done 统一维护在持续开发面板。每项完成要回填本节的证据记录。未附证据的规划不表示实现完成。

共同完成门：实现/集成 → 自动检查 → 正式 staging → 适用 Windows 验收 → 参考作品/AI 使用证明。阶段出口、质量/性能数字以总体规划第 7 节为准。新 Provider、工具、schema 字段在其任务完成前均属拟新增。

基础版任务继续有效：P0 与本文件 STAB 对应；UX 对应 LAY/VIS；原 SEARCH/AI/CREATE/CFG/HAR/REL 详细项按名称和验收映射，**不同年代同名编号必须同时标注文档与日期**。不因新增 36 项专业建设任务而关闭既有未完成项。

### PRO-01 — 现状、能力与证据台账

- **阶段 / 依赖**：S0；无。
- **实施**：核实 d02812d6 之后的 HEAD、运行时/预览/Creator 真正调用链、既有测试和当前 CI；校正 DESK/CCA 旧状态。
- **交付物**：按能力登记模型/运行/预览/AI/设备五种支持状态、代码入口、证据 SHA、负责人和缺口。
- **验收**：官方三壁纸、指针/音频、Web 推帧和候选工作流均有可复查结论；已有代码不重复开发。
- **验证环境**：本机/CI 可做静态与自动核实；Windows 效果保持待验。
- **证据记录**（2026-10-03，本轮）：负责人=自动推进会话（产品所有者签收待补）；目标 SHA=`d02812d6`（= HEAD）；实现提交=本轮文档提交；自动检查=`scripts/verify-windows-syntax.sh` 通过（0 真实错误，仅已登记 mingw 缺口）、`scripts/run-pure-logic-tests.sh` 31/31 通过；真机/作品证据=无（本机无 Windows）；交付物=`docs/CAPABILITY_EVIDENCE_LEDGER.md`（Scene 10 组件 kind 五态表、壁纸/组件/创作三张能力台账、DESK-01/03/04/05 与 CCA 校正结论、5 项登记缺陷 D-1～D-5）。限制：所有视觉效果与桌面行为未取证；限制与下一步见该台账第 7 节。面板状态改为 🟡（未做真机签收，不得 Done）。

### PRO-02 — 对标作品与评测集冻结

- **阶段 / 依赖**：S0；PRO-01。
- **实施**：按总体规划选定壁纸 12 场景、S6 四类高级场景、8 类组件；拆开发集和保留集。
- **交付物**：版本化 brief/素材来源/复杂度/参数/预期行为/评分表；AI 30+30 开发集及 10+10 保留集目录。
- **验收**：每场景能指出对应能力与验收；保留题不进入 Skill 示例；对标素材和软件版本可追溯。
- **验证环境**：样本设计可自动辅助；作品选择和基准视觉需人工。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### PRO-03 — 参考环境、预算与度量协议

- **阶段 / 依赖**：S0；PRO-01、PRO-02。
- **实施**：登记 x64/ARM64、显示器/DPI、OS/驱动；实测四场景与 Creator 阶段耗时，冻结资源/费用阈值。
- **交付物**：可复现采集步骤、原始数据、参考机指纹、性能/质量/成本阈值与例外规则。
- **验收**：同场景 30 秒×3 可比较；p95/peak/全进程树覆盖；未测指标显式缺失，不编造数值。
- **验证环境**：需要 Windows 实机、对标软件、已配置 Provider；无环境时完成协议并登记阻塞。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### CAP-01 — 运行时能力目录与查询

- **阶段 / 依赖**：S2；PRO-01。
- **实施**：从真实注册/校验/后端能力生成目录，区分可声明/可运行/可预览/AI 可创作/真机已验；复用 Creator registry。
- **交付物**：稳定 ID/版本/类型/单位/范围/限制/后端/依赖/资源成本；受控能力查询和文档导出。
- **验收**：真实、缺失、仅声明、后端不支持四类查询正确；未知能力被拒；Skill/工具/validator 与同一目录一致。
- **验证环境**：纯逻辑/契约测试可本机；设备能力探测需 Windows。
- **证据记录**（2026-10-03，本轮，负责人=自动推进会话）：目标 SHA=`d02812d6`；实现提交=本轮 `src/content/binding/MiaoCapabilityCatalog.cpp` 等；自动检查=`MiaoDeskCapabilityCatalogTest` 220 项通过（含四类查询、未知拒绝、目录与代码同名、deviceVerified 恒否、导出与目录同源）、`ExtractJsonStringArray` 37 项、加载器与 validator 两处"未知 capability 拒绝"断言通过、`scripts/verify-capability-catalog.sh` PASS、`scripts/run-pure-logic-tests.sh` 33 目标全通过、mingw 语法门 0 真实错误；新增 `creator_capabilities_get` 自述带能力分级。变更-检测：把 `asset.font` 标成可执行被 CapabilityCatalogTest 拒绝（该条目已改回仅声明），把 `scene.spriteRenderer` 改名被 `verify-capability-catalog.sh` 拒绝，数组取值器吞掉非字符串元素被 `JsonStringFieldTest` 拒绝。真机/作品证据=无。限制：目录描述代码事实，不描述真机效果；CAP-02（能力卡/配方）与设备探测未做。面板状态改为 🟡。

### CAP-02 — 能力卡、配方与参考知识库

- **阶段 / 依赖**：S2；CAP-01、PRO-02。
- **实施**：扩充现有四 Skills 的内容资源，按能力和创作阶段检索；覆盖设计原理、组合方式、性能、正反例和修复。
- **交付物**：每项可创作能力至少最小例+组合例；首批壁纸/组件各 3 个真实渲染样例；版本/摘要和随包检查。
- **验收**：AI 能解释并正确使用检索能力；更换安装版本不引用不可用字段；样例与保留集分离。
- **验证环境**：结构/检索自动验证；样例渲染与视觉人工签收。
- **证据记录**（2026-10-03，本轮，负责人=自动推进会话）：目标 SHA=`d02812d6`；自动检查=`scripts/verify-skill-teaches-executable-capabilities.sh` PASS（45 条可创作且可执行的能力全部被至少一份 Skill 提到；变异检测：往目录加一个没人教的能力即红）、既有四道 skill 门 PASS。补齐内容：`wallpaper-content` 增加 10 个组件 kind 的"画/不画×后端"表、图片资产写法与 25 MiB 上限、8 个后处理效果及其成对规则、时间通道；`widget-content` 改正"resize 为 true 可用"与"尺寸可改"两处说法（resize 是惰性字段，产品内无任何入口改变组件尺寸）。事实校正三处：scene.json 的 `videoRenderer` 无渲染器（视频走媒体文件+宿主的视频播放器另一条路）、`asset.font` 无消费方（textRenderer 用系统字体名）、`builtinName:"gradient"` 无实现。真机/作品证据=无。**未做**：每项能力的最小例+组合例、3+3 个真实渲染样例、AI 检索与解释能力评测 —— 后两项需要真实渲染与真实模型调用，本轮不声称。面板状态 🟡。

### CAP-03 — 内容包演进与可复用资产

- **阶段 / 依赖**：S2；CAP-01、P0-09。
- **实施**：建立 schema/capability 版本协商、兼容与迁移；复用资产/效果配方，确定参数引用和包内依赖解析。
- **交付物**：版本矩阵、迁移/回滚、依赖快照、资产去重与预算规则；沿用现有包生命周期。
- **验收**：旧官方/用户 fixture 可加载或给出明确诊断；迁移失败保留旧包；无缺引用、跨包误覆盖。
- **验证环境**：逻辑/打包自动；正式安装升级 Windows 验证。
- **证据记录**（2026-10-03，本轮，负责人=自动推进会话）：目标 SHA=`d02812d6`；实现=`src/tests/ShippedPackagesValidate.cpp`（发行包包级校验回归门）+ `ContentPackageValidator` entry 规则修正；自动检查=`ShippedPackagesValidate` 8/8 通过、`ContentPackageValidatorTest` 131 项通过（两种 entry 布局均通过、五种非法入口均被拒）；变异检测通过（把规则改回只认 `scene/`，门即红）。**发现的缺陷 D-6**：校验器的 entry 规则只认 `scene/scene.json`，而加载器接受根下 `scene.json`，8 个随产品发行的包因此全部过不了包级校验 —— 之所以没被发现，是因为既有 `BuiltinWallpaperPackages` 覆盖的是另一条链，不跑包级校验。真机/作品证据=无。**未做**：schema/capability 版本矩阵与迁移/回滚、资产去重与依赖快照。面板状态 🟡。

### CAP-04 — 统一预览、桌面与渲染证据

- **阶段 / 依赖**：S2；CAP-01、CAP-03、DESK-01。
- **实施**：对齐相同包摘要/参数/种子/输入/时间在 D2D/D3D11 的预览和应用；复用 CCA-08/09。
- **交付物**：目标后端采样、录像/多帧、状态 fixture、性能与错误报告；缺能力显式拒绝。
- **验收**：同输入同后端结果在预定容差内；不把封面当运行证据；device loss/坏素材可恢复。
- **验证环境**：图像/交互/后端集成需要 Windows；纯证据身份测试可本机。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### CAP-05 — 组件数据与动作公共契约

- **阶段 / 依赖**：S2；CAP-01。
- **实施**：定义 Provider 快照/状态/时间戳/缓存/更新计划与 Action 参数/结果/取消/幂等；读与写权限分开。
- **交付物**：可供官方/用户/AI 共用的契约、模拟 Provider/Action、能力查询与错误码。
- **验收**：越权/未知/撤销拒绝，错误可诊断；数据线程不阻塞 UI；不以任意脚本实现动作。
- **验证环境**：纯逻辑/模拟服务可自动；宿主交互在 WPRO 验证。
- **证据记录**（2026-10-03，本轮，负责人=自动推进会话）：目标 SHA=`d02812d6`；实现=`src/include/miaodesk/WidgetDataActionContract.h` + `src/desktop/widgets/WidgetDataActionContract.cpp`（纯逻辑）；自动检查=`WidgetDataActionContractTest` 41 项通过（六种 ProviderState 各自可辨、无时间戳按过期处理、未知动作/缺 operationId/权限撤销/未实现各自有代码、重复 operationId 回放且不二次执行、失败不记入已执行凭据、跨会话记住凭据）；变异检测通过（去掉回放分支即红 4 项）。真机/作品证据=无。**未接线**：三个官方 Provider 尚未改用这份契约（time/weather/tasks 仍是各自的结构），动作注册表尚无生产调用方；接入属 WPRO-03/04。面板状态 🟡。

### WALL-01 — 专业构图与基础绘制原语

- **阶段 / 依赖**：S3；CAP-03、CAP-04；运行时扩展遵守 S1 稳定门。
- **实施**：审计并补图层、锚点、遮罩、混合、裁切、文字/矢量、颜色与素材参数；明确后端差异。
- **交付物**：原语矩阵、跨比例样例、像素/渲染回归、能力卡和 AI 配方。
- **验收**：横屏/超宽/竖屏主体不误裁、透明边正确；预览/桌面一致；组合效果符合资源预算。
- **验证环境**：核心自动检查+Windows 后端/视觉。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WALL-02 — 深度视差与局部动态

- **阶段 / 依赖**：S3；WALL-01。
- **实施**：实现/完善深度图、分层视差、局部水/风/呼吸/扭曲，提供幅度/频率/相位与边缘保护。
- **交付物**：自然风景、人物局部动态两类包、素材准备方法与 AI 配方。
- **验收**：移动/静止指针、边缘、比例变化和循环无裂缝/突跳；效果可调且非纯整体平移。
- **验证环境**：渲染回归+真机交互与视觉评审。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WALL-03 — 动画、状态与有界行为

- **阶段 / 依赖**：S3；WALL-01、CAP-01。
- **实施**：完善动画混合、事件/状态图、确定性随机、平滑调参；明确暂停/恢复时间语义。
- **交付物**：行为契约、动画样例、触发/循环/时间测试与 AI 组合说明。
- **验收**：多效果同步、昼夜切换、重复事件、时钟变化和暂停恢复正确；循环无跳变/无界运行。
- **验证环境**：状态机自动+Windows 动画采样。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WALL-04 — 粒子、多 Pass 与专业特效库

- **阶段 / 依赖**：S3；WALL-01。
- **实施**：核实现有粒子/Render Graph；补所需发射/力/纹理动画与组合后处理，按后端建立预算。
- **交付物**：雨雪/星尘/霓虹/折射参考包、效果参数与资源成本、AI 配方。
- **验收**：多 Pass 确实执行，组合顺序正确；坏资源/超预算可拒绝或显式降档；持续运行无增长。
- **验证环境**：GPU readback/渲染测试+真机性能。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WALL-05 — 音频、指针与媒体响应质量

- **阶段 / 依赖**：S3；DESK-03、DESK-04、DESK-05、WALL-03。
- **实施**：复用已有接线，验证指针所属屏、频谱/节拍/静音、设备切换、Web 推帧；接入受控媒体元数据。
- **交付物**：输入 fixture、恢复矩阵、响应配方和延迟/资源测量。
- **验收**：跨屏不误触发；暂停按策略停止采集；换设备自动恢复；AI 用真实通道；无媒体服务有明确状态。
- **验证环境**：分析/信封可自动；设备、音频和页面 E2E 需 Windows。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WALL-06 — 播放、多屏与电源策略

- **阶段 / 依赖**：S3；P0-03～08、CAP-04、PRO-03。
- **实施**：验证视频循环/格式/同步、Web 生命周期、逐屏/跨屏、质量档、全屏/电池/锁屏暂停与释放。
- **交付物**：播放能力清单、性能档、自动规则与系统生命周期 E2E。
- **验收**：反复切换无错误复活/孤儿；停止释放资源，恢复分配正确；不影响 Widgets/Search。
- **验证环境**：部分 CI+Windows 多屏/电源/休眠。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WALL-07 — 2D 壁纸参考集与 WE 对照

- **阶段 / 依赖**：S3；WALL-01～06、CAP-02、PRO-02/03。
- **实施**：完成 12 个 S3 参考场景，按同机/等价素材/质量设置对照；确保可经公开包创作。
- **交付物**：源包、参数、录像/帧、盲评、测量、差距报告。
- **验收**：总体规划质量门逐类通过；官方特例不得计为公开能力；未通过类保留阻塞。
- **验证环境**：自动渲染/采集+Windows 对标和人工评审。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WPRO-01 — 组件尺寸族与自适应布局

- **阶段 / 依赖**：S4；CAP-03、CAP-05、P0-04；遵守 S1 稳定门。
- **实施**：定义尺寸族、布局约束、长文本/溢出策略、实例配置和切尺寸迁移；保留 Preset 尺寸保护。
- **交付物**：布局原语与参数、至少两尺寸样例、几何迁移测试、AI 规则。
- **验收**：多 DPI/比例下不重叠裁切；切尺寸/跨屏/重启保持数据和位置；非法尺寸被拒。
- **验证环境**：布局逻辑自动+Windows 跨 DPI。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WPRO-02 — Native 组件视觉与控件原语

- **阶段 / 依赖**：S4；WPRO-01。
- **实施**：建设文字/图标/图片/列表/进度/图表/按钮/开关及加载/空/错误/禁用主题规则。
- **交付物**：原语示例包、视觉状态表、参数/能力卡。
- **验收**：长中文、不同字体和主题可读；用户/AI 可用同一原语组合，不依赖官方特例。
- **验证环境**：渲染回归+视觉评审。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WPRO-03 — 动作、命中与拖动交互

- **阶段 / 依赖**：S4；WPRO-01、CAP-05。
- **实施**：实现受控 Action dispatch；划分控件/拖动区，提供忙碌/成功/失败、幂等与恢复。
- **交付物**：待办/计时/媒体等动作样例、交互脚本、AI Action 卡。
- **验收**：点击不会误拖；连续点击/迟到结果/权限撤销正确；数据持久化并能恢复失败前态。
- **验证环境**：动作逻辑自动+真实窗口输入 E2E。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WPRO-04 — 真实 Provider 与数据状态

- **阶段 / 依赖**：S4；CAP-05。
- **实施**：统一现有 time/weather/tasks；接入日历、计时、媒体、照片、系统状态的可支持来源。
- **交付物**：每源的配置/权限/缓存/错误/取消规则、模拟数据与真实连接探针。
- **验收**：不用假数据冒充在线；断网/限流/撤权/坏数据可恢复；来源与新鲜度可见。
- **验证环境**：模拟故障自动；实际源和权限 Windows 验证。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WPRO-05 — 组件更新调度与能耗

- **阶段 / 依赖**：S4；WPRO-04、PRO-03。
- **实施**：建立事件、时间计划、可见性调度与共享缓存；内容重绘和 compositor re-present 分开计数。
- **交付物**：刷新策略/测量、后台资源回收与恢复回归。
- **验收**：无变化不重绘；多个实例不重复轮询；锁屏/休眠后无请求风暴；长稳与预算达标。
- **验证环境**：计数/调度自动+Windows 实机测量。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WPRO-06 — 组件键盘、读屏与系统适配

- **阶段 / 依赖**：S4；WPRO-02、WPRO-03。
- **实施**：完成焦点、Tab/Enter/Esc、可访问角色/名称/状态、高对比度、减少动态效果。
- **交付物**：键盘路径、可访问性树检查、读屏与主题验收记录。
- **验收**：无需鼠标可完成核心任务；读屏能识别动作与结果；焦点不丢、不抢普通应用。
- **验证环境**：自动输入/可访问性检查+人工读屏。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### WPRO-07 — 8 类官方组件与 macOS 对照

- **阶段 / 依赖**：S4；WPRO-01～06、CAP-02。
- **实施**：交付时钟/天气/待办/议程/计时/媒体/照片/系统状态，每类至少两个适用尺寸；整合库/配置体验。
- **交付物**：16 个以上尺寸变体、源包、真实数据/交互记录、对照评分。
- **验收**：固定用户任务完成；状态/DPI/键盘/性能通过；同能力能被 AI 创作复用。
- **验证环境**：自动功能回归+Windows/macOS 任务对照与人工评审。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### AIP-01 — 需求设计与能力规划

- **阶段 / 依赖**：S5；CAP-01、CAP-02；复用 CREATE-01/CCA。
- **实施**：把 brief 转为构图/动效/数据/交互/预算方案，检索真实能力，记录依赖与缺项。
- **交付物**：结构化设计摘要与能力计划、按需检索记录、unsupported 处理。
- **验收**：无虚构能力；信息足够直接制作；缺关键需求才问；对组合任务选出可执行方案。
- **验证环境**：受控模型/fixture 自动；真实 Provider 评测。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### AIP-02 — 专业素材准备与复用

- **阶段 / 依赖**：S5；AIP-01、CAP-03。
- **实施**：复用受控素材工具，准备分层/遮罩/深度/粒子图集，验证尺寸/透明/引用/来源。
- **交付物**：素材清单、生成/导入流程、降级/修复与预算记录。
- **验收**：缺服务或坏素材不伪造成功；产物真实存在；引用可移植；无多余资产无限累积。
- **验证环境**：素材/事务自动+图片 Provider 与视觉验证。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### AIP-03 — 真实运行观察与多模态证据

- **阶段 / 依赖**：S5；AIP-02、CAP-04；复用 CCA-08/09。
- **实施**：连接多时刻/比例/数据 fixture 与交互/性能证据；证明图片真正到达视觉 Provider。
- **交付物**：候选摘要绑定证据、真实请求探针、无视觉/无后端支持的受限模式。
- **验收**：不把路径当模型见图；不用静态封面验动画；坏候选不会污染证据；未覆盖项可见。
- **验证环境**：协议/身份自动+Windows 渲染与真实视觉模型。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### AIP-04 — 专业评审与有界修复

- **阶段 / 依赖**：S5；AIP-03。
- **实施**：按构图/风格/动态/可读性/需求评分，诊断到元素，做局部修复；沿用 CCA 预算与候选模型。
- **交付物**：评审结构、修复优先级、前后证据、失败样本库。
- **验收**：修复带来可测改进；不改主题/破坏正确部分；耗尽预算保留最好有效候选，取消无串扰。
- **验证环境**：状态/预算自动+真实模型与人工盲评。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### AIP-05 — 连续创作、版本与应用恢复

- **阶段 / 依赖**：S5；AIP-04；复用 CREATE-04/06/07/08、CCA-10/11。
- **实施**：验证局部修改、多会话/workspace、重启恢复、候选版本、应用幂等与前态恢复。
- **交付物**：编辑差异、版本/参数记录、恢复入口与故障回归。
- **验收**：颜色/尺寸/数据源修改精准；失败保留上一版；切会话不改错作品；恢复不覆盖无关的新操作。
- **验证环境**：事务/归属自动+真实 UI/重启 E2E。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### AIP-06 — 专业质量、组合与成本放行

- **阶段 / 依赖**：S5；PRO-02/03、AIP-05、WALL-07、WPRO-07。
- **实施**：对保留 10+10 任务各跑 3 次；分开统计成功/结构/质量/续改/能力幻觉/延迟/费用。
- **交付物**：模型/Skill/目录版本锁定的评测报告、原始结果与失败归因。
- **验收**：总体规划阈值逐领域通过；不删除失败样本；每次模型/Skill/能力变更可比较。
- **验证环境**：评测可自动采集；真实 Provider 和人工质量签收。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### AIP-07 — AI 知识与能力发布兼容

- **阶段 / 依赖**：S5；AIP-06、CAP-03。
- **实施**：建立新能力同时更新知识/工具/校验/预览/评测的发布门；支持旧包/旧 Skill 和后端降级。
- **交付物**：兼容矩阵、能力变更说明、回滚/失效策略和随包检查。
- **验收**：新旧组合正确支持或明确拒绝；能力下线不幻觉；知识缺样例/证据时不能标专业可用。
- **验证环境**：自动兼容/打包+目标设备升级。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### ADV-01 — 真实 3D 资源与相机

- **阶段 / 依赖**：S6；WALL-01、WALL-04、CAP-03；S3/S4 稳定后集成。
- **实施**：建立网格导入/资源校验、坐标/相机/透视/深度、基础材质与后端预算，首选可验证的受限格式集。
- **交付物**：格式/能力矩阵、真实模型预览/桌面包、AI 3D 能力卡。
- **验收**：真实几何/遮挡/相机可见，坏模型可拒；不能以空间枚举存在判完成；x64/ARM64 通过。
- **验证环境**：加载/渲染自动+GPU/真机。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### ADV-02 — 专业材质、灯光与环境

- **阶段 / 依赖**：S6；ADV-01。
- **实施**：实现材质贴图/灯光/阴影或明确的质量档、雾/环境效果，补纹理/模型动画支持。
- **交付物**：室内与户外 3D 参考包、参数、资源预算与 AI 配方。
- **验收**：灯光/材质/雾实际影响像素；动态与预览一致；不同 GPU 档有明确能力/质量结果。
- **验证环境**：GPU 渲染与性能+人工视觉。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### ADV-03 — 骨骼、形变与受限物理

- **阶段 / 依赖**：S6；ADV-02、WALL-03。
- **实施**：实现网格/骨骼或等效形变表达、约束/弹性等受限物理；确定步长、配额、暂停恢复。
- **交付物**：角色动态与交互形变作品、参数控制、AI 创作样例。
- **验收**：连续交互稳定，无爆炸/无界计算；暂停恢复正确；作者无需修改宿主 C++。
- **验证环境**：模拟/动画自动+长期交互/视觉。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### ADV-04 — 可编程视觉与故障隔离

- **阶段 / 依赖**：S6；WALL-04、CAP-04、AIP-03。
- **实施**：先验证隔离/超时/资源上限/device loss/fallback，再建设受限 AI Shader 或等效程序化创作通道。
- **交付物**：隔离 ADR、ABI/能力模型、编译/诊断、受控 authoring 工具、Skill/安全契约同步、四类高级作品。
- **验收**：坏 Shader/资源/进程不拖垮桌面；不扩张系统权限；AI 能生成和修复真实效果；必要宿主布局变更同步所有权/打包契约。
- **验证环境**：故障注入自动+Windows GPU/恢复；开放前须完整门禁。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### PRO-04 — 完整桌面组合与一体创作

- **阶段 / 依赖**：S7；S3、S4、S5；高级组合依赖 S6。
- **实施**：承接 FUT-01/02，定义 Wallpaper+Widgets+布局+显示器分配的版本化 Profile，支持整桌预览/显式应用/恢复。
- **交付物**：Profile schema、一次创作/继续修改、事务与兼容、参考桌面。
- **验收**：部分失败不留下半套桌面；切显示器/重启保持；AI 续改不丢未指定内容。
- **验证环境**：事务自动+多屏桌面 E2E。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### PRO-05 — 专业版兼容、长稳与发布证据

- **阶段 / 依赖**：S7；PRO-04、S6、原 REL 门。
- **实施**：运行旧数据迁移、8 小时长稳、完整真机矩阵；汇总同 SHA CI、能力/作品/质量/性能证据。
- **交付物**：单一专业版候选报告，含渠道能力（尤其 MSIX 文件搜索）、限制、原始数据和签收。
- **验收**：无范围内 P0；每条必达能力都有签收；不能混 SHA/机器/作品摘要；升级回滚安全。
- **验证环境**：CI 自动汇总+实机与人工签收。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### PRO-06 — 作者资料、交接与专业目标签收

- **阶段 / 依赖**：S7；PRO-05。
- **实施**：整理作者参考、配方/示例、用户创作指导、能力版本与迁移说明，逐项目标核验。
- **交付物**：可离线随包使用的知识资料、最终对标矩阵、发布说明与维护责任。
- **验收**：第三位开发者能复现样例/评测和领取后续项；只有全部专业门通过才声明总体目标完成。
- **验证环境**：文档/包检查自动；产品所有者最终效果签收。
- **证据记录**：待领取；负责人、目标 SHA、实现提交、自动检查、真机/作品证据、限制、下一步均待回填。

### 每轮交接模板

```text
任务 ID / 负责人 / 日期：
目标 SHA / 实现提交 / 能力与 Skill 版本：
本轮完成的用户路径与交付物：
自动检查（命令、结果、日志）：
Windows/作品/人工证据（设备、包摘要、链接）：
未通过或未执行项 / 阻塞条件：
面板新状态 / 下一候选及依赖：
CHANGELOG（规划变化）/ FEATURE_CHANGELOG（实际行为变化）：
```

小任务只填与其相关的证据，不堆积无意义源码字符串测试；涉及真实状态、竞态、资源或交互的变化必须保护真实用户路径。规格调整与状态变化同轮更新，历史证据不覆盖删除。
