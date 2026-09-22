# MiaoDesk 本地 AI 架构

- 状态:设计文档,待评审后实施
- 日期:2026-09-20
- 上游:`PRODUCT_VISION.md`(漂亮的智能桌面)· `L3-PI-RUNTIME-CONTRACT.md` · `AI_GENERATED_DESKTOP_SANDBOX.md`
- 硬件:NVIDIA DGX Spark(GB10 Grace Blackwell)

## 1. 目标

让 MiaoDesk 的全部 AI 功能可以**完全运行在用户自己的硬件上**,不依赖任何云端服务商:

```text
搜索框的 AI 入口 · Conversation Panel 对话 · Desktop Agent 工具调用
壁纸 / 组件内容生成(skills/) · Harness 工作台
```

设计约束:

1. **不改变产品路由**。现有 `Conversation Panel → Pi Runtime → Provider` 链路保持不动。
2. **不削弱安全边界**。本地模型必须拿到与云端模型完全相同的工具沙箱。
3. **不给桌面渲染路径增加负担**。AI 始终不进入每帧渲染路径。

## 2. 硬件现实(DGX Spark)

规格来自 NVIDIA 官方文档与第三方实测。**标注 ⚠️ 的是来源不一致或未经官方确认的数字,不作为设计依据。**

| 项目 | 数值 |
| --- | --- |
| 芯片 | GB10 Grace Blackwell Superchip(CUDA 6144 核,5th-gen Tensor Core,原生 FP4) |
| 算力 | 最高 1 PFLOP @ FP4(**含稀疏**);⚠️ NVIDIA 未公布 FP8/FP16/BF16,第三方估计互相矛盾,本设计不依赖 |
| 内存 | **128 GB 一致性统一 LPDDR5x** |
| 内存带宽 | **273 GB/s** ← **这是绑定约束** |
| CPU | 20 核 Arm(10× Cortex-X925 + 10× Cortex-A725),⚠️ 约 3.9 GHz(第三方) |
| 存储 | 1 TB / 4 TB NVMe(自加密) |
| 网络 | ConnectX-7,标称 200 GbE/口,**实测约 96 Gbps/口**(PCIe Gen5 x4 限制) |
| 形态 | 150×150×50.5 mm,1.2 kg,240 W 电源,SoC TDP 140 W |
| 功耗实测 | 空闲 40–45 W,LLM 推理 60–90 W,满载 <200 W |
| 系统 | DGX OS 7.5.0(基于 Ubuntu 24.04),内核 6.17,CUDA Toolkit 13.0.2 |
| 官方支持推理引擎 | **vLLM · SGLang · TensorRT-LLM · llama.cpp · NVIDIA NIM · LM Studio** |

### 2.1 带宽决定了模型选型方向

273 GB/s 大约是 RTX 5090 的 1/3、H100 的 1/7。它决定了:

- **稠密大模型不可用**:70B 稠密 @ 4-bit(约 40 GB)解码仅约 **7 tok/s**,可用但慢。
- **MoE 是甜点区**:200B 以下的 MoE(如 gpt-oss-120b @ MXFP4)约 **14.5 tok/s** —— 这是 NVIDIA 官方 reviewer 与第三方实测一致的结论。
- **单机是单用户/少用户设备**:60–90 W 推理功耗,273 GB/s 带宽由所有并发请求共享。设计目标按 **1–3 并发用户**规划,不做服务集群。
- **KV cache 与权重抢同一 128 GB 池**:官方 vLLM playbook 用 `--max-model-len 131072` + `--gpu-memory-utilization 0.8`。长上下文必须显式规划,不能默认拉满。
- **集群是未来选项**:最多 3 台直连 / 4 台经交换机;官方称双机 405B、四机最高 700B。v1 只做单台,但架构上保留 endpoint 抽象以便扩容。

### 2.2 性能预期(未调优的官方基准,非承诺值)

| 模型 | 量化 | 解码 | 说明 |
| --- | --- | --- | --- |
| gpt-oss-20B | MXFP4 | **>49 tok/s** | 最快,留有余量 |
| gpt-oss-120B | MXFP4 | **14.5 tok/s** | 甜点区主力 |
| Qwen3-32B | — | 9–10 tok/s | 稠密,偏慢 |

以上为 stock Ollama/Open WebUI 的未调优结果。**调优后通常有明显提升,但在真实 Windows 环境验证前不作为承诺。**

## 3. 关键结论:接入本地模型不需要改产品代码

这一点已逐行核对现有实现,不是推断:

| 能力 | 现状 | 位置 |
| --- | --- | --- |
| Provider-neutral 路由 | 已有 | `Conversation Panel → Pi Runtime → Provider` |
| Provider 注入 | `providerId` + `baseUrl` + `model` + `apiKey` | `PiRuntime::ProviderSetup`(`PiRuntime.h:62-70`) |
| loopback 免密钥 | 已有,自动注入占位密钥 | `PiRuntime.cpp:374` — `if (apiKey.empty() && IsLoopbackUrl(baseUrl)) apiKey = L"miaodesk-local"` |
| 多 Profile + 默认选择 | 已有 | `LoadDefault()`(`ApiRuntimeProfile.h:185`) |
| 凭据存储 | Windows Credential Manager,按 profile 隔离 | `MiaoDesk/ApiProfile/<id>` |
| Header 安全校验 | 已有,拒绝控制字符与非 ASCII | `IsHttpHeaderSafe()` |
| 工具沙箱 | **宿主侧强制,与模型无关** | `main.cpp` allowlist |
| 超时 / 取消 | 已有 | `PiRuntimeStatus::Cancelled`、`ReadLine(timeoutMs)` |
| 流式输出 | 已有 | `DeltaCallback` |

