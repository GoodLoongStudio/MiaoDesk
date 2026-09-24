# 本地 AI 部署手册(DGX Spark)

- 状态:实施文档
- 日期:2026-09-20
- 上游:`LOCAL_AI_ARCHITECTURE.md`
- 适用:NVIDIA DGX Spark(GB10 Grace Blackwell,DGX OS 7.5.0)

本手册把架构文档落到可执行步骤。**所有版本号在部署当天必须重新核对** —— vLLM 对 Arm + `sm_121` 属 Tier 3 支持,只有 nightly wheel 可用,版本移动快。

## 0. 前置条件

```text
硬件   DGX Spark,128 GB 统一内存,≥1 TB NVMe
系统   DGX OS 7.5.0(基于 Ubuntu 24.04,内核 6.17)
工具链 CUDA Toolkit 13.0.2(随系统提供)
容器   Docker + NVIDIA Container Runtime(随系统提供)
网络   与 Windows 客户端同一局域网,ConnectX-7 或板载网口均可
```

确认机器就绪:

```bash
nvidia-smi                      # 应显示 GB10,128 GB 统一内存
cat /etc/dgx-release            # 确认 DGX OS 版本
docker info | grep -i nvidia    # 确认 NVIDIA Container Runtime
```

### 0.1 先量出口,再决定走哪条路(2026-09-22 实测)

**下面 §1.1 / §1.2 写的两条命令,在妙喵爱美丽那台节点上一条都跑不起来。** 先花十秒量一下
真实出口,否则会照着文档空转到怀疑人生:

```bash
for u in https://huggingface.co https://modelscope.cn https://pypi.org \
         https://pypi.tuna.tsinghua.edu.cn https://registry-1.docker.io https://nvcr.io; do
  printf '%-38s ' "$u"; curl -s -o /dev/null -m 8 -w '%{http_code}\n' "$u"
done
```

该节点(gx10-9e57)的实测结果:

| 可达 | 不可达 |
| --- | --- |
| `modelscope.cn`(302) | `huggingface.co`、`hf-mirror.com` |
| `mirrors.aliyun.com`(301) | `pypi.org` |
| `pypi.tuna.tsinghua.edu.cn`(302) | `registry-1.docker.io`、`auth.docker.io` |
| `mirrors.cloud.tencent.com`(200) | `nvcr.io`、`ghcr.io`、`github.com` |

于是本节两条命令的真实状态:

- **`docker pull nvcr.io/nvidia/vllm:<TAG>` —— 走不通。** NGC 不可达。本文档开头
  "所有版本号在部署当天必须重新核对"把注意力放在 TAG 上,而真正的障碍是 registry 本身:
  核对出一个正确的 TAG 也拉不到。占位符不是这个坑的形状。
- **`huggingface-cli download …` —— 走不通。** HuggingFace 与 hf-mirror 都不通。
  模型改从 **ModelScope** 拉(`modelscope download --model <id> --local_dir …`),
  它恰好可达。

**容器路线死了不等于没路走。** 那台节点上已经有一套能用的原生环境,不需要任何镜像:

```text
~/envs/vllm/bin/python   vllm 0.28.0 · torch 2.13.0+cu130 · cuda available True
~/models/<model>         权重已在磁盘上
```

`pip install` 同样受限 —— 装新包用 `-i https://pypi.tuna.tsinghua.edu.cn/simple`。
下次换机器/换网络,**先跑上面那个 for 循环**,不要假设出口和今天一样。

## 1. 推理服务器(vLLM)

### 1.1 拉取镜像

优先使用 NVIDIA 为 Spark 提供的官方 playbook 与预构建 NGC 容器:

```bash
# 参考: https://build.nvidia.com/spark  (vLLM playbook)
# 版本号以 playbook 当天给出为准,不要沿用本文档的示例值
docker pull nvcr.io/nvidia/vllm:<TAG-FROM-PLAYBOOK>
```

