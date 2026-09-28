# 壁纸与组件专用创作 Agent 实施计划（方案 2）

- 建立：2026-09-27。
- 状态：**待执行的实施计划，不代表功能已经实现**。本次只编制计划，后续由其他 AI/开发者执行。
- 核对基点：`ca69e7ad031cad8365ee63d6ad74c92d2d3bac76`。执行前必须重新核实 HEAD 与工作区，保护本计划之后的改动。
- 方案：一个 Content Creator Agent，共用制作流程，按 Wallpaper / Widget 加载不同 Skills；复用现有 Pi 实现、随包 Node、Provider 配置、内容框架和 Native UI。
- 上游：[产品愿景](PRODUCT_VISION.md)、[设计基线](DESIGN_BASELINE.md)、[开发路线](DEVELOPMENT_ROADMAP.md)。主产品任务状态仍在 [TODO.md](TODO.md)；本文是 CREATE-01～04、AI-01～02 相关工作的实施分解。

## 1. 给执行者的目标与边界

交付一个能持续修改作品的专用创作流程：理解需求、检查能力、制作内容包、调用真实校验器、生成实际渲染证据、根据反馈修复，最后交由用户预览并应用。

**本方案的价值必须体现为成功率和质量的提升，不能仅增加一个 Agent 名称或一段 system prompt。**

必须保持：

- 一套 Pi 运行时实现和依赖树。普通聊天、创作模式可拥有不同会话和按需子进程，不复制第二套 Agent 引擎，不新增正式产品 EXE。
- 常驻桌面渲染不依赖 Agent。创作结束后释放额外运行资源，已有壁纸与组件继续运行。
- 官方、用户、AI 内容走相同 Package / Parameter / Scene / Capability 契约。只有宿主正式 API 可以改变桌面状态。
- 现有已校验候选自动入库的产品行为继续保留；**入库与应用是两件事**。用户显式点击应用才改变桌面。
- 壁纸与组件各有领域规则，不拆成两个独立 Agent 系统。初版不实现多 Agent 协作，不引入通用任务编排平台。
- 本地 AI、DGX、模型路由与推理服务部署均不属于本计划。使用现有可用 Provider；视觉评审依赖经实际验证的图片输入能力。
- 本轮内容生成限定为当前受支持的声明式 Scene 内容及受控素材。沿用现有 Skill 对 Web、脚本与 3D 的限制，不顺带开发新渲染引擎。
- 只在必要信息缺失时询问用户；用户需求足够时直接制作，不在每个内部步骤增加确认。

本计划不授权执行者替用户发布版本、合并 PR、购买服务或把用户桌面截图发送给外部服务。真实模型评测使用已配置服务与专用测试内容，遵守既有权限与费用约定。

## 2. 当前事实与待补能力

| 事项 | 当前实现证据 | 本计划要补的部分 |
| --- | --- | --- |
| 创作入口 | `src/desktop/control/ContentCreatorBridge.cpp` 的 InitialPrompt 加载三个 Skills | 需求摘要、领域配置和工作流由可测试的控制器管理 |
| 创作界面 | `src/ui/ai/ContentCreatorDialog.cpp` 已有两种模式、预览、停止、重试与状态提示 | UI 只展示状态和发送意图，逐步抽出流程逻辑 |
| 会话共享 | 创作窗口使用 SharedConversationPiRuntime；重置/忙碌与普通聊天相互影响 | 独立作品上下文、取消令牌和生命周期 |
| Pi 请求 | `src/ai/pi/PiRuntime.cpp` 当前 prompt 只发送文本；models.json 的 input 为 text | 核实锁定 Pi 版本的多模态 RPC，再接入真实图片输入，不能只改声明 |
| Skill 使用 | content-package-basics → wallpaper-content 或 widget-content → content-review | 版本记录、优秀样例、设计方法与真实能力一致性 |
| 产物接收 | 当前从模型回复/工具结果文本中识别包路径 | 结构化候选提交与宿主生成的身份、版本及摘要 |
| 校验反馈 | InspectContentPackage 已执行；失败文本进入下次用户发起的生成 | 同一创作任务内有上限的自动修复，不要求用户重复点击 |
| 候选保留 | 上一候选及“当前预览是上一版”提示已实现 | 不可变候选快照与按版本保留，避免同路径被改写 |
| 预览 | 已支持实时 Scene、暂停/继续、重载、全屏、静态回退 | 离屏渲染证据、时间采样、模型视觉评审 |
| 入库与应用 | 已校验候选自动入库；Apply 通过正式控制路径；防重复应用当前主要按路径 | 版本级幂等、目标绑定、应用前再验证和一次恢复 |

以上是代码事实，不代表本次已经完成 Windows 真机验收。执行者不得删除已有修复或把它们重新列为从零开发。

## 3. 架构决策

### 3.1 Agent、Skill、宿主各自负责什么

| 层 | 责任 | 不应承担 |
| --- | --- | --- |
| 创作 Agent | 理解意图、组织设计、选择受支持能力、修改候选 | 自行宣布校验通过、修改正式桌面状态 |
| Skills | 包契约、壁纸/组件规则、制作方法、样例与评审标准 | 保存会话状态、代替实际校验器或硬件测量 |
| 创作控制器 | 阶段推进、请求归属、候选版本、预算、取消、修复与恢复 | 每帧渲染、第二套通用 Agent loop |
| 包与渲染服务 | 真实校验、素材托管、渲染证据、能力反馈 | 依赖 AI 返回文本判断成功 |
| Native UI | 需求输入、阶段反馈、候选展示、用户应用/恢复意图 | 自行改私有 INI、从聊天文本解析成功状态 |

Pi 继续负责单轮工具循环；控制器只负责作品生命周期和阶段间的强制检查。不要在外层重写 Pi 的工具选择循环。

### 3.2 会话与运行时：复用代码，分开状态

首版默认方案：

1. 普通聊天保持现有会话；创作使用独立启动配置和状态目录，继续调用同一 PiRuntime 实现。
2. 同时最多运行一个创作任务。不同作品可保存草稿，但不为每个窗口常驻一个 Node 进程；第二个创作请求进入明确队列，支持取消排队。
3. 允许普通聊天与一个创作任务各持有一个按需 Pi 子进程。配置来源和依赖树相同，进程句柄、日志上下文、工作目录、工具集合、models/settings 文件及回调归属分开。
4. 作品切换后用宿主持有的需求摘要、候选和操作记录恢复上下文。当前使用 `--no-session`，不能假设 Pi 已替产品持久化作品。
5. 初始空闲回收策略建议为 5 分钟后释放创作子进程；持久化草稿仍在。具体时间在 CCA-03 测量后调整并记录。
6. 取消、清空、关闭创作只作用于对应作品和轮次。关闭普通聊天不应重置创作。退出产品应有界等待并清理所有自有创作工作。

如锁定版本的 Pi 提供已验证的可靠多会话能力，可在 CCA-01 提交替代设计记录；必须满足同样的隔离、取消和资源上限。未验证之前按上述方案实现，不在两种模式间同时维护两套策略。

### 3.3 建议落点（角色/名称均为拟新增，不是现有 API）

| 模块 | 建议落点 | 复用对象 |
| --- | --- | --- |
| 创作控制器、作品记录、预算 | `src/desktop/control/` | DesktopControlService 与内容生命周期服务 |
| Pi 创作启动配置与会话隔离 | `src/ai/pi/` | PiRuntime、扩展生成与 Provider 配置 |
| 创作工具适配 | `src/ai/tools/` | NativeTools、NativeToolIsolation |
| 包快照和候选版本 | `src/content/package/` + 宿主侧状态管理 | MiaoContentPackage、manager/catalog |
| 预览证据采集 | `src/desktop/preview/` | D2D/D3D11 renderer、现有 preview renderer |
| 界面适配 | `src/ui/ai/` | ContentCreatorDialog |
| 回归与评测 | `src/tests/`、`tests/` | 当前 CMake / Node / Windows CI 入口 |

不新增顶层 source domain。若确实需要，必须同改源码布局、领域契约和路径检查；新实现文件必须进入正确的 CMake target，不能只通过语法检查。

## 4. 数据与流程契约（先定清，再接 UI）

以下字段为拟定的最小协议，CCA-02 允许按现有类型命名调整，但不能丢掉身份与版本校验。

### 4.1 四类宿主记录

| 记录 | 必需信息 |
| --- | --- |
| CreationBrief | kind、用户目标、视觉方向、尺寸/比例、数据与交互需求、素材来源、允许能力、已确认限制、用户参数 |
| CreationSession | sessionId、briefRevision、当前阶段、Provider 配置版本（无凭据）、skill 版本/摘要、当前轮次、预算、取消状态、上一有效候选 |
| CandidateRevision | candidateId、revision、托管 package ID、内容摘要、只读快照位置、校验结果、目标后端、预览证据、评审结果、来源轮次 |
| OperationRecord | operationId、sessionId、turnId、候选摘要、目标显示器/实例、调用结果、实际变更标记、时间、可恢复前态 |

- ID 由宿主生成。模型提交的路径、ID、成功声明全部按输入处理，不能成为事实来源。
- 内容摘要覆盖 manifest、scene、参数及引用素材；不能只 hash 路径或单个 JSON。
- 校验、截图、评审、应用都绑定同一候选摘要；候选修改即使路径相同，也必须是新 revision。
- 候选经宿主封存后不可被模型继续修改。编辑在工作副本进行，校验与采集操作使用固定快照。
- 作品元数据只存产品本地状态目录；路径方案需通过现有 path budget。凭据不写入记录、日志、prompt 或工具参数。
- 初版保留当前与上一份有效候选，以及应用恢复所需版本；其余中间产物按会话预算回收，不实现完整版本历史产品。

### 4.2 状态转换

| 当前阶段 | 条件 / 事件 | 下一阶段与行为 |
| --- | --- | --- |
| Draft / NeedsInput | 需求足够、用户提交 | Preparing：冻结 brief 与运行能力 |
| Preparing | Skills/能力/素材已就绪 | Generating：在专属工作区制作 |
| Generating | 收到结构化候选提交 | Validating：宿主校验并封存候选 |
| Validating | 校验成功 | Rendering：采集候选实际效果 |
| Validating / Rendering | 可修复错误且预算足够 | Repairing：携带定位信息局部修改，再回到 Validating |
| Rendering | 实际渲染成功 | Reviewing：有视觉能力时评审；无能力时标记视觉未审 |
| Reviewing | 有具体可修复问题且预算足够 | Repairing；不重置用户主题，不绕过校验 |
| Reviewing | 无阻塞问题或用户需要自行判断 | Ready：展示候选及未覆盖项，不声称所有效果已验 |
| Ready | 用户明确点击应用、版本和目标仍有效 | Applying：再次校验后调用正式 API |
| Applying | 正式 API 确认成功 | Applied：记录结果与可恢复前态 |
| 任一非终态 | 用户取消 | Cancelling → Cancelled：停止新步骤、处理在途结果，不谎称已撤销副作用 |
| 任一工作阶段 | 服务错误 / 不可修复 / 预算耗尽 | Failed 或 NeedsInput：保留上一有效候选，说明原因和下一动作 |

排队是调度状态，不算正在生成。入库是已选定有效候选的存储状态，不等于 Applied；中间修复版本不逐个刷进用户资源库。

关闭窗口后的默认行为：请求取消该窗口正在进行的制作任务，保留草稿和最后有效候选；应用事务已经开始时以正式 API 的真实结果结算。用户没有授权的后台无限制作不属于首版。

### 4.3 取消、版本和幂等

所有异步消息必须携带 sessionId、turnId、generation/epoch 和 operationId；接收端验证归属后才更新 UI、候选或记录。停止请求后增加 epoch，旧回调不得覆盖新会话；但已开始的正式应用仍要记录真实结果，不能只把消息丢掉。

应用幂等键绑定“候选摘要 + 目标显示器/组件放置目标 + 用户操作 ID”。重复点击/回调返回同一结果；用户再次明确添加第二个组件是新的操作 ID，允许执行。不要永久禁止同一候选被再次使用。

恢复应保存应用前的精确状态，而不是取“最近使用的第一项”。若用户随后另行修改桌面，恢复前检查前提并让用户选择，不能覆盖无关的新操作。应用失败不改原状态；无法保证原子提交的分支必须有补偿步骤和故障测试。

## 5. 创作工具契约

以下是**拟新增工具能力**，名字在 CCA-02 冻结；不得写成已经可调用的 API。

| 拟定工具 | 作用与结果 | 宿主必须执行的限制 |
| --- | --- | --- |
| creator_capabilities_get | 返回当前 schema、节点/绑定、数据提供者、后端与素材能力 | 从真实运行时能力构建；区分声明支持、可渲染与可交互 |
| content_skill_get（已有，复用） | 按领域读取规则与样例，记录版本 | 只读白名单；不把全部规范每轮重复注入 |
| creator_package_read / creator_package_update | 读写当前候选的声明式文件 | 仅当前工作区内获准的包文件；大小/类型/路径限制；失败写入原子回退 |
| creator_asset_import | 导入用户选定素材，返回托管 asset ID | 只消费用户选定资源句柄或既有受控产物；核对格式、大小和解码，不接受任意系统路径 |
| creator_image_generate | 复用现有图片服务生成素材并纳入当前包 | 返回真正存在的托管素材；服务未配置时提供可用替代/请求素材，不伪造图片 |
| creator_candidate_submit | 提交候选并取得结构化校验结果 | 使用 MiaoContentPackage 与宿主校验路径；返回字段/节点/文件定位和后端能力结论 |
| creator_preview_evidence | 为封存候选生成真实帧与诊断 | 限定候选快照、尺寸、帧数、超时；不截取用户整个桌面 |

设计要求：

- 上述是产品内容包事务工具，不能扩张成第二套通用 read/write/shell 工具。
- 专用创作启动配置不继承普通聊天的任意 `bash/read/edit/write` 权限。需要的包读写通过受约束的接口完成；普通聊天的既有工具策略不在本计划内重写。
- 工作目录本身不是沙箱。宿主验证路径、reparse point、候选所有权与扩展加载规则；不能仅靠 prompt 说“不要越界”。按进程/会话配置验证扩展实际拿到的工具清单。
- 工具 worker 与 UI 间必须有会话绑定的可信调用上下文；模型不能伪造 sessionId 访问另一个作品。长任务的进度/取消经宿主代理传递，不能同步阻塞 UI 线程。
- 不新增 AI 可调用的 Apply 工具。正式应用从 Native UI 意图进入控制器和 DesktopControlService。
- 返回结构化错误码、是否可重试、定位信息和摘要；技术错误原文可放诊断详情，主界面保持可理解。
- 当前 `wallpaper_validate_package` 不能直接当通用 `.mdwall/.mdwidget` Scene 校验器使用；不得通过宽松替身绕过真实产品校验。
- 关闭通用 shell 前先提供素材与包制作所需的最小工具，否则只是切断创作能力。两部分必须作为同一可验证切换交付。

## 6. 质量反馈与预算

### 6.1 三层检查分开报告

