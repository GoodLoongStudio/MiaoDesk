# 妙喵 Store Demo v0.1 执行计划

> 状态：规范执行队列（Store 首发 demo，非全产品）
> 日期：2026-08-28
> 分支策略：正式交付只从 `main` 构建
> 上位文档：`docs/MIAODESK-PRODUCT-BASELINE.md`、`docs/MIAODESK-DESKTOP-COMPLETION-PLAN.md`

## 1. 产品定义

**一句话：** 会说话的动态桌面 —— 一个快捷键搜索电脑，用自然语言预览并换上动态壁纸与桌面小组件。

**Store 定位：** Windows Store 上的 **最初 demo 包**（功能有限，但必须吸睛、稳定、可复现）。

**不要承诺：** Wallpaper Engine 全功能、Scene 编辑器、播放列表深度、Harness 工作台、x64 通用版（v0.1 可仅 ARM64）。

**黄金路径（30 秒 Wow）：**

```text
安装 → 首次引导（可选演示模式）
  → Alt+Space
  → 「给我一个海边动态壁纸」
  → 预览卡片出现 → 点 Apply → 桌面变了
  → 「加个玻璃时钟」→ 小组件出现
  → 截图 / 录屏可分享
```

---

## 2. 功能白名单（v0.1 必须交付）

### 2.1 必须可见、必须稳定

| 模块 | 用户能力 | 实现锚点（当前 main） |
|------|----------|------------------------|
| 搜索入口 | `Alt+Space`；应用 + 文件极速搜索；Enter 进 AI | `SearchWindow` + `GozSearch` + `AppSearch` |
| AI 对话 | 聊天面板；流式回复；工具状态卡片；Esc 收起不杀会话 | `ConversationPanel` + `PiRuntime` |
| AI 换壁纸 | 自然语言 → `desktop_preview_wallpaper` → 用户 Apply | `GeneratedDesktopPreview` + showcase presets |
| AI 加组件 | 自然语言 → `desktop_preview_widget` 或设置页「新建小组件」 | A2UI sandbox + `WidgetFixedPreset` |
| 壁纸 showcase | Aurora 极光 / Neon 赛博 / Ocean 深海（各 1 个固定格式） | `desktop_preview_examples` + library Scene |
| Widget showcase | 玻璃时钟 / 今日待办 / 玻璃天气（各 1 个固定格式） | `WIDGET_PRODUCT_MODEL_M3` |
| 设置（精简） | Provider/Model/API Key；壁纸库入口；小组件页；开关 | `SettingsCenter` / `DesktopAiSettingsPage` |
| 托盘与生命周期 | 开机可选；退出不丢桌面状态；日志不进用户脸 | 现有托盘 + persistence |

### 2.2 必须隐藏或降级（首发不出现）

| 模块 | 处理 |
|------|------|
| DeepSeek Harness 高级工作台 | 设置内折叠为「开发者 / 高级」或 v0.1 完全隐藏 |
| 自动化 / 性能 / 播放列表 / 多屏深度 | 设置内隐藏或标「即将推出」 |
| Scene 编辑器 / Timeline / Shader | 不出现在导航 |
| 终端式 L3 CLI | 已退休，不得恢复 |
| 内部名词 | UI 不出现 Pi / Node / Harness / RPC |

### 2.3 演示模式（零配置开箱，强烈建议 v0.1 包含）

**问题：** 仅 ARM64 + 自备 API Key → Store 转化极差。

**v0.1 方案（二选一，计划内优先 A）：**

- **A. 内置演示 Provider（推荐）**  
  未配置 Key 时，壁纸/Widget 黄金路径仍可用；AI 对话显示「演示模式：仅支持内置壁纸与小组件 showcase」，复杂 Agent 任务引导去设置填 Key。
- **B. 首启强制引导**  
  必须配置 Key 才能用 AI，但提供「先看演示」按钮自动跑黄金路径（无模型调用）。

验收：全新安装、无 Key、无开发者工具，仍能完成壁纸 Apply + 三款小组件创建。

---

## 3. 与现有里程碑的关系

本计划 **不替代** `MIAODESK-DESKTOP-COMPLETION-PLAN.md`，而是 **裁剪并前置** 其中与 Store demo 相关的闸门：

```text
必须先关闸（工程）
  M2 真机分层验收 pending 项
  M3 Widget 五阶段真机验收 pending 项
        ↓
Store Demo 产品层（本计划 SD0–SD5）
        ↓
M4+ 全产品壳（Store v0.1 不等待 M4 完成）
M12 完整消费者更新通道（Store v0.1 可用 MSIX + 手动更新说明）
```

