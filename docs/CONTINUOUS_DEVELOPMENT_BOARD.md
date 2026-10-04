# MiaoDesk 持续开发面板

- 状态：**当前唯一执行队列**
- 建立：2026-10-03
- 代码核对基线 SHA：`d02812d63a2dbf480c4cb00faffb4f69a38b3bc4`；最新代码提交 `60bf938d`（本机 44 目标全通过，exit 0；21 道仓库门 + mingw 语法门全通过；同 SHA CI 在跑）。`a85efd5c`（CAP-03）的 CI 已 **8/8 全 success**。
- 专业版规划：[PROFESSIONAL_DESKTOP_PLAN.md](PROFESSIONAL_DESKTOP_PLAN.md)；更新：2026-10-03
- 上游：`PRODUCT_VISION.md` → `DESIGN_BASELINE.md` → `DEVELOPMENT_ROADMAP.md`
- 详细验收与历史证据：`TODO.md`
- 用户可感知功能变化：`FEATURE_CHANGELOG.md`

## 1. 总目标

MiaoDesk 的目标不是堆出最多功能，而是成为一个用户愿意每天开机后一直运行的**漂亮、稳定、智能的 Windows 桌面**。

用户主要感知三个界面：

1. 顶部搜索框：应用搜索、文件搜索、进入妙喵 AI；
2. 动态桌面：Wallpaper、Widgets、Content Framework 与 AI 创作内容；
3. 妙喵 AI / DeepSeek Harness：持续助手与专业工作台。

新增必达目标：**Wallpaper Engine 级壁纸、macOS 级组件、专业 AI 创作**。底层能力与 AI 知识/样例/评测同步交付。完整目标与 S0～S7 出口见总体规划；本面板是唯一执行队列。

S1 基础版稳定性收口期间采用以下投入参考；S1 后按专业能力依赖推进，不把新能力永久限制在 10%：

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
- 用户可感知行为变化同一次提交更新 `FEATURE_CHANGELOG.md`；规划/范围/门槛变化写根 `CHANGELOG.md`，同步愿景、基线、路线和任务规格。
- 新能力必须同时有 AI 能力卡/样例/真实预览与质量证据；不以底层代码存在判定 Creator 已会使用。
- 每轮推进结束都更新本面板：状态、证据、阻塞原因、下一候选任务。
- 自动推进遇到需要产品决策、破坏性迁移、凭据、签名、商店提交或人工视觉判断时停止该项，记录阻塞并领取下一项安全任务。
- 不自动删除用户数据、不自动改变发布渠道、不自动提升版本号或标记 RC。

状态：

- ✅ Done：自动检查与所需真机验收均完成
- 🟡 In progress：正在实现或验证
- 🟠 Needs device：代码/CI 已具备，但必须真机签收
- ⛔ Blocked：有明确外部依赖或产品决策
- ⬜ Ready：可直接领取
- ⏳ Planned：已规划，依赖/阶段门尚未满足；满足后改 Ready
- 🔎 Verify：实现已有，需核实集成/自动证据，之后按结果转 Needs device 或 Done

## 3. 旧基础范围能力快照（不可作为专业版完成率）

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

这些百分比是目标扩展前的管理估计，不是测试结果。专业版不沿用这些分母：按 S0～S7 出口和第 14 节能力任务计量，PRO-01 建立基线后逐项补证据。

## 4. P0 — 核心稳定性与防回退

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| P0-01 | ✅ | AI Creator 真实跨进程打开门禁 | 是 | x64/ARM64 均从第二进程请求 Wallpaper/Widget Creator，确认真实窗口 Visible/Ready 后才成功 |
| P0-02 | ✅ | ARM64 Quick Test 启动准确 Dev Host | 是 | 快测清理旧单实例并确认驻留 EXE 来自 `C:\MiaoDeskDev` |
| P0-03 | ⬜ | Wallpaper 20 次启用/停用/reload 循环 | 是+真机 | 无错误复活、重复 Surface、Widget 误停用 |
| P0-04 | 🟡 | Widget 20 次创建/启停/删除循环 | 是+真机 | 无孤立 HWND、位置丢失、重复实例、错误背景。**自动部分的几何不变式与去重规则已落地**（见下）；孤立 HWND/错误背景仍要 Windows |
| P0-05 | ⬜ | Explorer restart 恢复 E2E | 部分 | Wallpaper/Widget/层级/交互恢复，至少重复 3 次 |
| P0-06 | 🟠 | 锁屏/解锁、休眠/恢复 | 否 | 状态、显示器分配与交互恢复，至少各 3 次 |
| P0-07 | 🟡 | App 重启状态一致性 | 是 | Wallpaper、Widgets、AI 当前会话、API profile、库状态一致恢复。**本机自动部分：请求 URL 与凭据必须同源已落地并修掉一个会外泄 Key 的缺陷**（见下）；四个恢复面的真机一致性仍要 Windows |
| P0-08 | 🟡 | 崩溃/强杀后的孤儿进程与窗口清理 | 是 | 无永久 Node/WebView2/Wallpaper/Harness 孤儿，无不可恢复单实例锁。**本机自动部分：Node 连坐 job + 收尸门 + 锁持有权裁决与 owner 身份记录（WallpaperEntry 已接）**（见下）；另四处启动点、接管动作与真机清理仍要 Windows |
| P0-09 | 🟡 | 用户数据升级/迁移安全 | 是 | 旧配置升级不丢 API profile、内容库、会话、组件布局。**AI 会话的迁移已修**（见下）；API profile / 内容库 / 组件布局三项的升级路径仍待查 |
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
| SEARCH-01 | 🟡 | 固定 30+ 查询基准集 | 是 | 全名/简称/中文/大小写/空格/同名文件等样本入仓；排序已在纯逻辑层入仓并有 22 项断言，真机样本待 Windows |
| SEARCH-02 | ⬜ | 排序与去重质量 | 是 | 固定样本的 Top-3 有可复现基线，主要误命中有回归测试 |
| SEARCH-03 | ⬜ | 异步搜索取消与输入响应 | 是 | 快速输入不展示过期结果，无明显 UI 卡顿 |
| SEARCH-04 | ⬜ | Goz 服务故障自动恢复 | 是 | 服务未就绪/退出后可诊断并恢复，不丢 App Search |
| SEARCH-05 | ⬜ | Search → AI 连续上下文 | 是 | 搜索无结果或主动转 AI 时，原查询自然成为对话上下文 |

## 7. P1 — 妙喵 AI

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| AI-01 | ✅ | 多会话持久化 | 是 | 主 AI / Wallpaper / Widget 默认续聊，可新建、切回，跨重启保存 |
| AI-02 | ⬜ | 会话标题与历史管理 | 是 | 自动标题、最近排序、重命名/删除规则清晰，不误删当前上下文 |
| AI-03 | 🟡 | Cancel / Retry 幂等 | 是 | 连续取消/重试不重复提交、不串会话、不留错误 Busy 状态。**Pi 侧已修：取消不再直接清忙位，「已请求取消但 worker 还没退」仍然是忙已成相位不变量**（见下）；L3 侧本来是对的，但两边的理由此前没人写下来 |
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
| CREATE-04 | 🟡 | 上一可用结果保留 | 是 | 新一轮生成/修复失败不覆盖上一份可预览结果。**自动部分已修：复用 candidateId 不再把上一版带走**（见下）；界面上的实际保留仍等宿主接线 |
| CREATE-05 | ⬜ | Preview / Apply 一致性 | 是+真机 | 同包同参数下预览与桌面主要视觉一致 |
| CREATE-06 | ⬜ | 连续修改语义 | 是 | “再小一点/换颜色/沿用上一版”修改正确 workspace，不新建错误作品 |
| CREATE-07 | ⬜ | Creator 重启恢复 | 是 | 聊天、当前 workspace、最近预览、生成状态可恢复 |
| CREATE-08 | ⬜ | Apply 失败回滚 | 是 | 应用失败保留原桌面；成功后可恢复应用前内容 |
| CREATE-09 | ⬜ | 生成质量评分与失败样本库 | 是+人工 | 每次模型/Skill 改动可与固定基线比较 |

## 9. P1 — Desktop / Content / Scene

| ID | 状态 | 任务 | 自动推进 | 完成标准 |
| --- | --- | --- | --- | --- |
| DESK-01 | 🔎 | 官方 Wallpaper Content Framework dogfood 闭环 | 是+真机 | 三官方包已用 scene.json；补正式运行、视觉、性能与多屏证据 |
| DESK-02 | ⬜ | 三官方 Widget 数据刷新策略验证 | 是+真机 | Clock/Weather/Tasks 不做无意义内容重绘 |
| DESK-03 | 🔎 | Scene Pointer Input 已有接线的归属/运行验收 | 是+真机 | pointer position/inside 与 click-through 契约一致 |
| DESK-04 | 🔎 | Scene Audio / WASAPI 已有接线的生命周期验收 | 是+真机 | 音频帧进入 Input Bus，暂停/设备切换可恢复 |
| DESK-05 | 🔎 | Web Wallpaper 已有推帧的页面/资源验收 | 是+真机 | 已有 JS API 真正收到宿主频谱数据 |
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