1. **确定性检查**：schema、路径、引用、capability、几何、参数范围、资源上限、后端支持。由产品代码裁决，模型评语不能覆盖硬失败。
2. **真实渲染与测量**：在实际 D2D 或明确支持的目标后端上加载、绘制、采集帧和诊断；性能数字来自测量。几帧截图不能证明整段动画平滑，也不能推导长期 CPU/GPU 占用。
3. **视觉与需求评审**：用需求摘要、真实采样图和规则判断构图、可读性、裁切、风格与符合度。评审建议需定位元素和修改方向，不只给总分。

首版优先 D2D 可预览子集；D3D11 专属效果必须单独验证采集与正式运行一致性。无对应真实预览时明确 unsupported，不用静态封面冒充已检查的动画。

### 6.2 视觉证据与多模态链路

- 默认采集当前目标比例，附至少一个不同宽高比；组件覆盖正常数据、空态和长文本 fixture。
- 动画按时间采样，例如起点、中段、循环末尾附近；记录时间、随机种子、输入与模拟数据。短循环额外观察首尾连续性。
- 输出包括 candidate digest、后端、尺寸、采样时间、渲染成功/失败和图像 artifact ID。只拍作品离屏输出，不包括私人桌面、聊天、真实待办或凭据。
- 图片必须真实进入 Pi/Provider 请求。当前 text-only 模型声明、纯文本 prompt 和传输逻辑均需核实，不能只把图片路径写进 prompt 就说模型看过。
- 模型不支持视觉时允许完成结构校验与用户预览，但标记“视觉评审未执行”。该路径可作为受限模式交付，不能把完整视觉反馈里程碑勾选为完成。
- 视觉评审是同一制作流程中的一次有界模型调用，不需要长期运行一个第二 Agent。评审收到作品与 brief；隐藏制作方自我评价，减少被“已经很漂亮”之类文本带偏。

### 6.3 初始预算（实施默认值，可按评测调整）

| 项目 | 首版预算 | 用尽后的行为 |
| --- | --- | --- |
| 自动修复 | 初次生成后最多 2 轮，结构与视觉修复共用计数 | 保留最佳有效候选，展示未解决项 |
| 视觉评审 | 初次 1 次，每次修复后至多再 1 次，总计不超过 3 次 | 不重复自评无限打磨 |
| 渲染证据 | 单次至多 6 张，单张最长边初始不超过 1280，单次总数据初始不超过 8 MiB | 压缩/减少采样并说明覆盖范围，不截断为假成功 |
| 完整自动制作 | 默认 10 分钟墙钟上限，包含生成、工具、排队外的工作与修复 | 支持用户显式继续新一轮，不能静默续跑 |
| 模型上下文与输出 | 服从当前 Provider 已验证的能力与配置 | 宿主裁剪历史并保留摘要，不伪造模型容量 |

不同 API 对图像数、分辨率和请求大小的限制以实际支持为准，适用更小者。取消和超时覆盖在途工具；必要时终止专属创作子进程并标记未完成产物，不能停止普通聊天或桌面宿主。

## 7. 实施任务与建议 PR 顺序

每个任务只在交付物、自动验证和适用 Windows 验收完成后勾选。无硬件时标“实现完成 / 真机待验”，继续不依赖硬件的任务，不伪造通过。下表依赖为代码/契约依赖；阶段放行还必须满足本节末尾的里程碑条件。

### CCA-00 重新核实现状与保留既有行为

- [ ] 完成（基线已核，真机验收项未做）；负责人 / 证据：2026-09-27，Claude Code。见下方「核对结果」。
- **依赖**：无。
- **实施**：核对 HEAD、工作区、现有 CREATE/AI 任务补记、Pi 版本、创作界面和工具注册；建立基线包与旧流程样本；逐项标注本计划哪些能力已被其他提交实现。
- **交付**：基线 SHA、已有行为表、需改模块清单、缺硬件/服务清单。
- **验收**：自动入库、上一候选保留、停止、全屏、错误回传、防重复应用等修复有回归保护；不撤销其他人的工作。

**核对结果（2026-09-27）**：

- 基线 SHA = `ca69e7ad031cad8365ee63d6ad74c92d2d3bac76`，与计划编制时一致；工作区只有 `docs/DOC-INDEX.md`、`docs/TODO.md` 两处既有改动和本计划文件本身，没有需要保护的未提交实现。
- 第 2 节表格里「已被其他提交实现、不得重做」并已在本机核实的一批：自动入库（`SetGeneratedPackage` → `InstallToLibrary`）、上一候选保留（`generatedPackageIsCurrentRound`）、停止（创作面发送按钮在忙时改为「停止」）、错误回传（`lastValidationError` → `AppendRuntimeFailureNote` 带进下一次生成）、预览与全屏、`MarkLibraryItemUsed` 两个应用路径的簿记。这些都有对应的 `tests/*.mjs` 契约门，本轮全绿。
- 本机能跑的验证全部跑过：12 道仓库闸门 + 46 个 node 契约测试 + 30 个纯逻辑测试目标（含本轮新增的 CCA-02 状态机）。缺 Windows 真机，也缺任何可用 Provider（视觉与图片生成均未配置），所以**没有**把任何一项标为「真机通过」。

### CCA-01 验证 Pi 协议、会话隔离与图像输入可行性

- [ ] 完成（已用真实运行时取得结论，Provider 侧仍待验）；负责人 / 证据：[CREATOR_AGENT_PI_CAPABILITY_PROBE.md](CREATOR_AGENT_PI_CAPABILITY_PROBE.md)、`scripts/probe-pi-rpc-capability.mjs`。
- **依赖**：CCA-00。
- **实施**：读取锁定版本的本地 Pi 实现/随包文档；验证 RPC 事件、取消、进程清理、new_session、扩展 allowlist 与图像消息形状；使用合成测试图核验支持视觉的已配置 Provider。
- **交付**：一页能力探测记录，包含请求/响应的脱敏样例、进程策略、目录/配置隔离方案和图片链路结论。
- **验收**：不假设当前文本 RPC 自动支持图片；Provider 不可用时保留图像探测待验。确需升级 Pi 时单列兼容性变更，不顺手更新整个 runtime lock。

**核对结果（2026-09-27，本机安装锁定版本真实启动 Pi）**：

- **不用升级 Pi**。0.83.0 的 RPC 已覆盖 `prompt` 带 `images`、`abort`、`new_session`、`get_state`；`--mode rpc --no-session --approve --provider --model --no-extensions --extension --tools --append-system-prompt` 每个 flag 在锁定版本里都真实存在。`runtime/agent/package-lock.json` 未改动。
- **决定性结论**：`RpcCommand` 的 `prompt` 接受 `images: {type, data, mimeType}`（`data` 是裸 base64）。但当 `models.json` 的 `input` 只有 `["text"]`（产品当前写法）时，pi-ai 会把图片**静默替换**成 `"(image omitted: model does not support images)"`，而 RPC 照样回 `success:true`、`agent_settled` 无 error、stdout 无任何异常。探测脚本对「Provider 实际收到的 HTTP 请求体」断言、两种声明各跑一次，证据在探测记录第 2 节。**这条决定 CCA-09 的验收口径：不能把「发过图片」当成「模型看过图」。**
- **CCA-03 的设计前提已满足**：同一 agent 目录起两个进程各自拿到不同 sessionId 且都活着；`new_session` 可用；`--no-session` 下不创建 session 目录（宿主必须自己保存作品状态）；`PI_CODING_AGENT_DIR` / `PI_CODING_AGENT_SESSION_DIR` / `--session-dir` / cwd 都可用于隔离。按「每个运行时一个进程」实现即可，不需要 Pi 侧改动，因此计划 3.2 的替代设计记录不触发。
- **CCA-04 的前提已满足**：`--tools` 确实约束扩展注册的工具。让扩展在 `session_start` 上报 `getActiveTools()`：点名则激活、不点名则移除、省略则给默认集合。创作会话的通用 `bash/read/edit/write/grep/find/ls` 因此可以被整体摘掉。
- **未验证项**：真实视觉 Provider（本机未配置）、Windows 进程清理、Provider 对图像数/分辨率/请求大小的限制。保持待验。

### CCA-02 冻结作品协议与控制器状态模型

- [~] 协议与状态机已冻结并有可执行测试；宿主侧接入（CCA-03/05/10）未做。
- **依赖**：CCA-00、CCA-01。
- **实施**：落实第 4～5 节最小数据结构、事件、错误码和服务边界；实现可独立测试的状态转换与预算，不依赖 HWND。
- **交付**：协议说明、控制器核心、状态/版本/预算测试。
- **验收**：可用 fake 事件覆盖成功、校验失败、取消、预算耗尽、迟到消息和切换作品；模型文本不能驱动「已应用」状态。

**核对结果（2026-09-27）**：

- 新增 `src/include/miaodesk/CreationWorkflow.h` + `src/desktop/control/CreationWorkflow.cpp`：第 4～5 节四类记录、`CreationMessage`（sessionId / epoch / turnId / operationId）、`ApplyIdempotencyKey`、预算（2 轮修复 / 3 次评审 / 6 帧 / 1280px / 8 MiB / 10 分钟墙钟）、八个创作工具的类型与会话绑定上下文。
- 形态是 **reducer + effects**：控制器不调服务、不依赖 HWND、不 import 任何 Windows 头；宿主执行 effect 再把结果作为事件喂回来。因此全部状态转换都能在 macOS 上真实编译并运行。
- 新增 `src/tests/CreationWorkflowStateTest.cpp`：**168 条断言，本机实跑 0 失败**，并接进 `MiaoDeskCreationWorkflowStateTest`（CMake target、链 `MiaoDeskCore`、`/W4`、`run-pure-logic-tests.sh`、Windows CI 的 build 清单与单独一步运行）。
- 覆盖计划 §8「纯逻辑」一行：成功路径、校验失败与有上限自动修复、取消后 epoch 迟到回调、旧轮次回调、brief revision、幂等应用、失败保持原状态、墙钟上限、伪造 sessionId 的工具调用、按阶段区分读写工具。
- **变异测试**：15 个已知失效注入，13 个让测试变红。剩下 2 个是**可证明冗余**的守卫——`ApplyRequested` 的空值检查与 `firstSettle` 强制 true 分别被 `ApplyPreconditionHolds` 和 `IsApplyInFlight` 提前覆盖，不影响任何可观测行为。不为它们补测试，因为那会变成「为了覆盖率假装在测某个分支」。
- 变异过程中查出并修掉四个真缺陷：① `RecordApplyOutcome` 把「新建条目」和「给出结论」混为一谈，一次 `ApplyFailed` 因此不算结算、状态卡在 Applying；② 双击的第二次点击不在账本上，两道 BeginApply 都发得出去（幂等只保护了回调，没保护用户的手）；③ 已经在飞的结算被「阶段不是 Applying」挡掉，取消后正式 API 的真实结果会被丢掉；④ `ApplyRequested` 的阶段守卫 `break` 时不留原因，用户点了按钮却查不到为什么。
- 一处简化：会话记录里原有一个「上一有效候选 ID」字段，与候选列表构成同一个事实的两个来源。清掉其中一份时另一份仍然 Green，看起来没有行为变化——正是双源的典型病症。已删除该字段，唯一来源是候选列表本身。
- **未做**：宿主侧还没有任何代码调用这个控制器。CCA-03 隔离会话、CCA-05 结构化候选、CCA-10 界面接入都不在本轮范围。

### CCA-03 隔离创作会话与资源生命周期

- [~] 启动配置已分开并有契约门；按需运行实例的排队/空闲释放/重开草稿已实现并有可执行测试，**进程未持续增长**仍待 Windows 真机。
- **依赖**：CCA-02。
- **实施**：给现有 Pi 实现增加创作启动配置，分开 session/state/cwd/回调与工具配置；限制一项活跃创作；实现排队、空闲释放、重开草稿；读取同一 Provider 来源。
- **交付**：按需创作运行实例及生命周期测试；相应 runtime/domain 文档更新。
- **验收**：聊天生成中可独立取消/重置创作且不影响聊天；两作品不串上下文；多次开关后无持续进程增长；Provider 修改有明确生效时机。
**本轮补记（2026-09-28，创作 profile 第一次真的被装上）**：

- 此前整条创作链在出厂构建里**一个字节都没生效**，而所有形状门都是绿的：`SetLaunchProfile` 与 `MakeCreatorLaunchProfile` 定义完好、被 CMake 编译、也有人读，但 `src/` 下**没有任何调用者**。`PiRuntime::launchProfile_` 因此始终是结构体默认值，而它的 `mode` 默认是 `Chat`。
- 三条后果都实测过：`--tools` 用聊天那份，七个 `creator_*` 名字被整体剥掉；`--extension` 落盘的是 Chat 变体，它本身不含创作工具；`MIAODESK_CREATOR_WORKSPACE` / `MIAODESK_CREATOR_SESSION` 不导出，而 `src/app/main.cpp` 的 `RunCreatorTool` 正是从这两个环境变量取工作区与会话。**创作轮次里模型一条 `creator_*` 都用不了** —— 八个已实现并测过的工具不可达。这不是"两个配置之间的风险切换"，是一个只有服务端、没有客户端的功能。
- 装的位置是 `ContentCreatorDialog` 而不是 `PiRuntime`：profile 是**宿主的意图**，而"用户主动打开 AI 制作壁纸/组件"这个窗口就是那个事件。放 PiRuntime 里只能猜。
- **卸与装一样重要**：共享 runtime 是刻意的（§4.2 上下文隔离但进程可复用），所以只装不卸会让聊天拿着创作的 allowlist 跑 —— 那正是本节整节在防的事。`WM_DESTROY` 里恢复聊天 profile，且**必须排在 `Stop()` 之后**：反过来会让一个正在跑的创作轮次在恢复中的 profile 下继续，接着要用的工具突然不在 allowlist 里，表现是"生成到一半工具开始失败"。
- 工作区根目录按 `AppPaths` 的既有惯例落在 `<StateRoot>/CreatorWorkspaces/<kind>/<sessionId>`，**一次作品一个子目录**（`main.cpp` 的 `FilesystemCreatorWorkspace` 用 `root_.parent_path()` 放 `revisions/` 与 `candidate-ledger.state`，所以包目录在里、两份宿主持账在它旁边）。复用规则由本计划自己的验收决定：CCA-10 要求"关闭与重开恢复草稿"，所以**重开是同一个工作区** —— `<sessionId>` 记在 `<kind>/active` 里，内容就是目录名本身；`active` 不存在才分配新身份并写回。
- **`<sessionId>` 不能用序号**（2026-09-28 改，此前这里写的是 `…/<kind>/1`）：`DeriveCreatorSessionId` 取路径的**最后一段**当会话 ID，`CreatorWorkspacePolicy::SessionMatches` 拿它和工具参数里的 `sessionId` 比，`ContentCandidateLedger` 又用 `sessionId` 拼 `candidateId`。写成 `1`/`2` 的话，同一 kind 的所有作品共用一个会话 ID —— 而归属判断正是靠这个字符串区分作品的，撞车的表现是"另一个作品的调用被接受了"。这个错误是新加的 `TestTheRealWorkspaceRootTheResolverWillExport()` 撞出来的：它把解析器会导出的那个根喂给策略，而策略对 `…/1` 无法把任何一个 `sessionId` 认成自己的。现在身份由 `NewCreatorSessionId` 生成（时间戳 + 序号 + 8 位随机尾巴），三段各防一件事，逐段由测试断言，八处已知失效注入全红。
- `tests/pi-launch-profile-isolation.mjs` 从"只查 profile 的形状"扩展到"查装上与卸下"：四个新断言各配一个只违反它的注入，全部确认会红（去掉聊天恢复 / 去掉创作安装 / 把恢复挪到 `Stop()` 之前 / 工作区置空）。写它们时顺手修掉自己两个错：没算 `miaodesk::` 限定，以及把相对下标和绝对下标混用（那条断言会变成恒真或恒假）。


