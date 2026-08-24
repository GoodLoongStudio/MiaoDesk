# TuringDesk 产品与开发基线

- 状态：唯一当前产品基线
- 日期：2026-08-24
- 正式开发分支：`main`
- 适用范围：TuringDesk Native 主线
- 原则：旧文档仅保留历史与技术参考；与本文冲突时，以本文为准。

## 1. 产品定义

TuringDesk 不再按内部 Runtime 层级拆成多个让用户理解的产品入口。

用户只需要理解：

```text
TuringDesk
├─ A. 顶部统一入口：本地极速搜索 + 图灵智能桌面 AI
├─ B. 桌面系统：Wallpaper Engine 级壁纸 + Widget + 编辑器
└─ C. 高级工作台：DeepSeek Harness
```

一句话定义：

> TuringDesk = 一个统一的桌面入口 + Wallpaper Engine 级桌面创作/运行系统 + AI 可控桌面 + 可选高级工作台。

用户界面统一使用“图灵智能桌面 / 图灵 AI / 高级工作台”等产品语言，不向普通用户暴露内部 Runtime、Pi、L3、Harness 协议、Node 等实现名词。

---

## 2. A：顶部统一入口

### 2.1 一个输入框

```text
Alt + Space
  ↓
TuringDesk 顶部输入框
  ├─ 应用搜索
  ├─ 文件 / 文件夹极速搜索
  ├─ AI 问答
  └─ 桌面控制 / 创作
```

原则：

1. 用户输入文字时立即展示应用和文件结果。
2. 本地结果可直接打开。
3. Enter / 明确任务指令进入 AI。
4. AI 可以执行经过 TuringDesk 验证的桌面操作，但必须以真实 Tool Result 为完成依据。
5. 不再创建互相竞争的多个搜索/AI 主入口。

### 2.2 本地搜索

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

目标：Everything 级响应速度；应用、文件、文件夹统一结果；补齐拼音、模糊匹配、历史权重与恢复能力。

### 2.3 AI 路由

```text
用户问题 / 桌面任务
  ↓
Pi Agent Runtime
  ↓ 不可用 / Provider 不兼容 / 启动失败
轻量 Direct Model 问答
```

Pi 是默认 Agent Runtime，使用 TuringDesk 自带 Node 24。Provider / Model / Base URL / API Key 由设置中心统一管理，API Key 长期只存 Windows Credential Manager。

Direct Model 仅作为普通问答 fallback，不承担第二套桌面自动化架构。

---

## 3. B：桌面系统

### 3.1 核心模型：Desktop Composition

TuringDesk 的桌面不是“一个壁纸窗口”，而是分层组合系统：

```text
Desktop Composition
├─ Wallpaper Layer
│  ├─ Image
│  ├─ Video
│  ├─ Web
│  └─ Scene
├─ Widget Layer
│  ├─ Web Widget
│  ├─ Native Widget
│  └─ Data-bound Widget
└─ Control Layer
   ├─ Settings Center
   ├─ Scene / Widget Editor
   └─ 图灵 AI Desktop Control API
```

Wallpaper 与 Widget 是两个独立层。切换壁纸不得删除用户的 Widget 布局。

### 3.2 产品目标

功能深度以 Wallpaper Engine 级体验为目标，可以完整学习成熟产品在壁纸库、多屏、播放列表、应用规则、性能、属性和编辑器方面的功能模型，但 TuringDesk 保持自己的品牌、代码、素材、包格式与交互实现。

TuringDesk 的差异化能力：

1. **AI 可调整桌面**：读取当前桌面状态，应用壁纸，调整属性、多屏、播放列表、性能策略、Scene 和 Widget。
2. **Widget 一等公民**：Widget 与壁纸独立持久化，可多屏摆放、缩放、隐藏、编辑，并能被 AI 创建和修改。
3. **AI 与手工编辑共用同一个状态模型**：AI 不是隐藏脚本入口，最终必须与 Settings / Editor 共用同一个 typed Desktop Control API。

### 3.3 设置中心一级信息架构

