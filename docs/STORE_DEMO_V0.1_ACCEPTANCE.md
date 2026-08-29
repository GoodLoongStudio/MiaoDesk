# 妙喵 Store Demo v0.1 验收手册

> 配套：`docs/STORE_DEMO_V0.1_PLAN.md`  
> 适用：ARM64 Windows 11 真机 / 验收 VM  
> 原则：**代码绿 ≠ 可上架**；本手册覆盖自动化 + 人工目视 + Store 素材。

---

## 1. 验收环境

| 项 | 要求 |
|----|------|
| 设备 | Windows 11 ARM64（Snapdragon 笔电/平板优先） |
| 版本 | 与 `main` release SHA 一致 |
| 安装 | `DEPLOY-NATIVE-ARM64.cmd` 或 `UPDATE-MIAODESK.cmd`（exact-head） |
| 验证 SHA | 安装目录 `.installed-build-sha` == `git rev-parse HEAD` |
| 网络 | 演示模式验收可断网；完整 AI 验收需 Provider 可达 |
| 账户 | 干净本地用户；无预装 API Key |

**记录模板：**

```text
设备型号：
Windows 版本：
构建 SHA：
安装方式：
验收人：
日期：
```

---

## 2. 自动化闸门（必须先过）

在仓库根目录或部署目录执行：

```powershell
# 1. 架构契约
powershell -ExecutionPolicy Bypass -File scripts\verify-l3-runtime-contract.ps1

# 2. 原生自检
MiaoDesk.exe --self-test
# 期望 exit 0

# 3. Widget 五阶段（M3）
MiaoDeskWidgetAcceptance.exe
# 期望 exit 0；产出证据目录见 WIDGET_ACCEPTANCE_EVIDENCE_M3.md

# 4. 独立校验证据包（若已 seal）
# 按 docs/WIDGET_INSTALLED_ACCEPTANCE_M3.md 运行 verifier
```

**失败处理：** 不得进入第 3–6 节人工验收；先修 `main` 或重新安装 exact-head。

---

## 3. 黄金路径（人工，连续 3 次）

每次从 **重启 MiaoDesk 或新会话** 开始，记录 Pass/Fail。

### 3.1 路径 A — 无 API Key（演示模式，SD3 完成后）

| 步骤 | 操作 | 期望 |
|------|------|------|
| A1 | 首次启动 | 首启引导出现；可跳过 |
| A2 | 设置 → 应用内置「海洋」或 Aurora showcase | 桌面动态壁纸可见 |
| A3 | 设置 → 小组件 → 新建时钟 ×3 | 极简 / 日期 / 玻璃各一，不重叠 |
| A4 | 点桌面图标 | 图标可点，不被 Widget 挡住 |
| A5 | 重启 Explorer | 壁纸 + 三只钟恢复 |

### 3.2 路径 B — 有 API Key（完整 Agent）

| 步骤 | 操作 | 期望 |
|------|------|------|
| B1 | 设置配置 Provider + Key + 测试连接 | 成功 |
| B2 | `Alt+Space` →「给我一个海边动态壁纸」 | 出现预览卡，非纯文字敷衍 |
| B3 | 点 Apply | 桌面变化；对话不谎称已应用（预览前） |
| B4 | 「加个玻璃时钟」 | preview widget → Apply → 桌面出现 |
| B5 | `Esc` 关面板 → 再 `Alt+Space` | 历史消息仍在 |
| B6 | `/runtime` | 显示 Pi Agent 主路由，非 Direct fallback |

**通过线：** 路径 A 或 B 各 **连续 3 次** 全步骤 Pass。

---

## 4. AI 话术抽检（10 条，有 Key）

见 `STORE_DEMO_V0.1_PLAN.md` 第 6.2 节表格。

记录表：

```text
# | 输入 | 实际工具/行为 | Pass?
1 | ...
...
10 | ...
```

通过线：≥8 Pass；#10 必须出现确认，不得静默删除。

---

## 5. 分层目视（签字项）

在 **路径 B 完成壁纸 + 三只钟后** 检查：

```text
[ ] 桌面图标在最上层，可点击
[ ] Widget 在图标下方、壁纸上方
[ ] 动态壁纸铺满工作区（非仅窗口内预览）
[ ] 打开设置窗口：Widget 仍可见
[ ] 打开搜索窗口：Widget 仍可见
[ ] 多显示器（如有）：主屏 placement 正确
```

验收人签字：________ 日期：________

---

## 6. 稳定性

| 项 | 方法 | 标准 |
|----|------|------|
| 冷启动 | 启动 10 次 | 0 崩溃 |
| 会话恢复 | 关面板 5 次 | 消息不丢 |
| Explorer | `taskkill /f /im explorer.exe` 后等待恢复 | 壁纸+Widget 回来 |
| 日志 | 打开 `Desktop\MiaoDesk-Logs\` | 无 Key/Token 明文 |

---

## 7. Store 包验收

| 项 | 标准 |
|----|------|
| MSIX 安装 | 干净机双击/Add-AppxPackage 成功 |
| 启动 | 无需 gh、无需 git、无需手动装 Node |
| 卸载 | 可卸载；用户内容策略与商店说明一致 |
| 体积 | 记录包大小 MB，评估是否需 Harness 瘦身 |
| 清单 | DisplayName=妙喵；arm64 |

---

## 8. 缺陷分级

| 级别 | 定义 | Store v0.1 |
|------|------|------------|
| P0 | 崩溃、遮挡图标、数据丢失、静默破坏 | 阻塞发布 |
| P1 | AI 全程无工具且未提示；壁纸/Widget 不恢复 | 阻塞发布 |
| P2 | 文案、次要 UI、非黄金路径功能 | 可带 known issues |
| P3 | 美化 | defer |

---

## 9. 证据归档

每次正式验收归档：

```text
evidence/store-demo-v0.1/<SHA>/
  build-sha.txt
  widget-acceptance-manifest.json + .sha256
  screenshots/   # 分层、预览卡、Apply 后
  golden-path-run-1..3.txt
  ai-prompt-matrix.txt
  msix/
  sign-off.pdf 或 sign-off.md
```

---

## 10. 签字发布

```text
[ ] 第 2 节自动化全绿
[ ] 第 3 节黄金路径 3×
[ ] 第 4 节 AI ≥8/10
[ ] 第 5 节目视签字
[ ] 第 6 节稳定性
[ ] 第 7 节 MSIX
[ ] STORE_DEMO_V0.1_PLAN.md 第 7 节 Go 闸门

发布 tag：store-demo-v0.1
```

验收负责人：________ 日期：________