**核对结果（2026-09-27，配置层已完成，运行实例层未做）**：

- 新增 `PiLaunchProfile`（`src/include/miaodesk/PiLaunchProfile.h` + `src/desktop/control/PiLaunchProfile.cpp`）：把原来写死在 `PiRuntime.cpp` 函数体里的 agent 目录、Pi session 目录、cwd、扩展路径、工具 allowlist、system prompt 提成数据。两份 profile 各一套，`MakeChatLaunchProfile` 的取值与今天逐字相同。
- `PiRuntime` 改为从 profile 取这些值；profile 未设置时每一项都回落到历史取值，所以普通聊天行为不变。进程 signature 里加入模式名，否则同一份 Provider 配置下两个运行时会互认对方的子进程。
- **防掉的是一类静默污染**：`EnsurePiNativeToolsExtension` 原来写死往一个固定路径写扩展文件。两份配置共用它时，后写的那份覆盖前一份，于是**另一个模式**下一次启动的进程加载到的是这一模式的工具集 —— 表现为「聊天突然不能写文件了」，而没有任何一处代码改过聊天的 allowlist。现在按 variant 分成两个文件，目标目录也可由调用方指定。
- 新增 `tests/pi-launch-profile-isolation.mjs`（已接进 repo-hygiene CI）：断言聊天 allowlist 一个字节没变、创作侧每一项都分开、两份 allowlist 只共享计划 §5 标为复用的 `content_skill_get`、`--tools` 与 `PI_CODING_AGENT_SESSION_DIR` 真的来自 profile、新文件进了 CMake。9 个已知失效全部让它变红。
- 副作用与处理：allowlist 换了名字和文件，`tests/tool-friendly-name-coverage.mjs` 需要跟着读新位置。已改为同时读两份 allowlist，并把创作侧那七个尚未实现的工具登记成**显式例外**——以后往创作 allowlist 里加工具而不给中文名，那条测试会要求下一个作者当场做决定，而不是静默通过。
**按需运行实例的调度（2026-09-28 增补）**：

- 新增 `CreatorRuntimeScheduler`（`src/include/miaodesk/CreatorRuntimeScheduler.h` + `src/desktop/control/CreatorRuntimeScheduler.cpp`）：`Idle → Starting → Active → Releasing` 四个状态，加一条 FIFO 队列。它只做**决策**，不认识 `PiRuntime`、不 import 任何 Windows 头：宿主拿着 `StartProcess` / `StopProcess` 这类 effect 自己去执行，结果再作为事件喂回来。
- 为什么不在 `PiRuntime` 里直接写：那里持有真实句柄与 `CreateProcessW`，于是「第二个创作请求进来时会发生什么」只能在 Windows 上试。而判错的代价在本机就该看得见，而不是留到用户第二次打开创作时才炸。
- 计划原文说这一条的验收需要 Windows 真机，那指的是「进程真的没有泄漏」。而**谁在什么时候拿到名额**是纯逻辑，而且它恰好是泄漏的成因：真正让进程堆起来的是「发起了一次启动，而当时已经有一个活着」。所以这里把后者钉死，前者才有一个不会发生的理由。真机那一半仍然待验，没有在本机宣称通过。
- 新增 `src/tests/CreatorRuntimeSchedulerTest.cpp`：**158 条断言，本机实跑 0 失败**，接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。10 个已知失效注入全红。
- 覆盖计划验收的每一条，各配一个只违反它的输入：同时最多一个创作（第二个请求只排队）、没有绑定的请求被拒而不占名额、取消只影响自己那一轮、撤掉排队不影响正在跑的那个、12 轮开关之后每次启动都发生在没有活跃进程的前提下、启动失败不卡在 Starting 且队列立刻顶着上、释放失败**不**被当成已释放、迟到回报被拒、空闲到点释放、只有当前会话的活动算续命、启动超时收回名额、重开草稿仍是同一个会话与工作区、同一会话不重复排队。
- 测试查出两个实现缺陷，都不是预想的那类：
  1. **撤排队只在 `Idle` 时做**。于是「另一个创作正在跑时，用户关掉自己那个排队的」会落到「停掉当前进程」那条路上 —— 他关的是自己那一轮，却被执行成取消别人的作品。这正是「取消只影响它自己那一轮」要防的事，而它只在 Active 同时有排队时才发生。
  2. **`activeSinceMs_ != 0` 这个守卫让 t=0 打开的创作永远不可能启动超时**。t=0 是合法时间戳；加上哨兵值之后，它卡在 Starting、占着全产品唯一的名额，而用户看不到任何反馈。`state_ == Starting` 已经保证这个字段被设过，再要一个哨兵值只是多余且有害。
- 另有一处是**测试自己写错**而不是实现错：续命之后我断言「再过 10ms 又到点」，而续命的含义是计时器重新起算，要到「新活动 + 整个空闲期」才到点。
- **仍未做**：宿主侧把它们接起来（真正持有一个 `PiRuntime` 实例、按 effect 起停）；Provider 修改的生效时机；以及真机上的「多次开关后无持续进程增长」。

### CCA-04 提供受约束的包制作与素材工具

- [~] 路径与归属策略、写入事务、工具名册、工作区状态、worker 分发、候选封存与结构校验均已实现并有可执行测试；**端到端生成合法包**与"关闭通用工具后仍可完整制作"的对照验证仍待 Windows 真机。2026-09-28 补：创作 profile 真的被装上（此前整条创作链在出厂构建里不生效），工作区最后一段改成会话身份本身。
- **依赖**：CCA-02、CCA-03。
- **实施**：提供包读写、素材导入/图片生成和真实能力查询；用创作专属 allowlist 替代通用文件/shell 权限；接入会话绑定和超时。
- **交付**：实际 Pi 扩展与 native adapter、工具 worker 注册、路径与所有权回归。
- **验收**：两种 kind 均能真正生成合法包；错误后无半写文件；拒绝跨作品访问、越界路径、reparse point 与未经允许的代码产物；关闭通用工具后仍可完整制作。

**核对结果（2026-09-27，策略层完成，工具本体未接）**：

- 新增 `CreatorWorkspacePolicy`（`src/include/miaodesk/CreatorWorkspacePolicy.h` + `src/desktop/control/CreatorWorkspacePolicy.cpp`）：创作工具的路径与作品归属策略。它**只判定、不碰盘** —— 文件大小、是否 reparse point 这类事实由宿主喂进来,策略只回答"允不允许、为什么"。
- 归属判断不自己查盘,是有意的:归属的事实来源是宿主自己的会话记录,而工具 worker 是另一个进程,拿到的只有 JSON 参数。工作区根由宿主从自己的状态里取出来传进来,策略按它判定 —— 于是"模型伪造 sessionId 访问另一个作品"在结构上不可能,而不是靠记得校验。
- 覆盖验收原文的每一条,且每一条都有拒绝码:`NotRelative`(盘符/UNC/根斜杠/反斜杠形式)、`Traversal`(任何一段 `..`,包括后半段会回到工作区里的)、`ForbiddenExtension`(代码与可执行产物)、`UnknownRole`(不在 `manifest.json`/`parameters.json`/`scene/*.json`/`preview.<图片>`/`assets/<受控素材>` 这套布局内,含 `assets` 下再建子目录、尾斜杠)、`TooLarge`(按类型分别设上限)、`WrongWorkspace`(跨作品)、`ReparsePoint`、`EmptySessionId`。
- 拒绝必须可定位:每条规则一个拒绝码 + 一句给人看的原因。第一版 `Allows()` 只返回 bool,于是"代码产物被拒"和"路径不在布局内被拒"在测试里长得一模一样 —— 实测把 `IsForbiddenExtension` 那一行短路掉,整个测试依然全绿,因为 `Classify` 仍以 `UnknownRole` 拒绝同一个路径。加了 `rejectCode` 输出之后那条改动立刻让 20 个断言变红。
- 反向断言同样重要:`MustAllow` 覆盖合法的九种路径,**以及它们的等价写法**（`./`、`//`、`/.`、反斜杠）。同一份文件换一种写法就该归到同一个角色,否则模型换个分隔符写法就被判 UnknownRole,而它以为路径是对的。
- `IsForbiddenExtension` 的措辞是"代码/可执行文件不在创作范围内",不是"不认识":计划 §1 明确本轮内容限定为声明式 Scene 内容与受控素材,所以这不是"暂时没实现"。
- 新增 `src/tests/CreatorWorkspacePolicyTest.cpp`:**359 条断言,本机实跑 0 失败**,接进 `MiaoDeskCreatorWorkspacePolicyTest`、`run-pure-logic-tests.sh` 和 Windows CI。
- 变异测试:26 个已知失效注入。其中 4 个让测试变红,剩下一批是**冗余守卫**（UNC 由根斜杠检查兜住、空工作区由第二处空值检查兜住),逐个确认过不影响可观测行为。变异过程查出两个测试自身的空洞,都在上面记了:`MustReject` 收了 `expected` 却从不用；两处断言只测了"assets"这个本身就通不过分类的路径,于是 `isDirectory` 那行短路掉测试依然全绿。

**包写入事务（2026-09-27 增补）**：

- 新增 `CreatorPackageTransaction`（`src/include/miaodesk/CreatorPackageTransaction.h` + `src/desktop/control/CreatorPackageTransaction.cpp`）：把 `Planned → Validated → Staged → Committed` 以及每步的失败出口显式建模成阶段机。它只判规则、不碰盘 —— 宿主按顺序执行几个动作,事务判定每一步能不能进。所以**整条失败路径能在任何机器上被执行到**,而不是只留在 Windows 上等真机。
- 摘要从事先算好:构造时就算出 before 与 after,Commit 阶段拿真实落盘内容再算一次,两者必须逐字节相同。允许"差不多就行"的话,封存与校验都失去锚点 —— 我们以为改了 X,实际改了 Y。
- 两处语义是这一轮改对的,都不是预想的那类问题:
  1. **提交失败不是 Rejected**。Commit 校验失败时目标文件已经被换掉了,副作用已发生,所以记成 `Unverified`。记成 Rejected(意味着"什么都没动")会让调用方以为可以若无其事地继续。`Unverified` 的下一步只能是标记候选失效。
  2. **顺序不对的调用不改阶段**。跳过暂存直接提交、跳过校验直接暂存,这些是 `Refuse` 而不是 `Reject`:阶段保持原样。用 Reject 表达后者会把一个已提交的事务改写成 Rejected,于是"它到底提交了没有"取决于最后一次调用的顺序 —— 那正是要避免的不可判定状态。
- 新增 `src/tests/CreatorPackageTransactionTest.cpp`:**84 条断言,本机实跑 0 失败**,接进 `MiaoDeskCreatorPackageTransactionTest`、`run-pure-logic-tests.sh` 与 Windows CI。9 个已知失效注入全红;变异过程中查出并删掉两处**冗余守卫**(暂存处的 reparse point 复查已被 `policy_.Allows` 覆盖;`after` 摘要可用性在 `before` 不可用时必然一起失败),并补了一条原先缺失的覆盖:工作区本身不完整(只有 manifest、没有 scene)时任何写入都要被拒 —— 我的快照一直取完整的,那两道闸短路掉测试全绿。
- **仍未做（属于本条剩余部分）**:
**工具名册与两份清单一致（2026-09-27 增补）**：

- 新增 `CreatorToolRegistry`（`src/include/miaodesk/CreatorToolRegistry.h` + `src/desktop/control/CreatorToolRegistry.cpp`）：八个 `creator_*` 工具的名册、mutating 分类,以及一次路由。路由只决定"这个调用能不能进、由谁执行",不碰盘。
- 这一步直接对应验收里的"按进程/会话配置验证扩展实际拿到的工具清单"。它不是细节:Pi 侧的 `--tools` 写在 `PiLaunchProfile.cpp`,worker 的允许表写在 `src/app/main.cpp`,**两者彼此不知道对方**。而不一致的故障是单向静默的 —— Pi 给了而 worker 不认,则 worker 静默退出 26、模型反复重试、用户看到"它一直不成功";worker 认而 Pi 没给,则能力躺着没人用、没有任何东西报错。
- 现在 `IsAllowedPiNativeTool` 改为调用 `miaodesk::creator::IsCreatorTool(...)`,名册是唯一事实来源。新增 `tests/creator-tool-surface-contract.mjs`（已接进 repo-hygiene CI）：从名册、Pi 的 allowlist 字面量、worker 的函数体三个源头读,要求两两覆盖一致,并规定两份 allowlist 除 `content_skill_get` 外不得有任何共享项。三个已知失效(把 worker 退回自己的字面量表 / 往 allowlist 加一个名册里没有的工具 / 从名册删一个工具)全部让它变红。
- 新增 `src/tests/CreatorToolRegistryTest.cpp`:**163 条断言,本机实跑 0 失败**,接进 `MiaoDeskCreatorToolRegistryTest`、`run-pure-logic-tests.sh` 与 Windows CI。覆盖名册往返、mutating 分类、跨作品(伪造 sessionId / 工作区不符 / 已取消)、未知工具、逐阶段判据、以及每个工具各自的必填参数。
- **可重试与不可重试要分开**:缺参数值得让模型重试,归属/阶段/取消不值得 —— 混为一谈的后果是模型反复重试同一个越界调用,而用户看不到任何进展。
- 变异测试 14 个注入全红。过程中查出三处覆盖空洞:`TestRequiredArgumentsPerTool` 传的是**参齐了的**调用,于是 content / digest / source 三道闸短路掉全绿 —— 已改为每个参数单独一条"缺它就被拒 + 补回来就放行"。

**Pi 扩展与 worker 分发已接上（2026-09-27 增补）**：

