# 作品演示视频脚本

建议视频控制在 **2～3 分钟**。重点不是讲完所有技术，而是让评委快速看到“它真的能工作”。

## 0:00–0:10 开场

画面：

- Windows 桌面；
- MiaoDesk 搜索框；
- 动态壁纸；
- 桌面 Widget。

旁白：

> 这是 MiaoDesk，一个 AI 原生 Windows 智能桌面。我们希望用户不需要先学习复杂软件，只需要告诉 AI 自己想做什么。

## 0:10–0:35 搜索

演示：

1. 唤起顶部搜索框；
2. 输入一个应用名称；
3. 再输入一个真实文件名；
4. 打开文件。

旁白：

> 一个入口同时搜索应用和本地文件，也可以直接进入妙喵 AI。

## 0:35–1:00 DGX Spark 本地 AI

演示：

1. 打开 API 配置；
2. 展示“本地 DGX Spark”配置；
3. 展示模型列表；
4. 打开妙喵 AI；
5. 顶栏选择对应 API；
6. 发出一个问题并得到回复。

旁白：

> MiaoDesk 使用统一 API Profile，可以同时保存本地 DGX Spark 和云端 Provider。每个 AI 窗口可以单独选择模型。这里的 Agent 推理运行在 DGX Spark 上。

## 1:00–1:45 AI 制作壁纸

输入：

> 帮我做一个夜晚下雨、带轻微光标视差效果的动态壁纸。

演示：

1. 打开 AI 制作壁纸；
2. Agent 生成；
3. 展示预览；
4. 展示 Play / Pause / Reload；
5. 应用到桌面。

旁白：

> AI 不是直接执行任意代码。它会读取 MiaoDesk 的 Wallpaper Skill，生成声明式内容包，再经过安全和性能检查。用户确认后，才真正应用到桌面。

## 1:45–2:15 AI 制作 Widget

输入：

> 帮我做一个简洁的桌面倒数日组件。

演示：

1. AI 制作组件；
2. 生成并预览；
3. 应用；
4. 桌面上出现组件。

旁白：

> 同一套 Agent 也可以根据 Widget Skill 创建桌面组件。壁纸和组件使用统一 Content Runtime，但拥有不同的安全规则。

## 2:15–2:35 Skills + 技术画面

快速切画面：

- GitHub `skills/`；
- DGX Spark；
- vLLM；
- Direct2D / D3D11；
- GitHub Actions。

旁白：

> 项目主体使用 Native C++，本地 AI 使用 DGX Spark 和 vLLM，内容生成规则以 Markdown Skills 开源，Windows x64 与 ARM64 都通过自动构建和安装验证。

## 2:35–2:50 结尾

旁白：

> 我们认为 AI PC 不应该只是本地运行一个聊天框。真正有价值的是让本地模型安全地连接到用户每天都在使用的桌面。这就是 MiaoDesk。

画面最后显示：

```text
MiaoDesk
AI Native Desktop for Windows
github.com/GoodLoongStudio/MiaoDesk
```

## 上传后需要提交的链接

视频上传 B 站后，把最终 URL 填到比赛表单。

建议标题：

> MiaoDesk：运行在 DGX Spark 上的 AI 原生 Windows 智能桌面
