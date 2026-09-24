# Third-Party Notices

MiaoDesk includes or redistributes the following third-party components. Runtime versions are pinned by the architecture runtime locks; applicable upstream license files are retained in the vendored payloads or alongside them.

## Pi

- Project: Pi
- Package: `@earendil-works/pi-coding-agent`
- Source: https://github.com/earendil-works/pi
- Pinned MiaoDesk version: `0.83.0`
- License: MIT
- MiaoDesk usage: default Agent Runtime for ordinary AI and desktop-agent requests, launched in RPC mode on MiaoDesk's private Node.js runtime.

## DeepSeek Harness

- Project: DeepSeek Harness
- Package: `@deepseek-ai/dsh`
- Source: https://github.com/deepseek-ai/deepseek-harness
- License: MIT
- MiaoDesk usage: official upstream package launched by `MiaoDeskHarness.exe` and rendered inside MiaoDesk WebView2.

The Windows x64 package resolves Pi and DeepSeek Harness together into one production `Runtime/Agent` dependency graph. End-user machines do not install them with npm or npx.

## Node.js

- Project: Node.js
- Source: https://nodejs.org/ / https://github.com/nodejs/node
- License: Node.js project license plus licenses for bundled third-party components.

Node is private to MiaoDesk and is not installed system-wide.

## goz

- Project: goz
- Source: https://github.com/mustafaahci/goz
- Pinned MiaoDesk version: `v0.1.1`
- License: MIT
- MiaoDesk usage: NTFS filename-search backend through `goz.exe` / `gozd.exe`.

## Microsoft WebView2 SDK

- Project: Microsoft Edge WebView2 SDK
- Package: `Microsoft.Web.WebView2`
- Pinned SDK version: `1.0.4129.50`
- Source: https://www.nuget.org/packages/Microsoft.Web.WebView2
- License/notices: retained under `third_party/webview2/`.

MiaoDesk keeps only the headers and x64/ARM64 static loaders required by the native build, plus the upstream license/notice and `manifest.json`. The Microsoft Edge WebView2 Runtime itself is treated as an operating-system component and is not duplicated in the repository.

## Microsoft PowerToys

- Project: Microsoft PowerToys / PowerToys Run Program plugin
- Source: https://github.com/microsoft/PowerToys
- License: MIT
- Copyright: Copyright (c) Microsoft Corporation. All rights reserved.
- MiaoDesk usage: application discovery follows the mature pattern of combining classic Windows program shortcuts with packaged-app identities/AUMIDs; the implementation is native to MiaoDesk.

## Flow Launcher

- Project: Flow Launcher
- Source: https://github.com/Flow-Launcher/Flow.Launcher
- License: MIT
- Copyright: Copyright (c) 2019 Flow-Launcher; Copyright (c) 2015 Wox
- MiaoDesk usage: the in-memory matcher is an independent compact adaptation of ordered-subsequence matching, contiguous-match bonuses, word-boundary bonuses and early-match weighting.

---

# 本地 AI 推理栈(Local AI Inference Stack)

以下组件用于 `docs/LOCAL_AI_ARCHITECTURE.md` 描述的可选本地 AI 模式。**它们由用户在自己的硬件上
部署，MiaoDesk 不安装、不分发、不打包这些组件**；此处列出是为满足许可透明度与归属要求。

Windows 发布包还通过 `packaging/windows/verify-no-bundled-local-models.ps1` 强制这一边界：常见模型权重
扩展名以及当前许可未完成书面确认的模型名称一旦出现在 staged package，打包立即失败。该门禁只是
**分发边界**，不是对上游许可证作法律结论；若未来要把任何本地模型权重随 MiaoDesk 一起分发，必须先
单独完成许可证审核并显式修改这条门禁。

## vLLM

- Project: vLLM
- Source: https://github.com/vllm-project/vllm
- License: Apache 2.0
- MiaoDesk usage: 本地推理服务器,对外暴露 OpenAI 兼容 API,供 MiaoDesk 的 Pi Runtime 作为
  Provider 接入。部署方式为用户自管的 Docker 容器,不在产品包内。

## gpt-oss-120b / gpt-oss-20b

- Project: gpt-oss(OpenAI open-weight models)
- Source: https://huggingface.co/openai/gpt-oss-120b · https://huggingface.co/openai/gpt-oss-20b
- License: Apache 2.0
- MiaoDesk usage: 本地 AI 模式的主对话 / Agent 模型候选 A,以及轻量任务模型。模型权重由用户自行下载。

## Qwen3.6-35B-A3B