- `PiNativeToolsExtension.cpp` 的 Creator variant 现在是一份**独立的源**,不是"少几个工具的聊天那份"。它不含任何通用 shell / 文件 / 图片 / 桌面工具:八个创作工具全部转发给宿主 worker,扩展本身没有任何写盘路径(连 `generateImage` 那套都不在里面 —— 创作的 cwd 就是工作区,把图片写进 `process.cwd()` 会整个绕开 `assets/` 布局)。
- `TOOL_NAMES` 由 `CreatorToolNames()` 在 C++ 侧生成,不手写第二份。名册加一个工具而这里漏注册的故障是静默的:模型从来没见过它,没有任何东西报错。生成它,这个方向就不可能漂移;`tests/creator-extension-source.mjs` 反向断言注册体覆盖名册,把另一个方向也堵住。
- 新增 `tests/creator-extension-source.mjs`(已接进 repo-hygiene CI):把生成的源按 `CreatorExtensionSource()` 的做法拼出来,用 Node 自己的解析器做一次真解析,再断言名册里每个名字都注册了、聊天那一套一个都没进来。8 个已知失效注入全红。
- 这一轮在这里踩过一次,记下来是因为它比失败本身更值得记:第一版的语法检查写的是 `node --experimental-strip-types --check`,而它**对带 import 的 .ts 文件无条件返回 0** —— 用一个故意写错的文件试过才知道它什么都不查。一个永远绿的闸门比没有闸门更糟,因为它会让人觉得这里验证过了。现在改成 `import()` 后按错误种类判断,并且用"故意写坏的文件必须报 SyntaxError"当探针:抓不到就退出 2,不假装通过。
- 会话与工作区改为宿主提供:新增 `MIAODESK_CREATOR_SESSION` / `MIAODESK_CREATOR_WORKSPACE`,**只对创作进程导出**。聊天进程拿不到,于是即使最坏情况下聊天侧加载到了创作扩展,工具调用也会因为拿不到绑定而被拒,而不是拿到一个指向桌面目录的工作区 —— 空值在这里是安全的那个方向。

**worker 侧分发（2026-09-27 增补）**：