```text
桌面设置
├─ 已安装
├─ 小组件
├─ 播放列表
├─ 多屏
├─ 应用规则
├─ 性能
└─ 常规 / AI
```

#### 已安装

目标布局：

```text
┌─────────────────────────────────────────────────────────────┐
│ 桌面设置                                      [导入壁纸]    │
├─────────────────────────────────────────────────────────────┤
│ 已安装 | 小组件 | 播放列表 | 多屏 | 应用规则 | 性能 | AI   │
├───────────────────────────────┬─────────────────────────────┤
│ 真实缩略图卡片库              │ Live Preview / 详情         │
│ 当前使用 / 收藏 / 类型标签    │ 标题 / 类型 / 属性摘要      │
│ 搜索 / 过滤                   │ 显示器目标                  │
│                               │ [应用到桌面] [编辑]         │
└───────────────────────────────┴─────────────────────────────┘
```

一级主页不是 Scene Editor，也不是一页传统 Win32 参数表单。

#### 小组件

小组件页最终提供：

- 桌面实时预览；
- Widget 卡片/列表；
- 启用/禁用；
- 显示器目标；
- 拖拽移动与缩放；
- 数值化位置/尺寸 Inspector；
- 删除、复制、编辑；
- AI 创建/修改结果立即同步。

Widget v1 默认 click-through，不得挡住桌面图标；交互式 Widget 后续作为显式 opt-in 能力。

### 3.4 桌面编辑器

Scene 与 Widget 共用一套编辑器骨架：

```text
左：Project / Layers / Assets
中：Live Renderer Preview
右：Typed Inspector
底：Timeline / Keyframe / Events
```

统一属性类型至少包括：bool、int/float、enum、string、color、vector、resource reference、curve/easing、data binding。

同一个 typed property schema 同时服务：

- 手工 Inspector；
- 项目序列化；
- AI 编辑；
- Undo / Redo；
- 运行时参数更新。

### 3.5 Wallpaper Layer

正式资源类型：

```text
Image
Video
Web
Scene
```

统一包方向：`.tdwall`。

目标能力：

- 图片 / 视频 / Web / Scene；
- 多显示器 Span / Clone / Primary / Independent；
- 每屏不同资源；
- 播放列表 / Schedule / Profile；
- 应用规则；
- 性能策略；
- Shader / Particle / Animation；
- Audio Reactive；
- Interaction；
- 2D / 3D Scene；
- Live Preview / Properties / Editor。

### 3.6 Widget Layer

统一包方向：`.tdwidget`。

Widget 类型方向：

```text
Web
Text
Clock / Calendar
Image
System Status
Media Controls
Data-bound Custom Widget
```

首版先使用隔离 WebView2 本地 HTML Widget，复用当前 Web runtime 的安全与恢复机制；后续补原生 Widget 类型。

---

## 4. AI Desktop Control

AI 必须通过 TuringDesk 专属 Desktop Control API 修改桌面，而不是长期直接修改 INI、进程内存或私有文件。

目标链路：

```text
图灵 AI
  ↓
Pi Agent
  ↓
typed TuringDesk desktop tools
  ↓
Desktop Control API
  ↓
Validate → Transaction → Runtime
  ↓
真实 Result + 新 State
```

最终能力至少包括：

```text
desktop_state_get
wallpaper_apply
wallpaper_properties_update
playlist_update
display_assignment_update
performance_policy_update
widget_create
widget_update
widget_remove
widget_list
desktop_undo
```

原则：

1. AI 修改前可读取当前状态。
2. 所有 mutation 必须校验。
3. 使用稳定 wallpaper / monitor / widget id。
4. 宽范围 AI 修改前必须具备事务和 Undo / Redo。
5. AI 不能仅凭语言声称“已经换好壁纸/小组件”，必须收到真实执行结果。
6. 用户不使用 AI 时，所有桌面功能仍可通过 UI 完成。

当前首批 AI 桌面接口：读取桌面状态、验证并应用 Web `.tdwall`、创建/更新/删除/列出桌面 Widget。后续逐步统一到版本化 Desktop Control API。

---

## 5. C：高级工作台

