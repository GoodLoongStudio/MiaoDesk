# 项目说明：MiaoDesk

## 1. 项目是什么

MiaoDesk 是一个面向 Windows 的 AI 原生智能桌面。它不是把聊天机器人简单塞进桌面，而是让 AI 真正成为桌面的一部分。

用户平时只需要记住三个入口：

- 顶部搜索框：搜索应用、搜索文件，也可以直接进入 AI；
- 动态桌面：展示动态壁纸和桌面小组件；
- 妙喵 AI：通过自然语言理解用户想做什么，并调用桌面能力完成任务。

例如，用户可以直接说：

- “帮我找刚才下载的 PDF。”
- “做一个有飘落樱花的动态壁纸。”
- “帮我做一个桌面倒数日组件。”
- “把今天的重要任务显示在桌面上。”

AI 不只是回答一段文字，而是通过产品提供的工具和 Skills 生成真实的桌面内容包，经过校验和预览后，再由用户确认是否应用到桌面。

## 2. 为什么做这个项目

传统桌面软件的功能入口非常多。搜索、壁纸、组件、设置、AI、文件管理通常互相分离，用户需要记住大量菜单和操作步骤。

MiaoDesk 想解决的问题很简单：

> **让用户只描述目标，不必先学会软件。**

我们把搜索、桌面内容和 AI 放在同一个产品里。AI 负责理解意图，系统负责安全地执行。这样既保留了 Windows 原生桌面的效率，也让自然语言成为新的交互入口。

## 3. 核心亮点

### 3.1 AI 能真正操作桌面能力

MiaoDesk 为 AI 提供了受控工具，而不是让模型任意执行系统命令。AI 可以调用产品允许的能力，例如：

- 搜索应用和文件；
- 读取内容创作 Skill；
- 创建壁纸包和组件包；
- 校验内容包；
- 预览生成结果；
- 在用户确认后应用到桌面。

模型负责“理解和规划”，产品负责“执行和校验”。

### 3.2 AI 可以生成动态壁纸和桌面组件

MiaoDesk 定义了一套声明式 Content Package。

AI 生成的不是任意脚本，而是结构化的 JSON 内容：

- `.mdwall`：壁纸包；
- `.mdwidget`：组件包；
- `manifest.json`：基本信息和能力；
- `parameters.json`：可调参数；
- `scene.json`：场景、动画、粒子、文字、数据绑定。

这样做的优点是安全、可验证、可预览，也便于在不同机器上复现。

### 3.3 本地 AI 可以运行在 DGX Spark

MiaoDesk 的 AI Provider 使用 OpenAI Compatible 接口。也就是说，Windows 客户端不需要为某一个模型写死代码。

在 DGX Spark 上运行 vLLM 后，只要把 MiaoDesk 的 API 配置指向 DGX Spark，就可以把聊天、Agent 规划和内容创作切到本地模型。

这样可以带来三个直接价值：

1. **数据更私密**：对话和桌面上下文可以留在用户自己的网络里；
2. **模型更自由**：可以按任务切换不同模型，不被单一云服务绑定；
3. **算力离用户更近**：桌面 Agent 可以长时间运行，不必把每一步都发往公网。

### 3.4 同时支持本地模型与云模型

MiaoDesk 的 API 配置中心可以保存多套 Provider。

例如：

- 本地 DGX Spark：运行 Nemotron、gpt-oss、Qwen 等模型；
- StepFun：通过 OpenAI Compatible API 使用 `step-3.5-flash`；
- 其他 OpenAI Compatible 服务。

不同 AI 窗口可以单独选择自己的 API 配置，默认使用配置列表中的第一项。

这让 MiaoDesk 可以在“完全本地”“云端优先”“本地 + 云端混合”之间灵活切换。

## 4. 技术实现思路

整体架构可以简化为：

```text
用户
 ↓
Windows 搜索框 / 妙喵 AI / AI 壁纸与组件窗口
 ↓
Pi Agent / Direct Model
 ↓
API Profile
 ├─ DGX Spark 本地推理
 ├─ StepFun
 └─ 其他 OpenAI Compatible Provider
 ↓
Agent Skills + Native Tools
 ↓
搜索 / 文件 / 壁纸 / Widget / Content Package
 ↓
安全校验 + 预览
 ↓
用户确认
 ↓
应用到 Windows 桌面
```

Windows 产品主体使用 Native C++ 开发。动态桌面使用 Direct2D / D3D11 渲染；必要时才使用 WebView2。AI Runtime、桌面 Runtime 和内容 Runtime 相互分离，避免模型直接控制系统底层。

## 5. Agent Skills

MiaoDesk 当前内置四个主要内容创作 Skill：

- `content-package-basics`：定义内容包最基础的格式与安全边界；
- `wallpaper-content`：教 Agent 如何制作动态壁纸；
- `widget-content`：教 Agent 如何制作桌面小组件；
- `content-review`：生成完成后的安全与性能检查。

典型流程是：

```text
用户需求
 → content-package-basics
 → wallpaper-content 或 widget-content
 → content-review
 → 预览
 → 用户确认
 → 应用
```

## 6. DGX Spark 在项目中的作用

DGX Spark 不是一个“为了比赛而加的外部服务”，而是 MiaoDesk 本地 AI 架构的重要算力节点。

它主要承担：

- 大语言模型推理；
- Agent 工具调用与规划；
- 长上下文桌面任务；
- 本地图像生成；
- 多模型 A/B 测试和任务路由。

目前项目已经完成 DGX Spark 节点上的本地模型运行验证，并保留 vLLM、ComfyUI 和 OpenAI Compatible 的统一接入方式。

## 7. 当前成果

目前 MiaoDesk 已经形成一条完整产品链：

- Windows 原生搜索框；
- 应用搜索与文件搜索；
- 妙喵 AI 对话窗口；
- 多 API 配置中心；
- 每个 AI 窗口可独立选择 Provider；
- AI 壁纸创作；
- AI Widget 创作；
- 壁纸库与组件库；
- Native Widget；
- Scene Runtime；
- Direct2D / D3D11 渲染；
- Agent Skills；
- 内容安全与性能校验；
- x64 / ARM64 自动构建、安装和卸载验证；
- DGX Spark 本地 AI 部署方案。

## 8. 项目的核心价值

MiaoDesk 想证明一件事：

> **AI PC 的下一步，不只是“本地跑一个聊天模型”，而是让本地模型真正连接到用户每天都在使用的桌面环境。**

DGX Spark 提供本地算力，MiaoDesk 提供桌面交互、工具、Skills、内容 Runtime 和安全边界。两者结合后，AI 才能从“回答问题”变成“创造和管理用户自己的数字桌面”。

项目地址：

https://github.com/GoodLoongStudio/MiaoDesk
