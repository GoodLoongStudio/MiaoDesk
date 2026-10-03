# macOS 组件能力对标与 MiaoDesk 建设基准

- 更新：2026-10-03；外部事实来自 Apple 官方文档，本次未做两平台实机对照。
- 目标与阶段：[专业桌面总体规划](PROFESSIONAL_DESKTOP_PLAN.md) S4。
- 状态统一在 [持续开发面板](CONTINUOUS_DEVELOPMENT_BOARD.md)，任务规格为 TODO 的 WPRO-01～07。

## 1. 官方依据

Apple 用户文档描述桌面/通知中心组件的浏览、搜索、添加、拖动、配置、切换尺寸和移除。MiaoDesk 对标这些日常能力，入口与布局按 Windows 适配。[添加和自定义组件](https://support.apple.com/en-gb/guide/mac-help/mchl52be5da5/mac)

WidgetKit 支持交互组件，按钮与开关可通过 App Intents 执行动作。MiaoDesk 对应建设 Native 控件和受控动作服务。[组件交互](https://developer.apple.com/documentation/widgetkit/adding-interactivity-to-widgets-and-live-activities)

WidgetKit 使用时间线和更新策略管理内容刷新；设计指南强调尺寸、信息密度与环境适配。MiaoDesk 对应建设更新计划、事件/缓存、尺寸族和视觉状态。[更新机制](https://developer.apple.com/documentation/widgetkit/keeping-a-widget-up-to-date)、[组件设计指南](https://developer.apple.com/design/human-interface-guidelines/widgets)

以下是 MiaoDesk 自身要求，不表示逐项复制 Apple API。iPhone 接力和 Apple 私有服务不纳入本轮；外观可以有自己的风格，信息可读性、直接操作、个性化与节能必须达到同等成熟度。

## 2. 能力矩阵

“已有基础”只说明代码入口，完整验收按任务证据确认。

| 能力 | 当前基础 | 专业版交付 | 任务 |
| --- | --- | --- | --- |
| 发现/管理 | WallpaperLibraryWindowV2、ContentPackageManagerDialog | 搜索、分类、预览、添加/移除、多实例；高 DPI/小窗口可操作 | UX-06、WPRO-07 |
| 尺寸/布局 | ContentGeometryPolicy、位置持久化、Preset 保护 | 尺寸族、布局约束、文本/列表溢出规则；切尺寸保留配置 | WPRO-01 |
| 信息表现 | Scene 文本/图像、三官方组件、D2D | 图标、列表、进度、简单图表、主题、完整数据状态 | WPRO-02 |
| 直接交互 | 待办编辑/存储、Native Host | 按钮/开关/动作状态；点击不误拖，重复点击/失败可恢复 | WPRO-03 |
| 数据服务 | time/weather/tasks 与 Capability Broker | 异步 Provider、缓存、时间戳、取消、权限撤销 | WPRO-04 |
| 刷新/节能 | Weather service 与已有刷新策略 | 事件/更新计划/可见性驱动，无变化不重绘 | WPRO-05 |
| 可访问性 | Native 窗口与部分输入 | 键盘、读屏角色/名称/状态、高对比度、减少动态效果 | WPRO-06 |
| 个性化 | 参数与 ContentWidgetSettingsDialog | 城市/时区/来源/主题，实时预览，实例独立 | WPRO-01/07 |
| 参考套件 | 时钟、天气、待办内容包 | 8 类、每类至少两个适用尺寸；官方与 AI 使用同样能力 | WPRO-07 |

代码入口：`src/desktop/widgets/`、`src/content/binding/`、`src/content/model/`、`src/ui/wallpaper/`。拟新增的 Provider/Action 不能仅凭名称对 AI 宣称可用。

### 2.1 本机核对到的措辞校正（2026-10-03）

矩阵里"当前基础"一栏是从代码入口推出来的，容易把**存在字段**读成**存在能力**。逐条核对后更正：

| 矩阵原措辞 | 核对结论 |
| --- | --- |
| 尺寸/布局：`ContentGeometryPolicy`、位置持久化、Preset 保护 | 一个包只有**一档固定尺寸**，没有 small/medium/large 尺寸族，也没有"切尺寸"入口。`geometry.resize` 是**惰性字段**：写 `true` 校验得过，但拖动只改 x/y，通用 `WidgetService::Update` 虽能带宽高，却没有任何调用方那么做 —— 所以用户改不了任何组件的尺寸。 |
| 直接交互：待办编辑/存储、Native Host | 待办只能在**管理界面**编辑并完成。Widget Surface 上只有拖动：无点击动作、无完成待办、无计时、无媒体控制、无右键菜单、无键盘输入。`DesktopControlService` 的完成待办不由组件触发。 |
| 数据服务：time/weather/tasks 与 Capability Broker | 三个数据源是三个**互不相同**的快照结构，没有统一 Provider 接口、没有时间戳、没有显式 loading 态、没有取消。broker 只认这三个前缀，且不处理权限撤销。 |
| 信息表现：Scene 文本/图像、三官方组件、D2D | 三个官方内容组件的确用真实数据（时钟走 `GetLocalTime`，天气走 Open-Meteo + 缓存 + 90s 重试，待办走 `TodayTaskStore`）。但 D3D11 上 `textRenderer` **不画**，而 D3D11 是唯一有粒子与后处理的后端。 |
| 可访问性：Native 窗口与部分输入 | 组件窗口是 `WS_EX_NOACTIVATE`，结构上不能取得焦点；无 UI Automation、无 `WM_GETOBJECT`、无读屏语义。 |
| 个性化：参数与 ContentWidgetSettingsDialog | 内容包支持多实例且参数按实例存；原生 preset 按显示器刻意单例。城市/时区/来源可配置只对天气成立。 |

完整的五态（可声明/可运行/可预览/AI 可教学/真机已验）见 [能力与证据台账](CAPABILITY_EVIDENCE_LEDGER.md)；
机器可查的版本见能力目录（`src/content/binding/MiaoCapabilityCatalog.cpp`）。

## 3. 固定用户任务

1. 添加两个城市的天气，切尺寸/主题，重启后分别保留配置。
2. 直接完成待办并持久化；重复点击和失败不产生重复动作。
3. 启动/暂停专注计时，锁屏/休眠后符合所选计时语义。
4. 浏览议程并打开详情；时区、过期数据、空日程有正确状态。
5. 控制媒体播放；服务不存在或会话切换时有反馈。
6. 浏览用户选定照片；缺文件、损坏图片和权限撤销可恢复。
7. 显示真实系统状态，不可用时明确标识，后台刷新符合预算。
8. 用键盘完成添加、配置、操作与移除，读屏能理解控件和状态。

覆盖双屏、混合 DPI、长中文及加载/空/错误/离线状态。比较完成率、误操作、步骤、延迟和可读性；跨平台不以绝对 CPU/GPU 数字判断优劣。

## 4. 交付门

每种控件有布局/键盘/命中/禁用/忙碌/错误语义，每个 Provider 有来源与更新策略，每个 Action 有结果和恢复。Native 原语、包表达、AI 能力卡、参考包、自动交互与真机签收齐备才能完成。

真实数据、实用交互、后台成本与实例生命周期是硬门。详细评分与样本量统一见总体规划第 7 节。