> ⚠️ 不要使用 `:latest`。部署完成后把实际使用的 TAG 记录到部署台账,复现与回滚都依赖它。
>
> ⚠️ **先读 §0.1。** NGC 不可达时这条命令必然失败,与 TAG 是否正确无关。此时改走节点上
> 已有的原生 venv:`~/envs/vllm/bin/vllm serve <本地权重目录>`,并用 `tmux` 托管。

### 1.2 准备模型目录

```bash
sudo mkdir -p /srv/miaodesk/models
sudo chown "$USER":"$USER" /srv/miaodesk/models
```

模型从 Hugging Face 拉取,**必须校验 SHA-256**,与项目 `runtime/<arch>/runtime-lock.json` 的做法一致:

```bash
export HF_HUB_ENABLE_HF_TRANSFER=1

# 主模型候选 A
huggingface-cli download openai/gpt-oss-120b \
  --include "*.safetensors" "*.json" "tokenizer*" \
  --local-dir /srv/miaodesk/models/gpt-oss-120b

# 主模型候选 B
huggingface-cli download Qwen/Qwen3.6-35B-A3B \
  --local-dir /srv/miaodesk/models/qwen3.6-35b-a3b

# 轻量模型
huggingface-cli download openai/gpt-oss-20b \
  --local-dir /srv/miaodesk/models/gpt-oss-20b
```

校验(示例,实际哈希以 Hugging Face 页面为准):

```bash
sha256sum /srv/miaodesk/models/*/*.safetensors | tee /srv/miaodesk/models/SHA256SUMS
```

> ⚠️ **HuggingFace 与 hf-mirror 在实测节点上均不可达**(见 §0.1)。等价写法,从可达的
> ModelScope 拉:
>
> ```bash
> pip install modelscope
> modelscope download --model AI-ModelScope/gpt-oss-120b \
>   --local_dir /srv/miaodesk/models/gpt-oss-120b
> ```
>
> 三条实务:
> 1. **大文件不要用 scp 上传** —— 黑客松手册红线 #7,公网带宽 50 台节点共用,
>    单次 >1 GB 严禁 scp。一律在节点上直接拉。
> 2. **先看仓库里 / 节点上已有什么。** 2026-09-22 那次部署,`~/models/` 下已经躺着完整的
>    Nemotron-3.5-Lightning-30B-A3B-NVFP4,`~/envs/vllm` 已经装好 —— 整个部署零下载。
>    照着本文档从零开始拉,是在重复别人已经做完的事。
> 3. SHA-256 校验照做,但**ModelScope 的哈希与 HF 页面的不是同一个来源**,记台账时
>    要写清校验值来自哪里,否则"校验通过"不是一个可复现的陈述。

### 1.3 启动参数

**候选 A(gpt-oss-120b)**:

```bash
docker run -d --name miaodesk-vllm-a --gpus all --ipc host \
  -p 8000:8000 \
  -v /srv/miaodesk/models:/models \
  nvcr.io/nvidia/vllm:<TAG> \
  vllm serve /models/gpt-oss-120b \
    --served-model-name openai/gpt-oss-120b \
    --dtype auto \
    --max-model-len 32768 \
    --gpu-memory-utilization 0.8 \
    --enable-auto-tool-choice --tool-call-parser openai
```

**候选 B(Qwen3.6-35B-A3B)**:

```bash
docker run -d --name miaodesk-vllm-b --gpus all --ipc host \
  -p 8001:8000 \
  -v /srv/miaodesk/models:/models \
  nvcr.io/nvidia/vllm:<TAG> \
  vllm serve /models/qwen3.6-35b-a3b \
    --served-model-name Qwen/Qwen3.6-35B-A3B \
    --max-model-len 32768 \
    --gpu-memory-utilization 0.8 \
    --enable-auto-tool-choice --tool-call-parser hermes \
    --structured-outputs-config.enable_in_reasoning=True
```

参数说明:

| 参数 | 为什么 |
| --- | --- |
| `--max-model-len 32768` | 官方 playbook 用 131072;桌面场景 32K 够用,省下的都给 KV cache |
| `--gpu-memory-utilization 0.8` | 官方 playbook 默认;显式控制权重与 KV cache 的分配 |
| `--enable-auto-tool-choice` + `--tool-call-parser` | 工具调用必须**匹配模型**,否则静默失败 |
| `--structured-outputs-config.enable_in_reasoning=True` | 候选 B 必需,否则推理内容可能让结构化输出被静默禁用 |