- 新增 `CreatorWorkspaceState`（`src/include/miaodesk/CreatorWorkspaceState.h` + `src/desktop/control/CreatorWorkspaceState.cpp`）：工具 worker 是**每次调用一个新进程**,手里没有任何宿主内存,所以"现在是什么阶段、取消没有、当前候选摘要是什么"必须落在工作区里 —— 宿主写、worker 读。行式 key=value,不引入 JSON 依赖;已知键重复出现即拒绝(两份矛盾的值没有该信的那一份),未知键忽略(将来加字段时旧文件仍读得出来)。
- 新增 `CreatorToolWorker`（`src/include/miaodesk/CreatorToolWorker.h` + `src/desktop/control/CreatorToolWorker.cpp`）：把路由接到真实分发。归属判定在宿主侧再做一次交叉复核(环境变量 vs 工作区状态文件 vs 路径导出的会话),然后才允许碰盘。
- `CreatorWorkspacePort` 是宿主对工作区的唯一入口。用抽象而不是直接传 `<filesystem>`,是为了让决策逻辑在 macOS 上真跑:一个 in-memory 实现能让"写坏一个字节之后"的每一条分支都被执行到 —— 这些恰好是最不该只留给 Windows 真机的路径。
- **"未实现"与"被拒绝"被显式分开**。`AvailabilityOf` 逐个工具列明它现在能不能执行,`creator_image_generate` / `creator_preview_evidence` / `creator_candidate_submit` 明确标为未实现并说明缺什么。此前这三个会落进默认分支,失败长得像一次拒绝 —— 而模型下一步该做的事完全相反:被拒绝该改参数,未实现该告诉用户这一步没做好。
- 新增 `src/tests/CreatorToolWorkerTest.cpp`:**183 条断言,本机实跑 0 失败**,接进 `MiaoDeskCreatorToolWorkerTest`、`run-pure-logic-tests.sh` 与 Windows CI。18 个已知失效注入全红。
- 变异过程查出三处我自己的实现缺陷,都不是预想的那类:
  1. **回退会把拒绝原因抹掉**。暂存内容与计划不一致之后我调了 `Rollback()`,于是 `Rejected` 被改写成 `RolledBack` —— "因为暂存内容对不上而被拒"这件事再也问不出来。已改为不回退:什么都没发生,`Rejected` 就是实话。
  2. **替换失败时的三种结局被混成一类**。现在按落盘内容分别处理:与计划一致算落地(但必须点明宿主报过错)、仍是写入前算 `ReplaceFailed`、两者都不是才算 `Unverified`。其中"报告失败但内容其实对上了"这一支原先被悄悄说成"已写入",那次失败报告会从此消失。
  3. **`DeriveCreatorSessionId` 把 `C:\` 当成会话名 `C:`**。同一个盘上所有作品会共用一个会话,而归属判断正是靠这个字符串区分作品的。根路径没有"最后一个目录",所以它没有会话。
- 另外查出三处**测试自身**的空洞,都是"两条守卫同时命中,删掉任一条都看不出来":伪造 sessionId 的用例让工作区路径也一起不符、"空工作区"与"只有分隔符的路径"拿到同一个拒绝码、以及第一版 port 的 `ReplaceTarget` 从 `stagedContents` 里找不到内容(于是每一笔本该成功的写入都看起来像失败,而那是 port 的 bug)。逐个隔离后三条注入立刻变红。
- `src/app/main.cpp` 接上真实文件系统:`FilesystemCreatorWorkspace` 实现同一个 port —— 路径先 `NormalizeCreatorRelativePath` 再过 `weakly_canonical` 确认仍在工作区内(挡联接点与大小写变体),reparse point **真的去问文件系统**(不问的话 junction 猜不出来),暂存写完**回读**再报事实(不回读就绕过了事务对暂存内容的核对),替换走 `MoveFileExW` + `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`。
- JSON 字符串字段提取从 `PiRuntime.cpp` 提到 `content/package/ContentJsonStringField.cpp`,两处共用一份。分成两份的后果不是重复代码,是**两处可以不一致** —— 而不一致的地方恰好是 `creator_package_update` 的 content:一个把 `\n` 解错、一个不解,写进包里的就是另一份内容,而两边都以为自己对。
- 新增 `tests/creator-tool-worker-dispatch.mjs`(已接进 repo-hygiene CI):断言创作工具在 `ExecuteNativeToolRaw` 之前被分派走、归属事实来自宿主环境而非参数、暂存回读、`MoveFileExW` 的原子性、reparse point 真的被问。7 个已知失效注入全红。这个闸门只管**形状**层面;哪一条分支真的会发出哪个拒绝码由 `CreatorToolWorkerTest.cpp` 承担(那里也做过变异验证),文件里写明了这个分工 —— 把 `reply.code = "Unverified"` 改成 `"Rejected"` 时形状闸门是绿的,因为另一处有同样的字符串。
**候选提交与结构校验（2026-09-27 增补）**：

- `creator_candidate_submit` 已接上,`creator_capabilities_get` 的回执里现在带**当前候选摘要** —— 模型必须能拿到它,否则 `creator_candidate_submit` 的 digest 参数只能猜,而猜错就是一次 DigestMismatch。
- 新增强制顺序:**摘要来自宿主对当前快照的计算,不是模型给的那个字符串**。模型给的只用来对账。一个字符串就能决定封存什么的话,模型可以指着旧内容拿到新 revision,也可以把两个不同的包说成同一版。
- `ContentCandidateLedger` 增加 `Serialize()` / `Parse()`。worker 每次调用都是新进程,所以"第几版、封存了哪些摘要、哪些已失效"必须能落盘再读回来 —— 否则每次调用都从第 1 版开始,而"同路径改内容生成新 revision"永远验证不了。解析失败时 `Parse` 返回 false,调用方必须当成"没有台账"而不是"空台账":后者会让这次提交被当成第 1 版,而盘上明明已经有第 3 版。
- 封存是**工作区之外**的一份独立副本(`../revisions/<摘要前缀>.sealed`)。指向工作区的话,"封存后修改源目录不能改变待应用候选"这条名存实亡 —— 因为封存的就是源目录。同一摘要再封一次是幂等成功:否则"同一份内容再次提交返回同一版"这条就废了。
- 封存成功后**必须**把新摘要写回工作区状态文件。不做这一步,状态里的候选摘要一直停在旧值,下一次带 `expectedDigest` 的写入会被当成"基于旧视图"而全部拒绝。
- 新增 `ContentPackageValidator`（`src/include/miaodesk/ContentPackageValidator.h` + `src/content/package/ContentPackageValidator.cpp`）:结构校验,纯逻辑。它检查的是**封存快照的字节**,不是盘上此刻的文件 —— 否则校验通过之后源目录再被改一下,这个结论还挂在上面。
- 校验的字段取自 `MiaoContentPackage` 的加载器要的那一套(`schema/id/name/version/kind/runtime/entry`),不是这里另定一套:校验器和加载器必须说同一种语言,否则"校验通过"到了加载那一刻还是失败,而那一次失败发生在用户眼前。`runtime: web` 显式拒绝 —— 计划 §1 明确本轮只做声明式 Scene 内容与受控素材。
- 新增 `src/tests/ContentPackageValidatorTest.cpp`:**119 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。9 个已知失效注入全红。
- 变异过程查出两个我自己的测试缺陷:(1) `schema` 用例多删了一个字符,把后面的 `id` 也带走了,于是它因为"缺 id"而失败 —— 测试绿了但绿得不对;(2) 把 `"1st"` 当成非法 id,而我按的是"标识符不能以数字开头"的常识,加载器的规则只要求首字符是 `isalnum`,数字满足它。校验器比加载器更严的后果是拒绝它本来能加载的包。
- 另外修掉两个实现缺陷:`HasField` 只查键在不在,于是 `"kind":""` 被放过(空串到了加载器那里仍然失败,只不过发生在用户眼前),改为 `HasNonEmptyField`;台账解析的 revision 用 `strtoul`,对 `"abc"` 静默返回 0,而 0 在回执里的含义是"被拒绝、没有分配版本号" —— 于是一条坏行会被读成一个"被拒绝的候选"。

**共享字段读解器与它自己的缺陷（2026-09-27 增补）**：

- `ExtractJsonString` / `ExtractJsonInt` 从 `PiRuntime.cpp` 提到 `content/package/ContentJsonStringField.cpp`,两个调用方(Pi 的 RPC 事件、创作 worker 的参数)共用一份。分成两份的后果不是重复代码,是**两处可以不一致** —— 而不一致的地方恰好是 `creator_package_update` 的 content。
- 新增 `src/tests/JsonStringFieldTest.cpp`:**24 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。
- 这里查出本次最值得记的一个缺陷:**"键位置"**。`find` 一个带引号的字段名会命中任何出现的地方,包括**值** —— `{"kind":"id"}` 里含有 `"id"`,于是裸字符串查找把"id 字段缺失"判成"id 在",而同一个文件在两处判据下得出两个不同结论。第一版的 `HasField` 传裸字段名、取值传带引号的键,两边甚至不是同一个键。现在统一为"前面只可能有空白,再往前必须是 `{` 或 `,`"才算键位置。
- 这个修法对 Pi 的 RPC 解析同样正确:某条事件的值恰好等于另一个字段名时,旧的按子串查找会取错值。它只会让误命中变少,不会改变本来正确的情形。
- 另一处:`from_chars` 会停在第一个不是数字的字符上,所以 `{"schema":1abc}` 被读成 1。一份写坏的 manifest 该被拒绝,而不是被静默读成一个合法值 —— 现在要求数字之后紧跟 `,` `}` `]` 或空白。

- **仍未做（属于本条剩余部分）**:
  - `creator_image_generate` 需要图片 Provider 落地;`creator_preview_evidence` 需要真实渲染后端。两者都不在本轮范围,已由 `AvailabilityOf` 显式标为未实现。
  - 两种 kind 端到端生成合法包,以及"关闭通用工具后仍可完整制作"的对照验证 —— 这两条要 Windows 真机,不能在本机或 CI 里宣称通过。

**本轮补记（2026-09-28，工具可用性表与不可用理由表不再能互相矛盾）**：

- `AvailabilityOf` 与 `UnavailableReason` 是**两份各自手写的表**。CandidateSubmit 早就接上了(宿主有真实校验、封存、摘要与台账),而原因表里还留着它一句"候选封存在当前构建里还没有接上。请不要声称已经提交或已经可以应用"。
- 那句话**从没被走到,所以没人发现**。而它一旦被走到,会以"宿主说的"身份让模型否认一件**刚刚真实发生过**的事 —— 用户那边看到的是候选明明提交成功、回执也拿到了,模型却说"我没有提交,也不能提交"。一个说反了的默认值比没有默认值危险。
- 改法不是删字符串,而是把不变式写进实现:`UnavailableReason` 先看 `AvailabilityOf`,可实现的一律返回空串。这样两张表再也不会分叉 —— 下一次有人往默认分支里加一条,不会静悄悄地作用到一个已接上的工具上。
- `CreatorToolWorkerTest` 从 241 条加到 **251 条**,新增的两条各自钉住这个漏洞的一侧:名册里每个工具都必须满足"可实现 ⇔ 没有不可用的理由";以及具体到 CandidateSubmit 那一格,它现在必须是空的,而一次真实的提交回复不可能是 `Unavailable`。
**再补记（2026-09-28，系统提示词此前让模型去调一个调不通的工具）**：

- 创作者的系统提示词写着:"用 `creator_asset_import` 导入已有素材、**用 `creator_image_generate` 生成你描述的图**"。而 `creator_image_generate` 是**未实现**的 —— 它需要一个图片 Provider,而模型路由不在本轮范围内(计划明确排除)。`AvailabilityOf` 把它标成 `NotImplemented`,`creator_capabilities_get` 也如实写着"未实现"。
- 提示词与能力自述**两张嘴说不同的话,而模型会信提示词那一张**。后果不是一次失败的调用(那反而好排查),是模型向用户承诺一件做不到的事:"我这就给你画一张"。
- 提示词现在改成:图片生成器在当前构建里不存在,不要调用它,也不要承诺画图;请用户导入素材,或者直说这一步还没有。新增 `ExecutableCreatorToolNames()` —— 名册里**现在真能用**的那几个名字,给提示词与文档用,让"你可以用 X"这句话只能在 X 真能用时说。
- `CreatorToolWorkerTest` 再添一条:未实现的 `creator_image_generate` 不在"现在能用"的名单里,而 `creator_asset_import` 在;且名单不能是空的(空了说明函数坏了,不是没有工具)。该测试随之到 **254 条**。
- 顺带:`AvailabilityOf` 与 `UnavailableReason` 两张表此前能互相矛盾(CandidateSubmit 早已接上却仍带着"还没有接上"的理由),已改为由可用性表单向推出不可用理由 —— 见上一段记录。
- **2026-09-28,工作区根目录的会话身份**:上面那条"落在 `…/<kind>/1`"是错的,已改。`DeriveCreatorSessionId` 取最后一段当会话 ID,序号让同一 kind 的所有作品共用一个会话 ID —— 新加的 `TestTheRealWorkspaceRootTheResolverWillExport()` 把它喂给策略才发现。现在 `<sessionId>` 由 `NewCreatorSessionId`(时间戳+序号+随机尾巴)生成,记在 `<kind>/active` 里,重开复用;`CreatorToolWorkerTest` 随之到 **314 条**,其中会话身份那十处已知失效注入全红(删尾巴、尾巴冻成常量、大写十六进制、尾巴缩到 4 位、序号不用、身份写死 `"1"`、字符线整体松掉、多收冒号、读取方丢连字符、读取方不过滤)。生成与读取现在共用 `IsUsableSessionChar` 与 `SanitizeCreatorSessionId` —— 原先两头各写一份,而"身份生成出来却读不回来"这种错只由 JS 门看文件形状,现在它是一条本机真跑的等式(`SanitizeCreatorSessionId(NewCreatorSessionId(t)) == 那个身份`)。装 profile 那条门另注入 5 处,全红。
### CCA-05 结构化候选与统一校验

- [~] 候选摘要、结构化 receipt、宿主封存均已实现并有可执行测试；`ContentCreatorDialog` 里的回复路径猜测仍在，尚未被替换。
- **依赖**：CCA-04（工具侧尚未落地，本条先做了摘要这一层）。
- **实施**：候选提交返回结构化 receipt；宿主通过真正的包校验服务验证、复制封存、生成 digest/revision；逐步移除正常流程的回复路径猜测。
- **交付**：候选存储/校验服务、错误定位格式、文本路径兼容策略。
- **验收**：错 kind、缺素材、非法 binding/参数、错误后端等均产生可定位失败；同路径改内容生成新 revision；封存后修改源目录不能改变待应用候选。兼容入口若保留也必须过相同校验。

**核对结果（2026-09-27，只做了摘要层）**：

- 新增 `ContentCandidateDigest`（`src/include/miaodesk/ContentCandidateDigest.h` + `src/content/package/ContentCandidateDigest.cpp`）：候选的内容身份。按「角色 → 包内相对路径」排序后，对 manifest / scene / 参数 / 引用素材 / 其余文件做长度前缀编码再取 SHA-256。
- 计划的两条原文要求各自对应一种会真实发生的故障，都有断言对着：只 hash manifest 会让改了一层还留着旧校验结论；hash 绝对路径会让同一份内容每次重新校验都变成新候选，幂等永不命中。前者由「每个部分单独改一个字节都必须换摘要」覆盖，后者由「同一份内容两次计算得到同一个摘要」覆盖。
- 摘要里带一个 `complete` 标志：缺 manifest / 缺 scene / 有空路径分块 / 空快照 / 同一文件列两次,摘要都标记为不可用。理由是这类残缺包仍然算得出一个看起来正常的哈希，不标记就等于给了一个合法身份。
- SHA-256 是自带的，**先用已发表测试向量验原语再验用法**：空串、`"abc"`、56 字节、100 万个 `a`、以及 55/56/64/65 字节的填充边界。没有拿"自己实现的 hash"自我认证。
- 编码可逆性用穷举证明：768 个"完整候选 + 两块可变分块"组合，要求摘要两两不同。它挡住的是去掉长度分隔后出现的真实碰撞 —— [Asset p="a"] + [Other q=role+path+"B"] 与 [Asset p="a"+role+path] + [Other q="B"] 裸拼接后是同一条字节流，两个不同的候选共用一个摘要,一个的校验结论就此盖在另一个头上。已用已知失效确认这条会红。
- 新增 `src/tests/ContentCandidateDigestTest.cpp`：**69 条断言，本机实跑 0 失败**，接进 `MiaoDeskContentCandidateDigestTest`、`run-pure-logic-tests.sh` 和 Windows CI。
- 变异测试：19 个已知失效注入，全部让测试变红（含 6 个 SHA-256 原语层的：旋转常量、初始向量、Sigma/sigma 位移、填充字节、尾块长度、输出字节序）。
- **结构化 receipt 与 revision 台账（2026-09-27 增补）**：

- 新增 `ContentCandidateLedger`（`src/include/miaodesk/ContentCandidateLedger.h` + `src/content/package/ContentCandidateLedger.cpp`）与结构化校验结果（`ContentValidationIssue` 带 `file` + `nodePath` + `repairable`,`ContentValidationFailure` 十二个枚举值）。
- **摘要只可能来自宿主封存的快照**。`claimedPath` / `claimedId` 一律按输入处理:模型报一个路径、内容却是别的,摘要照样"对得上",封存就白做了。测试用两个不同的 claimedPath 与一个假的 claimedId 交同一批 parts,断言摘要与 revision 都不变。
- **同路径改内容 = 新 revision；换路径不改内容 = 同一版**。两条都有断言,且都带反例:换路径再交一次必须仍拿第一次封存的那份快照,否则"封存后不可变"会白说。
- **失败不分配 revision**。给它 revision 等于说"这是某个有效候选的第 N 版",而它并没有通过校验。同一份内容第二次附带失败结论也不会把已通过的记录改成失败,反过来亦然 —— 第一次发生的结论是事实,不能被后来的调用覆盖。
- **宿主标记失效后不能靠"再交一次同样的内容"复活**:摘要没变说明它仍指向那份已经不可信的封存。这条是变异测试查出来的 —— 第一版 `LastValid` 用 `invalidated_` 与 `accepted` 两处都判一遍,而 `Invalidate` 已经把 `accepted` 清成 false,于是短路掉 `invalidated_` 那一处测试依然全绿。同一件事查两遍没意义,已删掉重复的那处,只留 `accepted`。
- 新增 `src/tests/ContentCandidateLedgerTest.cpp`:**57 条断言,本机实跑 0 失败**,接进 `MiaoDeskContentCandidateLedgerTest`、`run-pure-logic-tests.sh` 和 Windows CI。7 个已知失效注入全部让测试变红,其中"摘要改用模型报的路径"一条同时红 6 处。
**结构化回执与回复路径猜测（2026-09-28 增补）**：

- 新增 `ContentCandidateReceipt`（`src/include/miaodesk/ContentCandidateReceipt.h` + `src/content/package/ContentCandidateReceipt.cpp`）：解析并**核验**一次候选提交的结构化回执。它要替掉的正是 `ContentCreatorDialog` 里那两条猜测 —— 从模型回复正文里正则扫一个 `.mdwall` 路径、再扫一个"看起来像内容包目录"的候选。猜中的代价不是难看,是**不可判定**:用户在正文里提到任何一个路径都会被当成这次生成的产物,于是"它到底做出来了没有"取决于模型怎么说话。
- 所以这里做两件事,而第二件才是重点:解析回执行,再拿**宿主自己的台账**对账 —— 摘要、candidateId、revision 三项都必须在台账里且一致,会话与 epoch 也必须与宿主持有的那次创作对得上。模型在正文里写一行格式正确的 `[receipt]` 是不够的。
- 拒绝的理由必须具体:会话不符、来自另一轮、台账里没有这个摘要、revision 不一致、candidateId 不匹配、已被标记失效,各自一个结论。一句"不可信"等于让排查从零开始。
- 新增 `src/tests/ContentCandidateReceiptTest.cpp`:**50 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。9 个已知失效注入全红。
- 最要紧的三条用例都不是"能解析":① 模型自己写一行 `[receipt]`,字段格式全对但 sessionId 是别的作品;② 摘要与 candidateId 都对,但 epoch 是上一轮的;③ 回执说 accepted,而宿主台账里根本没有这个摘要。另外④ 回执行必须**顶行** —— 半句话里出现的 `[receipt]` 是模型在描述它在做什么,不是工具返回的结构化数据。
- 查出并修掉一个我自己的顺序错误:**失效判断必须排在 `Sealed()` 之前**。`ContentCandidateLedger::Sealed()` 的判据是 `receipt.accepted`,而 `Invalidate()` 会把 accepted 置回 false —— 于是"封存过但已被标记失效"和"从来没封存过"在 `Sealed()` 看来是同一件事。第一版顺序是反的,于是一个被宿主主动失效的候选拿到的是"台账里没有这个摘要",而它明明在台账里,那条错误信息会把排查引向完全错误的方向。
- `CreatorToolWorker` 的提交回执现在**第一行就是结构化那一行**,人话跟在后面。宿主据此确认"有一个可用候选",不再需要从散文里猜。`CreatorToolWorkerTest` 里加了一条:把回执 payload 交给 `VerifyCandidateReceipt`,用从 port 落盘台账重建出来的台账核验,结论必须是 `Trusted` —— 这正是宿主(新的那个进程)会做的事。
**回复凭据分级（2026-09-28 增补）**：

- 新增 `CreatorReplyInterpreter`（`src/include/miaodesk/CreatorReplyInterpreter.h` + `src/desktop/control/CreatorReplyInterpreter.cpp`）：读一段工具结果,并**说清凭据是哪种**。`Receipt` 是宿主核验过的结构化回执,是唯一能让宿主不猜的那种;`ProseScan` 是正文里扫到的路径,仍然可用,但不能单独驱动"可以应用"。
- 分成两种不是洁癖:把两者混成"找到了",用户就无法知道现在看到的东西是工具交回来的,还是模型一句话里提到的 —— 而这两者的可信度差得很远。`trustworthyWithoutFurtherChecks` 就是给宿主的那句明示。
- **回执可信时,包就是工作区本身**。创作工具写的就是它;这一刻宿主不必再从正文里扫路径 —— 扫到的任何路径都不比"宿主持有的工作区"更可信。
- **一段存在但没通过核验的回执,必须一直带到结论里**。第一版只在"正文也没扫到东西"时才提它,于是一条被拒的回执,只要正文里恰好还提到一个路径,就消失得无影无踪 —— 而那正是最该让人知道的事:有东西被拒绝过,而现在准备采信的是一个更弱的凭据。
- `ProsePathIsUsable` 五道判据(存在 / 是目录 / 有 manifest / 扩展名对得上 kind / 在工作区内)逐条有一个拒绝原因。少一道,"别的作品目录里的一个 .mdwall"就会被当成本次的产物。
- 新增 `src/tests/CreatorReplyInterpreterTest.cpp`:**37 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。7 个已知失效注入全红。
- 顺带撞到一个**既有的命名地雷**:`ContentCreatorKind` 在 `miaodesk::creator` 里有两个定义(`ContentCreatorBridge.h` 与 `CreationWorkflow.h`),此前只是没人同时 include 两者。现在按 bridge 的那一个引用,并在注释里写清为什么 —— 这是会复发的坑。
- **仍未做，而根因与我先前写的不同（2026-09-28 更正）**：把 `InterpretCreatorReply` 接到 `ContentCreatorDialog`。我先前写"界面上取不到会话 ID、工作区、epoch、台账"，**这一句把因果说反了**。真正的原因是一次**从未发生的调用**：`PiLaunchProfile.h` 有 `SetLaunchProfile`，`PiLaunchProfile.cpp` 有 `MakeCreatorLaunchProfile`，两者定义完好、被 CMake 编译、也有人读 —— 但 `src/` 下**没有任何调用者**。于是 `PiRuntime::launchProfile_` 始终是结构体默认值，而它的 `mode` 默认是 `Chat`：
  · `sessionDir` 空 → `PI_CODING_AGENT_SESSION_DIR` 退回 `agentDir`，创作会话与聊天同目录；
  · `workingDirectory` 空 → 没有创作 cwd；
  · `toolAllowlist` 空 → `--tools` **根本不传**，Pi 用它自己的默认工具集，也就是 CCA-04 要拿掉的 `read/bash/edit/write/grep/find/ls` 全都在；
  · `systemPrompt` 空 → 退回聊天那一份；
  · `mode != Creator` → `MIAODESK_CREATOR_WORKSPACE` / `MIAODESK_CREATOR_SESSION` **不导出**，而 `src/app/main.cpp` 的 `RunCreatorTool` 正是从这两个环境变量取工作区与会话。

  所以"宿主持有会话 ID、工作区、epoch、台账"不是取不到，是**导出它们的那个调用没发生**。这不是界面缺数据，是运行时不装 profile。`tests/pi-launch-profile-isolation.mjs` 已加第 4 节盯着"必须有调用者"，它现在是**故意红着**的。

- **宿主封存的物理动作已接上**(见 CCA-04 相应小节):封存在工作区之外的 `../revisions/` 下,同一摘要幂等,且封存的就是算过摘要的那一份快照。
- **仍未做**:`ContentCreatorDialog.cpp` 里那两条猜测本身还在(它们现在只是不再是唯一路径)。要动的是一个 2000 行的 Windows UI 文件,而它的验收(用户看到的界面)在本机给不出证据 —— 所以没有宣称已替换。纯逻辑这一层先落地,是为了让替换时有一个可依赖的判据,而不是又一段正则。

### CCA-06 升级 Skills、能力说明与制作样例

- [~] "运行时上限优先于文档副本"已变成可执行闸门;样例与版本/摘要在资产侧已有雏形,**样例的渲染验证与评测集分离需 Windows 侧证据**。
- **依赖**：CCA-04、CCA-05。
- **实施**：保留四个现有 Skill 的职责，加入受支持工具流程、设计方法、失败修复和少量高质量示例；校正与实际几何、素材、材质、binding、数据能力不符的规则；新增引用资源纳入打包与一致性检查。
- **交付**：每个领域至少 3 个可渲染样例及对应需求、包、参数和效果说明；skill 版本/摘要记录。
- **验收**：示例全部经过真实校验与目标后端渲染；运行时没有提供的 CPU/GPU/任务操作能力不能靠虚构 binding 伪造；运行时上限优先于文档副本。样例和评测集分开。

**核对结果（2026-09-28，"虚构能力"这一类首次有了执行点）**：

- 验收第二条"运行时没有提供的 CPU/GPU/任务操作能力不能靠虚构 binding 伪造"此前**没有任何闸门在看**:规范(`skills/`)、契约文档(`docs/MIAO_CONTENT_PACKAGE_V1.md`)与代码是三份各自手写的文本,于是文档可以宣称一个运行时根本没有的能力,而两边都不报错。
- 实测到的那一处:`skills/content-package-basics/SKILL.md` 把 capabilities 举例成 "clock.read / weather.read / **audio.read**",而 `MiaoContentCapabilityBroker::RequiredCapability` 只认识 `time.` / `weather.` / `tasks.` 三个前缀 —— **没有 `audio.`**。内容能拿到的音频是**输入通道** `input://audio/*`(`MiaoInputBus.h` 的闭集),它不需要也没有对应的 capability。
- 为什么这一处特别值得修:loader 对 capabilities 只查 id 的字符集(`MiaoContentPackage.cpp:IsCapabilityId`),所以 `audio.read` **不会报错**。后果是更坏的两种:作者声明了就以为音频已授权,接着写 `audio.*` 数据绑定被 broker 拒掉,而他会以为是别的问题;或者声明了、它完全无效,而模型告诉用户"音频已启用"。**一个虚构的能力比一个缺失的能力更难发现:缺失会亮红,虚构只会静悄悄地不工作。**
- 新增 `scripts/verify-skill-capability-contract.sh`(已接进 `repo-hygiene.yml`)。判据从代码**读出**,不再抄一份清单 —— 抄的那份会在改名那天悄悄说相反的话(2026-09-22 `verify-skill-material-rule.sh` 吃过这个亏):
  - 数据能力:从 `MiaoContentDataBinding.cpp` 的 `RequiredCapability` 解析 (前缀 → capability);
  - 输入通道:从 `MiaoInputBus.h` 的常量表解析,并**反查 `ChannelShape` 是否分支到每一个** —— 加进常量表却忘了进形状表,那个通道生产者写不进、场景声明它无效,而只读常量表的门会照样绿。
  - 检查范围是会教到作者的全部文本:四个 SKILL.md、`MIAO_CONTENT_PACKAGE_V1.md`、六个自带样例的 manifest、以及 `MiaoContentPackage.cpp` 里那个"一个合法包"的自测夹具(它是照做就行的活样本)。