## 12. 专业版范围迁移与独立扩展

下列旧条目保留映射，专业版执行状态以第 14 节为准，避免重复计数：

| ID | 状态 | 任务 | 完成标准 |
| --- | --- | --- | --- |
| FUT-01 | → PRO-04 | Whole Desktop Profile | Wallpaper + Widgets + Layout + Monitor Assignment 一次预览/应用 |
| FUT-02 | → PRO-04 | AI 一句话生成完整桌面 | Creator 可生成 Desktop Profile，并继续对话修改 |
| FUT-03 | → WALL / ADV | 更深 Scene GPU 能力 | 按 `MIAO_SCENE_ENGINE_ROADMAP.md` 继续推进，不复活旧大型 Editor |
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

当前先领取 PRO-01 做新范围与已有实现的证据核对；随后按依赖准备 PRO-02/03，并继续以下最高优先级稳定性候选。CAP-01/02 可在真机等待期间推进：

1. `P0-08` 崩溃/强杀资源清理 —— 收尸门已上线,剩下的**不可恢复单实例锁**还没人查:
   现在没有任何地方能回答"这个句柄属于谁、它卡住的时候谁来杀";
2. `P0-07` App 重启状态一致性 —— AI 侧已做一轮（请求 URL 与凭据同源），继续
   Wallpaper/Widgets/AI 会话/库状态四个恢复面的**自动**部分；
3. `P0-03` Wallpaper 状态循环的自动回归部分；
4. `SEARCH-01` 固定查询集；

已完成自动部分（真机仍待）：`P0-04`（几何不变式 + 去重规则）、`P0-07`（凭据同源）、
`P0-08`（子进程收尸门 + Node 连坐 job）、`AI-03`（Pi 侧轮次相位，取消不再清忙位）、
`CREATE-04`（复用 candidateId 不再带走上一版可预览结果）。

遇到需要物理设备的环节，保留 `🟠 Needs device`，继续领取下一项可自动执行任务。

### P0-08 剩下的那一半：单实例锁不可恢复 —— 诊断完成，修复需要产品决策

本轮把 P0-08 的"孤儿进程"那一半做完(子进程收尸门 + Node 连坐 job),剩下的是验收原话的
后半句:**"无不可恢复单实例锁"**。诊断做完了,结论如下。

**结构**：全仓 5 处 `CreateMutexW`,而"该不该再起一个"全部靠同一个判据 ——

    bool NamedMutexExists(const wchar_t* name) {
        HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name);
        if (!mutex) return false;
        CloseHandle(mutex);
        return true;
    }

三处启动点的第一行都是 `if (NamedMutexExists(...)) return true;`：

   * `WallpaperEntry.cpp` 的 `LaunchHelper`;
   * `HarnessHost.cpp` 的 `LaunchBackgroundHarnessOwner`;
   * `WallpaperEngine.cpp:1914` 自己 `CreateMutexW` 建锁的那一段。

**为什么不可恢复**：命名 mutex 只要有**任何一个句柄**开着就存在。`OpenMutexW` 只回答
"它在不在",**不回答"它属于谁、那个进程还活着吗"**。于是:

   * 持有者**死了** —— 内核会关掉它的句柄,mutex 随之消失,这条**本来就能恢复**;
   * 持有者**卡住但没死**(Hung) —— mutex 一直在,`NamedMutexExists` 一直返回 true,
     `LaunchHelper` 一直 `return true`,宿主一直以为 helper 在跑。而**没有任何地方记录
     持有者的 PID**,所以连"该杀谁"都答不出来。用户只能手工开任务管理器。

这正是"不可恢复"的含义,而且它不是假想:helper 是个渲染进程,卡死是它最常见的故障形态。

**修正上一轮的判断**：我当时说"剩下那一半需要产品决策",这个判断切错了地方 ——
把**发现卡住**和**接管卡住**当成了一件事。发现卡住不需要任何决策,它只是把
`OpenMutexW` 的那个布尔换成带理由的裁决;**那一半本轮已经做了**
(`MiaoLockOwnership`,`Wedged` 裁决 + 五处变异,见下一节记录)。
真正需要人拍板的仍然只有接管动作:要不要 `TerminateProcess` 一个还活着的进程、
要不要先试着重启它一次。而它现在被隔在判定之外 —— `MayStartNewOwner(Wedged)`
为假,选择权整个留给宿主。

**下一步(可自动做)**：把"看到锁之后该不该接管"的判定抽成纯逻辑 ——
给定(记录的 PID、现在、心跳时间戳、进程是否存活、租约时长)→ 起新的 / 已经在跑 /
可以接管 / 明确放弃并说出原因。宿主照它的结论走。这一步不含产品决策。
**真正需要人拍板的是**：接管时是否允许 `TerminateProcess`,以及要不要给用户一个提示。

### 本轮推进记录（2026-10-03 续，WALL-03 宿主侧时间策略）

- 代码提交：`bd23cfe6`（前一代码提交 `51d01b07`）；本轮文档 + 代码，未提升版本号。
- 本机跑了什么：40 个纯逻辑目标全通过（exit 0）—— 新增 `MiaoSceneTimelinePolicyTest`
  174 项、`ShippedAnimationContinuity` 48 项，`MiaoSceneRuntimeTest` 新增 6 项共 33 项，
  `ContentSelfTests` 从 9 项增至 11 项（接上两个此前**在整个仓库里没有调用方**的 SelfTest，
  其中帧调度器那条第一次执行就红了 —— 红的不是代码是断言）；mingw 交叉语法门 0 真实错误；
  21 道仓库门全通过；26 处变异全部变红。
- **本轮修正过自己一次结论**：第一版把"动画与 binding 写同一属性"也一并拒绝，理由写的是
  "binding 是死的"；跑到三道产品 SelfTest 夹具上才发现理由不成立（binding 只在 Initialize
  跑一次，它在动画开跑之前是可观测的）。边界改划在"两条动画"，binding 那一侧改为报告 +
  写进 Skill。四处文档已同步改正。
- CI：`7e08e97b` 已推送，8 项在跑。
- 真机：**未取证**。本机不是 Windows —— 动画采样、视觉、桌面行为一律给不出证据。
- 已完成（本机可自动部分）：WALL-03 的宿主侧时间策略（`SceneClock` 暂停恢复 /
  `ParameterSlew` 平滑过渡 / `AuditAnimationContinuity` 循环接缝与一帧瞬移）、
  `Validate` 的"两条动画不得写同一属性"拒绝规则、发行内容 25 条动画轨道的连续性审查。
- **已登记而未修**：`animation://miao-cloud/blink-blink` 的淡变是 1/240 秒，任何帧率下
  渲染不出来。登记而不是静默放过，也不是本机改美术后放行。
- 下一候选（依赖已满足且本机可自动验证）：`CAP-02` 的"每项可创作能力最小例 + 组合例"仍缺示例包；
  `P0-03/P0-04/P0-07/08/09`、`WALL-06`、`PRO-03/05`、CAP-04、WALL-03 的真机动画采样都要 Windows 侧，
  一律留 `🟠`。WALL-03 剩"状态机/行为图、确定性随机独立设施、`SceneClock` 与 `ParameterSlew`
  接进宿主"三项，后一项要等播放宿主那一轮（要先决定暂停由谁调）。
- 阻塞：真机签收需要 Windows x64/ARM64 各一台、显示器/DPI 矩阵、已配置的 Provider 与对标软件。

### P0-07「库状态一致恢复」：一处定位完毕的静默丢行（未修，登记在案）

排查 `WallpaperLibrary` 的恢复路径时定位到一个真实缺陷,形状与本会话修过的几处相同
(静默失败 + 调用方只问成败),但本轮**没有动手修** —— 剩下的上下文不足以把改动做完并跑完
验证,所以先把位置与见证固定下来,而不是留下半截代码。

**位置与见证**:`src/desktop/wallpaper/library/WallpaperLibrary.cpp:283`

    if (!item.id.empty() && item.kind != LibraryWallpaperKind::Unknown) items_.push_back(std::move(item));

`Kind` 缺失或不在本构建认识的字表里时,这一行**什么都不说**就把行丢掉,而 `Load`
照常返回 true。`WallpaperLibrary.h` 上**没有任何**"跳过几行/跳过哪些"的出口。

**为什么这是 P0-07 + P0-09 的缺陷**:
· P0-07 的验收是"库状态一致恢复";这里的实际行为是"恢复成一个**更短的**库,并报告成功"。
· P0-09 的验收是"旧配置升级不丢 … 内容库";一个旧构建(或手改过的 INI)写出本构建
  不认识的 `Kind`,那一行就在下次加载时消失。
