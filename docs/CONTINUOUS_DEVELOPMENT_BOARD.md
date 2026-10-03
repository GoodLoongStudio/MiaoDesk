# MiaoDesk 持续开发面板

- 状态：**当前唯一执行队列**
- 建立：2026-10-03
- 基线 SHA：`0b0e986cfed979057f8d16e26233b798391a50f0`
- 上游：`PRODUCT_VISION.md` → `DESIGN_BASELINE.md` → `DEVELOPMENT_ROADMAP.md`
- 详细验收与历史证据：`TODO.md`
- 用户可感知功能变化：`FEATURE_CHANGELOG.md`

## 1. 总目标

MiaoDesk 的目标不是堆出最多功能，而是成为一个用户愿意每天开机后一直运行的**漂亮、稳定、智能的 Windows 桌面**。

用户主要感知三个界面：

1. 顶部搜索框：应用搜索、文件搜索、进入妙喵 AI；
2. 动态桌面：Wallpaper、Widgets、Content Framework 与 AI 创作内容；
3. 妙喵 AI / DeepSeek Harness：持续助手与专业工作台。

当前开发策略从“继续加功能”切换为：

```text
40% 稳定性 / 防回退
25% 产品体验
15% AI / Creator
10% 性能
10% 新能力
```

## 2. 执行规则

自动或人工推进都遵守以下规则：

- 每轮只领取**最高优先级、依赖已满足、可在当前环境验证**的任务。
- P0 未稳定前，不为了新能力绕过已有稳定性门。
- “代码存在”不等于完成；完成必须满足该任务的验收条件。
- 需要真实 Windows、多显示器、DPI、休眠/Explorer 等物理环境的任务，不允许用 CI 代签。
- 修复真实回退时，必须补能覆盖**用户真实路径**的回归门；只测内部函数不算关闭回退。
- 用户可感知变化必须同一次提交更新 `FEATURE_CHANGELOG.md`。
- 每轮推进结束都更新本面板：状态、证据、阻塞原因、下一候选任务。
- 自动推进遇到需要产品决策、破坏性迁移、凭据、签名、商店提交或人工视觉判断时停止该项，记录阻塞并领取下一项安全任务。
- 不自动删除用户数据、不自动改变发布渠道、不自动提升版本号或标记 RC。

状态：

- ✅ Done：自动检查与所需真机验收均完成
- 🟡 In progress：正在实现或验证
- 🟠 Needs device：代码/CI 已具备，但必须真机签收
- ⛔ Blocked：有明确外部依赖或产品决策
- ⬜ Ready：可直接领取

## 3. 当前能力快照

| 领域 | 当前判断 | 说明 |
| --- | --- | --- |
| Windows 桌面底座 | 约 90% | 三正式 EXE、Shell host、安装/运行结构已成型 |
| Wallpaper | 约 80% | Scene/Image/Video/Web 主链存在，真实生命周期验收仍不足 |
| Widgets | 约 85% | Native Host、三内置组件、Content Widget、PaintReady 已有 |
| Search | 约 80% | App + Goz 文件搜索已通，排序与异常恢复还需数据化 |
| 妙喵 AI | 约 80% | Pi/Provider/多会话/持久化已成型，交互与恢复继续打磨 |
| AI Content Creator | 约 75% | 壁纸/组件生成、预览、应用主链存在，质量与 E2E 需系统验收 |
| Content Framework | 约 80% | Definition/Instance/Package/Parameter/Scene 主体已落地 |
| Scene Runtime | 约 65% | 2D 主链较完整，输入/音频/沙箱/更深 GPU 能力未完全闭环 |
| x64 / ARM64 工程链 | 约 90% | Build/Package/Fast Dev 已建立 |
| 真机稳定性与长期运行 | 约 55% | 当前最大短板 |
| 性能基线 | 约 45% | 工具已有，固定参考机数据不足 |

这些百分比是项目管理估计，不是自动测试结果；任务完成仍以本面板的逐项验收为准。

