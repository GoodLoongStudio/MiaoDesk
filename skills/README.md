# MiaoDesk 内容创作 Skills

一套用于生成 MiaoDesk 壁纸与小组件内容包的 skill。**完全自包含,不依赖也不修改 `src/`、`docs/`、`packaging/` 下任何原有文件。**

## 包含的 skill

| Skill | 职责 | 何时用 |
| --- | --- | --- |
| `content-package-basics` | 包契约:manifest / 稳定 ID / Package Root / capability / 参数 / 绑定 | **每次必用**,是所有其他 skill 的底座 |
| `wallpaper-content` | 壁纸领域规则:`.mdwall`、wallpaper profile、载体×运行时选型、click-through 语义 | 生成壁纸时 |
| `widget-content` | 组件领域规则:`.mdwidget`、widget profile、geometry policy、桌面层级 | 生成组件时 |
| `content-review` | 安全与性能门禁 | **产出后必跑**,通过才允许交付 |

## 分类约定

MiaoDesk 桌面内容 = **载体 × 运行时**两个正交维度,不是 "Image / Video / Web / Scene" 的平面四分类:

- **载体** `ContentKind`:Wallpaper(壁纸层,桌面图标之下)/ Widget(组件层,桌面图标之上,可交互)
- **运行时** `ContentRuntimeKind`:Scene / Web
- **Scene 内部按表现形态分**(是 Scene 的配置档,不是顶层类型):静态图 / 动态场景 / 视频 / 3D(未实现)

Web 运行时是给**用户手工或第三方内容**用的,不属于 skill 的产出范围。详见 `docs/WALLPAPER_ENGINE_BENCHMARK.md` §6。

## 组合方式

```text
生成壁纸:
  content-package-basics → wallpaper-content → content-review

生成组件:
  content-package-basics → widget-content → content-review
```

`content-package-basics` 定义"什么是合法包",两个领域 skill 各自叠加领域规则,`content-review` 做最后一道关。三者职责不重叠:改壁纸规则只动 `wallpaper-content`,改安全底线只动 `content-review`。

## 输出形态

每个 skill 产出的是内容包源文件,不是可执行代码:

```text
<name>.mdwall/            <name>.mdwidget/
├─ manifest.json          ├─ manifest.json
├─ parameters.json        ├─ parameters.json
└─ scene.json             └─ scene.json
```

格式契约以仓库内既有文档为准:

- `docs/MIAO_CONTENT_PACKAGE_V1.md` — manifest v1 与 Package Root 边界
- `docs/MIAODESK_CONTENT_FRAMEWORK.md` — ContentDefinition / Parameter / Scene Runtime
- `docs/DESIGN_BASELINE.md` §5 — Widget geometry 与交互模型
- `assets/widgets/GlassClock.mdwidget/` — 可参照的完整示例

## 硬底线(任何 skill 都不能覆盖)

1. 只产出 JSON 内容包。**不输出 HTML / JavaScript / CSS / shell 命令 / 原生可执行文件。**
2. 不产出 Web 运行时内容(内嵌浏览器仅承载用户手工/第三方内容),不产出 Script 组件或脚本资产。
3. 包内只引用 Package Root 之内的相对路径,不用 `../` 越界,不用 symlink / junction。
4. `capabilities` 是数据能力声明(如 `clock.read`),不是 Win32 / 文件系统 / 注册表权限。
5. 内置包只读;新包不得复用任何内置包的 id。
6. 预览不等于应用。落到用户桌面必须经用户显式确认,且提交是原子的。
7. 遵守 `content-review` 列出的全部性能上限。