- Project: Qwen3.6-35B-A3B(Alibaba Qwen)
- Source: https://huggingface.co/Qwen/Qwen3.6-35B-A3B
- License: Apache 2.0
- MiaoDesk usage: 本地 AI 模式的主对话 / Agent 模型候选 B。与候选 A 并行评估,以实测数据定夺。

## Qwen3-4B-Instruct-2507

- Project: Qwen3-4B-Instruct-2507(Alibaba Qwen)
- Source: https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507
- License: Apache 2.0
- MiaoDesk usage: 本地 AI 模式的轻量任务模型备选(意图路由 / JSON 抽取 / 摘要)。

## Qwen3-Embedding

- Project: Qwen3-Embedding(Alibaba Qwen)
- Source: https://huggingface.co/Qwen/Qwen3-Embedding-4B · https://huggingface.co/Qwen/Qwen3-Embedding-0.6B
- License: Apache 2.0
- MiaoDesk usage: 可选的语义检索增强,用于补充(非替代)goz 关键词文件搜索。v1 默认不部署。

## DeepSeek-R1-0528-Qwen3-8B

- Project: DeepSeek-R1 distilled series
- Source: https://huggingface.co/deepseek-ai/DeepSeek-R1-0528-Qwen3-8B
- License: ⚠️ **代码 MIT;模型权重条款需向 DeepSeek 书面确认**
- MiaoDesk usage: 可选的显式推理模型。v1 默认不部署;**许可澄清前不得进入任何分发版本**。

## ComfyUI

- Project: ComfyUI
- Source: https://github.com/comfyanonymous/ComfyUI
- License: GPL-3.0
- MiaoDesk usage: 本地图像生成运行时。NVIDIA 为 DGX Spark 提供官方 playbook,Image Gen Quick Start
  默认模型为 Z-Image-Turbo。由用户自管部署;MiaoDesk 通过一个自有的薄 OpenAI 兼容 shim 调用其
  `POST /prompt` 接口。
- 注意:GPL-3.0 的传染性仅作用于 ComfyUI 自身的分发。MiaoDesk 通过 HTTP API 调用它,不链接、
  不分发其代码,因此不受其 copyleft 约束;但用户若自行修改并再分发 ComfyUI,需遵守 GPL-3.0。

## Z-Image-Turbo

- Project: Z-Image-Turbo(Tongyi-MAI)
- Source: https://huggingface.co/Tongyi-MAI/Z-Image-Turbo
- License: Apache 2.0
- MiaoDesk usage: 本地 AI 模式的默认图像生成模型。6B + 8 GB Qwen3-4B 编码器,约 16–24 GB,
  中英文字渲染良好,NVIDIA DGX Spark playbook 默认模型。

## Qwen-Image(v1.0)

- Project: Qwen-Image(Alibaba Qwen)
- Source: https://huggingface.co/Qwen/Qwen-Image
- License: Apache 2.0
- MiaoDesk usage: 本地 AI 模式的按需加载图像模型,中文文字渲染 SOTA。
- ⚠️ **不可使用 Qwen-Image-2.1**:其采用 Qwen Research License Agreement(2026-09-20),
  明示 "NON-COMMERCIAL PURPOSES ONLY"。只有 v1.0 是 Apache 2.0。

---

## 明确排除的组件(许可与商业分发冲突)

以下组件经评估**不纳入** MiaoDesk 的本地 AI 模式,列此以备忘:

| 组件 | 排除原因 |
| --- | --- |
| FLUX.1 [dev] / FLUX.1 Kontext [dev] | FLUX.1 [dev] Non-Commercial License;**权重非商用**,模型卡仅允许输出商用 |
| FLUX.2-dev / FLUX.2-klein-9B | FLUX Non-Commercial License,同上 |
| Stable Diffusion 3.5 Large | Stability Community License,**仅年营收 <100 万美元时免费**,超出需 Enterprise 许可 |
| Qwen-Image-2.1 | Qwen Research License,明示 NON-COMMERCIAL |
| HunyuanImage-3.0 | tencent-hunyuan-community 许可条款未能核实 |
| Stable Diffusion XL 1.0 | Open RAIL++-M 允许商用且无营收上限,但模型卡明说 "cannot render legible text",不满足产品需求 |
| GLM-4.5 / GLM-4.5-Air / GLM-4.6 | 许可证存疑(官方模型卡写 "mistralai/MIT + deepseek license",社区报告为 MIT);商用前须书面澄清 |
| Llama 4 全系 | Llama 4 Community License 限制严苛("Built with Llama" 署名义务、700M MAU 上限、欧盟多模态限制) |

> 本清单随 `docs/LOCAL_AI_ARCHITECTURE.md` §7.6 更新。任何新增本地 AI 组件前,先在此登记许可证结论。