## 4. P0 — 核心稳定性与防回退

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| P0-01 | ✅ | AI Creator 真实跨进程打开门禁 | 是 | x64/ARM64 均从第二进程请求 Wallpaper/Widget Creator，确认真实窗口 Visible/Ready 后才成功 |
| P0-02 | ✅ | ARM64 Quick Test 启动准确 Dev Host | 是 | 快测清理旧单实例并确认驻留 EXE 来自 `C:\MiaoDeskDev` |
| P0-03 | ⬜ | Wallpaper 20 次启用/停用/reload 循环 | 是+真机 | 无错误复活、重复 Surface、Widget 误停用 |
| P0-04 | ⬜ | Widget 20 次创建/启停/删除循环 | 是+真机 | 无孤立 HWND、位置丢失、重复实例、错误背景 |
| P0-05 | ⬜ | Explorer restart 恢复 E2E | 部分 | Wallpaper/Widget/层级/交互恢复，至少重复 3 次 |
| P0-06 | 🟠 | 锁屏/解锁、休眠/恢复 | 否 | 状态、显示器分配与交互恢复，至少各 3 次 |
| P0-07 | ⬜ | App 重启状态一致性 | 是 | Wallpaper、Widgets、AI 当前会话、API profile、库状态一致恢复 |
| P0-08 | ⬜ | 崩溃/强杀后的孤儿进程与窗口清理 | 是 | 无永久 Node/WebView2/Wallpaper/Harness 孤儿，无不可恢复单实例锁 |
| P0-09 | ⬜ | 用户数据升级/迁移安全 | 是 | 旧配置升级不丢 API profile、内容库、会话、组件布局 |
| P0-10 | ⬜ | 同 SHA 发布门 | 是 | x64 Build/Package/MSIX、ARM64 Package、Repo Hygiene 必须绑定同一完整 SHA |

## 5. P0/P1 — 布局、输入与视觉

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| UX-01 | 🟠 | DPI / 分辨率矩阵 | 否 | 1366×768、1080p、1440p/4K；100/150/200%；主要窗口均可达 |
| UX-02 | 🟠 | 双屏/竖屏/跨 DPI | 否 | 窗口落点正确、无屏外、组件不裁切 |
| UX-03 | 🟠 | 中文 IME / 键盘 / 焦点 | 否 | Tab/Shift+Tab/Enter/Esc/微软拼音在 Search/AI/Settings/Creator 一致 |
| UX-04 | ⬜ | 玻璃视觉统一 | 是+真机 | Search、AI、Settings、库、Creator 的间距/字号/圆角/状态一致 |
| UX-05 | ⬜ | 空状态/错误状态/加载状态统一 | 是 | 每个可点击动作都有成功、进行中、失败反馈，无“点了没反应” |
| UX-06 | ⬜ | Library 响应式布局 | 是+真机 | 小窗口、高 DPI、长分类名下不重叠/截断 |
| UX-07 | ⬜ | Creator 连续体验 | 是 | 打开即显示、历史/新对话/继续修改自然，不因后台加载阻塞窗口出现 |

## 6. P1 — Search

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| SEARCH-01 | ⬜ | 固定 30+ 查询基准集 | 是 | 全名/简称/中文/大小写/空格/同名文件等样本入仓 |
| SEARCH-02 | ⬜ | 排序与去重质量 | 是 | 固定样本的 Top-3 有可复现基线，主要误命中有回归测试 |
| SEARCH-03 | ⬜ | 异步搜索取消与输入响应 | 是 | 快速输入不展示过期结果，无明显 UI 卡顿 |
| SEARCH-04 | ⬜ | Goz 服务故障自动恢复 | 是 | 服务未就绪/退出后可诊断并恢复，不丢 App Search |
| SEARCH-05 | ⬜ | Search → AI 连续上下文 | 是 | 搜索无结果或主动转 AI 时，原查询自然成为对话上下文 |