两个候选用不同端口,可同时在线做 A/B 实测,互不影响。

## 2. 图像生成服务

✅ **产品侧 OpenAI-compatible 图片调用链已接通。** MiaoDesk 可为图片单独配置 `imageBaseUrl` / `imageModel`，并直接调用 `<imageBaseUrl>/images/generations`；聊天和图片可以使用不同端口。选型依据与许可分析见 `LOCAL_AI_ARCHITECTURE.md` §7.6。

### 2.1 为什么单独一节

`image_generate` 是独立的图片能力，不把聊天 provider 宣称为 vision-capable。对于 `local-openai-compatible` / `openai-compatible`，MiaoDesk 自己通过 OpenAI Images 兼容协议调用图片端点；具名云 provider 仍可走 Pi compat。建议图片服务独立部署、独立端口。

### 2.2 产品侧前置改动(必须)

`src/ai/pi/PiNativeToolsExtension.cpp` 当前硬编码:

现在不再依赖 `getImageModel()` 是否认识本地 provider 名。通用 OpenAI-compatible 图片端点由 MiaoDesk 直接请求 `/images/generations`，要求返回 `data[0].b64_json`。配置中心可直接填写 Image Base URL 与 Image Model。

### 2.3 运行时:ComfyUI + OpenAI 兼容 shim

**选 ComfyUI**,因为它是唯一有 NVIDIA 官方 Spark playbook 的图像运行时
(`build.nvidia.com/spark/comfyui`,2026-09-10 更新,Image Gen Quick Start 默认模型即 Z-Image-Turbo)。

**但它的 API 不是 OpenAI 兼容** —— 是 `POST /prompt` + 完整 workflow JSON,没有
`/v1/images/generations`。因此需要一个很薄的 shim:

```text
产品 image_generate
   ↓ POST /v1/images/generations {prompt, size}
shim(约几十行)
   ↓ 翻译成 ComfyUI workflow JSON
   ↓ POST http://127.0.0.1:8188/prompt
ComfyUI
   ↓ 轮询 /history 取结果
   ↓ 返回 b64_json
产品
```

**为什么不选 vLLM-Omni**:它的 API 正好是 `/v1/images/generations`(DALL-E 兼容,服务 Qwen-Image 与
Z-Image-Turbo),但**其 Blackwell / Arm64 / DGX Spark 支持情况未记录**。若后续它发布 Spark 支持,
可以去掉 shim 这一层,是更干净的形态。

**Ollama 不可用**:其 OpenAI 兼容面只有 chat/completions/embeddings,**完全没有文生图**。

### 2.4 模型

**主模型:Z-Image-Turbo**(Tongyi-MAI)

- Apache 2.0,6B + 8 GB Qwen3-4B 编码器,约 16–24 GB。
- NVIDIA Spark playbook 默认模型;ComfyUI v0.33.2 + PyTorch cu130。
- 中英文字渲染都好(模型卡明说 "accurately rendering complex Chinese and English text")。
- 9 步 BF16 默认注意力实测 **12.1s**(NVIDIA 开发者论坛)。

**按需加载:Qwen-Image v1.0 FP8**

- Apache 2.0(**注意:Qwen-Image-2.1 是 Qwen Research License,明示 NON-COMMERCIAL,不可用**)。
- 中文文字 SOTA(LongText-Bench / ChineseWord / CVTG-2K)。
- 50 步 bf16 实测 212s / 63 GB;减少步数可显著改善。
- Spark 未经 NVIDIA 验证,**因此不常驻**。

**明确不选**:FLUX.1 [dev] / Kontext [dev](权重非商用)、FLUX.2-dev / FLUX.2-klein-9B(非商用)、
SD 3.5 Large(仅年营收 <$1M 免费)、HunyuanImage-3.0(许可未核实)、SDXL(不能渲染可读文字)。

### 2.5 部署