**结论:本地模型只是"另一个 Provider profile"。产品侧零改动。**

### 3.1 一个必须注意的边界

`IsLoopbackUrl` 与 `NeedsKey` 都只豁免 `localhost` / `127.0.0.1` / `::1`,**不豁免私网段**。因此:

```text
http://127.0.0.1:8000/v1      → loopback,免密钥,零配置
http://192.168.1.50:8000/v1   → 非 loopback,要求真实访问令牌
```

DGX Spark 放在局域网上属于后者。**这是正确的安全默认值** —— 局域网端点应当认证。设计上不为它开特例;令牌走既有的凭据存储通道即可,同样零代码改动。

## 4. 总体拓扑

```text
┌─────────────────────────────┐
│ Windows 客户端(x64 / ARM64)  │
│  MiaoDesk.exe               │
│    Conversation Panel       │
│    Pi Runtime + Bundled Node│
│    MiaoDesk Native tools    │
└──────────────┬──────────────┘
               │ HTTPS / HTTP(局域网)
               │ Base URL = http://<spark>:8000/v1
               │ Authorization: Bearer <访问令牌>
               ▼
┌─────────────────────────────┐
│ DGX Spark(用户自有)          │
│  DGX OS 7.5.0 (Ubuntu 24.04)│
│  vLLM OpenAI-compatible API │
│  ├─ 主模型 A/B 候选           │  主对话 / Agent(见 §6.2)
│  ├─ gpt-oss-20b  (MXFP4)    │  轻量任务
│  ├─ Qwen3-Embedding-4B      │  语义检索(可选)
│  └─ DeepSeek-R1-0528-Qwen3-8B  推理(可选)
└─────────────────────────────┘
```

**没有 GoodLoongStudio 的服务器参与任何一环。** 这与隐私政策模式 B 的承诺一致。

### 4.1 为什么不放在同一台 Windows 机器上

可以,但不推荐作为主形态:

- Windows 客户端常驻桌面,GPU/内存被渲染路径占用;再塞一个 120B 模型会互相挤压。
- DGX Spark 是独立主机(150×150×50.5 mm、1.2 kg、240 W),本来就设计为局域网设备。
- 分离后,推理升级 / 换模型不影响客户端;客户端升级也不影响推理服务。

同一台机器(loopback)作为**降级形态**保留:小模型(gpt-oss-20b 或 Qwen3-4B 级)可以在客户端本机跑,零配置。

## 5. 推理服务器选型

官方支持六个引擎,按桌面产品的要求筛:

| 引擎 | OpenAI 兼容 API | 结构化输出 | 前缀缓存 | 结论 |
| --- | --- | --- | --- | --- |
| **vLLM** | 完整 | guided decoding | 完整 APC | **选它** |
| SGLang | 完整 | 完整(compressed FSM 更快) | 完整(RadixAttention) | 备选 |
| TensorRT-LLM | 完整 | 需 xgrammar/outlines | 不完整 | 不选,锁定 NVIDIA 工具链 |
| llama.cpp | 完整 | 部分(GBNF/OAI 语法) | 部分 | 仅用于低配回退 |
| LM Studio | 有 | 有 | 不完整 | 不选,面向 GUI 用户 |
| NIM | 完整 | 有 | 不完整 | 不选,NGC 锁定 + 需 NVAIE 许可 |

**选 vLLM**,理由:

1. NVIDIA 为 DGX Spark 提供**官方 playbook 与预构建 NGC 容器**,版本可锁定、可复现。
2. 完整支持本设计需要的全部能力:continuous batching、paged attention、prefix caching、guided decoding(JSON schema)、function calling。
3. 多 GPU / 多节点扩展路径与未来的 Spark 集群一致。
4. `--gpu-memory-utilization` 可显式控制 KV cache 与权重的内存分配 —— 这对 128 GB 统一内存是必需 knob。

**已知代价(必须在文档中承认)**:vLLM 对 Arm + `sm_121` 属 Tier 3 支持, nightly wheel 才可用;官方 reviewer 明确说 Spark "不是最成熟的 NVIDIA 平台"。因此部署文档必须锁定确切的 wheel / 容器版本。

**SGLang 作为备选**:其 compressed-FSM structured output 更快、RadixAttention 在多轮会话缓存上更强。若实测结构化输出吞吐不足,可切换。

**llama.cpp 仅用于回退**:客户端本机 loopback 跑小模型时用,因为它的 CPU/低内存路径更省。

## 6. 模型选型

### 6.1 选型原则

1. **许可证必须允许商业分发** —— 本产品要走 Microsoft Store,非商用许可直接出局。
2. **必须装得进 128 GB 并给 KV cache 留空间**。
3. **优先 MoE**(受 273 GB/s 带宽约束)。
4. **工具调用与结构化 JSON 输出必须可靠** —— Desktop Agent 和内容生成都依赖它。

### 6.2 主对话 / Agent 模型

v1 **并行评估两个候选,以实测数据定夺**,不凭纸面参数选定。两者都在 NVIDIA 为 DGX Spark 验证过的模型矩阵内,都是 Apache 2.0。

**候选 A:`openai/gpt-oss-120b` @ MXFP4**

| 项 | 值 |
| --- | --- |
| 许可证 | Apache 2.0,商用无归属负担 |
| 参数 | 117B 总 / 5.1B 激活 |
| 上下文 | 131,072 |
| 解码速度 | **14.5 tok/s(实测)** |
| 显存 | 约 63 GB 权重,剩约 65 GB 给 KV cache |
| 已知代价 | 必须用 harmony 响应格式,更容易触发 vLLM 结构化输出边界情况 |

