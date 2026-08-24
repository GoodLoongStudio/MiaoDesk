# TuringDesk 产品与开发基线

- 状态：唯一当前产品基线
- 日期：2026-08-24
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

> TuringDesk = 一个统一的桌面入口 + Wallpaper Engine 级桌面系统 + Pi Agent AI + 可选的 DeepSeek Harness 高级工作台。

---

## 2. A：顶部统一入口

### 2.1 一个输入框，不做两个入口

本地极速搜索和 Pi Agent 使用同一个顶部输入框。

```text
Alt + Space
  ↓
TuringDesk 顶部输入框
  ├─ 应用搜索
  ├─ 文件 / 文件夹极速搜索
  └─ AI / Pi Agent
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
Pi Agent Runtime
  ↓ 不可用 / Provider 不兼容 / 启动失败
轻量 Direct Model 问答
```

#### Pi Agent Runtime

Pi 是普通 AI 请求和桌面操作的首选 Agent Runtime。

正式组件：

```text
Bundled Node 24
  ↓
@earendil-works/pi-coding-agent
  ↓
Pi Agent Loop
```

产品原则：

- 身份统一为 `图灵智能桌面 AI` / `Turing Intelligent Desktop AI`；
- Agent 规划、工具循环、上下文、Skills 和 Extensions 由 Pi 管理；
- Provider / Model / Base URL / API Key 继续由 TuringDesk 设置中心统一管理；
- API Key 只长期存储在 Windows Credential Manager；
- Pi 支持 OpenAI Chat Completions、OpenAI Responses、Anthropic Messages、Google Generative AI 等协议；
- 文件、脚本、Git、文档生成等通用能力优先走 Pi 工具体系；
- TuringDesk 只保留真正属于桌面产品的专属 Tool；
- 模型不得自行声称动作完成，必须以真实 Tool Result / 文件结果 / 命令结果为准。

#### 轻量 Direct Model fallback

只承担：

- 普通问答；
- 简单连续对话；
- Pi Runtime 不可用时保证 AI 入口仍可使用。

明确限制：

- 不承担复杂 Agent 工具循环；
- 不作为第二套桌面自动化架构；
- 不执行任意 Shell；
- 身份始终是 `图灵智能桌面 AI`。

---

## 3. B：设置中心

设置中心一级主页的 UI 和信息架构以当前确认的 DesktopLibrary / Wallpaper Engine 风格布局为准。

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
- Pi Runtime 状态；
- Node Runtime 状态；
- Skills / Extensions 状态；
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
5. Harness 和 Pi Runtime 共享 Provider / Model / Base URL / API Key 配置，但运行时互相独立。

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

## 6. Pi 工具体系原则

TuringDesk 不再为每一种通用任务增加一个独立 C++ Tool。

通用任务优先走 Pi：

```text
文件读取 / 写入 / 编辑
目录搜索
Shell / PowerShell
Git
压缩与解压
CSV / JSON / 文本处理
PPTX / DOCX / XLSX 生成
脚本执行
Skills / Extensions / Pi Packages
```

TuringDesk 专属 Tool 只负责：

```text
settings_open
wallpaper / scene
.tdwall
playlist
multi-monitor
performance policy
notifications
其他真正依赖 TuringDesk 内部状态的桌面操作
```

Windows Shell 必须由 TuringDesk 提供稳定运行环境，不能要求普通用户预装开发工具。

---

## 7. 当前完成度

以“用户真实可用”而不是“代码存在”为标准：

| 模块 | 状态 |
|---|---|
| 顶部统一入口 | 已有应用搜索、文件搜索、AI 入口，继续收口 |
| goz 极速文件搜索 | MFT/USN 后端已接入，继续优化排序/恢复/体验 |
| Pi Agent 主路由 | **正在由旧 Runtime 全面迁移到 Pi** |
| 轻量 Direct Model | 已有流式问答，保留为失败回退 |
| Agent 通用工具 | 迁移到 Pi read/write/edit/search/shell/Skills/Extensions 体系 |
| TuringDesk Native Tools | 收缩为桌面产品专属 Tool |
| 设置中心一级主页 | 已恢复卡片库 + 右侧详情方向 |
| 桌面编辑器 | Layer/Inspector/Timeline 仍需继续开发 |
| Image / Video / Web / Scene 运行时 | 基础能力已存在 |
| `.tdwall` | 包结构和 Web 生成基础已存在 |
| 多显示器 / 规则 / 性能 | 已有模块，仍需真实机深度验收 |
| DeepSeek Harness | 后台、WebView、共享配置基础已存在，继续稳定生命周期 |

---

## 8. 下一阶段只做四件事

### P0-1：Pi Runtime 完整迁移

- 删除旧 Agent Runtime、旧协议桥和旧 RuntimeBundle；
- Bundled Node 直接承载 Pi；
- Provider / Model / Base URL / API Key 自动映射到 Pi；
- 接通 Pi SDK/RPC 流式输出；
- 接通文件工具、Shell、Skills、Extensions；
- ARM64 真机验证。

### P0-2：统一入口稳定

- goz 搜索体验；
- 本地结果与 AI 切换逻辑；
- Alt+Space；
- 搜索、问答、桌面任务均从同一个输入框进入。

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

## 9. 完成标准

```text
代码写完       ≠ 完成
编译通过       ≠ 完成
CI 通过        ≠ 完成
Mock 通过      ≠ 完成
真实 Windows 用户流程通过 = 完成
```

Pi Runtime 的最低真实验收：

```text
帮我在桌面创建一个 txt 文件
帮我读取并修改这个文件
帮我执行 PowerShell 并返回真实输出
帮我生成一个不依赖 Office 的 PPTX
```

当前所有正式交付只进入 `main`。
