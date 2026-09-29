# 技术栈说明

## 1. 一张表看懂

| 部分 | 技术 |
| --- | --- |
| Windows 客户端 | Native C++ / Win32 |
| 桌面渲染 | Direct2D / D3D11 |
| Web 能力 | Microsoft WebView2（按需） |
| AI Agent | Pi Agent + MiaoDesk Native Tools |
| AI 接口 | OpenAI Compatible API |
| 本地推理 | NVIDIA DGX Spark + vLLM |
| 本地图像生成 | ComfyUI + OpenAI Compatible Shim |
| 内容系统 | MiaoDesk Content Package / Scene Runtime |
| Skills | Markdown SKILL.md |
| 文件搜索 | Goz NTFS 索引服务 |
| 自动构建 | GitHub Actions |
| 安装包 | NSIS |
| 架构 | Windows x64 / ARM64 |

## 2. NVIDIA 技术

### NVIDIA DGX Spark

MiaoDesk 使用 DGX Spark 作为本地 AI 推理节点。

主要用途：

- 运行本地大语言模型；
- Agent 推理；
- 长上下文任务；
- 多模型切换；
- 本地图像生成。

### CUDA

DGX Spark 的本地推理环境使用 CUDA 加速。项目部署文档当前以 DGX OS 自带的 CUDA 13.x 环境为基线。

### NVIDIA Container Runtime

当网络和镜像环境允许时，可以使用 NVIDIA Container Runtime 部署 vLLM / ComfyUI 等 GPU 服务。

### NVIDIA DGX Spark Playbooks / NGC

部署方案参考 NVIDIA 为 DGX Spark 提供的官方 Playbook。图像生成部分采用 NVIDIA Spark 文档支持的 ComfyUI 路线。

> MiaoDesk Windows 客户端本身不直接链接 CUDA 或 TensorRT。GPU 推理运行在 DGX Spark 服务端，客户端通过标准 API 调用。

## 3. NVIDIA 相关模型

项目中已经验证或纳入 DGX Spark 方案的 NVIDIA 相关模型包括：

### Nemotron-3.5-Lightning-30B-A3B-NVFP4

用途：

- 本地 Agent；
- 对话；
- 工具规划；
- DGX Spark 本地推理验证。

NVFP4 对 DGX Spark 这类 Blackwell 平台有很高的部署价值，可以在较小内存占用下运行更强的模型。

## 4. 其他本地模型候选

### gpt-oss-120b

定位：较强主模型候选。

适合：

- 复杂任务规划；
- 长上下文；
- Agent 多步执行。

### gpt-oss-20b

定位：轻量模型。

适合：

- 简单桌面任务；
- 快速响应；
- 低成本本地推理。

### Qwen3.6-35B-A3B

定位：兼顾质量和资源占用的主模型候选。

适合与图像模型同时驻留在 DGX Spark 时使用。

### Z-Image-Turbo

定位：本地图像生成。

运行方式：

```text
MiaoDesk → OpenAI Images API → Shim → ComfyUI → Z-Image-Turbo
```

用于生成壁纸素材等视觉内容。

## 5. StepFun / 阶跃星辰

MiaoDesk 的 API 配置中心支持 OpenAI Compatible Provider，因此可以直接配置 StepFun。

当前测试配置示例：

```text
Provider：StepFun
Model：step-3.5-flash
```

主要用途：

- 云端聊天；
- Agent 推理；
- 与 DGX Spark 本地模型做效果对比；
- 在本地模型不可用时，由用户手动选择云端 Provider。

MiaoDesk 不把 StepFun 写死在业务逻辑里，而是通过统一 API Profile 管理。

## 6. Agent 与 Skills

MiaoDesk 的 Agent 不依赖一个巨大 System Prompt 完成所有事情。

复杂内容创作被拆成 Skill：

- 内容包基础规则；
- 壁纸规则；
- Widget 规则；
- 安全与性能审核。

Skill 是 Markdown 文件，模型按任务读取。

这样更容易：

- 修改；
- 审核；
- 版本管理；
- 给其他 AI 使用；
- 避免 Prompt 无限膨胀。

## 7. 为什么采用这套技术栈

核心原则只有三个：

1. **桌面必须轻**：常驻部分优先 Native C++；
2. **模型必须可换**：统一走 OpenAI Compatible Provider；
3. **AI 必须受控**：模型负责规划，系统工具负责执行，Skills 负责规则。

项目地址：

https://github.com/GoodLoongStudio/MiaoDesk