**候选 B:`Qwen/Qwen3.6-35B-A3B` @ FP8**

| 项 | 值 |
| --- | --- |
| 许可证 | Apache 2.0 |
| 参数 | 35B 总 / 3B 激活 |
| 上下文 | 262,144 原生(YaRN 可到约 1M) |
| 解码速度 | **30–50 tok/s(推算,非实测)** |
| 显存 | 约 35 GB 权重,剩约 93 GB 给 KV cache |
| 优势 | 中文与多语言更强;SWE-bench Verified 73.4;混合 Gated DeltaNet 架构;多模态 |
| 已知代价 | 结构化输出在推理内容未单独解析时可能被静默禁用,需 `--structured-outputs-config.enable_in_reasoning=True` |

**为什么不定一个**:候选 A 有实测吞吐但上下文短一半、显存占用高近一倍、且 harmony 格式有已知坑;候选 B 纸面全面占优但**没有任何实测 tok/s**。在真实 Windows + 真实网络环境跑完 §8 的验收清单前,选定任何一个都是赌。

**决策门**:两个候选都跑完 §8 清单后,按"中文对话质量 → 工具调用成功率 → 实测 tok/s → 显存余量"排序定夺。落选者保留为可切换配置。

**若 v1 要做本地图片生成,候选 B 实际接近必选** —— 候选 A + Z-Image-Turbo 常驻只剩约 25 GB 给 KV cache,长上下文会立刻吃紧。详见 §7.7。

### 6.3 轻量任务模型

**`openai/gpt-oss-20b` @ MXFP4** 或 **`Qwen/Qwen3-4B-Instruct-2507`**(Apache 2.0)。

- gpt-oss-20b:Spark 实测 **>49 tok/s**,21B/3.6B 激活,128K 上下文。
- Qwen3-4B-Instruct-2507:BFCL-v3 61.9、LiveBench 63.0,是小型模型里工具调用最强的。
- 用途:意图路由、JSON 抽取、摘要、内容生成初稿。
- 可与主模型同时常驻;候选 A + 20b 合计约 80 GiB,候选 B + 4B 合计约 40 GiB(余量更宽)。

**更小的路由/分类档**:`Qwen3-1.7B` / `Qwen3-0.6B`(Apache 2.0)。

### 6.4 嵌入模型(语义检索增强,可选)

**`Qwen/Qwen3-Embedding-4B`**(Apache 2.0,2560 维,32K 上下文,MTEB 多语言领先)。

- 用于增强 goz 的关键词文件搜索 —— **这是增强,不是替代**。goz 保持现有行为不变。
- 更轻的选择:`Qwen3-Embedding-0.6B`(1024 维)。
- ⚠️ 该系列在 MTEB 的多语言分数多为自报,建议用自建中英测试集复测。

### 6.5 推理模型(可选)

**`deepseek-ai/DeepSeek-R1-0528-Qwen3-8B`**(MIT 许可证)。

- 用途:需要显式推理链的场景(复杂桌面自动化规划)。
- ⚠️ 许可证细节需向 DeepSeek 确认:代码 MIT,模型权重可能另有条款。
- v1 可不部署;需要时再加,不影响其他模型。

### 6.6 明确不选

| 模型 | 原因 |
| --- | --- |
| Qwen3-235B-A22B | MXFP4 约 120 GB,**装不进**(还需 KV cache 空间) |
| GLM-4.5 / GLM-4.5-Air | ⚠️ **许可证存疑**。社区报告为 MIT,但官方模型卡仍写 "mistralai/MIT + deepseek license",而 DeepSeek 代码是 MIT、权重是自定 Model License。商用前必须取得书面澄清 |
| GLM-4.6 | ⚠️ 未找到可验证的权重或模型卡,不作为设计依据 |
| Llama 4 全系 | Llama 4 Community License 限制严苛(欧盟多模态限制、命名义务、"Built with Llama"标注),与商店分发冲突 |
| Kimi K2 / MiniMax 系 | 均为修改版 MIT(保留署名要求),增加合规成本;非必要不引入 |

## 7. 按任务切换模型(模型路由)

### 7.1 为什么这是必需,不是优化

三个理由,严重程度递增:

**理由一:不同任务的最优模型不同。**

| 任务 | 需要的能力 | 最优模型 |
| --- | --- | --- |
| 普通对话 | 中文质量、长上下文 | 主模型候选(§6.2) |
| Desktop Agent | 工具调用可靠性 | 主模型候选 |
| 意图路由 / JSON 抽取 | 低延迟 | 轻量模型(§6.3) |
| 内容生成(skills/) | 结构化 JSON 严格合法 | 主模型候选 |
| **图片生成** | **完全不同的模态** | **扩散模型,不是 LLM** |

**理由二:图片生成需要另一个模态的模型。** 它不是一个"更强的 LLM"能替代的,必须是一个图像生成模型。

**理由三(阻塞级):图片生成当前被硬编码在云端,本地模式下直接坏。**

```javascript
// src/ai/pi/PiNativeToolsExtension.cpp:46
const DEFAULT_IMAGE_MODEL = "google/gemini-2.5-flash-image";
// :154
const model = getImageModel("openrouter", DEFAULT_IMAGE_MODEL);
// :140-147
async function resolveOpenRouterApiKey() {
  const explicit = process.env.OPENROUTER_API_KEY?.trim();
  if (explicit) return explicit;
  const baseUrl = (await currentMiaoDeskBaseUrl()).toLowerCase();
  return baseUrl.includes("openrouter.ai")
    ? process.env.MIAODESK_MODEL_API_KEY?.trim() ?? ""
    : "";   // ← 本地 baseUrl 走到这里,返回空
}
```