```bash
# ComfyUI(以 NVIDIA Spark playbook 当天给出的镜像与版本为准)
docker run -d --name miaodesk-image --gpus all \
  -p 192.168.1.50:8188:8188 \
  -v /srv/miaodesk/models:/models \
  <COMFYUI-IMAGE-FROM-PLAYBOOK>:<TAG>
```

健康检查:

```bash
curl -fsS http://192.168.1.50:8188/health
```

模型权重从 Hugging Face 拉取并校验 SHA-256,与 §1.2 做法一致。

### 2.6 内存共存约束(硬约束)

图像模型与 LLM **共享 128 GB**:

```text
候选 A(gpt-oss-120b 63 GB) + 20b(16 GB) + Z-Image-Turbo(16–24 GB)
   ≈ 103 GB,只剩约 25 GB 给 KV cache → 长上下文立刻吃紧

候选 B(Qwen3.6-35B-A3B 35 GB) + 4B(4 GB) + Z-Image-Turbo(16–24 GB)
   ≈ 63 GB,剩约 65 GB 给 KV cache → 宽裕

候选 B + 按需 Qwen-Image(30 GB)
   ≈ 93 GB,加载前必须先卸载 Z-Image-Turbo → 冷启动数十秒
```

**结论:要做图像生成,基本应当选候选 B 作为主模型。** 这条直接影响 §1.3 的两个候选部署决策。

按需加载 Qwen-Image 时,UI 必须明示等待(冷启动数十秒),不能静默卡住。

## 3. 访问控制

### 3.1 启用访问令牌

**局域网端点必须认证**(产品侧 `IsLoopbackUrl` / `NeedsKey` 不豁免私网段,会强制要求密钥):

```bash
# 生成一个高熵令牌
openssl rand -hex 32 > ~/miaodesk-vllm.token
chmod 600 ~/miaodesk-vllm.token
```

把它作为环境变量注入容器(不要写进镜像或命令行历史):

```bash
docker rm -f miaodesk-vllm-a
docker run -d --name miaodesk-vllm-a --gpus all --ipc host \
  --env-file ~/miaodesk-vllm.env \
  -p 192.168.1.50:8000:8000 \
  -v /srv/miaodesk/models:/models \
  nvcr.io/nvidia/vllm:<TAG> \
  vllm serve /models/gpt-oss-120b \
    --api-key "$(cat ~/miaodesk-vllm.token)" \
    --served-model-name openai/gpt-oss-120b \
    --max-model-len 32768 --gpu-memory-utilization 0.8 \
    --enable-auto-tool-choice --tool-call-parser openai
```

注意 `-p 192.168.1.50:8000:8000` —— **只绑定局域网 IP,不绑 `0.0.0.0`**。

### 3.2 防火墙

```bash
sudo ufw default deny incoming
sudo ufw allow from 192.168.1.0/24 to any port 8000 proto tcp comment 'MiaoDesk Windows client'
sudo ufw enable
```

按需收窄到客户端单 IP。

## 4. Windows 客户端配置

### 4.1 写入凭据

令牌进 Windows Credential Manager,**不要**写进配置文件明文:

```powershell
# 目标名必须与 profile id 对应
cmdkey /generic:MiaoDesk/ApiProfile/local-spark /user:miaodesk /pass:"<令牌>"
```

或在设置中心 → API 配置里新增 profile 并填写密钥(推荐,产品自己写入凭据存储)。

### 4.2 新增 Profile

```ini
[profile:local-spark]
name=本地 DGX Spark
type=OpenAI Compatible
baseUrl=http://192.168.1.50:8000/v1
model=openai/gpt-oss-120b
default=1
```

`PiRuntime`、`Direct Model`、`DeepSeek Harness` 都通过同一份默认 profile 读取,自动全部指向本地推理服务。

### 4.3 降级形态(本机 loopback)

在 Windows 客户端本机跑小模型时:

```ini
[profile:local-loopback]
name=本机小模型
type=OpenAI Compatible
baseUrl=http://127.0.0.1:8080/v1
model=openai/gpt-oss-20b
```

loopback 会自动注入占位密钥(`PiRuntime.cpp:374`),**无需填 API Key**。