DeepSeek Harness 是高级 Agent 工作台，不是普通 AI 问答默认路径。

```text
设置中心 / 高级入口
  ↓
启动后台服务
  ↓
用户明确点击“打开高级工作台”
  ↓
TuringDeskHarness WebView2
```

后台启动不得自动弹浏览器；与 TuringDesk 共用 Provider / Model / Base URL / API Key 配置，但 Runtime 生命周期独立。

---

## 6. 工具体系原则

通用任务优先走 Pi：

```text
read / write / edit / grep / find / ls
PowerShell / shell
Git
文本 / JSON / CSV
Skills / Extensions / Packages
```

TuringDesk Native Tool 只负责真正依赖产品内部状态的能力：

```text
settings
wallpaper / scene / .tdwall
widget / .tdwidget
playlist
multi-monitor
performance policy
notifications
```

不得重新发展第二套通用 C++ Agent 工具栈。

---

## 7. 当前完成度

以用户真实可用而不是“代码存在”为标准：

| 模块 | 当前状态 |
|---|---|
| 顶部统一入口 | 应用 / 文件 / AI 已接通，继续优化搜索体验 |
| goz 极速搜索 | MFT/USN 已接入并进入真实 ARM64 集成测试 |
| Pi Agent 主路由 | 已接通 Bundled Node + Pi RPC + Native Tool E2E |
| Direct Model fallback | 保留普通问答 fallback |
| Image / Video / Web / Scene | 运行时基础已存在 |
| 多显示器 / 规则 / 性能 | 核心模块已存在，继续真实机深度验收 |
| 壁纸库 / Playlist / Schedule / Profile | 数据和运行链已存在，前台产品化不足 |
| `.tdwall` | Web 包创建/校验基础已存在 |
| AI 壁纸能力 | 已能生成 `.tdwall`；正在扩展为读取/应用/编辑桌面状态 |
| Widget | 已进入持久化 + WebView2 overlay + AI CRUD 的第一阶段 |
| 设置中心 | 仍需重构为真实缩略图、Live Preview、Properties、Widgets 页 |
| Scene / Widget Editor | 共用编辑器目标已确定，尚未进入完整实现 |
| 高级工作台 | 后台/WebView/共享配置基础已存在 |

---

## 8. 当前最高优先级

AI 主链已经接通，下一阶段主线切换到 Desktop Composition：

### P0-1：桌面库产品化

- 真实缩略图卡片；
- 当前使用标识、收藏、类型标签；
- Live Preview；
- Properties；
- Apply / Edit 主链；
- 多屏目标可视化。

### P0-2：Widget 第一版

- Widget 页面；
- Web Widget runtime 稳定；
- 拖拽 / Resize；
- 多屏目标；
- AI Create / Update / Remove / List；
- `.tdwidget` 包基础。

### P0-3：统一 Typed Property System

- 壁纸和 Widget 共用属性 schema；
- Inspector 与运行时绑定；
- 为 Scene Editor 和 AI 控制提供唯一状态模型。

### P0-4：Scene / Widget Editor v1

- Project / Layers；
- Live Preview；
- Inspector；
- Timeline / Keyframe；
- Undo / Redo；
- 再进入 Shader / Particle / Audio Reactive / 3D / Interaction。

详细执行清单见 `docs/WALLPAPER_ENGINE_PARITY.md`，架构见 `docs/DESKTOP_COMPOSITION_ARCHITECTURE.md`。

---

## 9. 完成标准

```text
代码写完       ≠ 完成
编译通过       ≠ 完成
CI 通过        ≠ 完成
Mock 通过      ≠ 完成
真实 Windows 用户流程通过 = 完成
```

桌面系统最低真实验收逐步扩展为：

```text
从壁纸库应用 Image / Video / Web / Scene
指定不同显示器使用不同桌面
全屏程序触发正确性能策略
让图灵 AI 生成并真实应用一个 Web 桌面
让图灵 AI 创建一个桌面小组件
手工移动/缩放该小组件后 AI 能读取并继续调整
重启 TuringDesk 后壁纸和 Widget 状态保持
```

当前所有正式交付只进入 `main`。