- 修掉的三处:skill 的举例改成真实闭集并说明音频/指针不走 capability 而走 `inputs[]`;`MIAO_CONTENT_PACKAGE_V1.md` 补上 capability 真表并明写"loader 不校验它是否真实存在,所以写错不会报错,只会静悄悄地不工作";自测夹具的 manifest 改回单能力并断言它就是 `clock.read`。
- 这个门自测过四处,每处都确认它是红的:把虚构能力塞回 skill、虚构一个 `input://pointer/z`、往 `MiaoInputBus.h` 加一个 `ChannelShape` 分支不到的幽灵通道、以及零比对守卫(一处声明都没比到时 exit 2,绝不静默通过)。
- **它证不了什么(与另外两个 skill 门相同的上限)**:它只比对 id,抓不到"把真能力说错用途"或"只字不提某个真能力";同一行出现否定词(不是 / 不提供 / 别写 ……)时该行不计入 —— 这是为了不把"audio.read 不是能力"这类说明报成违规,代价是夹在否定句里的真违规也会被跳过。实测确认过这一种,要人工读。
- **仍未做**:每个领域 3 个样例的"对应需求、参数和效果说明"、样例的真实渲染验证(需目标后端)、评测集与样例的显式分离、skill 版本/摘要记录。这些在本机给不出证据。

### CCA-07 建立有上限的自动修复

- [~] 有上限自动修复的调度逻辑已实现并有可执行测试；宿主侧续行与 UI 阶段事件未接。
- **依赖**：CCA-05、CCA-06。
- **实施**：把真实校验错误转换成修复输入，同轮自动续行；保存需求与上一有效 revision；每次修改重验；识别重复相同错误和不可修复服务错误。
- **交付**：修复调度与预算、错误用例、UI 阶段事件。
- **验收**：注入一个可修复参数错误可自动修正；不可修复样本达到上限后停止；取消后不再启动修复；不得靠删掉用户要求或忽略 validator 使测试通过。

**核对结果（2026-09-28，调度层完成，宿主续行未接）**：

- 新增 `CreationRepairPlanner`（`src/include/miaodesk/CreationRepairPlanner.h` + `src/desktop/control/CreationRepairPlanner.cpp`）：把一份校验结论变成"一次可执行的修复指令"或者"停下来并说明停在哪"。它不 import Windows 头,所以这些边界在本机就能真验。
- 计划验收的后三条各自对应一种真实失控,都各配一个只违反它的输入:
  - **取消优先于一切**。哪怕有可修的问题、预算还多、需求也在,也不启动。取消之后启动一轮修复,是在替一个已经不存在的决定花时间,而且它会把"已取消"拖回生成中 —— 用户看到的是取消没生效。
  - **墙钟优先于轮数**。轮数是经验值,墙钟是用户已经等掉的时间。墙钟为 0 表示不设限,不能把"没配上限"读成"已经超时"。
  - **同一错误出现两次就停**。第一次修、第二次确认它是否偶然,第三次还是同一条说明这条路不通 —— 那时候该让用户看一眼,而不是继续把预算烧完。同一个错误按"失败种类 + 文件 + 字段路径 + 一句话"四者一起判:位置不同的同类错误不算重复,那是另一个问题。
- **"不得靠删掉用户要求使测试通过"是最容易被绕过的一条**,所以 `RepairRequest` 刻意包含**用户的原始需求**:修复指令把它原样带给模型,并明确禁止删需求、说明删了也过不了(宿主会重验)。而缺 brief 时**不自动修** —— 一个通过删需求"修好"的候选,比一个明确失败的候选更难发现。
- 可修复错误产出的是**带定位信息的指令**(文件 + 字段路径 + 校验器那句话),不是把整份报告倒给模型 —— 后者会让它重写整个包,而它只需要改一个字段。不可修的那条不进指令:带上去等于让模型去改一个改不动的东西。
- 新增 `src/tests/CreationRepairPlannerTest.cpp`:**44 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。8 个已知失效注入全红。
- **仍未做**：宿主侧的"同轮自动续行"(把指令交给 Pi 再喂回校验结果)、修复过程的 UI 阶段事件。这两件都要动真实运行时与界面。

### CCA-08 渲染证据采集

- [~] 证据记录与判据已实现并有可执行测试；宿主离屏采集已接，**像素/透明路径与 Windows 真机渲染未验**。
- **依赖**：CCA-05。
- **实施**：复用当前渲染器建立宿主离屏采集接口；记录时间、后端、比例与 fixture；D2D 优先，D3D11 使用经验证的 readback 路径；隔离临时资源与超时。
- **交付**：候选帧与渲染诊断 artifact、像素/透明/失败路径测试。
- **验收**：采集的是渲染器真实输出；样本绑定正确摘要与时间；缺素材或渲染失败不会用封面图替代成功；不需要抓取私人桌面。

**核对结果（2026-09-28，判据层完成，宿主离屏采集已接）**：

- 新增 `RenderEvidence`（`src/include/miaodesk/RenderEvidence.h` + `src/content/package/RenderEvidence.cpp`）：渲染证据的记录与判定。它不碰盘、不 import Windows 头,所以这些规则不需要一个 GPU 就能真验。
- **四条验收里最容易被绕过去的是"缺素材或渲染失败不会用封面图替代成功"**,所以它在这里是一个**独立的状态**而不是一个布尔。把失败帧退回一张内置封面图是最省事的"让用户看到点什么"的做法,而它的后果是一个坏包看起来渲染得很好。`PlaceholderSubstituted` 与 `Rendered` 分开,且前者永远不计入可用帧。
- 占位还**优先于"帧数够了"被报出来**:几帧真的、几帧是占位时,悄悄只报成功的那几帧,等于替候选挑了一份好看的成绩单。
- 另外三条各自对应一种会真实发生的错:
  - **样本绑定正确摘要** —— 拿上一版的截图冒充这一版,是所有"看起来成功了"里最隐蔽的;空摘要一律拒绝(放过它等于放过任意候选)。
  - **样本绑定正确时间** —— 相邻两帧间隔太小说明采集卡住了,时间倒流说明顺序不可信;两种都不是证据。
  - **不需要抓取私人桌面** —— 每个样本都要带 `offscreen` 标志,为 false 一律拒绝,并说明它会把私人桌面带进 artifact。
- 尺寸、帧数、字节上限与 `CreationBudget` 里的 `evidenceFramesMax` / `evidenceMaxEdgePx` / `evidenceMaxBytesPerRun` 同一组常量;上限为 0 表示不设限,不能把"没配上限"读成"已经超时"。
- 失败与成功并存时整批仍算可用,但结论里必须点明有失败帧 —— 那正是"不隐瞒"的落点。
- 新增 `src/tests/RenderEvidenceTest.cpp`:**42 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。9 个已知失效注入全红。
- `creator_preview_evidence` 已从"未实现"移到"可实现",`CreatorToolWorker` 走宿主 `CollectEvidence` 采集、再用 `AssessRenderEvidence` 判定;`CreatorToolWorkerTest` 加了 6 条用例(合法取证、采集失败、占位不算成功、摘要不符、非离屏、无可用摘要),该测试随之到 **241 条**。
- 宿主侧 `FilesystemCreatorWorkspace::CollectEvidence` 自建一个 **DC render target** 做离屏采集,不碰用户屏幕上的任何窗口 —— 验收第四条由这一行保证;渲染失败直接返回原因,**不退回封面图**。
- **仍未做**：像素/透明路径的断言(需要真实 readback 与逐像素比较)、artifact 落盘与时间/fixture 记录的完整实现,以及 Windows 真机上的真实渲染验证。这些在本机给不出证据。

### CCA-09 接入视觉评审与反馈修复

- [~] 视觉评审的**门**已实现并有可执行测试;真实图像链路、评审记录与盲评样本结果仍是 Windows 侧的事。
- **依赖**：CCA-01、CCA-06、CCA-07、CCA-08。
- **实施**：将实际帧通过已验证图像链路发送；按壁纸/组件 rubric 返回结构化问题、位置、严重性和改进建议；复用同一修复预算；报告视觉未审模式。
- **交付**：多模态输入适配、评审记录、可控测试与真实样本结果。
- **验收**：至少识别已知裁切/对比度样例并推动有效修改；图像传输失败不能记为“已看图”；同包硬校验失败不能被高视觉分覆盖；修复后重新采集对应 revision。

**核对结果（2026-09-28，判定层完成，图像链路未接）**：

- 新增 `VisualReviewGate`(`src/include/miaodesk/VisualReviewGate.h` + `src/content/package/VisualReviewGate.cpp`)。它收的**全部是宿主持有的事实** —— 图像链路到底通没通、送出去的那一帧绑在哪个候选上、同一份候选的硬校验过没过 —— 模型给的分数与问题清单只是输入,推不倒这三件。
- **验收第三条"同包硬校验失败不能被高视觉分覆盖"从此有了执行点**。此前宿主机把模型一段评审原样当成 `ReviewCompleted` 喂进状态机,于是"包没装上"和"包不好看"是同一笔账:视觉分一高,一份 manifest 都不合格的候选也能走到 Ready。现在硬校验没过时结论是 `RefusedHardFailure`,**视觉分一分都不顶用**,并且它不算"看过图" —— 它是被硬校验挡下的,和视觉无关。
- 硬校验没过与链路没通**同时**发生时,两件都要在 note 里说清。第一版按"先报更根本的那一关"把"没看图"咽了回去,而排障时最需要的恰恰是两件事同时摆在眼前:一条只会让人先去看 manifest,看完还是不知道为什么没有图。
- 验收第二条"图像传输失败不能记为'已看图'":`imageDelivered == false` 时结论只能是 `NotReviewed`,`sawTheImage = false`,即使模型那段评审读起来完全正常、即使它还报了几个问题。**那几条问题也不会交给修复轮** —— 它们来自一张没到过模型眼前的图,照它们去改等于让用户为一次没发生的评审付费。
- 第三类是帧绑错了:`frameDigest != candidateDigest` → `NotReviewed`。拿上一版的截图评这一版,是所有"已看图"里最隐蔽的一种,所以它也走未审,而不是"看过但内容不对"。空摘要一律未审,并且把空的那一侧显式写出来。
- 交给修复轮的**只有** `severity == Blocking` 的问题。把不阻塞的一起塞进去,修复轮会去改一个本来就允许存在的小瑕疵,而预算烧在它身上 —— 而预算与硬校验共用同一份计数(`CreationBudget::repairRoundsMax`)。
- 问题清单要 `kind + region + suggestion` 三样齐全:只有"不好看"两个字,下一轮评审还会报同一条,预算就烧在同一个问题上。
- 新增 `src/tests/VisualReviewGateTest.cpp`:**51 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。**10 个已知失效注入全红**:链路失败算看过、硬校验失败不拒绝、不查帧绑定、两者顺序颠倒(咽掉"没看图")、未审也算看过、Blocking 算通过、不挑阻塞问题、阻塞清单含非阻塞、空帧摘要算绑上、咽掉没看图。
- **仍未做**:真实的图像链路(帧怎么送、送多大、失败怎么分类)、Provider 无视觉能力时的上报路径、评审记录落盘,以及"用已知裁切/对比度样例真的让模型改对一次" —— 后两条需要真实 Provider 与真实画面,在本机给不出证据。

### CCA-10 接入现有 Native 创作界面

- [~] "关闭与重开恢复草稿"的状态与判定已实现并有可执行测试;界面适配层与真实持久化仍需 Windows 侧。
- **依赖**：CCA-03、CCA-05、CCA-07、CCA-08；完整视觉模式还依赖 CCA-09。
- **实施**：将界面业务状态迁到控制器；展示准备/生成/校验/预览/修复/就绪、取消和错误；保留播放、重载、全屏、参数与候选；需求足够时不再强制重复提问。
- **交付**：两模式共用适配层，清晰的候选/轮次标识和键盘操作。
- **验收**：保留原有 DPI/布局/快捷键行为；关闭与重开恢复草稿；旧轮次不能覆盖新结果；上一有效候选显示明确，生成失败不误报成功。真实 Windows 测试不能由字符串断言替代。

**核对结果（2026-09-28，草稿状态与判定层完成，界面未动）**：

- 画面上 `CreationWorkflow` 有两个钩子:`SetDraftPersistHook` 与 `CreatorWorkspaceState`。**前者全仓库只有测试在调**;后者只存 sessionId / turnId / epoch / cancelRequested / candidateDigest —— 没有需求正文。于是"关闭与重开"要么什么都不恢复(用户重新输入一遍,正好违反"需求足够时不再强制重复提问"),要么恢复一个不完整的草稿:窗口打开着、看起来有内容,而里面那句话不是用户说过的。用户会以为是自己记错了,然后照着错的做下去。
- 这就是为什么"恢复一半"是这一轮的重点,而不是"能不能序列化":序列化本身不解决问题,恢复一半才致命。
- 新增 `CreationDraftStore`(`src/include/miaodesk/CreationDraftStore.h` + `src/desktop/control/CreationDraftStore.cpp`):定义重开真正需要的字段(需求正文每一项 + 上一有效候选的摘要/摘要说明/版本号 + 取消标记 + epoch/turnId),以及**五种互不相同**的恢复结论:
  - `RestoreAndResume` —— 草稿完整、没取消,可以接上继续,不必再问用户;
  - `RestoreAsDraft` —— 草稿在但用户取消过,**只显示,不自动继续**。取消之后自己动起来,是他取消没生效;
  - `AskUserAgain` —— 需求不足必须再问,而且**必须说清缺哪一项**。"需求不足"四个字等于让用户猜自己上次漏了什么;
  - `StartFresh` —— 没有草稿;
  - `RejectAmbiguous` —— 草稿接不上(解析失败、版本行不是 1、没有 sessionId、或属于另一个作品会话)。**绝不当成 StartFresh**:那样用户看到一个全新窗口,而他记得自己写了一整段需求。