---

## 4. 执行阶段

### SD0 — 真机闸门关闭（阻塞项，约 1–2 周）

**目标：** 在 Snapdragon Windows 真机上，桌面分层与恢复 **可签字验收**。

**任务：**

1. 在 exact-head ARM64 包上跑完整 M3 序列（`MiaoDeskWidgetAcceptance.exe`）：
   - [ ] Widget 在壁纸之上、图标之下
   - [ ] 打开设置 / 搜索不隐藏、不暂停 Widget
   - [ ] Explorer 重启后 Widget 恢复
   - [ ] 显示器重连后 placement 配置一致
2. 补 M2 分层人工目视签字（截图 + `widget-acceptance-evidence.manifest`）
3. 修复闸门失败项 **只改 DesktopShellHost / WidgetService / 验收脚本**，不扩功能面
4. `main` 上 ARM64 CI 全绿 + 证据包可独立校验

**退出标准：**

```text
installed SHA == checkout HEAD
→ 三款固定小组件可见且不重叠
→ icons > widgets > wallpaper
→ settings + search 阶段通过
→ explorer + monitor 阶段通过
→ 证据 manifest + sha256 校验通过
→ 人工目视签字表完成
```

**负责人建议：** 桌面运行时 + 一名真机验收操作员（ARM64 设备）。

---

### SD1 — Demo 产品裁剪（约 1 周）

**目标：** 用户看到的只有「妙喵 demo」，不是工程全家桶。

**任务：**

1. **导航裁剪**  
   - [ ] 设置中心仅保留：壁纸（库 + 当前应用）、小组件、妙喵 AI、关于  
   - [ ] Harness 入口移出主路径（隐藏或开发者开关）
2. **文案与品牌**  
   - [ ] 全 UI 使用「妙喵 / 妙喵智能桌面」，去除内部 Runtime 名词  
   - [ ] Store 用 `packaging/windows-store` 显示名「妙喵」对齐应用内文案
3. **首启引导（First Run）**  
   - [ ] 3 步：快捷键说明 → 演示壁纸一键体验 → （可选）配置 AI Key  
   - [ ] 可跳过；跳过后仍可走 showcase 按钮
4. **文档**  
   - [ ] 新增 `docs/STORE_DEMO_V0.1_ACCEPTANCE.md`（见第 6 节清单）  
   - [ ] README 增加「Store Demo 范围」小节，与全产品基线区分

**退出标准：** 新用户 5 分钟内能在 UI 里找到所有 v0.1 功能，且看不到 Harness/自动化/性能主入口。

---

### SD2 — 黄金路径打磨（约 1–2 周）

**目标：** AI Agent 固定模式 + 预览 Apply 链路 **可演示、可重复**。

**任务：**

1. **Pi Agent 固定模式（已部分落地，需验收）**  
   - [x] `--tools` 含内置 + MiaoDesk 扩展工具  
   - [x] `--no-extensions --extension <miaodesk-native-tools.ts>`  
   - [x] Agent 向 system prompt  
   - [ ] 真机验证：10 条黄金话术 ≥8 次触发正确工具（见 6.2）  
   - [ ] Direct fallback 时 UI 明确「本轮无工具」
2. **壁纸黄金路径**  
   - [ ] `desktop_preview_examples` 在对话内一键列出 妙喵云境 / 霓虹之城 / 月影秘境  
   - [ ] 预览卡 Apply/Reject 视觉与 `SEARCH_BAR_VISUAL_SPEC` 一致  
   - [ ] Apply 后 `DesktopSnapshot` 与肉眼一致（Pi `wallpaper_state_get` 可读）
3. **Widget 黄金路径**  
   - [ ] 对话：「加个玻璃时钟」→ preview widget → Apply → 真桌面出现 `玻璃时钟`  
   - [ ] 设置页：「＋ 新建桌面小组件」三次 → 三种固定格式且不重叠  
4. **搜索入口统一**  
   - [ ] 搜索栏提交 AI 请求进入同一 `conversationId`，不新开 detached 会话  
   - [ ] 本地 `/apps`、`/files` 与 AI 自然语言行为在帮助里写清差异
5. **自动化 guard**  
   - [ ] `verify-l3-runtime-contract.ps1` 锁定 Agent 启动参数  
   - [ ] 新增 `scripts/verify-store-demo-scope.ps1`（隐藏入口、首启、showcase 标记）

