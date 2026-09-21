# 妙喵 MiaoDesk 隐私政策

*最后更新：2026 年 9 月 20 日*

本政策适用于 Microsoft Windows 上的妙喵 MiaoDesk 桌面应用（"本应用"，产品名：MiaoDesk；发布者：GoodLoongStudio）。

**一句话总结：本应用无账号、无遥测、无广告。你的数据只在你的设备、你自己的局域网、与你自己选择的 AI 服务商之间流动 —— 三者都不经过 GoodLoongStudio。**

## 我们收集什么

### 1. 对话内容（仅在你自己配置时）

本应用的 AI 对话需要**你自己**在设置中配置模型服务商（统称"AI 服务商"）。支持两种模式，由你选择：

**模式 A：云端服务商**

- 你在设置中填写该服务商的 API Key 与 Base URL。
- 发起对话时，**对话文本**直接从你的设备发送给你配置的服务商，用于生成回复。
- 对话的处理、存储与删除遵循**该服务商自己的隐私政策**。请在配置前阅读其条款。

**模式 B：本地 / 局域网推理（推荐用于敏感场景）**

- 你在设置中把 Base URL 指向**你自己拥有**的推理服务器（例如你自己的 DGX Spark、工作站或局域网主机），该服务器运行开源模型。
- 此时对话文本只从你的设备发送到**你自己的那台机器**，**不离开你自己的网络**。
- 我们不参与该服务器的部署、运维或监控，也无法访问其上的任何内容；该服务器上的数据由其所有者（即你）全权控制。
- 此模式下无需 API Key；若你的服务器启用了访问令牌，该令牌同样只保存在你的本机凭据存储中。

无论哪种模式：

- 我们（GoodLoongStudio）**不经过任何中转服务器**，不存储、不复制、不查看你的对话内容。
- 我们不因你使用本地模式而获得任何额外数据。

### 2. 本地数据（仅在你的设备上）

以下数据只保存在你的设备本地，用于让功能可用，**不上传、不出设备**：

| 数据 | 用途 |
|---|---|
| 文件搜索索引 | 实现"搜索应用与文件"功能 |
| 壁纸 / 小组件配置 | 记住你的桌面布局与偏好 |
| 应用设置（含 API Key / 访问令牌） | 保存在你的本机凭据存储中 |

### 3. 我们不收集

- 我们不创建账号，不要求手机号或邮箱。
- 我们不安装遥测 / 崩溃上报 SDK。
- 我们不嵌入第三方广告或追踪。
- 我们不出售、不出租任何数据。

## Microsoft Store 数据声明

针对 Microsoft Store 的"本应用声明（App declares）"要求，本应用的声明为：

- **None collected or transmitted**（不收集、不传输数据）——除下述由用户主动触发的场景外；
- 由用户主动配置 AI 对话时，对话文本将传输至用户指定的终点，此行为由用户主动发起。终点分为两类：
  - 用户指定的**云端** AI 服务商，受该服务商政策约束；
  - 用户自己拥有和运营的**本地 / 局域网**推理服务器，数据不离开用户自己的网络。

两种情况下 GoodLoongStudio 均不接收、不中转、不存储任何对话内容。

## 儿童隐私

本应用面向一般受众，不面向 13 岁以下儿童，也不收集儿童个人信息。

## 权限说明

本应用可能请求文件访问等系统能力，全部用于上述本地功能，不会用于其他目的。

## 政策更新

如有变更，我们会在此页面更新"最后更新"日期。继续使用即表示接受更新后的政策。

## 联系我们

发布者：GoodLoongStudio
- GitHub：https://github.com/GoodLoongStudio
- 仓库 / 问题反馈：https://github.com/GoodLoongStudio/MiaoDesk/issues

---

# MiaoDesk Privacy Policy

*Last updated: September 20, 2026*

This policy applies to the MiaoDesk desktop application on Microsoft Windows ("the App"), published by GoodLoongStudio.

**In one sentence: no account, no telemetry, no ads. Your data flows only between your device, your own local network, and the AI provider you choose yourself — none of it passes through GoodLoongStudio.**

## What we collect

### 1. Conversation content (only when you configure it yourself)

The AI chat feature requires **you** to configure a model provider ("the AI provider"). Two modes are supported; you choose:

**Mode A — cloud provider**

- You enter that provider's API key and Base URL in the App settings.
- When you start a chat, the **conversation text** is sent directly from your device to the configured provider to generate a reply.
- Processing, storage and deletion follow **that provider's own privacy policy**. Please review it before configuring.

**Mode B — local / LAN inference (recommended for sensitive use)**

- You point the Base URL at an inference server **you own** — your own DGX Spark, workstation, or LAN host — running open-source models.
- Conversation text then travels only from your device to **your own machine** and **never leaves your own network**.
- We do not deploy, operate, or monitor that server and cannot access anything on it; its data is controlled entirely by its owner, which is you.
- No API key is required in this mode. If your server enforces an access token, that token is likewise kept only in your local credential store.

In either mode:

- GoodLoongStudio operates **no relay servers** and does not store, copy, or view your conversations.
- We gain no additional data from your use of local mode.

### 2. Local data (on your device only)

The following stays on your device and is never uploaded:

| Data | Purpose |
|---|---|
| File search index | Powers "search apps and files" |
| Wallpaper / widget configuration | Remembers your desktop layout |
| App settings (including API key / access token) | Stored in your local credential store |

### 3. What we do NOT do

- No account creation, no phone or email required.
- No telemetry or crash-reporting SDK.
- No third-party ads or tracking.
- We never sell or rent data.

## Microsoft Store data declaration

For the Microsoft Store "App declares" requirement, this App declares:

- **None collected or transmitted**, except for the user-initiated scenario below;
- When the user actively configures AI chat, conversation text is transmitted to a user-designated endpoint. That endpoint is one of two kinds:
  - a user-designated **cloud** AI provider, governed by that provider's policy;
  - a **local / LAN** inference server owned and operated by the user, over which data never leaves the user's own network.

In both cases GoodLoongStudio neither receives, relays, nor stores any conversation content.

## Children's privacy

This App is intended for a general audience, is not directed at children under 13, and does not knowingly collect personal information from children.

## Permissions

The App may request system capabilities such as file access; all are used solely for the local features described above.

## Policy changes

If this policy changes, we will update the "Last updated" date here. Continued use constitutes acceptance of the updated policy.

## Contact

Publisher: GoodLoongStudio
- GitHub: https://github.com/GoodLoongStudio
- Issues: https://github.com/GoodLoongStudio/MiaoDesk/issues