- 序列化沿用 `CreatorWorkspaceState` 的行式 key=value,但**带上转义**:需求正文是用户自己写的话,里面有换行与等号本来就很正常。第一版转义漏了逗号 —— 写着 `parameter=note,a,b=c`,那条参数被切成三段,整份草稿解析失败。是 `CreationDraftStoreTest` 里"还是两条参数"那条断言先发现的。**这类错误的坏处是它只在用户的真实需求含有那个字符时才发作**,而那时用户看到的是窗口打不开,不是一行报错。
- 转义里 `\` 必须最先转:否则 `\n` 会被二次解释成换行,于是一段含 `\n` 字面量的需求会把格式撑坏。
- 认不出的字段**不报错**:新版本加一个字段时,旧版本代码还要能读。报错会让"加一个字段"变成"所有人的旧草稿全失效"。
- 新增 `src/tests/CreationDraftStoreTest.cpp`:**59 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。**12 个已知失效注入全红**:换行/逗号/等号/反斜杠四种不转义、缺版本行也当成草稿、别的工作会话也恢复、取消过也自动继续、需求不足也恢复、陌生字段报错、坏数字静默读成 0、不判种类、不点名缺什么。
- **仍未做**:把 `SetDraftPersistHook` 接到真实的窗口关闭路径、把草稿落到 `CreatorWorkspaceState` 所在的会话目录、界面本身的适配层(两模式共用、候选/轮次标识、键盘操作)、DPI/快捷键回归。这些都要动真实窗口。

### CCA-11 入库、应用幂等与一次恢复

- [~] 幂等账本、应用前重验、精确前态已在 CCA-02 落地并有测试；**一次恢复的判定已实现并有可执行测试，宿主侧读盘/写回与恢复 UI 未接**。
- **依赖**：CCA-05、CCA-10。
- **实施**：有效候选按现有策略自动入库，避免中间版本刷屏；应用绑定摘要/目标/operation ID；应用前重验；记录真实结果与精确前态；实现一次恢复。
- **交付**：正式 API 路径、事务/补偿策略、幂等账本与恢复 UI。
- **验收**：双击仅产生一次应用或一个组件实例；用户明确再次添加仍可成功；同路径新版本不会被误认为已应用；失败保持原状态；恢复不会覆盖用户后来的无关改动。

**核对结果（2026-09-28，幂等账本早已在 CCA-02 落地，本轮补"一次恢复"的判定）**：

- 先把已经做到的部分写清楚,免得它们被这轮的进展盖住:`ApplyIdempotencyKey = 候选摘要 + 目标 + operationId`,`RecordApplyOutcome` 只让**第一次给出结论**(`resolved` 从 false 翻 true,成败都算),`ApplyPreconditionHolds` 在进入正式 API 前再验候选与目标,`beforeState` 记的是**精确前态**。这四条已有 `CreationWorkflowStateTest` 覆盖(双击只发一次 `BeginApply`、结算后重复回调被重复确认挡住、新的 operationId 允许再次执行、摘要变化必须重走、应用失败回 Ready 而不是 Applied)。
- 新增 `ContentApplyRecovery`(`src/include/miaodesk/ContentApplyRecovery.h` + `src/desktop/control/ContentApplyRecovery.cpp`):把一笔"已发出、没有结论"的在途应用变成一个可执行的恢复动作。它不 import Windows 头、不碰盘,只比较账本与宿主持有的那一句现状。
- **"恢复不会覆盖用户后来的无关改动"是最容易做反的一条**,因为在四种真相里,**撤销看起来总是最稳妥的那一个**:一笔没结论的应用摆在面前,当作没落地、写回前态,读起来很谨慎;可一旦用户后来又应用了另一份,写回前态就把人家后来那份抹掉了 —— 而用户看到的是自己的设置莫名倒退,他不知道为什么。所以四种真相是四个**不同的结果**,不是一个布尔:
  - `FinishApply` —— 目标上挂的就是这一笔的候选。此时**撤销才是错的**:它把一个已经生效的东西退回旧值。而补结算之前必须先重验候选(崩溃这段时间里它可能已被宿主标记失效),所以 `revalidateCandidateFirst = true`。
  - `RollBack` —— 目标上还是账本记下的前态。写回那一份**精确前态原文**;`beforeState` 为空时**不**撤销(取"最近用过的一项"会把用户一个无关的设置写回去),改成 `GiveUpUnrecognized`。
  - `AbandonSuperseded` —— 目标上挂着**另一份**候选。这一笔作废,**一个字都不写**。
  - `GiveUpUnrecognized` —— 宿主读不到目标当前挂的是哪一份(`observedReadable == false`),或者读到的与账本完全对不上。**它绝不能被顺手写成"按没落地处理"**:那是对用户桌面的猜测,而猜错的方向恰好是破坏性的。
- **一次**:已经拿到正式结论的一笔不再恢复。恢复本身就在写桌面,第二次就是把一个已经结算的结果又发生一遍。失败同样是结论。
- **同路径新版本不会被误认为已应用**:`IsAlreadyAppliedOnTarget` **按摘要比,不按路径比**。同路径换了内容就是新版本,摘要不同,所以它不算已应用 —— 路径相同会让人说出"这已经在桌面上了",而新版本永远出不来。
- "当前挂的是哪一份"只看 `resolved && committed` 的条目:在途的和失败的都没有改变桌面,把它们算成当前生效会让恢复去撤销一个从未存在的东西。前一本账从**账本自己**推(`PreviousCandidateDigestForTarget`),不让宿主另传一份"之前是什么" —— 多一个来源就多一处可以互相矛盾的事实。
- 新增 `src/tests/ContentApplyRecoveryTest.cpp`:**49 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。**11 个已知失效注入全红**:读不到也撤销、落地了也撤销、被接管也撤销、已结算也再恢复、缺前态也撤销、在途算当前生效、前一本账含未落地、空摘要算已应用、补结算不重验、缺摘要也恢复、空串与 "none" 区别对待。其中"空摘要算已应用"这一处**第一轮是绿的** —— 那个前置判断只有在"目标上什么都没有"时才看得出差别,而那一节原本没有覆盖它,补上之后才转红。
- **仍未做**:宿主侧的读盘与写回(`FinishApply` 后补一次 `RecordApplyOutcome`、`RollBack` 真写回、`AbandonSuperseded` 只入账不写盘)、开机/开窗时扫账本的触发点、恢复过程的 UI 呈现。这三件都要碰真实运行环境与桌面 API,在本机给不出证据。

### CCA-12 集成回归与资源验证

- [ ] 完成；负责人 / 证据：待填写。
- **依赖**：CCA-09、CCA-10、CCA-11。
- **实施**：按第 8 节运行真实代码测试，验证 Windows x64、ARM64 编译/打包；多轮开关制作窗口，测试网络异常、取消和 in-flight 工具；与原流程比较创作及桌面资源。
- **交付**：同 SHA 自动检查与物理 Windows 记录、未覆盖项。
- **验收**：关键状态/边界测试全过；长时桌面与搜索无新回归；释放创作 runtime 后资源回到可解释范围；缺 Windows 证据时保持本项待验。

### CCA-13 固定样本 A/B 与质量放行

- [~] "能不能说效果更好"的**判定**已实现并有可执行测试;真实评测(样本、模型调用、人工盲评)需要授权与预算,本机不做。
- **依赖**：CCA-00、CCA-06、CCA-09、CCA-12。
- **实施**：按第 9 节，对基线流程与新流程运行相同样本、模型、预算和输入；盲评视觉与需求符合度，记录失败与成本。
- **交付**：逐样本报告、质量/成功率/耗时/成本对比，以及放行或继续调整的结论。
- **验收**：不能把更多调用预算带来的提升归因于 Agent 架构；不把评测示例直接加入 Skill 后在原样本上自证；未见收益则保留问题样本改进，不宣传“效果最好”。

**核对结果（2026-09-28，判定层完成，评测本身未跑）**：

- §9 写了几条"不许",它们共同的特点是**要靠执行者自己约束自己**:相同预算比架构、更高质量预算看上限不能混一组;不能把更多的调用预算带来的提升归因于架构;不把评测示例直接加入 Skill 后在原样本上自证;不得用删除要求提高成功率;关键可靠性失败为 0;20 题的小样本只支持本样本结论;配置字段全部记录。
- 一次自己给自己的评测天然有动机松一点 —— 多给新流程一点预算、把评测样例顺手加进 skill、把需求表里的两行删掉再统计。**每一种都让数字变好,每一种都看不出来**。所以这里把"不许"变成可判定的:新增 `ContentReleaseGate`(`src/include/miaodesk/ContentReleaseGate.h` + `src/content/package/ContentReleaseGate.cpp`),读入两臂的配置与逐样本指标,给一个"能不能对外说"的结论以及为什么不能。它不产生数字、不访问任何服务 —— 数字是评测跑出来的,而"这组数字能不能支撑那句话"必须能判。
- 八个拒绝各对应一条:配置没记全(少一项,差异就归不到任何原因上,而报告里会写成"新架构更好");两种预算口径混成一组;靠多花调用拿到的更好;关键可靠性失败非 0;必需项分母被改动;评测样本与 skill 示例重叠;样本不够;结论比样本大。
- 其中两处值得单说:
  - **必需项两头都要堵**。分母变小是删用户要求,分母变大是给对手加码。第一版把"候选方分母更严"那条解释写成了"会让本来更好的基线看起来更差" —— 那正好说反了:更严的分母让**候选方**看起来更差。**一条解释错了的拒绝比没有拒绝更容易让人改错地方**,所以它现在按方向分开写。
  - **限次成功率低于基线时不是拒绝,是第四个结论**:`verdict = Release` 但 `allowRelease = false`。它是一次诚实的评测,只是没有收益。混成"拒绝"会让人以为数据有问题,于是去查数据 —— 而数据没问题。
- 多花的调用在放行时**必须单独披露**(`reason` 里带上"额外耗时与费用按 §9 单独披露"),不并进"更好"。花得多本身不是错(质量上限那一路口径就是要花得多),错的是把它算成架构的功劳。
- 写这一轮时踩到两个自己的错,两处改的都是代码而不是测试:
  - **函数内 `static` 由入参初始化**:第一版 `FieldsOf()` 的字段表写成 `static const std::vector<...>`,而它由 `config` 构造 —— 于是它只按**第一次**传进来的那份配置构造,之后每次调用都返回第一次的结论。表现是:一个配置齐全的臂先被问过之后,**任何**缺字段的臂都读成"记全了"。测试里必须有第二个形状不同的配置才看得见。`static` 现在被刻意去掉,并在注释里写了为什么。
  - **对称写法的死代码**:样本对齐那一关我先按"两臂各走一遍"写,还专门造了"候选方少一题多一题(总数仍相等)"的用例去撞它 —— 它照样被单方向那一遍抓住。算术上这是必然:上面刚比过两边样本数相等,"基线的每个 id 恰好出现一次"就蕴含双射。所以那是一行抓不到东西的代码,留着会让人以为这里有两道保险。已删掉,理由写在原处。
- 新增 `src/tests/ContentReleaseGateTest.cpp`:**54 条断言,本机实跑 0 失败**,接进 CMake、`run-pure-logic-tests.sh` 与 Windows CI。**13 个已知失效注入全红**:两臂预算口径混用、预算污染放行、自证样本放行、可靠性失败放行、只堵一个方向的必需项、通过数超限放行、样本量放行、外推结论放行、低于基线也说成更好、样本对齐失效、配置字段永远齐全、配置字段永远缺失、额外调用不披露。
- **仍未做**:真实评测本身 —— 冻结样本集、跑两臂、人工盲评、成本与 p50/p95 记录、桌面资源峰值的采集。这些要授权与预算,本机不做,也不该由本计划自动发起。

### CCA-14 文档、打包与交接收口

- [~] 两处过期断言/失效引用已修正并各配一个闸门；打包产物与交接收口本身需 Windows 侧证据。
- **依赖**：CCA-12、CCA-13。
- **实施**：同步 Pi runtime、内容沙箱、Skill 索引、源码归属与打包；修正仍称 AI 不能生成组件的旧表述；明确受限模式；在主 TODO 关联实际证据。
- **交付**：变更摘要、运行手册、已知限制、可复现验收和下一位执行者交接记录。
- **验收**：安装包不需要系统 Node/npm/源码；技能引用资源随包完整；当前版本发布若被用户授权，继续执行 REL-01～03，同一 SHA 和真机要求不被本计划替代。

**核对结果（2026-09-28，文档侧两处各有闸门，打包本身未动）**：

- **BASE-03 漏了第四处副本。** "修正仍称 AI 不能生成组件的旧表述"这一条,BASE-03 已经修过三处(`AI_GENERATED_DESKTOP_SANDBOX.md`、`PiNativeToolsExtension.cpp`、`PiRuntime.cpp`),但漏了 `docs/L3-PI-RUNTIME-CONTRACT.md:91` —— 而它恰恰是最容易被当成契约读的那一处。原句:"Widget 是 Native-only preset,AI 只能读取其状态,**不能创建或修改组件**"。
- 为什么这一句值得单独一个门:它错得很**讲得通**。它想说的是"没有**工具**能改现役组件状态",这是一条安全承诺;但它把 Content Creator 一起否掉了 —— 而用户主动发起的 `✨ AI 制作组件` 正是要产出 `.mdwidget` 内容包。**一句错的约束比没有约束更坏**:它会让人据此砍掉一个已经实现的功能,或者拒绝修一个本可以修的问题。
- 已改成三张面对照表(Pi 工具面只读 / Content Creator 产出内容包 / 正式 API 才可变现役状态),保留"AI 输出是数据,不是代码"这条跨三面的不变式,并把撤回本身写在原处,注明 2026-09-28 撤回与残余风险的精确表述位置。
- 新增 `scripts/verify-stale-claim-retraction.sh`(接进 `repo-hygiene.yml`):那几句被判定为错的原文不许再出现在任何活文档里;允许出现的文件必须**显式登记并写明理由**(与 `verify-native-source-hygiene.sh` 的 `appdata_allow` 同一个做法 —— 例外必须可数,否则它会静悄悄地长成一个窟窿)。另一半**反向**:更正记录本身不许被改写,三面对照表与那条不变式还得在。撤回不等于改写,对的那一面必须仍然写在那里。
- 门自测过两个方向:把撤回的断言塞回一份活文档 → exit 1;把三面对照表的 `| AI Content Creator` 改掉 → exit 1。
- **第二处:`skills/README.md` 指的实物范本在装好的产品里不存在。** 它只写 `assets/widgets/GlassClock.mdwidget/`(仓库路径),而根 CMakeLists 装的是 `install(DIRECTORY assets/widgets/ DESTINATION Widgets)` —— 用户在一台装好的机器上照这句话找,什么都找不到,而这句话是 README 里唯一指向"照着做就行"的实物范本的一行。这正是验收里"技能引用资源随包完整"要的那件事。
- 新增 `scripts/verify-skill-referenced-paths.sh`(接进 `repo-hygiene.yml`):skill 与用户会读的文档里反引号跨度内的每个路径,**既要真的存在**,又要在指向装了产品的目录时说得出安装布局下的名字。安装目的地从 CMakeLists **读出**,不抄一份。
- 写这个门时踩了两次自己的假失败,两次都改了门而不是改文档:后视把 `/` 和 `.` 排掉,于是 `../skills/` 与 `<install>/skills/` 这两种**正确**写法被判成"本文档一次都没提到过";后视之前还把反引号跨度算错(闭合反引号在被 strip 的尾部 `/` 之后),于是唯一真正该报的那一行反而没报。**假失败比没有门更坏** —— 它会让人把整个结论关掉,而真的那一条也就跟着死了。
- 门自测:塞一个不存在的路径 → exit 1;改回 `install(DIRECTORY assets/widgets/ DESTINATION Widgets)` 本身 → 同一条重新变绿。
- **仍未做**:安装包是否真的不需要系统 Node/npm/源码(stage.ps1 的断言在 Windows 侧运行)、`packaging/` 的产物核对、REL-01～03、以及变更摘要/运行手册/交接记录的整理。这些在本机给不出证据。

### 里程碑与建议 PR 拆分

| 里程碑 | 覆盖任务 | 放行条件 |
| --- | --- | --- |
| M0：协议与风险实证 | CCA-00～02 | 状态/工具契约确定；会话与图片 RPC 能力有证据或明确阻塞 |
| M1：独立创作基础 | CCA-03～05 | 真实 Pi 能制作两类包，独立上下文、结构化校验和快照可用 |
| M2：技能与自动修复 | CCA-06～07 | 合法包制作和有限修复闭环，失败可解释且保留有效候选 |
| M3：真实视觉反馈 | CCA-08～09 | 真实帧进入模型、反馈改进作品，完整模式不可用时明确未完成 |
| M4：用户流程 | CCA-10～11 | UI、自动入库、显式应用与恢复在 Windows 完整走通 |
| M5：质量与交接 | CCA-12～14 | 回归、资源与 A/B 证据齐全，文档和包一致 |

建议每项或同里程碑内紧密关联的两项一个 PR。每个 PR 保持默认路径可用；允许先接内部开关逐步替换，但不能长期维持两套创作实现。代码依赖独立的准备工作可穿插，不要求执行者创建多 Agent 或同时修改同一文件。

## 8. 必须覆盖的验证矩阵

| 层次 | 必测场景 | 证据类型 |
| --- | --- | --- |
| 纯逻辑 | 状态转换、预算、取消 epoch、旧消息、版本、幂等 | 直接调用生产逻辑的可执行测试 |
| 工具协议 | 真 Pi 注册与 allowlist、结构化消息、错误传播、路径/会话限制 | 固定版本 runtime + 可控服务/fixture，不能只查源字符串 |
| 多模态 | 图片内容确实进入请求、能力不支持、超大/坏图、失败回退 | 脱敏请求断言与真实 Provider 合成图测试 |
| 包和渲染 | 两 kind、合法/损坏包、引用/能力/参数错误、D2D 像素、后端限制 | 产品 loader/validator/renderer 的执行结果 |
| 生命周期 | 聊天与创作同时存在、作品切换、关闭、重开、停止在途工具、进程异常退出 | Windows 实操、日志和进程/句柄记录 |
| 内容提交 | 校验后源文件变化、重复应用、目标变化、失败补偿、恢复时存在后续修改 | 正式控制 API 与持久状态前后对照 |
| 视觉体验 | 多比例、组件长文本/空态、动画多个时间点、DPI、全屏、键盘 | 固定测试数据的截图/录屏与人工检查 |
| 性能 | 一个活跃创作的峰值、空闲回收、连续 30 次开关及桌面稳定 | 相同参考机、相同内容的全进程树数据 |

复用现有测试与 Windows 工作流。需要的新增测试必须接入实际执行目标，不能只创建文件；对取消/幂等/路径等关键规则至少验证一个已知失败会使测试变红。付费模型评测单独按需运行，不把在线模型响应作为普通提交 CI 的不稳定依赖。

## 9. A/B 评测与“效果更好”的判断

在 CCA-00 固定基线版本，在 CCA-13 冻结新版本。最低样本：10 个壁纸任务 + 10 个组件任务，每个流程每题重复 3 次，完整比较至少 120 次制作；先用 4 题试跑估算费用与时间，再决定正式批次。未经预算允许不要自行发起大规模在线评测。

建议题型（具体提示词在实施时冻结）：

- 壁纸：纯图、分层视差、轻粒子、循环动效、音频响应、竖屏构图、超宽屏、用户素材、局部改色/减弱动作、不支持的 3D 要求。
- 组件：时钟、天气、待办、长标题、空数据、尺寸变化、深浅背景、参数修改、保留结构的局部修改、运行时不存在的数据/交互能力。
- 不支持项单独统计“正确说明限制/提供可用替代”，不要求产生虚构可用内容，也不混入受支持任务的成功率分母。

必须报告：

| 指标 | 定义 |
| --- | --- |
| 首次有效率 | 第一次候选同时通过真实包校验和目标后端加载的任务比例 |
| 限次成功率 | 在相同总时间/工具/模型预算内达到可预览状态的比例 |
| 需求符合度 | 固定必需项通过数；不得用删除要求提高成功率 |
| 视觉质量 | 匿名随机顺序人工对比构图、可读性、动效、精致度；记录胜/平/负 |
| 修改保持度 | 局部修改后，未要求修改的内容与有效参数是否保持 |
| 可靠性 | 误应用、重复实例、串会话、未停止修复、越界操作次数 |
| 效率/费用 | 端到端 p50/p95、修复次数、模型调用、token/费用与失败消耗 |
| 桌面资源 | 创作峰值、结束后空闲值、原桌面渲染是否回归 |

初始放行建议：关键可靠性失败为 0；受支持样本限次成功率不低于基线；人工成对评审中新流程胜出数高于落败数，且报告全部平局/分歧；质量提升的额外耗时与费用单独披露。20 题的小样本只支持本样本结论，不声称对所有创作普遍最优。

评测分两种报告：相同总预算用于比较架构收益；更高质量预算用于观察质量上限，不能混为一组。模型、Provider、版本、工具、Skill、输入素材和目标后端全部记录；人工评审者看不到方案标签与制作方自评。

## 10. 与主 TODO、现有契约的关系

| 主 TODO | 本计划负责的实施部分 | 主任务仍需保留的验收 |
| --- | --- | --- |
| AI-01 | 阶段事件、失败/等待/取消反馈 | 普通聊天整体 UX |
| AI-02 | 专用会话取消、预算和提交幂等 | 通用聊天工具重试的既有问题 |
| CREATE-01 | brief、候选版本、上一有效结果、控制器 | 两模式完整用户流程 |
| CREATE-02 | 预览接线、关闭资源与渲染采集 | 全屏/重载/DPI 等真机回归 |
| CREATE-03 | 后端能力、真实图像输入与视觉一致性 | 桌面实际表现与人工验收 |
| CREATE-04 | 生成、校验、应用、恢复与质量评测 | 正式 Windows 端到端验收 |
| LIB-02 / LIB-03 | 复用生命周期与参数服务 | 非 AI 内容的导入/配置流程 |
| PERF-03 / REL-01～03 | 创作资源证据与打包变更 | 主产品性能与发布签收 |

主 TODO 继续承担产品级完成状态；本计划 CCA 项承担实现进度，两处只通过链接关联，不复制历史长日志。某个 CCA 通过不自动勾选对应 CREATE 或 REL。

执行时需要同步而非盲从的旧契约：`L3-PI-RUNTIME-CONTRACT.md` 中“AI 不能创建组件”与当前创作包流程已有偏差；`AI_GENERATED_DESKTOP_SANDBOX.md` 已更正自动入库与显式应用的区别。以产品基线、用户确认的方案和实际正式控制边界统一修订；不能借旧描述删掉已支持的 `.mdwidget` 创作，也不能把新计划当成已经落实的安全隔离。

## 11. 可直接发给执行 AI 的任务说明

> 在 MiaoDesk 仓库实施 `docs/CONTENT_CREATOR_AGENT_PLAN.md` 的方案 2：复用现有 Pi 实现，建立专用内容创作会话与有界工作流，结合已有 Skills、真实包校验、渲染反馈、自动修复和用户显式应用。
>
> 先读项目指导、产品/设计/开发基线、当前 TODO 和本计划，检查 HEAD 与工作区。仓库仍在持续开发；不要覆盖其他改动，不要重复实现已有候选保留、错误回传、自动入库、预览或按钮修复。优先完成 CCA-00，再按依赖推进。
>
> 第一批完成 CCA-00～02：确认锁定 Pi 版本的会话、工具和图像能力，冻结作品/候选/操作协议并交付可执行状态测试。之后按 M1～M5 小步提交，每步保持现有路径可用。不要另建 Agent 引擎、增加多 Agent、依赖本地 AI 部署、升级未授权的运行时依赖，或恢复大型编辑器路线。
>
> 关键要求：创作与聊天上下文/取消隔离；专用工具只访问该作品；模型交付结构化候选，宿主作真实校验；采集真实渲染帧，图片必须实际送到支持视觉的模型；修复有轮数/时间上限；保留最后有效候选；自动入库不等于应用；只有用户显式应用才改变桌面，且有幂等和一次恢复。
>
> 本机不能运行 Windows 时，完成能做的逻辑/协议检查并写清限制，继续独立工作；不得把源码断言、交叉语法检查、模拟服务或旧 SHA 的 CI 通过写成当前真机通过。在线大批量评测、发布和 PR 合并遵循用户另行授权，不能为“完成计划”自行执行。
>
> 每轮结束交付：实际修改、相关 CCA 状态、实现提交或差异、运行过的检查和结果、未验证/阻塞项、下一项及依赖。在相应 CCA 附证据链接，更新主 TODO 的关联摘要，但不提前勾选产品级验收。

## 11.5 交给下一位执行者的记录（2026-09-28）

下面这些是**本机（macOS）已经做完并有可执行证据**的部分，以及**做不了、且为什么做不了**的部分。第二半比第一半重要：它写清楚哪一条是真被挡住，哪一条只是没排上 —— 免得下一位把已经探过的路再探一遍。

### 已经做完，且每一条都有本机可跑的门或测试

| CCA | 状态 | 证据（本机可跑） |
| --- | --- | --- |
| CCA-04 | 工具名册/worker/事务/封存/校验已有测试；本轮补：可用性表与不可用理由表不再能互相矛盾；系统提示词不再让模型去调未实现的工具；工作区最后一段必须是会话身份本身（改用 `NewCreatorSessionId`） | `CreatorToolWorkerTest` 314 条（真编译真跑）+ `CreatorWorkspacePolicyTest` 388 条 |
| CCA-05 | 凭据分级（Receipt vs ProseScan）、candidate receipt、包结构校验 | `CreatorReplyInterpreterTest` 37 条、`ContentCandidateReceiptTest` 50 条、`ContentPackageValidatorTest` 121 条 |
| CCA-06 | "运行时上限优先于文档副本"变成闸门；虚构能力的那一处已修 | `scripts/verify-skill-capability-contract.sh` |
| CCA-07 | 有上限自动修复的调度逻辑与八种停止理由 | `CreationRepairPlannerTest` 44 条 |
| CCA-08 | 渲染证据的记录与判定（含"占位不是成功"优先于"帧数够了"） | `RenderEvidenceTest` 42 条、`CreatorToolWorkerTest` 的取证六例 |
| CCA-09 | 视觉评审的门：硬校验失败不被视觉分覆盖、图像没送达不算看过 | `VisualReviewGateTest` 49 条 |
| CCA-11 | 幂等账本与应用前重验早已在 CCA-02；本轮补一次恢复的判定（四种真相） | `CreationWorkflowStateTest` 168 条、`ContentApplyRecoveryTest` 49 条 |
| CCA-13 | "能不能说效果更好"的判定：八个拒绝各对一条"不许自己放水" | `ContentReleaseGateTest` 54 条 |
| CCA-14 | 撤回过的断言不许回来；skill 引用的路径必须真的在那儿且说得出发名前 | `verify-stale-claim-retraction.sh`、`verify-skill-referenced-paths.sh` |
| CCA-10 | 关掉再打开的草稿状态与五种恢复结论 | `CreationDraftStoreTest` 59 条 |

全部新模块都刻意**不 import Windows 头**，所以它们在 macOS 上真编译真跑，而不是只过 `-fsyntax-only`。每一轮都用**已知失效注入**验过：往实现里塞一个坏编辑，确认对应测试转红。累计注入约 70 处，全红；其中四处**第一轮是绿的**（`IsAlreadyAppliedOnTarget` 的空摘要前置判断、`CreationDraftStore` 的逗号转义、`ContentReleaseGate` 的函数内 `static` 由入参初始化，以及本轮"会话序号不用了"——唯一性当时被随机尾巴兜住，而测试只断言了"相邻两次不同"，于是唯独它一路绿），都是先补测试再转红 —— 而那四种恰恰是最难靠读代码发现的三类。**读写两头各写一份**也是同一类：生成方和读取方各有一份"哪些字符能进会话 ID"的判定，而当时本机只能从 JS 门看文件形状、看不到那条闭环；把两处合成一个 `SanitizeCreatorSessionId` 之后，它才变成一条等式。

### 做不了，而且不是没排上

1. **CCA-04 / CCA-08 / CCA-10 / CCA-12 的真机验收**：需要一台 Windows 机器跑真 D2D/D3D11 后端、真 Pi 进程与真实画面。本机只有 `verify-windows-syntax.sh`（mingw `-fsyntax-only`，0 处真实错误）与 `run-pure-logic-tests.sh`（真编译真跑）。**这两个都不是真机通过**，别把它们写成真机通过。
2. ~~**创作 profile 从未被装上（CCA-03）**~~ **已修（2026-09-28）**：`SetLaunchProfile` 与 `MakeCreatorLaunchProfile` 此前都无调用者，于是创作跑在**聊天的 profile** 上 —— `--tools` 用聊天那份（七个 `creator_*` 名字被整体剥掉）、`--extension` 落盘的是 Chat 变体（它本身不含创作工具）、`MIAODESK_CREATOR_WORKSPACE` / `MIAODESK_CREATOR_SESSION` 不导出（而 `main.cpp` 的 `RunCreatorTool` 正是从这两个变量取工作区与会话）。**八个 `creator_*` 工具此前不可达。** 现在 `ContentCreatorDialog` 打开时装上创作 profile、`WM_DESTROY` 时恢复聊天 profile，工作区落在 `<StateRoot>/CreatorWorkspaces/<kind>/<sessionId>`（`<sessionId>` 记在 `<kind>/active` 里；2026-09-28 把它从 `…/<kind>/1` 改了过来，理由见上面 CCA-04 那一节）。
   **仍未验**：本机只验到"文件形状对"。它真的让创作轮次拿到受约束工具与工作区，要在 Windows 上开一次 AI 制作壁纸，确认 `creator_package_update` 不被 `--tools` 剥掉、`MIAODESK_CREATOR_WORKSPACE` 真的导出了、以及关窗之后聊天仍能写文件。
3. **`creator_image_generate`**：需要一个图片 Provider，而"本地 AI、DGX、模型路由与推理服务部署"被本计划明确排除。它现在**按不可用上报**（`NotImplemented` + 一句能给人看的原因），并且系统提示词已改成不承诺画图。不要把它标成"已实现"。
4. **CCA-13 的实测**：要授权与预算（§9 自己写了"未经预算允许不要自行发起大规模在线评测"）。判定层已完成，测量层一行没跑 —— 也不要假装跑过。

### 已知会误导人的三个地方（下一轮先看这里）

1. `tests/image-provider-installed.mjs` 在这个工作副本上**必然失败**：`runtime/agent/node_modules` 没有安装。它在干净树上失败得一模一样，不是本轮引入的。
2. `gh` 未登录，所以 `scripts/verify-rc-ci.mjs` 只能跑到一半。需要它给结论时要先登录。
3. `docs/L3-PI-RUNTIME-CONTRACT.md` 曾有一句"AI 只能读取其状态，不能创建或修改组件"，已于 2026-09-28 撤回。BASE-03 修过三处副本、漏了这一处。现在有 `verify-stale-claim-retraction.sh` 盯着，想再写回去会被挡下。

## 12. 执行记录模板

```text
任务 / 当前里程碑：
执行者 / 日期：
起始 SHA / 工作区已有改动：
已核对的现有实现：
新增或调整的契约：
改动文件 / 实现提交：
自动检查（实际命令、结果、SHA）：
Windows / Provider 实测（设备、配置、结果）：
性能 / 质量证据：
未完成与原因：
下一任务 / 必要输入：
```

所有任务初始未勾选。本次编制计划不计为 CCA 实现或产品验收完成。
