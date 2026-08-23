# TuringDesk 产品与开发基线

- 状态：唯一当前产品基线
- 日期：2026-08-23
- 正式开发分支：`main`
- 适用范围：TuringDesk Native 主线
- 原则：旧文档仅保留历史与技术参考；与本文冲突时，以本文为准。

## 1. 产品只保留三个核心入口

TuringDesk 不再按 L1/L2/L3/L4 拆成多个让用户理解的产品入口。

用户只需要理解：

```text
TuringDesk
├─ A. 顶部统一入口：本地极速搜索 + 图灵智能桌面 AI
├─ B. 设置中心：桌面库 / 桌面编辑 / 系统设置
└─ C. DeepSeek Harness：高级 Agent 工作台
```

一句话定义：

> TuringDesk = 一个统一的桌面入口 + Wallpaper Engine 级桌面系统 + Codex CLI AI + 可选的 DeepSeek Harness 高级工作台。

---

## 2. A：顶部统一入口

### 2.1 一个输入框，不做两个入口

本地极速搜索和 Codex CLI 使用同一个顶部输入框。

```text
Alt + Space
  ↓
TuringDesk 顶部输入框
  ├─ 应用搜索
  ├─ 文件 / 文件夹极速搜索
  └─ AI / Codex CLI
```

交互原则：

1. 用户输入文字时立即展示应用和文件结果。
2. 本地结果可以直接选择并打开。
3. 用户直接按 Enter、Ctrl+Enter，或输入明显问答/任务指令时，进入 AI。
4. 不再设计“文件搜索入口”和“AI 入口”两个互相竞争的主入口。

### 2.2 本地极速搜索

正式后端：

```text
TuringDesk Search UI
  ↓
GozSearch Adapter
  ↓
goz.exe / gozd.exe
  ↓
NTFS MFT + USN Journal
```

Everything 不再属于当前正式基线。

目标体验：

- Everything 级响应速度；
- 应用、文件、文件夹统一结果；
- 后续补齐拼音、模糊匹配、历史权重、结果排序；
- 搜索服务异常时自动恢复，不影响 AI 使用。

### 2.3 AI 路由

正式路由只保留两级：

```text
用户问题 / 桌面任务
  ↓
Codex CLI
  ↓ 不可用 / Provider 不兼容 / 启动失败
轻量 Direct Model 问答
```

不再把 DirectTools 作为默认中间层。

#### Codex CLI

Codex CLI 是普通 AI 请求和桌面操作的首选 Agent Runtime。

- 身份：`图灵智能桌面 AI` / `Turing Intelligent Desktop AI`
- 可以使用 TuringDesk 注册的 Native Tools；
- Chat Completions Provider 通过 Codex Relay 转换为 Responses API；
- Responses Provider 可直接连接；
- 不允许模型自行声称桌面动作完成，必须以 Tool Result 为准。

#### 轻量 Direct Model fallback

只承担：

- 普通问答；
- 简单连续对话；
- Codex CLI 不可用时保证 AI 入口仍可使用。

明确限制：

- 不承担复杂 Agent 工具循环；
- 不作为第二套桌面自动化架构；
- 不执行任意 Shell；
- 身份始终是 `图灵智能桌面 AI`，不能回答成 DeepSeek、OpenAI、Codex CLI 或“TuringDesk L3”。

---

## 3. B：设置中心

设置中心一级主页的 UI 和信息架构以当前确认的旧版 DesktopLibrary / Wallpaper Engine 风格布局为准。

### 3.1 一级页面布局

```text
┌─────────────────────────────────────────────────────────────┐
│ 桌面设置                                      [导入壁纸]    │
│ 场景、播放列表、多屏、应用规则、性能和 AI                  │
├─────────────────────────────────────────────────────────────┤
│ 已安装 | 播放列表 | 多屏 | 应用规则 | 性能 | AI / 高级设置 │
├───────────────────────────────┬─────────────────────────────┤
│                               │                             │
│  桌面卡片库                   │  当前选中桌面详情           │
│  ┌──────┐ ┌──────┐           │  标题 / 类型 / 描述         │
│  │预览  │ │预览  │           │  显示器目标                 │
│  │标题  │ │标题  │           │  [应用到桌面]               │
│  └──────┘ └──────┘           │  [编辑] / 其他操作          │
│                               │                             │
└───────────────────────────────┴─────────────────────────────┘
```

一级主页不是 Scene Editor，也不是普通参数表单。

### 3.2 设置中心主要内容

至少包含：

#### 桌面相关
- 已安装桌面资源；
- 图片 / 视频 / Web / Scene；
- 导入壁纸；
- 应用到全部显示器或指定显示器；
- 收藏、删除、最近使用；
- 播放列表；
- 多屏配置；
- 应用规则；
- 性能策略。

#### 桌面编辑

从桌面卡片的“编辑”进入二级编辑器：

```text
左：Layer / Project
中：实时 Renderer Preview
右：Inspector
底：Timeline / Keyframe
```