**退出标准：** 黄金路径脚本（6.1）在干净 ARM64 上连续 3 次成功。

---

### SD3 — 演示模式 / 降低门槛（约 1 周）

**目标：** Store 用户无需 GitHub、无需先懂 API Key 也能「哇」一下。

**任务：**

1. 实现 SD1 选定的演示模式（A 或 B）
2. **API Key 引导**  
   - [ ] 设置页妙喵 AI：Provider 预设 + 粘贴 Key + 测试连接  
   - [ ] Key 仅存 Credential Manager，不出日志
3. **离线 bundle**  
   - [ ] 确认 Store 包内 RuntimeBundle 完整，安装后无 npm/Node 下载  
   - [ ] 包体积与 Store 限制评估（记录到验收文档）
4. **错误体验**  
   - [ ] Pi 失败、WebView2 缺失、goz 未启动 → 用户可读提示 + 推荐操作（非 exit code）

**退出标准：** 无 Key 新装完成 SD2 黄金路径（演示模式）；有 Key 完成完整 AI 对话 + 工具链。

---

### SD4 — Store 打包与提交（约 1–2 周）

**目标：** 可上传 Partner Center 的 ARM64 MSIX（FullTrust）。

**任务：**

1. **MSIX**  
   - [ ] 完善 `packaging/windows-store/AppxManifest.xml.in`（能力说明、隐私链接占位）  
   - [ ] 资产：Logo、截图 3–5 张、30s 宣传视频脚本（按 6.3）  
   - [ ] `ProcessorArchitecture=arm64` 与依赖声明核对  
   - [ ] 本地 `makeappx` / CI 产出可安装包
2. **权限与审核叙事**  
   - [ ] FullTrust 说明：桌面壁纸层注入、非沙箱桌面 Widget  
   - [ ] 隐私政策：API Key 本地存储、可选联网至用户配置的 Provider  
   - [ ] 不上传 Harness 外站自动打开行为
3. **更新通道（v0.1 最小）**  
   - [ ] 版本号与 `.installed-build-sha` 策略文档化  
   - [ ] 用户侧：Store 更新或包内「检查更新」跳转 Store（不要求 M12 完整独立更新器）

**退出标准：** MSIX 在干净 ARM64 VM/真机安装 → 启动 → 完成黄金路径；Partner Center 提交包无阻塞性校验错误。

---

### SD5 — Store Demo 总验收（约 3–5 天）

**目标：** 签字发布 v0.1。

**任务：** 执行第 6 节全部清单；修复 blocker；打 tag `store-demo-v0.1`；Store 提交。

**退出标准：** 第 7 节「发布闸门」全部勾选。

---

## 5. 时间线（建议）

| 周 | 阶段 | 交付物 |
|----|------|--------|
| W1 | SD0 | M3 证据包 + 人工签字 |
| W2 | SD0 收尾 + SD1 | 裁剪 UI + 首启引导 |
| W3 | SD2 | 黄金路径 3× 通过 |
| W4 | SD3 | 演示模式 + Key 引导 |
| W5 | SD4 | MSIX + 商店素材 |
| W6 | SD5 | 总验收 + 提交 |

并行：x64 不作为 v0.1 blocker；若 Store 策略要求 x64，单独开 SD4b（+2–3 周）。

---

## 6. 验收清单

### 6.1 黄金路径自动化（ARM64，可脚本化部分）

```text
1. DEPLOY-NATIVE-ARM64.cmd 或 UPDATE-MIAODESK.cmd 安装 exact-head
2. MiaoDesk.exe --self-test == 0
3. MiaoDeskWidgetAcceptance.exe 五阶段 == 0
4. scripts/pi-agent-e2e.mjs（CI 同源）== 0
5. 手动：Alt+Space → AI 预览壁纸 → Apply → wallpaper_state 变化
6. 手动：AI 或设置创建三款小组件 → 目视分层正确
```

### 6.2 AI 话术抽检（Agent 模式，有 Key）

每条期望：**触发工具**（非纯聊天）；桌面类走 preview 工具。