· 调用方只问成败:`src/desktop/wallpaper/WallpaperService.cpp:210` 是
  `if (!library.Load(&error))` —— 成功即往下走,于是拿着一个悄悄变短的库继续。

**为什么它不是"本该如此"**:`InferKind` 那条路(同文件 394 行附近)遇到不认识的类型会
`SetError` 明说"不支持的壁纸文件类型",而 `Load` 这条路什么都不说。同一个文件里两种口径。

**修法(含,已做)**:`MiaoLibraryRowFilter`(纯逻辑,本机有门)出判定与那句话,
`WallpaperLibrary::Load` 在同一个分支里把它记进新增的 `SkippedRows()` /
`SkippedRowCount()`,宿主第一次能说出"N 个库项目无法识别,已跳过: …"。
**不改变**"这一行不进库"的行为 —— 那是要动 UI 的决定;先让丢失可见。

顺带把"字段缺失"与"写了本版本不认识的词"分成两种理由:**损坏**与**版本差异**
对用户是两回事(该修配置 vs 该升级)。`Load` 读 Kind 时也改留原文,
不再把"字段缺失"预先塞成字面量 `"unknown"` —— 那会让两者混为一谈。

### 本轮推进记录（2026-10-04 再续十，P0-08 五处启动点接线，其中一处刻意不接）

上一轮只接了壁纸那一处,还剩三处老布尔 + 一处不该接。本轮做完。

- **先把 owner 记录那套提到共享模块** `MiaoLockOwnershipHost`(路径 / 发布 / 退休 / 判健康)。
  理由很直接:同一个"该不该再起一个"有五处启动点,各写一遍就会出现"壁纸那边判得出卡住、
  后台 Harness 那边还是一句 `return true`"这种分裂。合流之后只有一份实现。
- **接上的三处**:
  - `src/app/main.cpp` 的 `LaunchHarnessBackgroundOwner`;
  - `src/harness/HarnessHost.cpp` 的 `LaunchBackgroundHarnessOwner`(与上面那个是两份
    基本相同的实现,这里也仍然是两份 —— 合并不在本轮,但契约已经统一);
  - `src/desktop/wallpaper/legacy/WallpaperEngine.cpp`:这里的问题更值得说。
    它在 `ERROR_ALREADY_EXISTS` 时 `SendExistingCommand(args)` 然后 **`return 0`**。
    而 `SendExistingCommand` 是 `FindWindowW` + `PostMessageW` —— 一个卡住的实例窗口
    **还在**,`FindWindowW` 找得到它,`PostMessageW` 也"成功";消息只是进了那个不再跑
    消息循环的队列。用户点"设置",什么都没有,而我们返回 0 表示一切顺利。
    现在按裁决改退出码:健康 → 0,卡住或判不了 → 5(含义写进注释)。**不起第二个、不杀进程,
    只是不再谎报转发成功。**
- **`DesktopWidgetStore` 刻意不接**:它那个锁是**存储锁**,不是进程单例 ——
  5 秒 `WaitForSingleObject` 超时,并且已经把 `WAIT_ABANDONED` 当成"拿到了"
  (进程握着锁死掉时内核会这么通知,这是 Windows 自己给的恢复路径)。
  也就是说"别人写一半死了"已经能恢复,而"别人真卡住"则由 5 秒超时 +
  `Desktop widget storage is busy; please retry.` 兜住 —— 那句话是如实的。
  要为它接心跳,就得在每次组件存储访问的热路径上多写一次文件,去回答一个
  超时已经回答了的问题。这是过度设计,记下来免得后人以为漏了。
- 真机:**未取证**。三处接线都要 Windows 上真起一次才敢签收;`WallpaperEngine`
  那条退出码 5 有没有人看,也还没有上游。

### 本轮推进记录（2026-10-04 再续九，P0-08 后半句：锁持有权裁决）

- **上一轮我说"剩下那一半需要产品决策",这个判断切错了地方。** 切错在于我把
  **发现卡住**和**接管卡住**当成了一件事。它们不是:前者只需要把 `OpenMutexW`
  给的那个布尔换成带理由的裁决,不含任何决策;后者才需要决定要不要
  `TerminateProcess` 一个还活着的进程。
- **本轮做前者**：新增 `MiaoLockOwnership`,把"这把锁现在是什么状态"判成五种 ——
  `Available` / `Running` / `Wedged`(活着但心跳过期) / `StaleRecord`(记录的进程不在了) /
  `Unusable`(判不了)。`MayStartNewOwner` 只在**不需要动任何活着进程**的两种为真。
- **为什么这已经是有用的**：`Wedged` 是当前 `OpenMutexW` 结构上判不出来的那一类 ——
  它只回答"锁在不在",不回答"它属于谁、那个进程还活着吗"。有了它,宿主第一次能说出
  "壁纸渲染进程 4242 还活着但已 61 秒没有心跳(租约 60 秒),它卡住了",而不是静默地
  以为一切正常。原因里也写清当前不会自动接管,并告诉用户怎么办。
- **两条不许 smuggle 的边界**:
  - 租约没配时**拒绝裁决**(`Unusable`),不许默默用一个默认租约 —— "多久算失联"
    恰恰是这条链上最该显式决定的一件事;
  - 锁存在但没有身份记录时按不可判处理,**不按"没有持有者"** —— 后者会让人以为这里判过。
- **接管仍然是产品决策,但现在它被隔在判定之外**:`MayStartNewOwner(Wedged)` 为假,
  而"要不要杀"由宿主拿裁决去问用户/问策略。判定本身不含这个选择。
- **本机跑了什么**:`LockOwnershipTest` 27 项(含反空洞自检);**9 处变异全红,0 存活**:
  恒判 Running / 失联边界改成 `>=` / 无符号下减 / 时钟倒退判成 Wedged /
  没配租约也裁决 / 锁存在但无记录判 Available / Wedged 允许起第二个 /
  Unusable 允许起 / pid=0 也算有记录。
- **紧接着把 owner 那一侧也接了**(`WallpaperEntry.cpp`):裁决要有输入才能动,
  而"谁持有、多久没心跳"此前**没有任何地方记录** —— 命名 mutex 不带身份。
  新增 `MiaoLockRecord`(纯编解码,本机有门)把 PID + 心跳落成一份 `.owner` 文本;
  `SingletonGuard` 只有**真的拿到锁**时写它(抢锁失败就写 = 两个 owner 都自称持有者),
  析构时收掉;`LaunchHelper` 里那个 `if (NamedMutexExists(...)) return true;`
  改成先问 `ExistingHelperIsHealthy`:健康的返回 true(别起第二个),**卡住或判不了的返回
  false —— 我们并没有拿到一个能干活的 helper**。"不起第二个"这条没变,变的是不再谎报成功。
- **第一版在这里又犯了一次老毛病,当场删掉**:我先把诊断写进一个
  `g_lastHelperDiagnosis` 全局,准备"留给界面读"。然后 grep 了一遍 ——
  **只有写、没有读**。这正是本会话删过五处的那种死代码:提供一个没人读的出口,
  比不提供更糟(它会让人以为这里接过了)。`WallpaperEntry.cpp` 里根本没有诊断显示面,
  所以现在只返回 bool,不提供字符串出口。真正给人看的那句话,等接上诊断面那一轮再做。
- 两条不许含糊的地方:
  - 锁在但**没有身份记录**时**不判**"没有持有者"(那会让两个 owner 同时上),判 Unusable
    并明确拒绝;
  - 心跳租约是显式常量 `kHelperHeartbeatLeaseSeconds = 30`,不默认 —— 配 0 会让裁决
    变成 Unusable,那是"读到了记录也判不了",比原来那个布尔更糊涂。
- 真机:**未取证**。另外四处启动点还没接;接上之后要在 Windows 上真起一次 helper 才敢签收。
  **接管动作仍然是产品决策** —— 现在诊断会说"它卡住了,请从任务管理器结束它后重试",
  不会自己去杀。

### 本轮推进记录（2026-10-04 再续八，P0-09：升级把用户的 AI 会话变成不可达字节）

- **修的真实缺陷**（P0-09 的"旧配置升级不丢 … 会话"）：用户在 AI 窗口的 API 下拉里
  选一个中央 profile,`SetProfileId(selected.id)` → `ReloadConfig` 会:
  1. 从 `api-profiles.ini` 填 `config_`,`profileId` 从空变成 "main";
  2. **`SessionHash` 里含 `profileId`**,于是会话文件的哈希随之改变;
  3. `if (profile.configured) RetireLegacyShadowState();` 把 `model-settings.json`
     —— 也就是 `L3Agent` 自己那份配置文件、记录"用户旧配置是什么"的唯一地方 —— 删掉。
  旧 `.bin` 还在盘上,但再也没有人能算出它的路径。用户累积的对话变成不可达字节,而
  界面上一个字都不提。`L3PersistenceSelfTest` 完全没覆盖这条路(只有 legacy 一轮、
  profile 一轮,没有"从 legacy 换到中央")。