`getImageModel("openrouter", ...)` 的 provider 字符串是硬编码的。当用户把 baseUrl 指向 DGX Spark 时:

- `baseUrl` 不含 `openrouter.ai` → 不复用主 key
- 没有 `OPENROUTER_API_KEY` 环境变量 → 返回 `""`
- `generateImage` 抛错:**"图片生成能力当前未配置:需要 OpenRouter API Key"**

**即:只要启用本地 AI,图片生成功能必然失效。** 这不是体验降级,是功能缺失。

### 7.2 路由放在哪一层

产品侧只有一个默认 profile,没有有序回退链(`LoadDefault()` 返回单个 profile)。因此路由有三层可选:

| 层 | 做法 | 产品改动 | 评价 |
| --- | --- | --- | --- |
| **L1 网关路由** | DGX Spark 上跑一个路由器,对外只暴露一个 OpenAI 兼容 endpoint,内部按任务分发 | **零** | 推荐用于 LLM 分流;但路由器只能从请求内容推断任务,不够精确 |
| **L2 多 provider 配置** | 让产品往 `models.json` 写多个 provider(`miaodesk` / `miaodesk-fast` / `miaodesk-image`),由 Pi 按任务选择 | 小 | **正确的长期形态**;需改 `ConfigurePiAgent` |
| **L3 客户端多 profile** | 用户手动切换 profile | 零 | 只适合人工决策,不适合自动分流 |

**建议:L1 + L2 组合。** LLM 分流先用 L1 上线(零风险),图片生成必须走 L2(因为它需要换 provider,不只是换模型名)。

### 7.3 当前产品配置的实际形态(改动的基准)

`PiRuntime::ConfigurePiAgent`(`src/ai/pi/PiRuntime.cpp:413`)写出:

```json
{
  "providers": {
    "miaodesk": {
      "name": "MiaoDesk Provider",
      "baseUrl": "<用户配置的 baseUrl>",
      "api": "<api 类型>",
      "apiKey": "$MIAODESK_MODEL_API_KEY",
      "models": [{
        "id": "<model>", "name": "<model>",
        "input": ["text"],
        "contextWindow": 128000,
        "maxTokens": 16384
      }]
    }
  }
}
```

配套 `settings.json`:`defaultProvider: miaodesk` + `defaultModel: <model>`。

**这里已经埋了两个问题:**

1. `input: ["text"]` —— 注释明确说明这是故意的("Image generation is a separate Pi tool/provider and must not cause every arbitrary chat endpoint to be advertised as vision-capable")。设计意图正确,但后果是图像能力必须走独立通道,而这个通道现在是断的。
2. `contextWindow: 128000` 与 `maxTokens: 16384` 是**硬编码常量**,不随用户选的模型变化。用户选一个 32K 上下文的模型,产品会告诉 Pi 它有 128K。这是既有缺陷,路由设计时应一并修掉。

### 7.4 目标形态

```text
Windows 客户端(零改动)
   ↓ baseUrl = http://192.168.1.50:8000/v1
   ↓ model   = miaodesk
┌──────────────────────────────────────────┐
│ DGX Spark                                │
│                                          │
│  模型路由器(L1)                            │
│    ├─ 有 tools 字段?      → 主模型(Agent)  │
│    ├─ 命中内容生成 skill? → 主模型(JSON)   │
│    ├─ 短请求 / 分类抽取?  → 轻量模型       │
│    └─ 否则               → 主模型(对话)    │
│                                          │
│  图像服务(独立端口,OpenAI images 兼容)      │
│    └─ POST /v1/images/generations         │
│                                          │
│  模型池                                    │
│    ├─ gpt-oss-120b / Qwen3.6-35B-A3B      │
│    ├─ gpt-oss-20b / Qwen3-4B              │
│    ├─ <本地扩散模型>(见 §7.6)              │
│    └─ Qwen3-Embedding(可选)               │
└──────────────────────────────────────────┘
```

路由器对外暴露**一个** model 名(如 `miaodesk`),客户端零改动。分流规则按请求特征:

```text
1. 请求带 tools / tool_choice          → 主模型(工具调用优先)
2. system prompt 命中内容生成 skill 签名 → 主模型(严格 JSON 模式)
3. 请求短且无 tools                     → 轻量模型(低延迟)
4. 其余                                 → 主模型
```

规则 2 需要一个稳定签名:`skills/` 的 SKILL.md 都有 `name:` frontmatter,可作为识别依据。**签名必须固定**,否则前缀缓存命中率会崩(§8 已列此项)。

### 7.5 图片生成本地化(必须改产品代码)

这是唯一无法用配置绕过的一项。三种做法:

| 方案 | 改动 | 风险 |
| --- | --- | --- |
| **A. 参数化 provider + model** | 把 `getImageModel("openrouter", DEFAULT_IMAGE_MODEL)` 改为从环境变量或 `models.json` 读取 | 最小;但依赖 `getImageModel` 是否支持非 openrouter provider |
| B. 直连本地图像 HTTP 服务 | 扩展里改为调用本地 `POST /v1/images/generations` | 不依赖 Pi 的 provider 抽象,但要自己处理认证与错误 |
| C. 保留云端,标注为可选 | 不动代码,文档说明本地模式下图片生成不可用 | 零改动,但"全本地 AI"的故事有缺口 |