## 7. P1 — 妙喵 AI

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| AI-01 | ✅ | 多会话持久化 | 是 | 主 AI / Wallpaper / Widget 默认续聊，可新建、切回，跨重启保存 |
| AI-02 | ⬜ | 会话标题与历史管理 | 是 | 自动标题、最近排序、重命名/删除规则清晰，不误删当前上下文 |
| AI-03 | ⬜ | Cancel / Retry 幂等 | 是 | 连续取消/重试不重复提交、不串会话、不留错误 Busy 状态 |
| AI-04 | ⬜ | Agent 活动反馈 | 是 | 工具调用、等待模型、生成、校验阶段用户可理解 |
| AI-05 | ⬜ | API profile 热切换语义 | 是 | 当前任务不静默换 Provider；下一轮明确使用新配置 |
| AI-06 | ⬜ | 连接/鉴权/限流/模型错误分类 | 是 | 用户能知道改字段还是稍后重试，且任何错误不泄露 Key |
| AI-07 | ⬜ | 附件/文件上下文 | 是 | 文件加入会话有明确范围、大小与隐私边界 |
| AI-08 | ⬜ | 对话长上下文压缩策略 | 是 | 长会话不会无限增长；摘要后继续问旧信息仍保持关键上下文 |

## 8. P1 — AI Content Creator

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| CREATE-01 | ✅ | Creator 窗口真实 IPC E2E | 是 | ACK 不等于成功；必须确认 Visible/Ready |
| CREATE-02 | ⬜ | 10 条 Wallpaper 固定生成集 | 是+真机 | 统计生成成功、校验、预览、需求符合度与延迟 |
| CREATE-03 | ⬜ | 10 条 Widget 固定生成集 | 是+真机 | 同上 |
| CREATE-04 | ⬜ | 上一可用结果保留 | 是 | 新一轮生成/修复失败不覆盖上一份可预览结果 |
| CREATE-05 | ⬜ | Preview / Apply 一致性 | 是+真机 | 同包同参数下预览与桌面主要视觉一致 |
| CREATE-06 | ⬜ | 连续修改语义 | 是 | “再小一点/换颜色/沿用上一版”修改正确 workspace，不新建错误作品 |
| CREATE-07 | ⬜ | Creator 重启恢复 | 是 | 聊天、当前 workspace、最近预览、生成状态可恢复 |
| CREATE-08 | ⬜ | Apply 失败回滚 | 是 | 应用失败保留原桌面；成功后可恢复应用前内容 |
| CREATE-09 | ⬜ | 生成质量评分与失败样本库 | 是+人工 | 每次模型/Skill 改动可与固定基线比较 |

## 9. P1 — Desktop / Content / Scene

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| DESK-01 | ⬜ | 官方 Wallpaper Content Framework dogfood 闭环 | 是+真机 | 至少一份官方 Wallpaper 不走 legacy_entry，完整通过新 Scene Runtime |
| DESK-02 | ⬜ | 三官方 Widget 数据刷新策略验证 | 是+真机 | Clock/Weather/Tasks 不做无意义内容重绘 |
| DESK-03 | ⬜ | Scene Pointer Input 主机接线 | 是+真机 | pointer position/inside 与 click-through 契约一致 |
| DESK-04 | ⬜ | Scene Audio Input / WASAPI 主机接线 | 是+真机 | 音频帧进入 Input Bus，暂停/设备切换可恢复 |
| DESK-05 | ⬜ | Web Wallpaper 音频桥真实推帧 | 是+真机 | 已有 JS API 真正收到宿主频谱数据 |
| DESK-06 | ⬜ | Scene 故障隔离与 fallback | 是 | 坏包/坏资源/渲染失败不拖垮桌面其它内容 |