- **修法**:退休旧文件**之前**,先把会话搬到新身份下。新增
  `MigrateSessionFromLegacyState`,判定落在 `MiaoSessionMigration`(纯逻辑,本机有门):
  只有"服务地址与模型还是同一套"才算来源提升,那时才搬;用户真换了配置时不搬 ——
  新会话是对的,旧会话留在原地。搬完之后把结论写进 `LastSessionMigration()`,
  宿主能说出口(与 `CreationWorkflow::LastRejectionReason` 同一个理由)。
- **顺手把 `SessionHash` 收成一份实现**:L3Agent.cpp 里那份删掉,转一手调
  `session_migration::SessionIdentityHash`。搬运要靠两边算出同一个路径,两处各写一遍时
  改一边不改另一边会静默失败 —— 那比不搬更难查。
- **顺手把哈希的具体数值钉住**:`<StateRoot>/l3-sessions/<hex>.bin` 就是用户的历史对话,
  改哈希常量或改参与字段 = 把每个老用户的会话全部变成不可达字节。这正是本轮修的缺陷的
  反面,所以 `SessionMigrationTest` 钉死两组数值。第一版只钉了"不同/相同"这类关系,
  改常量它照样全绿 —— 又一处"没钉具体值"的洞。
- **门第三次逮到我,而这次它逮对了**：`tests/multi-api-routing.mjs` 里有一条
  `agent.includes("Lower(config.profileId)")`,我为了把哈希收成一份实现,把那行从
  L3Agent.cpp 搬走了,门就红了。**先验不变量再动门**:`SessionMigrationTest`
  现在真的在本机跑,并且断言"profileId 变了哈希就变"(还有两组哈希具体数值),
  比原来那条 grep 更强。确认之后才把门改成断不变量真正所在之处 ——
  共享实现必须用 profileId、L3Agent 必须转一手、必须把 config_.profileId 喂进去、
  纯测试必须真的变 profileId、必须钉哈希值。**五条各自单独变异验过,全红。**
- **本机跑了什么**：`SessionMigrationTest` 23 项(含反空洞自检);
  **8 处变异全红,0 存活**:恒判真换配置 / 恒判来源提升 / 没变也说搬 /
  不归一化末尾斜绳 / 不看模型 / 不看端点 / 哈希少一个字段 / 哈希常量改掉。
- 真机:**未取证**。"选了 profile 之后历史对话还在不在"要 Windows 上真选一次;
  旧文件不存在/读不出来这两条分支也没有真机证据。
- **本轮补上自己代码里的一个洞**:`MigrateSessionFromLegacyState` 第一版在
  "新身份下已有会话"时**直接跳过拷贝、然后退休旧文件** —— 旧会话就此不可达,
  而 `lastSessionMigration_` 一个字都不写。一次无声的丢失,出在我为了防止无声丢失
  而写的代码里。现在这种情况会明说"旧的那份没有被搬过去,哪一份算数需要人工决定"。
- **并且差点又交付一个只写不读的字段**:`LastSessionMigration()` 加完之后
  **zero 调用方**。grep 出来之后才接上 `/status` —— 用户敲一个命令就能看见上次升级
  对会话做了什么。这是本会话第三次差点留下死代码,每次都靠同一个动作救回来:
  写完出口就 grep 它的读者。

### 本轮推进记录（2026-10-04 再续七，CREATE-04：复用 candidateId 带走上一版）

- **修的真实缺陷**：`ValidationFailed` / `EvidenceFailed` / `ReviewCompleted` 都按
  `FindCandidate(pendingCandidateId_)` —— 找到谁就改谁。而候选 ID 是**原样收下模型给的**
  （`CandidateSubmitted` 那条注释写着"candidateId 由宿主重新分配"，实际并没有，只有模型
  留空时才由我们编一个）。于是新一轮复用一个 ID 时，被改的是**上一轮那条已经成功的**记录：

      第 1 轮: cand-1 / digest-1 校验通过        → LastValidCandidate() = digest-1
      第 2 轮: 模型又交出 cand-1(摘要 digest-2),校验失败
               → FindCandidate("cand-1") 命中第 1 轮那条,validated = false
               → LastValidCandidate() = 空

  用户在改需求重新生成之后指着上一版说"就用这个"，而它已经不在了。这就是 CREATE-04 的验收。
  现有测试 `TestPreviousCandidateSurvivesFailure` 之所以是绿的，只因为它给第二轮用了不同 ID。
- **修法**：`pendingDigest_` 一起记，内部查找换成 `FindPendingCandidate()` ——
  **ID 与摘要都对上才认**。新一轮的失败找不到自己要改的那条，什么都不动。
- **本机跑了什么**：`CreationWorkflowStateTest` 174 项通过（新增正向一条 + 反向一条：
  ID 与摘要都相同就是同一条记录，它失败时上一版确实失效）；变异检测把两个重载都退回
  "只按 ID" → **红**，报的就是 CREATE-04 那两条。
- **一个值得记下的坑**：第一次变异只改了其中一个重载，测试**仍然全绿** —— 因为
  `Apply` 走的是另一个重载。一个没打到执行路径上的"破坏"看起来和"测试没覆盖"一模一样。
  变异检测的价值正在于此。
- 真机:**未取证**。这条链路目前还没有宿主在发 `CandidateSubmitted`,所以"界面上那一版
  还在不在"要等接线之后才能签收。

### 本轮推进记录（2026-10-04 再续六，AI-03：取消不是完成）

- **修的真实缺陷**（AI-03 的"不留错误 Busy 状态 / 不串会话"）：
  同一个仓库里有两个 AI 运行时，而它们对"取消"的处理**不一样**：

      L3Agent::AskAsync   Stop(); if (worker_.joinable()) worker_.join();  busy_ = true;
                          ↑ 等旧轮次真的结束，才起新一轮
      PiRuntime::Stop()   request_stop(); WriteLine(abort); busy_.store(false);
                          ↑ worker 还在跑，忙位已经清了

  于是"取消 + 立刻重试"这条路在 Pi 上是活的：`busy_` 已经是 false，
  `AskAsync` 的 `busy_.exchange(true)` 放行，第二轮起来了 —— 与还没退完的第一轮
  共用同一个 Node 进程、同一根 stdin/stdout 管子。第一轮没读完的 delta 会递进
  第二轮的 onDelta 里。这正是"串会话"。而 `PiRuntime::AskAsync` 里连 `Stop()` 都不调，
  只 `request_stop()`，旧的 worker 句柄被直接覆盖。
- **修法**：`busy_` 这个孤立 bool 换成轮次**相位**（Idle / Running / Stopping），
  判定的纯逻辑落在 `MiaoTurnLifecycle`。关键一条：`PhaseAfterStopRequest`
  **不许返回 Idle** —— 取消是请求，完成要等 worker 自己退出（`RunTurn` 收尾时）。
  `Busy()` 改成 `phase != Idle`，于是"取消中"仍然是忙。
  `AskAsync` 用 `compare_exchange_strong` 按相位裁决，并且 Stopping 上的拒绝
  给的是"正在取消上一轮，请稍俟再发"而不是"正忙" —— 用户取消后马上重试，
  界面上该说的是"再等一下"，"正忙"看着像坏了。
- **又删掉一个不可达的探测量**：第一版 `TurnTimeline` 里有个
  `OverlappingTurns()`（"曾经出现过并发轮次"）。变异检测逮到它不可达：
  取消进 Stopping 挡住起轮，只有 `WorkerExited` 回 Idle，而它在回 Idle 之前已经把
  `inFlight_` 减掉了 —— "起轮时 inFlight_ > 0"凭正确的 API 根本走不到。
  一个永远为 false 的探测量与没有它长得一模一样。换成可达的断言：
  **被拒的起轮不会多造出一个在飞的轮次**（`InFlightTurns()` / `StartedTurns()` /
  `RejectedStarts()` 三个计数器）。
- **门第二次逮到我**：`AskAsync` 原本内联 `onDone(L"Pi Runtime 正忙")`,而
  `ContentCreatorDialog` 的 `kBusyRejectionMarker` 就是这一个串 —— 它拿 find() 分辨
  "这个 done 是一次拒绝"还是"这一轮已完成"。我把措辞挪进相位裁决之后,
  `tests/content-creator-modes.mjs` 当场红了。它逮的是对的:前缀一改,一个**从未发送**的
  请求就会被当成"本轮生成已完成"报给用户。修法不是把字面量写回去,而是把契约钉住 ——
  `MiaoTurnLifecycle.h` 新增 `kTurnRejectionPrefix`,两档差异只追加在后头,门改成从那个常量
  推导 marker 应有的值。三个模块共用一处定义。prefix / marker / 不 emit 三处都变异验过。