**必须先验证一件事**:`@earendil-works/pi-ai/compat` 的 `getImageModel` 支持哪些 provider 字符串。本仓库未安装 `node_modules`,无法静态确认。若它支持 `openai-compatible` 之类的通用值,方案 A 就是几行改动;若只支持具名 provider,则需方案 B。

### 7.6 图像模型选型:商用许可是硬过滤器

这是整个本地 AI 设计里约束最紧的一环。**多数"最好"的模型因许可证直接出局。**

> 选型依据 NVIDIA 官方 Spark playbook(build.nvidia.com/spark)与第三方实测。显存为按参数量估算
> (FP16 ≈ 2 B/param,FP8 ≈ 1 B/param)加文本编码器与 VAE,多数模型卡不公布官方数字。

| 模型 | 商用分发 | Spark 验证 | 显存(估) | 1024px 速度 | 中文文字 |
| --- | --- | --- | --- | --- | --- |
| **Z-Image-Turbo** 6B(Tongyi-MAI) | ✅ **Apache 2.0** | ✅ **ComfyUI playbook 默认模型** | ~16–24 GB | **12.1s**(9 步,实测) | ✅ **好** |
| Qwen-Image 20B(v1.0) | ✅ Apache 2.0 | ❌ 无 Spark 证据 | ~30–50 GB | 212s(50 步 bf16) | ✅ 最好 |
| FLUX.2-klein-4B | ✅ Apache 2.0 | ❌ | ~13 GB(官方) | 4 步 | ❌ **差(官方列为局限)** |
| FLUX.1 [schnell] 12B | ✅ Apache 2.0 | ✅ 多模态 playbook | ~18–30 GB | **2.6s**(TensorRT FP4) | ⚠️ 弱 |
| SD 3.5 Large | ⚠️ **仅年营收 <$1M 免费** | ✅ | ~29 GB(实测) | 82s | ✅ 强 |
| FLUX.1 [dev] / Kontext [dev] | ❌ **权重非商用** | ✅ | 72–95 GB(实测) | — | ⚠️ |
| FLUX.2-dev / FLUX.2-klein-9B | ❌ **FLUX 非商用许可** | ❌ | ~34 / ~66 GB | — | ⚠️ 差 |
| Qwen-Image-2.1 7B | ❌ **Qwen Research License,明示 NON-COMMERCIAL** | ❌ | ~25 GB | — | ✅ |
| HunyuanImage-3.0 | ⚠️ **许可未核实** | ❌ | 4 GPU demo | — | ✅ |

**四个必须点名的许可陷阱:**

1. **FLUX.1 [dev] / Kontext [dev]** 与 **FLUX.2-dev / FLUX.2-klein-9B**:模型卡说"输出可商用",但**权重许可限制使用**。BFL 系只有 `schnell` 与 `FLUX.2-klein-4B` 是 Apache 2.0。
2. **SD 3.5 Large**:Stability Community License,**仅年营收 <100 万美元时免费**,超出需 Enterprise 许可。对要走商店的商业产品是真实坑。
3. **Qwen-Image-2.1**:Qwen Research License Agreement(2026-09-20)明示 "NON-COMMERCIAL PURPOSES ONLY"。**只有 Qwen-Image v1.0 是 Apache 2.0**,不要混用。
4. **HunyuanImage-3.0**:许可条款未能核实,商用前必须书面确认。

**运行时:一个被低估的障碍**

| 运行时 | Spark 验证 | API | 问题 |
| --- | --- | --- | --- |
| **ComfyUI** | ✅ **官方 playbook(默认 Z-Image-Turbo)** | `POST /prompt` + 完整 workflow JSON | **不是 OpenAI 兼容**,没有 `/v1/images/generations` |
| diffusers | 可用(bf16,cu130) | 无服务端,是 Python 库 | 需自己包一层 HTTP |
| NVIDIA TensorRT | ✅ 多模态 playbook | 仅 CLI demo 脚本,无 HTTP API | FP4 最快,但需逐模型 build engine |
| Ollama | playbook 仅 LLM | **完全没有文生图** | 不可用 |
| **vLLM-Omni** | ❌ **Blackwell/Arm64/DGX Spark 支持未记录** | ✅ **`POST /v1/images/generations`(DALL-E 兼容)** | API 最合适,但硬件支持未验证 |

**这是接入路径的关键分叉**:产品侧要改的 `image_generate` 需要的是一个 HTTP 图像端点。
ComfyUI 有官方 Spark 验证但 API 不匹配;vLLM-Omni 的 API 匹配但硬件未验证。
v1 的务实解法:**在 ComfyUI 前加一个很薄的 OpenAI 兼容 shim**(几十行,把 `prompt/size` 翻译成 workflow JSON),这样既拿到官方验证的运行时,又拿到产品需要的 API 形状。

**核心矛盾 —— 文字渲染 vs. 速度、验证与显存**

本产品是中文桌面产品,壁纸与组件常含文字。在文字渲染上:

- **Qwen-Image v1.0** 最好(LongText-Bench / ChineseWord / CVTG-2K SOTA,模型卡明说"especially for Chinese")
- **Z-Image-Turbo** 很好(模型卡明说"accurately rendering complex Chinese and English text")
- **FLUX.1 系与 FLUX.2-klein** 差或弱(FLUX.2-klein 官方把"文字可能不准确或畸变"列为局限)
- **SDXL** 模型卡直接说"cannot render legible text"

但 Z-Image-Turbo 12.1s vs FLUX.1-schnell TensorRT 2.6s,差 4.6 倍;Qwen-Image 更慢且显存占用高一倍。

