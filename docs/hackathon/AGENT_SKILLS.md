# Agent Skills 说明

MiaoDesk 把“怎么制作壁纸、怎么制作组件、怎么检查安全性”写成独立的 Markdown Skill。

这样做的目的，是让 AI 可以学习产品规则，但不能绕过产品规则。

## Skill 1：Content Package Basics

文件：

[`skills/content-package-basics/SKILL.md`](../../skills/content-package-basics/SKILL.md)

作用：

- 定义内容包结构；
- 定义 manifest；
- 定义 parameters；
- 定义 scene；
- 限制路径越界；
- 限制危险文件；
- 规定 AI 只能生成受控内容。

一句话：

> 教 AI “MiaoDesk 内容包应该长什么样”。

## Skill 2：Wallpaper Content

文件：

[`skills/wallpaper-content/SKILL.md`](../../skills/wallpaper-content/SKILL.md)

作用：

- 教 AI 制作动态壁纸；
- 处理动画、粒子、音频响应、鼠标位置响应；
- 保证壁纸不影响桌面图标；
- 限制不支持的 3D / Script 等能力。

一句话：

> 教 AI “怎样做一张真的能在 MiaoDesk 里运行的动态壁纸”。

## Skill 3：Widget Content

文件：

[`skills/widget-content/SKILL.md`](../../skills/widget-content/SKILL.md)

作用：

- 教 AI 制作桌面小组件；
- 定义组件尺寸；
- 定义交互；
- 控制刷新频率；
- 避免组件占用过多资源。

一句话：

> 教 AI “怎样做一个长期放在桌面上的轻量组件”。

## Skill 4：Content Review

文件：

[`skills/content-review/SKILL.md`](../../skills/content-review/SKILL.md)

作用：

- 安全检查；
- 性能检查；
- 路径检查；
- 资源大小检查；
- 壁纸 / Widget 专项检查。

只有全部通过时，内容才允许进入交付。

一句话：

> 它相当于 AI 内容的“自动质检员”。

## Agent 工作流程

```text
用户：
“帮我做一个下雨的动态壁纸”

AI：
1. 读取 content-package-basics
2. 读取 wallpaper-content
3. 生成 .mdwall
4. 运行 content-review
5. 调用产品校验器
6. 打开预览
7. 用户确认
8. 应用到桌面
```

## 为什么不用一个超长 Prompt

因为产品规则会持续增加。

如果全部塞进一个 Prompt：

- 很难维护；
- 很容易冲突；
- 每次请求都浪费上下文；
- 很难让第三方 AI 复用。

Skill 文件可以独立版本管理，并且和代码一起开源。

Skills 总入口：

https://github.com/GoodLoongStudio/MiaoDesk/tree/main/skills