- 顺带：第一版 `JudgeTurnStart` 的 Idle 早退忘了把 `allowed` 置 true
  （结构体成员默认 false），于是"空闲时也发不出消息" —— 12 项断言当场逮到。
- **顺手修了三处文档引用**：`verify-doc-citation-symbols.sh` 报
  `LOCAL_AI_ARCHITECTURE.md` 引 `PiRuntime.cpp:368` 讲 `ConfigurePiAgent`。
  本轮只加了 2 行 include,把那条本来已偏 2 行的引用推出了 ±3 窗口。顺带查了
  PiRuntime.cpp 在文档里的全部引用,改掉能逐字核实的三处(loopback 密钥 374→318、
  `ConfigurePiAgent` 368→372、命令行 551-555→498-502)。
  另有三处引用的内容在这个文件里根本找不到(`["text"]` 的"产品当前写法"引 :455、
  `449-452` 的图片能力注释、`:526-528` 的 systemPrompt)—— 它们**不在**那道门的
  检查形状里(引用点前没有反引号标识符),所以门放行。**记录在案,没有改。**
- **本机跑了什么**：`TurnLifecycleTest` 41 项（含反空洞自检）；
  **11 处变异全红，0 存活**（10 处相位 + 1 处前缀契约）：取消直接清成 Idle（缺陷本身）/ 空闲时取消也进 Stopping /
  worker 退出不回 Idle / Idle 上不许起轮 / Stopping 上放行 / 两种拒绝同一句话 /
  起轮不记在飞 / 被拒的起轮也算跑起来 / worker 退出不回收 inFlight / 拒绝不计数。
- 真机:**未取证**。"取消之后马上重试会不会串话"要在 Windows 上真发一轮才知道；
  连着点取消时用户看到哪句话也没有真机证据。

### 本轮推进记录（2026-10-04 再续五，P0-08：强杀之后 Node 会活下来）

- **做的事**：`TerminateProcess(MiaoDesk)` **不跑析构函数**。于是"父进程自己会收拾
  子进程"这件事在强杀路径上一条都不成立 —— `PiRuntime::CleanupProcess` 写得再好,
  也只是给正常退出路径准备的;强杀之后 Node 会一直活下去,握着一根已经断掉的 stdin
  管子,而没有任何人再去收它。这就是 P0-08 验收原话里的"无永久 Node … 孤儿"。
- **修法**：新增 `include/miaodesk/MiaoChildProcessReaper.h`,把子进程装进一个带
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 的 job —— 父进程一死(不管怎么死的),内核连坐
  杀掉 job 里的全部进程。`PiRuntime::LaunchProcess` 接上它。
  装不进去(最常见是父进程自身已在调试器/CI 的 job 里且不许嵌套)时行为与今天完全一样,
  但**必须写进日志** —— 看不见的收尸缺口比没有更糟。
- **新门 `scripts/verify-child-process-reapers.py`**：把每个 `CreateProcessW` 站点分成三类,
  每类都要给出证据 —— 装进 job / 有界同步(当场等 HANDLE 并在超时·失败时终止) /
  已登记的独立生命周期。此前**没有任何门**管这件事:每个站点是不是真的收尸只能靠人肉翻。
- 扫描结果(10 个真实站点,注释里的提及不算):
  - 4 个**已经**在收尸:`WebWallpaperHost`、`ContentWidgetHost`、`NativeWidgetHost`、
    `HarnessProcessManager` 各自 `CreateJobObjectW` + `AssignProcessToJobObject`;
  - 2 个有界同步(自己等并终止,不需要 job):`GozSearch`、`NativeToolIsolation`;
  - 3 个**故意分离的生命周期**,登记了它们靠什么停:`app/main.cpp` 与 `HarnessHost.cpp`
    的后台 Harness 所有者(自带单实例 mutex + stop event),`WallpaperEntry` 的壁纸 helper;
  - 1 个**真的没有**:`PiRuntime` 的 Node —— 本轮补上。
- **顺带发现一处重复**:启动后台 Harness 所有者的函数在仓库里有两份,而且**名字都不一样** ——
  `src/app/main.cpp:195` 是 `LaunchHarnessBackgroundOwner()`,`src/harness/HarnessHost.cpp:54`
  是 `LaunchBackgroundHarnessOwner()`。做的事几乎一样(同一个 EXE、同一条命令行、同一个轮询),
  但两份各自维护。门把两处都列了出来,合并属另一轮。
- **登记为什么查两样**：只查"stop event 的符号在不在文件里"是不够的 ——
  把事件名改了、留着一个名字没变的常量,门会一路绿灯,而那时父进程死后没有任何人能拦下
  这个孤儿。所以登记要求符号在,且符号**定义成的事件真名**对得上。第一版写成正则时把
  不属于反斜杠的点也转义了,于是永远匹配不上,门把三个正确登记的站点全判成"登记已过期" ——
  改成普通字符串比较。
- **CI 当头一棒,是我自己的错**:收尸门第一版把仓库根**写死**成
  `ROOT = '/Volumes/Ext/Projects/MiaoDesk'`(本机路径)。本机全绿,ubuntu runner 上
  一个站点都扫不到。**是门自己的"零条比对不可能是通过"守卫报出来的** —— 它没有打印
  一句"每个站点都有收尸路径"就退出 0,而那正是这个仓库反复栽的形状。
  修复提交 `9175fb0b`:ROOT 改从 `__file__` 推;守卫现在会打印它解析到的两个路径并直接说
  "那个根目录看着像某台开发机的路径,就是 ROOT 又被写死了"。复现方式是把 src/
  scripts/tests/.github 复制到另一个绝对路径再跑。顺带 grep 全仓确认新加的文件里
  没有别的硬编码开发机路径。
- **本机跑了什么**：收尸门 6 处变异 **6 红 0 存活**:拆收尸接线 / 装了就丢返回值 /
  有界同步不再等 HANDLE / 有界同步不再终止 / stop event 改名(登记过期) /
  **门自己扫不到任何站点**。最后一条是它自己的反空洞守卫 —— 少了它,扫描路径写错时门会
  打印"每个站点都有收尸"而实际一条都没查。`verify-shell-scripts-parse.sh`
  扩到 `scripts/*.py`(此前那个 python 动画门写坏语法,这里照样报"全部可以解析");
  mingw 单编 `PiRuntime.cpp` / `L3Agent.cpp` 均 0 错误;21 道 shell 门 + 2 道 python 门
  + 17 道 node 契约门全通过。
- 真机:**未取证**。"强杀之后桌面上真的没有孤儿进程"要 Windows;`AttachToReaper` 在
  ERROR_ACCESS_DENIED 那条路上究竟多常见,也要真机才知道。

### 本轮推进记录（2026-10-04 再续四，P0-07 AI 侧：一把 Key 会被发到另一台服务）

- **修的真实缺陷**（P0-07 的"API profile 一致恢复"）：
  `L3Agent::ReloadConfig` 从 `api-profiles.ini` 读权威配置，而 `/provider`、`/endpoint`、
  `/key` 这几个**活着的**本地命令（AI 对话里敲的，`ConversationPanelImpl` 直接转发）
  就地改 `config_`。而 `config_.profileId` **只有 `ReloadConfig` 会写** —— 于是：

      /provider https://别的服务/v1 某个模型
      → config_.baseUrl 换成别的**主机**,config_.profileId 一个字符都没动
      → 下一次请求 URL = https://别的服务/v1/...      ← 用户刚敲的
                   Key = MiaoDesk/ApiProfile/<原 profile>  ← 原来那个 profile 的 Key

  **一把签给 A 服务的 Key，被发到 B 服务上去。** `/key` 那一侧更糟：它按同一个
  `profileId` 选槽，会把用户敲的新 Key **覆盖回那个 profile 自己存的 Key** 上，
  而设置页里一个字都不会提，其它每个用这个 profile 的 AI 窗口下一轮就都用上它。
- **修法**：`/provider` 换主机时丢掉 profile 身份，凭据回到旧槽（`/key` 写的也是旧槽）。
  地址一个字符都不动。`/endpoint` **故意不丢** —— 只换路径时主机没变，profile 的 Key
  仍然是发给同一台主机的，丢了反而让用户莫名失去配好的 Key。见下一条。
- **顺带说实话**：`/provider` 原先回"模型配置已保存。"，而那个值写进
  `model-settings.json`，这个文件的读者 `L3Agent::LoadConfig()` **零调用方**
  （L3Agent.h 自己写着 "ReloadConfig and the normal runtime path do not consume their
  state."）。下次 `ReloadConfig` 会用回 API 配置中心的值。改成了如实说明。