## 5. 健康检查

```bash
# 服务端
curl -fsS http://192.168.1.50:8000/health && echo " vllm ok"
curl -fsS -H "Authorization: Bearer $(cat ~/miaodesk-vllm.token)" \
  http://192.168.1.50:8000/v1/models
```

日志位置与应记录内容:

```text
记录: 请求量 / 延迟 / 错误率 / 显存占用 / 超时 / 退出码
禁止: 对话内容 / API Key / Bearer Token
```

## 6. 上线前验收清单

**服务端**

```text
[ ] /health 返回 200
[ ] /v1/models 列出预期模型
[ ] 无令牌请求被拒绝(401)
[ ] 防火墙仅放通客户端来源
[ ] 容器 TAG 与模型 SHA-256 已记录到部署台账
```

**客户端(每个候选模型 × 每种 API 模式)**

```text
[ ] Conversation Panel 能流式收到回复
[ ] Desktop Agent 工具调用端到端成功(至少一次真实工具执行)
[ ] 非流式 JSON schema 约束输出合法率 100%
[ ] max_tokens 截断时返回可诊断错误,不是静默坏数据
[ ] 前缀缓存在多轮会话中命中稳定
[ ] 内容生成:skills/ 产出的 .mdwall/.mdwidget 能通过 wallpaper_validate_package
[ ] 预览 → 用户确认 → Apply 全链路走通,Preview 未越界写状态
[ ] 超时与取消生效,服务挂起时产品不卡死
```

**这两项是全部分析的依据,未完成前不得对外承诺任何性能数字。**

## 7. 故障排查

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| 客户端报"未配置 API Key" | 私网 IP 不在 loopback 白名单 | 配置访问令牌(§3.1) |
| 工具调用静默失败 | `--tool-call-parser` 与模型不匹配 | 按 §1.3 表格核对 |
| 结构化输出偶发坏 JSON | 流式下引导解码不生效 | 用非流式;或加 `extra_key` 回退 |
| 结构化输出 HTTP 400 | `max_tokens` 不足导致 JSON 截断 | 提高 `max_tokens`,并处理 400 |
| 首 token 很慢 | 模型仍在加载 / KV cache 不足 | 等 `vllm serve` 就绪;降 `--max-model-len` |
| 显存不足 | 权重 + KV cache 超 128 GB | 降 `--max-model-len` 或 `--gpu-memory-utilization` |
| 候选 B 结构化输出被跳过 | 推理内容未单独解析 | 确认 `--structured-outputs-config.enable_in_reasoning=True` |

## 8. 升级与回滚

- **模型与服务器版本分离**:模型放 `/srv/miaodesk/models/<name>`,服务器用容器 TAG,两者可独立回滚。
- 升级流程:拉新 TAG → 起新容器(不同端口 / 名字)→ 跑 §6 清单 → 切换客户端 Base URL 端口 → 停旧容器。
- 回滚:客户端 Base URL 指回旧容器端口即可,**不需要动客户端配置以外的东西**。
- 保留上一个已知良好版本的容器 TAG 与模型目录,至少一个版本。

## 9. 完全卸载

```bash
docker rm -f miaodesk-vllm-a miaodesk-vllm-b
rm -rf /srv/miaodesk/models
rm -f ~/miaodesk-vllm.token ~/miaodesk-vllm.env
# Windows 侧:删除 profile,并在凭据管理器删除 MiaoDesk/ApiProfile/local-spark
```

## 10. 本手册未验证的部分

必须明说,避免被当成已完成的承诺:

- **所有容器 TAG 与模型哈希是占位**,部署当天需从官方 playbook 与 Hugging Face 页面核对。
- **候选 B(Qwen3.6-35B-A3B)没有任何实测 tok/s 数据**;它的 30–50 tok/s 是从 273 GB/s 带宽推算的。
- **首 token 延迟、并发承载、长时间稳定性均未实测**。
- **`--structured-outputs-config.enable_in_reasoning=True` 的确切参数名需以所用 vLLM 版本的 CLI 为准**,版本间有变化。

这些都要在真实环境跑完 §6 清单后才能定稿。