| # | 用户输入 | 期望工具 / 行为 |
|---|----------|----------------|
| 1 | 给我一个蓝色海洋风格的动态壁纸 | `desktop_preview_wallpaper` 或 `desktop_preview_examples` |
| 2 | 把壁纸换成极光那种 | `desktop_preview_wallpaper`（aurora 类 preset） |
| 3 | 在桌面右上角加个时钟 | `desktop_preview_widget` 或引导设置 |
| 4 | 列出我桌面上的小组件 | `desktop_widget_list` |
| 5 | 看看现在桌面是什么壁纸 | `wallpaper_state_get` |
| 6 | 在桌面创建一个记事本文件 test.txt | `file_create` |
| 7 | 打开桌面文件夹 | `folder_list` 或 `file_open` |
| 8 | 你好 | 纯对话，无工具 |
| 9 | 帮我总结刚才做了什么 | 对话 + 可选 `wallpaper_state_get` |
| 10 | 删除桌面所有文件 | **确认卡片**，不静默执行 |

通过线：≥8/10 符合期望；无静默 Direct fallback 冒充 Agent 成功。

### 6.3 Store 素材验收

- [ ] 图标与「妙喵」品牌一致  
- [ ] 截图 1：搜索栏 + 玻璃 UI  
- [ ] 截图 2：动态壁纸全屏  
- [ ] 截图 3：三款小组件 + 图标可用  
- [ ] 截图 4：AI 预览卡 + Apply  
- [ ] 视频：黄金路径 ≤30s  
- [ ] 描述文案无「Wallpaper Engine 完整替代」表述  

### 6.4 稳定性与恢复

- [ ] 冷启动 10 次无崩溃  
- [ ] Explorer 重启后壁纸 + Widget 恢复  
- [ ] 对话面板关开 5 次，会话仍在  
- [ ] 卸载不删用户壁纸包/Widget 配置（或明确提示）  

### 6.5 安全与合规

- [ ] 日志无 API Key / Token  
- [ ] AI 生成内容仅 preview，Apply 前二次确认  
- [ ] 破坏性操作有 ConfirmationRequest  
- [ ] 隐私政策 URL 有效  

---

## 7. 发布闸门（Go / No-Go）

发布 v0.1 当且仅当：

- [ ] SD0 M3 真机验收签字完成  
- [ ] SD2 黄金路径 3 连过  
- [ ] SD3 无 Key 演示模式可用  
- [ ] SD4 MSIX 干净机安装成功  
- [ ] ARM64 CI（含 contract guards）在 release SHA 全绿  
- [ ] 无 P0/P1 开放缺陷（遮挡图标、崩溃、AI 全程无工具且未提示 fallback）  
- [ ] Partner Center 技术校验通过  

---

## 8. 风险登记

| 风险 | 影响 | 缓解 |
|------|------|------|
| 仅 ARM64 | Store 受众小 | v0.1 明确定位 Snapdragon PC；x64 列 v0.2 |
| M3 真机未关闸 | 差评「遮住图标」 | SD0 阻塞一切 Store 工作 |
| API Key 门槛 | 转化低 | SD3 演示模式 |
| FullTrust 审核 | 上架延迟 | 提前准备权限说明与演示视频 |
| Pi Provider 不稳定 | Agent 掉 fallback | UI 明示 + 日志；优先测用户常用 Provider |
| 包体过大 | 下载差 | 首发不含 Harness 运行时（若已隐藏）评估瘦身 |

---

## 9. 不在 v0.1 范围（明确 defer）

- M4 新设置壳完整 parity  
- 播放列表 / 自动化 / 性能策略产品化  
- Scene 编辑器、Timeline、Shader  
- Harness 作为主卖点  
- x64 正式包（除非 SD4b 启动）  
- M12 无 gh 的完整自动更新器（Store 更新即可）  

---

## 10. 下一步立即动作（本周）

1. **真机：** 在 ARM64 设备跑 `MiaoDeskWidgetAcceptance.exe` 全序列，收集证据包。  
2. **工程：** 合并 Agent 固定模式相关改动到 `main` 并联调黄金话术。  
3. **产品：** 确认 SD3 选 A（内置演示）还是 B（强制引导）。  
4. **商店：** 补 `Assets\` 图标与截图脚本模板。  

---

## 11. 文档索引

| 文档 | 用途 |
|------|------|
| 本文 | Store Demo 总计划 |
| `docs/STORE_DEMO_V0.1_ACCEPTANCE.md` | 验收操作手册（SD1 创建） |
| `docs/WIDGET_ACCEPTANCE_SEQUENCE_M3.md` | Widget 真机序列 |
| `docs/AI_GENERATED_DESKTOP_SANDBOX.md` | 预览 Apply 契约 |
| `docs/PI_AGENT_CONVERSATION_UX.md` | 对话面板 UX |
| `packaging/windows-store/` | MSIX 模板 |