- **拆出 `MiaoAgentConfigAuthority.{h,cpp}`（纯逻辑，本机有门）**，与这个仓库里另几次
  拆分同一模式（`MiaoD3D11RenderPolicy`、`TodayTaskPresentation`、`WidgetIdentityRules`）：
  `L3Agent.cpp` 为了 `wincred.h`/`winhttp.h` 只能 Windows 上编，于是"URL 与凭据是否同源"
  这条在本机一行都跑不到。模块只做判定：URL 拼装、凭据槽选择、同源裁决、以及换主机时
  该不该丢身份。`L3Agent` 的 `CurrentApiUrl`/`SaveApiKey` 改成走它，槽名用 `static_assert`
  两边焊死（每处 profile 的 Key 槽名是三方共用的契约，改一边不改另一边会静默换槽）。
- **本机跑了什么**：`AgentConfigAuthorityTest` 38 项（含反空洞自检：喂四个明知该被抓的
  合成输入，恒返回 `bound=true` 的函数必须露出来）；**16 处变异全红，0 存活**。
- **CI 为难过一次,是门的功劳**:`b5e5403c` 上 `Repo Hygiene` 与 `Windows x64 Build` 两个 job
  红了(同一根因,不是编译错误)。两道 job 都跑 `tests/multi-api-routing.mjs`,而那道门断的是
  我把凭据槽构造搬走**之前**的旧写法 —— 断的是拼写,不是契约。修复提交 `49414565`:门改成断
  契约本身,并比原来更严(槽必须从 profile id 推出来、内联第二份不许回来、static_assert 必须在)。
- 真机:**未取证**。两件事必须分开说:
  - `L3PersistenceSelfTest`(只能 Windows 上跑)**正好走 `/provider`**,这里跑不了。
    逐行读过之后判断它不受影响:那个 legacy 块跑在新建的临时 LOCALAPPDATA 里,
    此时没有任何 profile,`config_.profileId` 从构造起就是空串,而空串时
    `RebindForLocalCommand` 是恒等的 —— session 路径与轮数期望都不变。
    **但这是推理,不是证据。**
  - P0-07 的 Wallpaper/Widgets/AI 会话/库状态四个恢复面的真机一致性仍要 Windows;
    "外泄的 Key 打中过哪台服务"也没有线上证据。

### 本轮推进记录（2026-10-04 再续三，P0-04 的自动部分）

- 代码提交：见本轮末尾。
- **上一轮我把 P0-03/04/07/08/09 整块说成"都要 Windows 侧"，这是错的。** 面板自己就写着
  这几项的"自动部分 = 是"，规划原文是"完成…的**自动部分**并排入真机验收"。
- 做完的自动部分（P0-04 的四条验收里覆盖两条）:
  - **位置丢失** → `NormalizeWidgetLayout` 从 `DesktopWidgetStore::Normalize` 提出来。
    那个 .cpp 为了 UTF-16 配置持久化 import 了 windows.h,于是这批不变量本机一行都跑不到。
  - **重复实例** → `NativeSingletonKey` / `SameNativeSingleton` 提到
    `WidgetIdentityRules.cpp`(纯字符串逻辑),P0-04"无重复实例"现在本机可验。
  - 顺带把 `NativeWidgetPreset.cpp` 变纯:它整个文件只有 `_wcsnicmp` 一个 Windows 调用,
    换成可移植的宽字符大小写不敏感比较(三个内置 source 全是 ASCII)。
- **修了一个真实缺陷**:`std::clamp(NaN, lo, hi)` 对 NaN 是**恒等函数**
  (`v < lo ? lo : (hi < v ? hi : v)`,两个比较对 NaN 都为假)。而
  `DesktopWidgetStore::ReadFloat` 用 `wcstof` 解析配置且不查有限性,`FloatText` 又用
  `%.6f` 写回 —— 一个 NaN 会写进配置文件、再读回来、再写回去,**自我延续**。
  带着 NaN 坐标的组件在 `UpdateLayeredWindow` 上直接失败或消失。读与归一化两侧都挡了。
- 本机跑了什么:`WidgetGeometryTest` 126 项(几何不变量 + 去重规则 + 常量自钉);
  P0-04 几何 6 处变异、去重 5 处变异全红;21 道仓库门 + mingw 交叉语法门全通过。
- **又踩到自己两次,都是同一个模式**:
  - `WidgetLayout` 的成员默认值重复写了一遍常量,于是改常量不改结构体 ——
    "新建组件落在哪里"会有两个答案(变异检测:把常量改成 0.0,104 项断言全绿)。
  - 测试用**同一个常量**做比较,于是"尺寸下限改成 0"也测不出来。
    修法是把常量本身的值单独钉住。
  - 另删掉一处不可达的尺寸下限夹紧(位置已夹到 <=0.95,收边后不可能 <0.05)。
- 真机:**未取证**。P0-04 剩下的"无孤立 HWND、错误背景"与 20 次循环本身都要 Windows。

### 本轮推进记录（2026-10-04 再续二，把宽高比裁决接进宿主诊断）

- 代码提交：`60bf938d`。
- **做的是什么**：WPRO-01 上一轮量出"三个官方组件全声明 `aspectRatio: 1.0`，八个参考屏幕上一个也不成立"，但那个结论只活在测试里 —— 现场一个字都不提，因为没有任何渲染器或宿主读那个字段。本轮把它接进 `ContentWidgetHost::SurfaceState`：FHD 上三个组件会打印
  `aspectDeclared=1.000 aspectActual=1.778 aspectDistortion=1.778 aspectHonored=false`。
  **不是修它**（修要动渲染，本机看不见效果），是让它不再安静。
- **为此加 `JudgeWidgetAspectOfBox` 重载**，吃宿主手里那个真实像素盒而不是从归一化反推：
  宿主要的渲染目标尺寸是既成事实（夹紧、DPI、桌面边界都可能让它与推算差一两个像素），
  诊断要的是既成事实。
- **第一版实现读 `box.aspect` 是错的**：宿主拿到盒子后可能改它，而它没有义务记得同步那个
  展示字段。于是"把 height 调小 24 像素"拿到的还是旧比例的裁决 —— 而那一条恰好是诊断要报的
  信息，是 `WidgetGeometryTest` 里"盒子与推算不同时裁决跟着变"逮到的。
- 本机跑了什么：44 个纯逻辑目标全通过（exit 0）；`WidgetGeometryTest` 75 项；
  4 处变异全红；21 道仓库门 + mingw 交叉语法门（0 真实错误）全通过。
- 真机：**未取证**。诊断行的实际输出只在 Windows 宿主里写进配置文件与日志；桌面视觉
  一个像素都没动。

### 本轮推进记录（2026-10-04 再续，CAP-03 资产门与 schema 版本矩阵）

- **补上两个此前没有任何门的验收项**："资产不重复打包"与"版本可演进/降级路径明确"。
- 新增 `ShippedPackageAssets`：对全部发行包 + 两个示例包走
  `MiaoContentPackage::Load`→反序列化→`Validate`→`MiaoAssetDatabase::Build`,逐包核
  三件事(每个声明的资产都有人引用、没有两份内容哈希相同、合计字节数记录)。
  本轮基线 **16 条资产、2710 KB、无人引用 0、内容重复 0**。
- **在这道门上又踩到自己一次**：第一版把判定写在循环里,于是把"无人引用"那一行删掉之后
  门照样全绿 —— 当前内容没有无人引用的资产,"检查还在不在"根本问不出来。
  改成抽出 `AuditPackageAssets` + 两个合成坏包的自检之后,两处变异都红了。
  这与本轮早些时候 `ShippedAnimationContinuity` 是同一个坑,第二次才长记性:
  **一道永远不失败的门,与一道好门在通过的那一刻长得一模一样。**
- schema 版本矩阵写成 `MIAO_CONTENT_PACKAGE_V1.md` 附录 A。只有一版时也写下来,
  理由是"将来加第 2 版时这里必须先改"。
- 本机跑了什么：`ShippedPackageAssets` 14 项通过；两处变异全红;21 道仓库门全通过。
- 用户可感知变化：**无**。本批防的是将来,不改当前行为,所以不进 `FEATURE_CHANGELOG.md`。

### 本轮推进记录（2026-10-04 续，WPRO-05 组件失败重试的风扇问题）

- 代码提交：见本轮末尾（WPRO-01 `beb322e9` 之后）。
- **修的是一个会一直烧 CPU 的缺陷**：`RepaintDue` 按 `nextRefreshAt == 0 || 现在 >= nextRefreshAt`
  决定要不要画，而 `nextRefreshAt` 只在**成功**的 `PaintSlot` 末尾赋值。`PaintSlot` 有
  五条提前返回的路径，一条都不赋值 —— 于是它保持 0，而 0 的含义正是"随时都该画"。
  一个画不出来的组件因此每 16ms 重试一次：一秒 60 次，一天 520 万次。
  `nextRefreshAt` 的赋值点一共四处，失败路径一处都没有。