## 10. P1 — 性能与长期运行

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| PERF-01 | 🟠 | 固定参考机性能基线 | 否 | desktop-only / wallpaper / widgets-3 / ai-idle，30 秒×3，记录 avg/p95/peak |
| PERF-02 | ⬜ | 无效刷新热点优化 | 是 | 只优化实际测得热点，提供优化前后数据 |
| PERF-03 | 🟠 | 2 小时长时运行 | 否 | 无未解释的内存/句柄/线程单调增长 |
| PERF-04 | ⬜ | 30 次窗口/预览/Creator 开关压力 | 是+真机 | 资源回收稳定，无越来越慢 |
| PERF-05 | ⬜ | WebView2 / Node / Pi 生命周期 | 是 | 仅在需要时存在，退出/切换后符合保留策略 |

## 11. P1/P2 — Harness 与发布

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| HAR-01 | ⬜ | Harness 冷启动/重复打开/重连 | 是 | 无重复服务、永久空白页；失败可重试 |
| HAR-02 | ⬜ | Harness 与桌面故障隔离 | 是+真机 | Harness 故障不影响 Search/Wallpaper/Widget |
| REL-01 | 🟠 | 干净 x64 安装/升级/卸载 | 否 | 用户配置与内容按策略保留，产品进程无异常残留 |
| REL-02 | ⬜ | ARM64 正式包同 SHA 验证 | 是 | 正式 Package 与 Fast Dev 分离且都可追溯 |
| REL-03 | ⛔ | MSIX 文件搜索服务策略 | 决策 | 明确 Store/MSIX 是否提供 gozd 服务；实现或写入已知限制 |
| REL-04 | ⬜ | RC 自动证据汇总 | 是 | 同 SHA CI + artifact + 真机签收汇总成单一报告 |
| REL-05 | 🟠 | Microsoft Store 最终签收 | 否 | 包、隐私说明、图标、版本、安装行为一致 |

## 12. P2 — 下一阶段差异化

这些任务不应抢占 P0/P1 稳定性：

| ID | 状态 | 任务 | 完成标准 |
| --- | --- | --- | --- |
| FUT-01 | ⬜ | Whole Desktop Profile | Wallpaper + Widgets + Layout + Monitor Assignment 一次预览/应用 |
| FUT-02 | ⬜ | AI 一句话生成完整桌面 | Creator 可生成 Desktop Profile，并继续对话修改 |
| FUT-03 | ⬜ | 更深 Scene GPU 能力 | 按 `MIAO_SCENE_ENGINE_ROADMAP.md` 继续推进，不复活旧大型 Editor |
| FUT-04 | ⬜ | 本地 AI 模型路由 | 独立扩展，不阻塞主产品稳定性与 RC |

## 13. 自动持续推进协议

自动开发循环每次执行：

```text
读取本面板 + main 最新 SHA
        ↓
确认没有未结束/失败的关键 CI
        ↓
选择最高优先级 Ready 且可自动验证的任务
        ↓
先读现有实现/测试/契约
        ↓
实现一个可审查的小步
        ↓
补真实路径回归测试
        ↓
提交 main
        ↓
等待相关 CI
        ↓
失败则修到绿；无法自动修则记录 Blocked
        ↓
更新 FEATURE_CHANGELOG + 本面板
        ↓
结束本轮
```

自动推进**不得**：

- 把“需要真机”的任务直接标 Done；
- 绕过失败 CI；
- 为追求通过删除有效测试；
- 在没有证据时重构核心所有权；
- 自动发布 Store、自动改版本号、自动删除用户/生产数据；
- 同时开多个高风险架构改动。

### 自动任务优先级

当前第一批自动候选：

1. `P0-03` Wallpaper 状态循环的自动回归部分；
2. `P0-04` Widget 生命周期循环的自动回归部分；
3. `P0-07` App 重启状态一致性；
4. `P0-08` 崩溃/强杀资源清理；
5. `SEARCH-01` 固定查询集；
6. `AI-03` Cancel / Retry；
7. `CREATE-04` 上一可用结果保留。

遇到需要物理设备的环节，保留 `🟠 Needs device`，继续领取下一项可自动执行任务。