**一个重要的缓解事实**:`skills/` 产出的内容包是 `scene.json`(声明式场景/矢量参数),**不是 AI 生成的光栅图**。内容框架的核心路径不依赖图像模型的文字渲染 —— 场景里的文字由 Scene TextRenderer 矢量绘制。只有 `image_generate` 这个面向用户的独立工具需要文字渲染。这把矛盾从"阻塞"降为"体验取舍"。

**v1 建议:Z-Image-Turbo 为主,按需加载 Qwen-Image**

```text
默认 / 通用图像   → Z-Image-Turbo(约 16–24 GB)
                     Apache 2.0 无争议;NVIDIA Spark playbook 默认模型;
                     中英文字都好;9 步约 12s
文字密集 / 高质量  → Qwen-Image v1.0 FP8(约 30 GB,按需加载)
                     中文文字唯一 SOTA;慢(50 步 bf16 212s,减少步数可显著改善);
                     Spark 未验证,因此不常驻
```

**若只能选一个:选 Z-Image-Turbo。** 它同时满足四项硬条件(Apache 2.0、Spark 官方验证、中文文字好、显存可接受),是唯一没有短板的选项。

**明确不选**:FLUX.1 [dev]、FLUX.1 Kontext [dev]、FLUX.2-dev、FLUX.2-klein-9B、Qwen-Image-2.1(全部许可冲突);SD 3.5 Large(营收门槛);HunyuanImage-3.0(许可未核实);SDXL(不能渲染可读文字)。

### 7.7 内存预算(路由与图像共存的真实约束)

128 GB 是**所有常驻模型共享**的。这是硬边界:

```text
主模型候选 A(gpt-oss-120b MXFP4)    约 63 GB
主模型候选 B(Qwen3.6-35B-A3B FP8)   约 35 GB
轻量模型(gpt-oss-20b / Qwen3-4B)    约 16 GB / 4 GB
Z-Image-Turbo(常驻)                 约 16–24 GB
Qwen-Image v1.0 FP8(按需,不常驻)    约 30 GB
────────────────────────────────────────────
候选 A + 20b + Z-Image-Turbo 常驻     约 103 GB,剩约 25 GB 给 KV cache → 紧张
候选 B + 4B  + Z-Image-Turbo 常驻     约 63 GB,剩约 65 GB 给 KV cache → 宽裕
候选 B + 4B  + Qwen-Image 按需        约 93 GB(图像加载时) → 需先卸载 Z-Image-Turbo
```

**结论:候选 B 的余量优势在有图像需求时是决定性的。** 候选 A + Z-Image-Turbo 常驻只剩约 25 GB 给 KV cache,长上下文会立刻吃紧;若还要按需加载 Qwen-Image,必须先卸载 Z-Image-Turbo,这会带来数十秒的冷启动。**要做图像生成,基本应当选候选 B。**

**按需加载是必需能力,不是优化。** Qwen-Image v1.0 必须不常驻;收到文字密集的图像请求时加载,用毕卸载,且加载前需先卸载 Z-Image-Turbo。这需要路由器管理模型生命周期,是 L1 网关的职责。冷启动数十秒,UI 上必须明示等待。

### 7.8 验收清单(路由与图像专项)

```text
[ ] 四类分流规则各命中正确模型(用日志核对,不靠推断)
[ ] 工具调用请求不会被误分流到轻量模型
[ ] 内容生成请求的 JSON 合法率不受分流影响
[ ] 前缀缓存在分流后仍稳定(签名固定)
[ ] 图片生成走本地服务,完全不触网(用抓包或防火墙日志证实)
[ ] FLUX.1 [schnell] 在 Spark 上 1024px 出图时间实测并记录
[ ] 图像模型与 LLM 共存不 OOM;按需加载生效
[ ] 任一模型不可用时,其余任务不受影响
[ ] 分流决策可从日志复现
[ ] 所有图像模型的许可证条款已书面确认可商用分发
```

## 8. 结构化输出与工具调用

Desktop Agent 与内容生成都依赖可靠的 JSON。实测既有能力都有**真实的坑**,必须写进部署验收:

1. **引导解码在流式下不生效**。vLLM 必须用非流式(或 `extra_key` 回退)才能保证 JSON 合法。
2. **超过 `max_tokens` 会截断 JSON,并以 HTTP 400 结束**,不是 200 + 坏 JSON。必须设置足够的 `max_tokens` 并处理 400。
3. **推理模型 + 结构化输出可能互相干扰**。gpt-oss 的 harmony 格式尤其容易触发。必须为不同模型分别做工具调用实测。
4. **prefix caching 依赖稳定的提示前缀**。系统提示 / 工具定义必须固定顺序,否则缓存命中率崩塌。
5. **`enable_auto_tool_choice` + `tool_call_parser` 必须匹配模型**,否则工具调用静默失败。

**上线前必须实测的项目**(每模型 × 每 API 模式):

```text
[ ] 非流式 JSON schema 约束输出,合法率 100%
[ ] 流式输出不依赖 JSON 合法性
[ ] max_tokens 截断时返回可诊断错误,不是静默坏数据
[ ] 工具调用(function calling)端到端成功
[ ] 前缀缓存命中率在多轮会话中稳定
[ ] 候选 A 与候选 B 分别通过上述全部项
[ ] gpt-oss-20b 与 Qwen3-4B 分别通过轻量任务全部项
```

## 9. 客户端接入

### 8.1 用户侧配置(零代码改动)

在既有 API 配置页新增一个 profile:

```ini
[profile:local-spark]
name=本地 DGX Spark
type=OpenAI Compatible
baseUrl=http://192.168.1.50:8000/v1
model=openai/gpt-oss-120b
default=1
```