- **修法**：新增 `WidgetRefreshPolicy`（纯逻辑）—— 失败指数退避到 30s 封顶
  （冷启动第一分钟 6 次、稳态每分钟 2 次，今天是 3750 次），成功立刻清零回到
  内容要的间隔（否则一次内容变化会晚最多 30 秒才上屏），延迟永不为 0。
  宿主五条失败路径都接上了 `ScheduleNextRefreshAfterFailure`。
- **顺带删掉三处挡不动任何东西的守卫**（都是变异检测逮到的，同一个教训第三次）：
  `SceneClock::Pause` 的提前返回、`ExtractJsonDouble` 的有限性检查、
  `NextRefreshDelayAfterFailure` 的 0 兜底。一行挡不住任何事的代码只会让人以为
  这里曾经有过一个案例。
- 本机跑了什么：43 个纯逻辑目标全通过；`WidgetRefreshPolicyTest` 174 项；
  7 处变异全红；21 道仓库门 + mingw 交叉语法门（0 真实错误）全通过。
- 真机：**未测量**。省下多少 CPU 与电要 Windows 实机；本轮修的是缺陷方向，
  不声称省了百分之几。
- 本会话已交付四批（都可本机验证）：`bd23cfe6` CAP-02 示例包负载链门；`7e08e97b` WALL-03
  宿主侧时间策略 + 发行内容动画连续性；`beb322e9` WPRO-01 组件几何测量 + `aspectRatio` 说实话；
  `511a663b` WPRO-05 失败重试退避。另有 **4 个 node 契约门在 clean HEAD 上就是红的**，
  与本会话无关（`creator-conversation-continuity`、`creator-window-keyboard-conformance`、
  `image-provider-installed`、`ungated-action-surfaces-report`）。
- 下一候选：`CAP-03` 的 schema/capability 版本矩阵与资产依赖快照（纯逻辑）；
  `WPRO-04` 的三个官方 Provider 接入 `WidgetDataActionContract`（契约已在，接线属宿主侧）。
  一律要 Windows 的：`P0-*`、`WALL-06`、`PRO-03/05`、CAP-04、WPRO-06/07、AIP 评测。

### 本轮推进记录（2026-10-04，WPRO-01 组件几何与 aspectRatio 不生效）

- 代码提交：`beb322e9`（WALL-03 两个提交之后）；本轮文档 + 代码，未提升版本号。
- 做了什么：新增 `MiaoWidgetGeometry.cpp`（纯算术）把"这个组件到底有多大"算出来，
  加 `WidgetGeometryTest` 61 项与 `ShippedWidgetGeometry` 10 项（每个发行组件在 8 块
  参考屏幕上的真实像素盒逐行打出来）。
- **结论**：三个随产品发行的组件全部声明 `aspectRatio: 1.0`，而八个参考屏幕上**没有
  一个成立** —— FHD 16:9 上实际 1.778、32:9 上 3.556。原因是这个字段没有任何渲染器
  或宿主读它，而 `ValidateInstance` 比的是归一化比例（0.30/0.30）、不是像素比例。
  与 `asset.font`、`videoRenderer` 同类，只是这次连校验都是虚的。
- **顺手修了一个真实的坑**：`ExtractJsonInt` 对 `"0.30"` 解析失败，第一版门把
  `defaultWidth` 读成"取不到"退回默认值 0.30 —— 而默认值恰好等于真值，于是三个组件
  全被报成"未声明 aspectRatio"，而 manifest 里明明写着 `1.0`。**默认值与真值撞车让
  整道门看起来在工作。** 修法是在共享读取器里加 `ExtractJsonDouble`（14 条新断言）。
- 本机跑了什么：43 个纯逻辑目标全通过（exit 0）；21 道仓库门 + mingw 交叉语法门
  全通过；几何 10 处、JSON 3 处变异全红。
- 真机：**未取证**。"真实比例只有 1.778/3.556"是算术结论，"用户在桌面上看到什么"未签收。
  三个内置组件的外观一个都没动 —— 为让门变绿去改一个看不见效果的东西，正是规划禁止的。
- 剩余（WPRO-01 未完成部分）：尺寸族（small/medium/large）没有定义 —— 那是产品决策
  （有哪几档、UI 上怎么选），本轮不发明；长文本/溢出策略、切尺寸迁移、
  `FitAspectInside` 接进宿主都没有（后者要 Windows 才能验留边效果）。
- 下一候选（依赖已满足且本机可自动验证）：`WPRO-05` 组件更新调度与能耗；
  `CAP-03` 的 schema/capability 版本矩阵与资产依赖快照。
  一律要 Windows 的：`P0-*`、`WALL-06`、`PRO-03/05`、CAP-04、WPRO-06/07、AIP 评测。

### 本轮推进记录（2026-10-03，自动推进会话）

- 代码提交：`34a6d55b` → `51d01b07` → `bd23cfe6`（三个提交同属本轮，基线 `d02812d6`）；
  本轮文档 + 代码，未提升版本号。
- 本机跑了什么：36 个纯逻辑目标全通过（exit 0）；mingw 交叉语法门 0 真实错误；
  17 道仓库门 + 6 道相关 node 契约门全通过；新增测试全部做了变异检测。
- CI：`51d01b07`（当前 HEAD）8 项全部完成，**全部 success**（Repo Hygiene / Windows x64 Build /
  Path Layout Contract / ARM64 Fast Dev / Windows x64 MSIX / Windows x64 Package /
  Windows ARM64 Package / Cleanup merged branches）。早前 SHA 上有两项显示 cancelled —— 那是
  推送下一个提交时 GitHub 取消被取代的运行，不是失败。
- 真机：**未取证**。本机不是 Windows —— 视觉、桌面行为、音频设备、多屏、性能一律给不出证据，
  因此 `P0-03/04/06/07/08`、`WALL-06`、`PRO-03/05` 与 CAP-04 的证据仍然缺着。
- 已完成（本机可自动部分）：PRO-01 台账、CAP-01/02/03/05 的契约与门、SEARCH-01 的排序基线与
  固定查询集、D-1/D-2/D-3/D-6/D-7 五项缺陷修复。
- 下一候选（依赖已满足且本机可自动验证）：`CAP-02` 的"每项可创作能力最小例 + 组合例"仍缺示例包；
  `P0-03/P0-04/P0-07/08/09`、`WALL-06`、`PRO-03/05`、CAP-04 都要 Windows 侧，一律留 `🟠`。
- 阻塞：真机签收需要 Windows x64/ARM64 各一台、显示器/DPI 矩阵、已配置的 Provider 与对标软件。

## 14. 专业版建设队列（S0～S7）