最终目标向 Wallpaper Engine 编辑器靠拢，而不是在一级设置主页直接塞编辑器。

#### AI 设置

设置中心内统一管理：

- Provider；
- Model；
- Base URL；
- API Key；
- Codex CLI 状态；
- Codex Relay 状态；
- API Key 使用 Windows Credential Manager 保存。

#### DeepSeek Harness

设置中心内提供明确的 Harness 区域：

- Harness 状态；
- 启动 / 停止；
- 打开 Harness 工作台；
- 与 TuringDesk 共用模型配置；
- 后台服务启动和 WebView UI 打开必须分离；
- TuringDesk 启动 Harness 后台时不能自动弹出 Web 页面。

---

## 4. C：DeepSeek Harness

DeepSeek Harness 是高级 Agent 工作台，不是普通 AI 问答的默认路径。

```text
设置中心 / 高级入口
  ↓
启动 Harness 后台服务
  ↓
用户明确点击“打开 Harness 工作台”
  ↓
TuringDeskHarness WebView2
```

原则：

1. 普通搜索和 AI 不依赖 Harness。
2. Harness 后台可以随 TuringDesk 管理生命周期。
3. 后台启动不得自动弹 UI。
4. Harness UI 只在用户明确打开时出现。
5. Harness 和 Codex CLI 共享 Provider / Model / Base URL / API Key 配置，但运行时互相独立。

---

## 5. Wallpaper / Desktop Engine 基线

运行时目标：Wallpaper Engine 级桌面引擎。

当前正式资源类型：

```text
Image
Video
Web
Scene
```

统一包方向：`.tdwall`

当前重点顺序：

1. 桌面库与应用流程稳定；
2. 多显示器和性能规则稳定；
3. Web / Video / Scene 运行时稳定；
4. `.tdwall` 统一包格式；
5. Scene Editor：Layer / Inspector / Timeline；
6. Shader / Particle / Audio Reactive / 3D / Interaction；
7. AI 生成 `.tdwall` 并可继续编辑。

---

## 6. 当前完成度

以“用户真实可用”而不是“代码存在”为标准：

| 模块 | 状态 | 完成度 |
|---|---|---:|
| 顶部统一入口 | 已有应用搜索、文件搜索、AI 入口 | 85% |
| goz 极速文件搜索 | 已接入 MFT/USN 后端，仍需排序/恢复/体验强化 | 80% |
| Codex CLI 主路由 | 主体已接入，Relay 已有；仍需彻底收口 app-server 解析边界 | 75% |
| 轻量 Direct Model | 已有流式问答，需统一“图灵智能桌面 AI”身份 | 80% |
| Native Tools | 文件、PPT、设置、壁纸包等基础工具已存在 | 75% |
| 设置中心一级主页 | 已恢复卡片库 + 右侧详情的正确方向 | 75% |
| 桌面编辑器 | 目标结构已明确，真正 Layer/Inspector/Timeline 尚未完整接回 | 40% |
| Image / Video / Web / Scene 运行时 | 基础能力已存在 | 80% |
| `.tdwall` | 包结构和 Web 生成基础已存在 | 60% |
| 多显示器 / 规则 / 性能 | 已有模块，仍需真实机深度验收 | 70% |
| DeepSeek Harness | 后台、WebView、共享配置基础已存在，生命周期仍需继续稳定 | 80% |
| 整体“普通用户可用度” | 核心链路成型，仍处于收口阶段 | 70% |

---

## 7. 下一阶段只做四件事

不要继续横向增加新模块，先收口：

### P0-1：统一入口稳定

- goz 搜索体验；
- 本地结果与 AI 切换逻辑；
- Alt+Space；
- 搜索、问答、桌面任务均从同一个输入框进入。

### P0-2：Codex CLI 稳定

- Codex CLI 永远优先；
- DirectTools 不参与默认路由；
- 修正 Windows app-server 非 JSON 输出污染；
- dynamic tool call 只解析真实 `arguments`；
- 最终消息 / streaming 均正常显示；
- 身份固定为“图灵智能桌面 AI”。

### P0-3：设置中心收口

- 一级页面严格使用“左卡片库 + 右详情”的布局；
- 卡片增加真实预览；
- “应用到桌面”和“编辑”成为一级核心动作；
- API、性能、多屏、规则、Harness 全部在同一个设置中心体系内；
- 不再出现多个互相重叠的设置窗口。

### P0-4：Harness 生命周期

- 后台静默；
- UI 显式打开；
- 不自动弹浏览器；
- 与设置中心状态同步；
- 真实 ARM64 机器验证。

完成以上四项后，再继续扩展 Wallpaper Engine 深层编辑能力。

---

## 8. 完成标准

```text
代码写完       ≠ 完成
编译通过       ≠ 完成
CI 通过        ≠ 完成
Mock 通过      ≠ 完成
真实 Windows 用户流程通过 = 完成
```

当前所有正式交付只进入 `main`。