访问令牌写入 Windows Credential Manager,目标名 `MiaoDesk/ApiProfile/local-spark`。

`PiRuntime`、`Direct Model`、`DeepSeek Harness` 三者都通过同一份默认 profile 读取,自动全部指向本地推理服务。

### 8.2 两种拓扑

| 拓扑 | Base URL | 密钥 | 适用 |
| --- | --- | --- | --- |
| **A. DGX Spark 局域网**(推荐) | `http://192.168.x.x:8000/v1` | 需要访问令牌 | 完整能力,120B + 20B + 嵌入 |
| **B. 客户端本机 loopback** | `http://127.0.0.1:8080/v1` | 免密钥(自动占位) | 降级形态,小模型,零配置 |

拓扑 B 自动生效于:`IsLoopbackUrl` 命中 → 注入 `miaodesk-local` 占位密钥。

### 8.3 发现机制:v1 不做

产品内**没有任何局域网发现能力**(无 mDNS/Bonjour/socket 发现代码)。v1 由用户手动填写 Base URL —— 零新增代码,且让用户明确知道自己的数据发去哪里。

自动发现留作后续增强,且必须满足:仅在同一广播域内、结果需用户确认、不得静默连接。

## 10. 安全边界

### 9.1 本地模型不获得额外权限

**工具沙箱由宿主强制,与模型无关**(`main.cpp` allowlist):

```text
settings_open · ppt_create · file_create · folder_list · file_open
wallpaper_validate_package · wallpaper_state_get · desktop_widget_list
+ generated preview tools
```

无论模型跑在云端还是 DGX Spark,可用工具集完全相同。这是本地 AI 设计的安全基石:**换模型不换沙箱**。

### 9.2 网络边界

- 推理服务器**只监听局域网**,不暴露到公网。
- 启用访问令牌(拓扑 A 强制)。即使泄漏,影响范围限于用户自己的网络。
- 建议进一步限制为仅允许Windows 客户端 IP 访问(防火墙规则)。
- 不使用 `--api-key` 之外的任何宽松模式。

### 9.3 凭据

- 访问令牌进 Windows Credential Manager,**不进** prompt / 日志 / session / 配置文件明文。
- 复用既有 `IsHttpHeaderSafe` 校验(拒绝控制字符与非 ASCII)。
- 日志可记录 endpoint / 模型 / 超时 / 退出码,**禁止**记录令牌(既有契约已要求)。

### 9.4 提示注入与内容生成

- AI 生成的内容包必须经 `content-review` 门禁(skills/)与产品侧 `wallpaper_validate_package` 双重校验后才可预览。
- **Preview ≠ Apply**:沙箱是临时目录,不写注册表 / 系统目录 / 持久桌面状态;只有用户显式确认才越过 commit 边界,且提交原子。
- AI 仍不输出 HTML / JavaScript / CSS / shell / 原生可执行文件。

### 9.5 隐私增益

本地模式下对话文本不离开用户自己的网络。隐私政策已同步修订(模式 B),`docs/privacy-policy.md`。

## 11. 性能预算与容量规划

### 10.1 客户端侧(不变)

| 约束 | 值 | 来源 |
| --- | --- | --- |
| 动画帧率 | 1–240,默认 60 | `MiaoSceneFrameScheduler.h:19-21` |
| 每 emitter 粒子 | ≤ 65536 | `MiaoSceneRuntimeModel.h:166` |
| 每 scene 粒子 | ≤ 131072 | `MiaoSceneRuntimeModel.h:167` |
| 渲染目标单维 | ≤ 16384 | `MiaoD3D11RenderPolicy.cpp:50` |
| 沙箱图片 / 视频 | ≤ 25 MiB / 250 MiB | `AI_GENERATED_DESKTOP_SANDBOX.md:74` |

AI / Node / Pi 不进入每帧渲染路径 —— 本地推理同样遵守。

### 10.2 服务端预算

| 项目 | v1 规划值 | 依据 |
| --- | --- | --- |
| 并发用户 | 1–3 | 273 GB/s 带宽共享 |
| 上下文长度 | 32768(默认),上限 131072 | 官方 playbook 用 131072;留 KV cache 余量 |
| GPU 显存利用率 | 0.8 | 官方 playbook 默认 |
| 常驻模型 | 主模型候选 + 轻量模型 | 候选 A + 20b 约 80 GiB(剩约 48 GB KV);候选 B + 4B 约 40 GiB(剩约 88 GB KV) |
| 预期首 token 延迟 | 未实测,**需在真实环境验证后填写** | 不可用官方未调优数字冒充承诺 |

### 10.3 容量不足时的路径

按代价从低到高:

1. 降低 `--max-model-len`(上下文换 KV cache)
2. 把轻量任务从 120B 移到 20b(已在规划内)
3. 加入 embedding 小模型 / 卸载不常用模型
4. 调优(调 batch / 量化 / 启用 spec decode)
5. **扩容**:加第二台 Spark(官方称双机 405B)—— 客户端只需改 Base URL,endpoint 抽象已支持

## 12. 降级与回退

现有 `LoadDefault()` 只返回**一个** profile,没有有序回退链。三种处理方式:

| 方案 | 做法 | 评价 |
| --- | --- | --- |
| **A. 网关层回退**(推荐) | 在 DGX Spark 的 vLLM 前加一层轻量网关,本地模型不可用时转发到用户自配的云端 provider | 产品零改动;但意味着数据可能出网,必须用户显式开启并明示 |
| B. 产品层回退链 | 改 `LoadDefault()` 支持有序 profile 列表 | 需改产品代码,违背"不改路由"约束 |
| C. 手动切换 | 用户在设置里换默认 profile | 零改动,但需要人工介入 |