详细交付与验收见 [TODO 第 11 节](TODO.md#11-专业级桌面建设任务2026-10-03)，阶段出口见 [总体规划](PROFESSIONAL_DESKTOP_PLAN.md)。下表是这些任务的唯一状态表；其依赖有变化时同步 TODO。

首轮仅 PRO-01 可立即领取。其余按依赖解锁；S2 的纯契约/知识工作可在 S1 真机等待期间推进，S3/S4 的高风险运行时扩展须守住 S1 稳定门。

### S0/S2 首批结论（2026-10-03）

| 项 | 状态 | 结论 |
| --- | --- | --- |
| PRO-01 | 🟡 | 台账见 `docs/CAPABILITY_EVIDENCE_LEDGER.md`；真机签收缺失 |
| D-1 | ✅ 已修 | 取证样本摘要恒空 → `MakeRenderedEvidenceSample` 由构造保证；两侧测试 + 变异检测通过 |
| D-3 | ✅ 已关 | 未知 capability 现在被加载器与 validator 拒绝（目录外即失败） |
| D-4/D-5 | 🟡 半关 | 目录能查出"仅声明／后端不支持"；渲染器的静默忽略待 WALL-01/04 |
| CAP-01 | 🟡 | 68 条能力、四类查询、导出与自述已接；真机设备探测未做 |
| CAP-02 | 🟡 | 能力卡已补齐（组件/资产/后处理/通道），漏教门已上线；3 个真实渲染样例与 AI 评测未做 |
| CAP-05 | 🟡 | Provider 六态 + Action 幂等契约与测试已交付；官方 Provider 与动作注册表尚未接入（WPRO-03/04） |
| D-6/D-7 | ✅ 已关 | 校验器 entry 规则只认工作区布局、把 8 个发行包全拒；已改规则 + 新增发行包包级校验回归门 |
| D-8/D-9 | ✅ 已关 | 待办组件沉默截断（计数说真话、只画 4 行、无提示）；已补溢出说明并让槽位数单一来源 |
| D-2 | ✅ 已修 | 原生待办卡片改读 `TodayTaskStore`；行模型纯逻辑 + 宿主闸门 + 预览同源 |

**状态图例补充**：状态只在本表维护；每轮结束写证据 SHA、本机跑了哪些门、剩余阻塞。⬜ 未开始的本机不可验项标注真机缺口，不得提前标 Done。

| ID | 阶段 | 状态 | 任务 | 依赖 |
| --- | --- | --- | --- | --- |
| PRO-01 | S0 | 🟡 | 现状、能力与证据台账 | 无 |
| PRO-02 | S0 | ⏳ | 对标作品与评测集冻结 | PRO-01 |
| PRO-03 | S0 | ⏳ | 参考环境、预算与度量协议 | PRO-01、PRO-02 |
| CAP-01 | S2 | 🟡 | 运行时能力目录与查询 | PRO-01 |
| CAP-02 | S2 | ⏳ | 能力卡、配方与参考知识库 | CAP-01、PRO-02 |
| CAP-03 | S2 | 🟡 | 内容包演进与可复用资产 | CAP-01、P0-09 |
| CAP-04 | S2 | ⏳ | 统一预览、桌面与渲染证据 | CAP-01、CAP-03、DESK-01 |
| CAP-05 | S2 | ⏳ | 组件数据与动作公共契约 | CAP-01 |
| WALL-01 | S3 | ⏳ | 专业构图与基础绘制原语 | CAP-03、CAP-04；运行时扩展遵守 S1 稳定门 |
| WALL-02 | S3 | ⏳ | 深度视差与局部动态 | WALL-01 |
| WALL-03 | S3 | ⏳ | 动画、状态与有界行为 | WALL-01、CAP-01 |
| WALL-04 | S3 | ⏳ | 粒子、多 Pass 与专业特效库 | WALL-01 |
| WALL-05 | S3 | ⏳ | 音频、指针与媒体响应质量 | DESK-03、DESK-04、DESK-05、WALL-03 |
| WALL-06 | S3 | ⏳ | 播放、多屏与电源策略 | P0-03～08、CAP-04、PRO-03 |
| WALL-07 | S3 | ⏳ | 2D 壁纸参考集与 WE 对照 | WALL-01～06、CAP-02、PRO-02/03 |
| WPRO-01 | S4 | ⏳ | 组件尺寸族与自适应布局 | CAP-03、CAP-05、P0-04；遵守 S1 稳定门 |
| WPRO-02 | S4 | ⏳ | Native 组件视觉与控件原语 | WPRO-01 |
| WPRO-03 | S4 | ⏳ | 动作、命中与拖动交互 | WPRO-01、CAP-05 |
| WPRO-04 | S4 | ⏳ | 真实 Provider 与数据状态 | CAP-05 |
| WPRO-05 | S4 | ⏳ | 组件更新调度与能耗 | WPRO-04、PRO-03 |
| WPRO-06 | S4 | ⏳ | 组件键盘、读屏与系统适配 | WPRO-02、WPRO-03 |
| WPRO-07 | S4 | ⏳ | 8 类官方组件与 macOS 对照 | WPRO-01～06、CAP-02 |
| AIP-01 | S5 | ⏳ | 需求设计与能力规划 | CAP-01、CAP-02；复用 CREATE-01/CCA |
| AIP-02 | S5 | ⏳ | 专业素材准备与复用 | AIP-01、CAP-03 |
| AIP-03 | S5 | ⏳ | 真实运行观察与多模态证据 | AIP-02、CAP-04；复用 CCA-08/09 |
| AIP-04 | S5 | ⏳ | 专业评审与有界修复 | AIP-03 |
| AIP-05 | S5 | ⏳ | 连续创作、版本与应用恢复 | AIP-04；复用 CREATE-04/06/07/08、CCA-10/11 |
| AIP-06 | S5 | ⏳ | 专业质量、组合与成本放行 | PRO-02/03、AIP-05、WALL-07、WPRO-07 |
| AIP-07 | S5 | ⏳ | AI 知识与能力发布兼容 | AIP-06、CAP-03 |
| ADV-01 | S6 | ⏳ | 真实 3D 资源与相机 | WALL-01、WALL-04、CAP-03；S3/S4 稳定后集成 |
| ADV-02 | S6 | ⏳ | 专业材质、灯光与环境 | ADV-01 |
| ADV-03 | S6 | ⏳ | 骨骼、形变与受限物理 | ADV-02、WALL-03 |
| ADV-04 | S6 | ⏳ | 可编程视觉与故障隔离 | WALL-04、CAP-04、AIP-03 |
| PRO-04 | S7 | ⏳ | 完整桌面组合与一体创作 | S3、S4、S5；高级组合依赖 S6 |
| PRO-05 | S7 | ⏳ | 专业版兼容、长稳与发布证据 | PRO-04、S6、原 REL 门 |
| PRO-06 | S7 | ⏳ | 作者资料、交接与专业目标签收 | PRO-05 |

### `20bd7639` 上 Windows x64 Build 的组件可见性冒烟红过一次 —— 同 SHA 另两条链同一条门全绿

- **是哪一道**：`packaging/windows/verify-widget-visibility.ps1` 的 `Wait-WidgetCount`
  ("Expected N paint-ready Widget surface(s), observed $count")。它起真的
  `MiaoDeskWallpaper.exe`、用 `EnumWindows` 数真的窗口,**只给 15 秒**准备时间,每 250ms 轮一次;
  壁纸进程中途退出则直接抛。**它是计时敏感的运行时冒烟,不是编译门。**
- **结论是 flake,有证据,不是猜的**:
  - 同一条门在**同一个 SHA `20bd7639`** 上跑过五次:`Windows x64 Build` **红**,
    `Windows x64 Package` **绿**、`Windows x64 MSIX` **绿**、`Windows ARM64 Package` **绿**。
    加上 ARM64 Fast Dev / Repo Hygiene / Path Layout Contract / Cleanup 也全绿,
    **同 SHA 八道里七道 success**。同一个 .ps1,同一份提交。五局四绿,而代码只有一份。
  - 同一条门在 `7758a567`(P0-04 那个提交)上也绿。
  - `7758a567` 之后改的三处 —— AI 对话凭据槽(`b5e5403c`)、Pi 的 Node 收尸(`dcf9750a`)、
    Pi 的轮次相位(`20bd7639`)—— **都不在壁纸/组件进程里执行**
    (`ReaperJob()` 只有 `PiRuntime::LaunchProcess` 会调)。
- **它又红了一次,而这一次拿到了直接证据**(`7cb2978f` 的同一个 Step 45):
  失败的完整诊断打在 annotation 里 ——

      WidgetRuntime=wallpaper.enabled=false Native Direct2D Widget host
        configured=1 desired=1 surfaces=1 visible=0 paintReady=1 directHwnd=1 layered=0
      Expected 1 paint-ready Widget surface(s), observed 0.

  `configured=1`、`desired=1`、`surfaces=1`、`paintReady=1` —— **组件栈是健康的,
  该建的渲染目标建出来了**;只有 `visible=0`。也就是说探针取样那一刻窗口还没翻成可见。
  如果本会话改的东西真的把组件创建弄坏了,`configured` 或 `surfaces` 会是 0,而不是 1。
  这条门查的是"可见性时序",而 `visible` 归窗口堆叠,不归我改过的任何一条路径。
- **同一次 run 里还有一条自然对照**:同一个 workflow 的第 787 步跑的是**同一条门**,
  但外面套了 `while` 重试(attempt 1..2),它就过了;第 951 步(Step 45)是**单发、无重试**的那一
  处,它红了。同一份构建。这个对照不是我设计出来的,是 workflow 本来就有的。
  (严格说它不是我安排的对照实验:两处的 ProductRoot 不同,一个指构建目录、一个指
  暂存根。所以它是佐证,不是单因实验证。)
- **仍然不知道的**:为什么 `visible` 慢到越过 15 秒窗口。CI 上是 Hyper-V 虚拟显示器
  1024×768(`HyperVMonitor`),窗口堆叠在那上面比真实桌面慢是有道理的,但我没有计时数据。
- **没有做的事**:匿名身份不能 re-run(401),所以我没有通过重跑取证;
  也没有把这条门调宽容期 —— 那是拿"让门别响"换"门还在查",而这个仓库里已经栽过四次。
  更该做的是**给这一处加重试**,和第 787 步一致 —— 但那要动 CI 结构,不在本轮自动部分里。

## 15. 本轮规划变更记录（2026-10-03）

- 建立专业版 36 项任务；新增部分均为待执行，未把规划标为功能完成。
- DESK-01/03/04/05 改为核验已有实现；P0 与真机门保持有效。
- 旧百分比保留为基础范围快照，专业目标重新建立分母。
- 本轮仅改文档；详细变更见根 CHANGELOG 的 2026-10-03 规划条目。下一项为 PRO-01。
