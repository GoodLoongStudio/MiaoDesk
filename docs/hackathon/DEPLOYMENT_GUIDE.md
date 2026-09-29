# 部署说明：MiaoDesk + DGX Spark

这份文档只保留比赛提交时最需要说明的部署流程。完整细节见：

[LOCAL_AI_DEPLOYMENT.md](../LOCAL_AI_DEPLOYMENT.md)

## 一、部署结构

MiaoDesk 分成两部分：

```text
Windows PC
  └─ MiaoDesk
      ├─ 搜索
      ├─ 妙喵 AI
      ├─ 动态壁纸
      ├─ Widget
      └─ Agent Skills

DGX Spark
  ├─ vLLM
  ├─ 本地大语言模型
  └─ ComfyUI / 图像生成模型（可选）
```

Windows 通过局域网访问 DGX Spark 上的 OpenAI Compatible API。

## 二、DGX Spark 端

### 1. 检查环境

```bash
nvidia-smi
cat /etc/dgx-release
```

确认 DGX Spark GPU、统一内存和 DGX OS 正常。

### 2. 启动本地模型

项目推荐使用 vLLM 暴露 OpenAI Compatible 接口。

示例：

```bash
vllm serve /path/to/model \
  --served-model-name local-model \
  --host 0.0.0.0 \
  --port 8000 \
  --max-model-len 32768
```

实际比赛环境中可以根据已经下载的模型和网络情况选择模型。

当前项目文档中已经验证或评估过的候选包括：

- NVIDIA Nemotron-3.5-Lightning-30B-A3B-NVFP4；
- gpt-oss-120b；
- gpt-oss-20b；
- Qwen3.6-35B-A3B。

### 3. 可选：本地图像生成

MiaoDesk 可以把图片生成配置成独立 API。

推荐结构：

```text
MiaoDesk
  → OpenAI Images Compatible Shim
  → ComfyUI
  → Z-Image-Turbo
```

这样聊天模型和图像模型可以使用不同端口、不同模型。

## 三、Windows 端

### 1. 安装 MiaoDesk

下载对应架构安装包：

- Windows x64；
- Windows ARM64。

运行安装程序即可。

### 2. 新增 API 配置

打开：

```text
MiaoDesk → API 配置
```

新增配置，例如：

```text
配置名称：本地 DGX Spark
接口类型：OpenAI Compatible
Base URL：http://<DGX-Spark-IP>:8000/v1
Model：local-model
API Key：按服务端设置填写
```

MiaoDesk 支持保存多套 API。

不同 AI 窗口可以选择不同配置；没有单独选择时，默认使用第一套配置。

### 3. 测试连接

点击“测试连接”或“探测模型”。

正常情况下应该能够：

- 探测到模型；
- 在 Model 下拉框选择模型；
- 妙喵 AI 正常回复；
- AI 壁纸 / Widget 创作窗口正常调用对应模型。

## 四、StepFun 云端配置示例

如果不使用本地模型，也可以配置 StepFun：

```text
配置名称：StepFun
接口类型：OpenAI Compatible
Base URL：https://api.stepfun.com/v1/chat/completions
Model：step-3.5-flash
API Key：用户自己的 StepFun Key
```

这套配置与 DGX Spark 本地配置可以同时保存在 API 配置中心。

## 五、如何验证部署成功

最简单的演示顺序：

1. 在 API 配置中心选择“本地 DGX Spark”；
2. 打开妙喵 AI；
3. 询问一个普通问题，确认本地模型可以回复；
4. 输入“帮我做一个动态壁纸”；
5. Agent 读取壁纸 Skill；
6. 生成内容包；
7. 在预览区查看效果；
8. 用户确认后应用到桌面。

如果需要对比，还可以把当前 AI 窗口切换到 StepFun，再执行同一请求。

## 六、安全原则

- API Key 保存在 Windows Credential Manager；
- 不把 Key 写进 GitHub；
- 局域网 DGX Spark 建议启用访问令牌；
- Agent 不获得任意 Shell 权限；
- AI 生成桌面内容时优先生成声明式 JSON；
- 内容包在应用前经过安全和性能检查。

项目地址：

https://github.com/GoodLoongStudio/MiaoDesk