**v1 采用 C + 保留 A 的架构位置**。本地服务不可用时,Pi Runtime 的既有超时 / 取消 / Direct Model fallback 机制生效,产品不会挂死。A 作为后续可选项,且开启时必须在 UI 上明确标示"数据将发送到云端"。

## 13. AI 功能 → 模型映射

模型分流规则见 §7.4。**标 ⚠️ 的是本地 AI 模式下当前不可用、需产品改动才能恢复的功能。**

| 功能 | 模型 | 说明 |
| --- | --- | --- |
| Conversation Panel 普通对话 | 主模型候选(§6.2) | 流式,`DeltaCallback` |
| Desktop Agent(Pi agent loop) | 主模型候选(§6.2) | 强工具调用要求 |
| 意图路由 / JSON 抽取 / 摘要 | gpt-oss-20b | 低延迟 |
| 壁纸 / 组件内容生成(skills/) | 主模型候选(§6.2) | 强结构化 JSON;产出交 `content-review` 门禁 |
| **图片生成(image_generate)** | **Z-Image-Turbo(常驻)/ Qwen-Image v1.0(按需)** | ⚠️ 当前硬编码 OpenRouter + Gemini,本地模式下**必然失效**;见 §7.1 / §7.5 / §7.6 |
| 包校验 | **无需模型** | 宿主侧 `MiaoContentPackage::Validate` |
| 状态读取 | **无需模型** | `wallpaper_state_get` / `desktop_widget_list` |
| DeepSeek Harness 工作台 | 同一本地 endpoint | 独立生命周期,可共用 profile |
| goz 文件搜索 | 非 AI | 关键词索引;Qwen3-Embedding 为可选增强 |
| 显式推理(可选) | DeepSeek-R1-0528-Qwen3-8B | v1 可不部署 |

## 14. 部署与运维

详见 `docs/LOCAL_AI_DEPLOYMENT.md`(实施文档)。要点:

- 使用 NVIDIA 官方 NGC 容器或 playbook,**锁定确切版本**(vLLM 对 `sm_121` 是 Tier 3 支持,nightly wheel)。
- 模型权重从 Hugging Face 拉取并校验 SHA-256,与现有 `runtime/<arch>/runtime-lock.json` 的做法一致。
- 健康检查:暴露 `/health` 与 `/v1/models`,客户端或运维脚本轮询。
- 日志:记录请求量 / 延迟 / 错误率 / 显存占用;**不记录对话内容与令牌**。
- 升级:模型与服务器版本分离,可独立回滚。

## 15. 商业发布要求

| 项目 | 状态 | 负责文档 |
| --- | --- | --- |
| 模型许可证 | 全部 Apache 2.0(gpt-oss / Qwen3 / Qwen3-Embedding),MIT(DeepSeek-R1 权重待确认) | `THIRD-PARTY-NOTICES.md` 需补充 |
| 推理服务器许可证 | vLLM(Apache 2.0) | 同上 |
| 隐私政策 | **已修订**,覆盖本地 / 局域网模式 | `docs/privacy-policy.md` |
| 第三方声明 | 待补充本地推理栈组件 | `THIRD-PARTY-NOTICES.md` |
| 本地 AI 部署文档 | 待编写 | `docs/LOCAL_AI_DEPLOYMENT.md` |

**合规红线**:GLM 系列在许可证澄清前**不得**进入任何分发版本;Llama 4 全系因许可限制不纳入。

## 16. 未决问题(需产品决策)

1. **GLM-4.5-Air 是否值得追**:它的 3B 激活 + MIT(待确认)组合很有吸引力,但许可证是硬门槛。建议向智谱取得书面澄清后再决定。
2. **网关层回退是否做**:方案 A 体验最好,但引入"数据可能出网"的岔路。建议 v1 不做,v2 再评估。
3. **是否需要 embedding 增强 goz 搜索**:这是产品能力增强,但会增加一个常驻模型和一份索引维护成本。建议先不做,验证主链路稳定后再评估。
4. **首 token 延迟承诺**:必须在真实 Windows + 真实网络环境实测后才能写进任何对外材料。
5. **模型更新策略**:用户自有硬件上的模型如何更新(自动 / 手动 / 锁定),涉及带宽与磁盘,需要单独设计。

## 17. 参考来源

- NVIDIA DGX Spark 产品页与官方硬件文档:`https://www.nvidia.com/en-us/products/workstations/dgx-spark/` · `https://docs.nvidia.com/dgx/dgx-spark/hardware.html`
- DGX Spark 官方推理引擎 playbook:`https://build.nvidia.com/spark`
- ServeTheHome DGX Spark 实测(功耗 / 带宽 / 基准):`https://www.servethehome.com/nvidia-dgx-spark-review/`
- vLLM 文档(structured outputs / prefix caching):`https://docs.vllm.ai/`
- gpt-oss 模型卡:`https://huggingface.co/openai/gpt-oss-120b` · `https://huggingface.co/openai/gpt-oss-20b`
- Qwen3-Embedding:`https://huggingface.co/Qwen/Qwen3-Embedding-4B`
- DeepSeek-R1 蒸馏系列:`https://huggingface.co/deepseek-ai/DeepSeek-R1-0528-Qwen3-8B`

> ⚠️ 标注警告的规格与许可证信息在本文档定稿后仍需复核;GLM 与 DeepSeek-R1 权重的许可证在商用分发前必须取得书面确认。
