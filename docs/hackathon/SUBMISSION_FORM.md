# DGX Spark Hackathon 表单简版文案

## 项目名称

MiaoDesk

## 开源仓库

https://github.com/GoodLoongStudio/MiaoDesk

## 一句话介绍

MiaoDesk 是一个 AI 原生 Windows 智能桌面：用户可以通过搜索框和妙喵 AI 搜索应用与文件、管理桌面，并让 Agent 直接生成动态壁纸和桌面组件；DGX Spark 用于在本地运行大模型和图像模型。

## 项目特点

1. AI 与 Windows 桌面深度结合，而不是独立聊天网页。
2. 支持本地 DGX Spark 与云端 Provider 同时存在。
3. 不同 AI 窗口可以独立选择 API 配置。
4. Agent 通过 Native Tools 完成真实操作。
5. 使用 Markdown Skills 教 AI 制作壁纸和组件。
6. AI 生成声明式 Content Package，而不是直接执行任意脚本。
7. 内容在应用前经过安全、性能和预览检查。
8. Windows x64 / ARM64 均有自动构建与安装验证。

## DGX Spark 用法

DGX Spark 运行 vLLM 和本地模型，MiaoDesk 通过 OpenAI Compatible API 连接。

主要承担：

- AI 对话；
- Agent 规划；
- 工具调用；
- 长上下文任务；
- 本地图像生成。

## NVIDIA 技术

- NVIDIA DGX Spark；
- CUDA 13.x（DGX OS 环境）；
- NVIDIA Container Runtime；
- NVIDIA DGX Spark Playbooks / NGC 部署参考；
- NVIDIA Nemotron-3.5-Lightning-30B-A3B-NVFP4。

## StepFun

MiaoDesk 支持通过统一 OpenAI Compatible API Profile 接入 StepFun。

测试配置示例：

- Model：`step-3.5-flash`

## Agent Skills

https://github.com/GoodLoongStudio/MiaoDesk/tree/main/skills

主要包括：

- content-package-basics
- wallpaper-content
- widget-content
- content-review

## 详细项目说明

https://github.com/GoodLoongStudio/MiaoDesk/blob/main/docs/hackathon/PROJECT_DESCRIPTION.md

## 部署说明

https://github.com/GoodLoongStudio/MiaoDesk/blob/main/docs/hackathon/DEPLOYMENT_GUIDE.md

## 技术栈

https://github.com/GoodLoongStudio/MiaoDesk/blob/main/docs/hackathon/TECH_STACK.md

## 视频

待上传 B 站后填写。

建议标题：

MiaoDesk：运行在 DGX Spark 上的 AI 原生 Windows 智能桌面

## 黑客松十日谈

文章草稿：

https://github.com/GoodLoongStudio/MiaoDesk/blob/main/docs/hackathon/HACKATHON_TEN_DAYS.md

发布到 CSDN / 知乎后，把公开文章 URL 替换到最终提交表单。

## 团队合影

按组委会表单单独上传，不提交到公开 GitHub。
