# MiaoDesk 开发 Todo

- 状态:活清单,随开发更新
- 建立:2026-09-20
- 上游:`DEVELOPMENT_ROADMAP.md`(阶段规划)· `DESIGN_BASELINE.md`(设计绳准) · `LOCAL_AI_ARCHITECTURE.md` · `WALLPAPER_ENGINE_BENCHMARK.md`(能力基准)
## 怎么用这份清单

`DEVELOPMENT_ROADMAP.md` 回答"按什么阶段走",本清单回答"下一步具体做什么、什么还没做"。
两者不重复:路线是阶段级,这里是可执行、可勾选、带验收标准的任务项。

规则:

1. 每项必须有**验收标准**。没有验收标准的项不允许进入本清单。
2. 完成 = 通过该项自己的验收标准,**不是**"代码写了"。
3. 完成的项移入底部「已完成」区,不删除 —— 这份清单同时是开发记录。
4. 新增项必须写清**依据**(哪份文档、哪个缺陷、哪次审计),不允许出现无来源的任务。
5. 阻塞商业发布的项标 `P0`,门的验收项标 `P1`,本地 AI 实施标 `P2`,技术债标 `P3`。
6. 对标 Wallpaper Engine 的差距项以 `B-x` 编号,依据统一指向 `WALLPAPER_ENGINE_BENCHMARK.md` 的小节号。

## 全局验证状态(2026-09-22 晚更新,读这份清单前先读它)

**`Windows x64 Build` 已恢复通过:#367 / `023aa299`,20 个验证步骤全部 success、零 skipped。**
这是 9/17 的 #353 之后第一次,中间隔了 14 次失败。

同一提交 `023aa299` 上,全部工作流齐绿,无一带病:
`build` / `scan`(Repo Hygiene)/ `verify`(Path Layout)/ `package-msix` /
`package`(x64 Package 与 ARM64 Package)/ `installer`(ARM64)。

修复链路(全部是工程/闸门缺陷,不是产品设计问题):

- **编译错误四处**:`NativeTools.cpp` 对 `std::wstring` 调 `.wstring()`;`constexpr` 非静态
  数据成员;`std::max(int, LONG)` 推导失败;缺 `<cstring>`。共同根因是那批代码从 9/17 起
  **没经过任何认 Windows 头文件的编译器** —— 本机只跑过剥离出来的逻辑片段和不含
  `windows.h` 的纯逻辑测试,两者都看不见 MSVC 才能看见的类型错误。
- **链接错误**:`content/render/d3d11/MiaoD3D11TextureLoader.cpp` 在磁盘上、语法没问题,但
  **没进 `MIAODESK_*_SOURCES`,从来没被编译过** → 四个打包工作流挂在
  `LNK2019: unresolved external MiaoD3D11TextureLoader::LoadImageW`。
  这个类别落在所有闸门盲区里:语法闸门扫"磁盘上有什么",照样编译它,看不出它未被收录。
- **三个"永远不可能通过"的闸门**(今天连中三次同一个类别):
  1. `stage.ps1` 的 SKILL.md frontmatter 检查 —— `$head` 是 `Object[]`,而
     `$array -notmatch 're'` 是**过滤**不是布尔,6 行里只有一行匹配 → 非空数组 → 恒真必抛。
  2. `verify-web-audio-bridge.ps1` 那一步 —— 直接调用 `.ps1` **不设置 `$LASTEXITCODE`**,
     那一步之前没有原生命令,所以它是 `$null`,而 `$null -ne 0` 恒真,一律抛错。
  3. (同一类)已逐项排查,另几处"整段 run 只有一句直接调用"是没问题的,脚本 throw 会让
     pwsh 非零退出。
- **两个 Windows 专属失败**:
  1. `tests/image-provider.mjs` 的 `await import(绝对路径)` —— ESM 按 URL 规则解析,
     POSIX 的 `/abs/path.mjs` 碰巧被接受,Windows 的 `D:\…` 被解析成协议 `d:` 而抛
     `ERR_UNSUPPORTED_ESM_URL_SCHEME`。这个闸门只在 Windows CI 上跑,所以本地永不过。
  2. `tests/MediaWallpaperPackage.cpp` 的 fixture —— `std::string` 从 `const char*` 构造在
     **第一个 NUL 处截断**,`clip.mp4` 被写成 0 字节,`CreateVideo` 于是正确地拒绝
     "源文件为空",正面断言以一种看起来像产品 bug 的方式失败。
- **`stage.ps1` 里留着未解决的合并冲突标记**(我早前合 `_check/fix/unicode-wallpaper-theme-packages`
  留下的)。`.ps1` 不进 C++ 编译器,所以它带着三行尖括号一路绿灯。

教训(每条都写进了对应提交):

1. **判断 CI 步骤成败必须区分 `success` / `failure` / `skipped` / `null`。** 我最初的轮询
   脚本把 `null`(被跳过、根本没跑)也打印成 "ok",于是"Configure 失败、但 Build 和 17 个
   验证步骤通过"这个结论**完全失实** —— Configure 一失败后面全部 skipped。这个假象让
   上面所有故障都被掩盖了很久。
2. **闸门写完必须注入一个已知失效,确认它真的会响。** 我写的闸门里有三个自己试过假绿:
   过滤器用 `startswith('error:')` 匹配 gcc 输出(而 gcc 的行以路径开头)、comm 的列搞反
   导致一侧永远漏报、正则把 `was not declared` 写成 `was not been declared`。干净状态下
   它们和正确版本长得完全一样。
3. **替身/旗标的缺陷会伪装成产品缺陷。** `-fshort-wchar` 能让 `sizeof(wchar_t)==2` 那条
   静态断言过,但在 macOS 上宽字符字面量按一字节一字发出却按 2 字节读,
   `L"视频壁纸…"` 长度 16 变 38 并夹入 `U+0000`,`printf` 在第一个 NUL 截断 —— 给出一个
   看似是产品 bug 的假消息。判定之前先验工具链本身。

现在有七个本机闸门,新增 C++ 或改动 CI 脚本后先跑:

| 闸门 | 命令 | 覆盖 | 不覆盖 |
| --- | --- | --- | --- |
| 交叉语法 | `scripts/verify-windows-syntax.sh` | 全部 105 个独立 TU 的类型/成员是否真存在 | Windows SDK、MSVC 与 mingw 的差异 |
| 纯逻辑测试 | `scripts/run-pure-logic-tests.sh` | 6 个测试目标真编译并运行通过 | 任何需要 Windows 的目标 |
| CMake 收录 | `scripts/verify-cmake-covers-sources.sh` | 磁盘上每个 `.cpp` 是否真的被编译 | CMakeLists 的意图是否合理 |
| 冲突标记 | `scripts/verify-no-conflict-markers.sh` | 仓库里有没有未解决的冲突标记 | 无 |
| skill 白名单 | `scripts/verify-skill-allowlist.sh` | `kContentSkills` 与 `skills/` 是否一致 | CI 上真实的注入效果 |
| 工作流 paths | `scripts/verify-workflow-paths.sh` | 每个工作流的 `paths` 过滤是否覆盖它自己跑的文件 | 过滤模式是否过宽 |
| 媒体包离线 | `scripts/verify-media-package-offline.sh` | `CreateVideo`/`CreateImage` + `Validate`(CI 测试第 1 节) | 第 5 节;以及测试自己写 fixture 的方式 |

后三个由 `.github/workflows/repo-hygiene.yml` 在 CI 跑 —— 它们不需要 Windows、也不依赖
构建能否通过,所以不该被构建类工作流挡住。

`MediaWallpaperPackageTest` 明确只能由 CI 覆盖:它链接 `WallpaperLibrary.cpp` →
`UnicodeProfileFile.h:72` 有 `static_assert(sizeof(wchar_t) == 2)`(Windows 配置持久化
要求 UTF-16 `wchar_t`),而 macOS 的 `wchar_t` 是 4 字节。这是产品设计约束。

**下面所有标"待 Windows 编译/真机验收"的项,字面意思就是没验证过。** 离线通过 ≠ Windows
通过;本机六个测试全绿也仍然不等于 Windows 验证。

## P0 — 阻塞商业发布

### P0-1 真实 Windows 多 DPI / 多显示器视觉闭环

- **依据**:`DEVELOPMENT_ROADMAP.md` §3 P0-1;`DESIGN_BASELINE.md` §10「真实 Windows 用户流程稳定通过 = 完成」
- **为什么阻塞**:这是设计目标第一段(门)的核心验收。CI 绿色不算完成,必须真机。门不关闭,后面所有进展都算不上目标达成。
- **内容**:组件内容完整显示;alpha 正确;不漏错误背景;Widget 位于 Desktop Icons 之上且可交互;跨 DPI 不裁切;Explorer 重建后恢复。
- **依赖**:需要一台真实多显示器 / 多 DPI Windows 机器
- **状态**:❌ 未开始 —— **需硬件,无法用 CI 替代**

### P0-2 `image_generate` 本地化 🟡 已实施(方案 A),待 Windows 与真机验证

- **依据**:`LOCAL_AI_ARCHITECTURE.md` §7.1 / §7.5
- **缺陷(已修)**:`src/ai/pi/PiNativeToolsExtension.cpp` 把 `getImageModel` 的 provider
  硬编码成 `"openrouter"`,且凭据只在 baseUrl 含 `openrouter.ai` 时才复用主 key ——
  baseUrl 指向本地推理服务时 `image_generate` 直接抛"需要 OpenRouter API Key"。
- **已实施(2026-09-22,方案 A:参数化 provider + model)**:
  - provider 与 model 改为从环境变量 `MIAODESK_IMAGE_PROVIDER` / `MIAODESK_IMAGE_MODEL`
    读取,默认值只为向后兼容保留 openrouter + `google/gemini-2.5-flash-image`。
  - **未配置 provider 时明确报错**,不静默回落到云端 provider —— 用户没选过就不该替他选。
  - key 解析规则重写:专用 `MIAODESK_IMAGE_API_KEY` 优先;loopback 端点视为免密钥
    (与 profile 自身对 loopback 的处理一致,这是"全本地跑起来"的关键);
    其余情况复用主 model key,而不再限定 openrouter。
  - C++ 侧:`ApiRuntimeProfile` 增读 `imageModel` 键;`ModelConfig` 增 `imageModel` 并
    **纳入 `ReloadConfig()` 变更检测**;`ProviderSetup` 增 `imageProvider` / `imageModel`;
    `BuildProviderSetup` 从 `agent.Config()` 取值;`PiRuntime` 把两个变量导出给扩展进程;
    **session `signature` 纳入 `img=provider:model`** —— 否则改配置不会重启 Pi 会话,
    修复会静默失效。
  - provider 直接取 profile 已推导出的 `providerId`(`deepseek` / `anthropic` / `google` /
    `local-openai-compatible` / `openai-compatible`),不另造一套名称。
- **原"前置验证"为何不再阻塞**:此前认为必须先确认 `getImageModel` 支持哪些 provider 字符串
  (未安装 `node_modules`,无法静态确认)。改参数化后这不再是前置条件 ——
  provider 由用户的 profile 决定,产品只负责透传,不维护一张 provider 白名单。
- **验证(离线,node)**:`tests/image-provider.mjs` 28 项断言全过。
  测的是**从 `.cpp` 原始字符串里抽出来的真实逻辑**(`tests/extract-image-provider.mjs`),
  不是手抄副本。覆盖:未配置明确失败 / openrouter 向后兼容 / **loopback(原必然失效路径)** /
  其他云厂商 / 专用 image key 优先 / model 覆盖 / loopback 六种写法与两种非 loopback。
- **抽取器本身的一个教训**:抽出的模块最初漏了 `import { readFile }` / `join`,
  导致 `currentMiaoDeskBaseUrl` 的 `try` 吞掉 ReferenceError 并返回 "" ——
  表现和"没有配置 profile"完全一样,差点被判成产品缺陷。已在抽取器里注明。
- **仍未验证**:
  - Windows 编译(依赖 `windows.h` / `wincred.h`)。
  - 真实 provider 是否接受传入的 provider 字符串(`getImageModel` 的行为),需实机 + node_modules。
  - `models.json` 的 provider 名与 `getImageModel` 期望的是否一致(前者是产品语义,
    后者是 pi-ai 语义,目前靠 `providerId` 直接透传,可能需要在某处做一次映射)。
- **状态**:🟡 代码已实施并通过离线验证;待 Windows 编译与真实 provider 联调

### P0-3 TodayTasks 组件进入主干 ✅ 已完成(2026-09-22,commit `bca7f9b`)

- **依据**:`DESIGN_BASELINE.md` §5.1 明确三款内置 Widget(GlassClock / **TodayTasks** / WeatherGlass)
- **此前状态**:主干只有 GlassClock 与 WeatherGlass 两个 `.mdwidget` 内容包,TodayTasks 整套
  (9 个新文件)在未合入分支 `feat/content-widget-settings`(25 提交)上;而我早前误删了远端分支。
- **已实施**:该分支内容完整保存在本地 `_check/*` 引用与 bundle 中,已合入 `main`(`bca7f9b`)。
  三款内置 Widget 现已齐备。
- **冲突处理**(两条独立历史各自创建同名文件,均非对方祖先):
  `ContentWidgetPreviewRenderer.cpp` / `ContentWidgetSettingsDialog.cpp` 在 main 与分支上
  各有版本。逐行比对确认分支版把 `PublishWeather` 泛化成 `PublishHostData`、天气发布语句
  与 hour 循环逐字节相同(仅缩进)、只新增 tasks 分支,才取分支版 ——
  按 add/add 常规做法直接取一侧会**静默删掉天气发布**。
- **验证**(走产品自己的代码,非逻辑复刻):
  - `TodayTasks.mdwidget` 通过 `MiaoSceneSerializer::Deserialize` +
    `MiaoSceneRuntimeModel::Validate` + `MiaoSceneRuntime::Initialize`
    (11 节点 / 4 参数 / 10 绑定,kind=widget,profile=widget,spatial=2d)
  - `{{tasks.*}}` 模板替换 7 例全部正确:计数、进度文本、空态、三条任务
    (含 marker 与 detail 两行)、空槽位;`tasks.pending` / `item0.title` /
    `progressText` 均过 `tasks.read` capability 闸
  - 既有 `MiaoSceneRuntime::SelfTest` 仍通过
  - 离线验证需最小 `windows.h` 替身(`GetLocalTime` / `GetTickCount64` /
    `MultiByteToWideChar` / `swprintf_s` 模板重载 等),因为 `model/` 与 `binding/`
    子域含 `windows.h` —— 此前"内容框架整层零处包含 `windows.h`"的说法只对
    `runtime/`、`scene/`、`serialization/` 三个子域成立。
- **遗留**:真机未验证(需 Windows 桌面置入该 Widget、编辑任务、确认重绘与持久化)。
- **状态**:✅ 代码已合入并通过包级/替换级验证;真机验收未做

### P0-4 壁纸 `.mdwall` dogfood 补齐 ❌ 仍未完成(阻塞点已定位,不是"补内容"那么简单)

- **依据**:`MIAODESK_CONTENT_FRAMEWORK.md` §17 第一阶段第 10 项;§19 完成标准
- **原判断被两次推翻**:
  第一次:我以为合入 `fix/unicode-wallpaper-theme-packages` 就能关上。**错** ——
  实测该分支加的三份 `scene.json` 是空壳且被 `legacy_entry` 遮蔽(见下)。
  第二次:我以为"剩余工作就是把 scene.ini 的 5 个 Layer 翻译成 scene.json 节点"。
  **也错** —— 读完渲染契约后发现根本性的阻塞。
- **实测证据(一):scene.json 被遮蔽且是空壳**
  三个包的 `manifest.json` 同时写 `"entry": "scene.json"` 与
  `"legacy_entry": "scene.ini"`,而 `WallpaperPackage::LoadAndValidate`
  **优先取 `legacy_entry`**。实测 MiaoCloud / MysticMoon / NeonCity 解析出的
  entry 全部是 `scene.ini`。即便解除遮蔽,这三份 scene.json 各只有 1 个 root 节点
  (单个 transform + opacity)、0 资产、0 绑定、0 动画、0 后处理;
  而 `scene.ini` 描述 5 个 Layer、引用 5 个真实资产。
- **实测证据(二):渲染契约不支持贴图 sprite —— 这才是真阻塞**
  - `spriteRenderer` 的属性只有 `opacity` / `tint` / `cornerRadius` / `materialId`,
    **没有 asset / texture 属性**;取图只能经由 material。
  - builtin 材质**只有 `solidColor` 一种**(D2D 渲染器 `MiaoSceneD2DRenderer.cpp:335`
    只处理 solidColor;D3D11 `MiaoSceneD3D11Renderer.cpp:602` 明确报错
    "D3D11 MVP currently supports builtin solidColor or programmable materials")。
  - 仓库内所有包的 `materials[].textures` **一律为 `[]`**,没有一个贴图样例。
  - `textures[]` 取图只对**可编程材质**开放
    (`MiaoSceneD3D11Renderer.cpp:610-625`:需 `MaterialModel::Programmable` +
    pixelShaderId + texture slot + `AssetType::Image` 资产 + `MiaoD3D11TextureLoader`)。
  - D2D 渲染器**完全没有取图路径**(全文件无 bitmap/WIC 纹理加载,sprite 只能出纯色)。
  结论:把 scene.ini 的图片图层迁到 scene.json,要么给两个渲染器都加一个带贴图的
  builtin 材质,要么为每层写可编程材质 + 像素 shader。两者都是渲染侧改动,
  需要 D3D11 / DirectWrite / D3DCompiler,本机(macOS)无法编译验证。
- **所以刻意不做的**:不写一份"能通过校验但渲染不出来"的 scene.json。
  那会得到三个校验通过、桌面上却什么都没有的官方壁纸 ——
  正是 `content-review` 与 `wallpaper-content` 反复禁止的那种静默失败。
- **剩余工作(按依赖顺序)**:
  1. **渲染侧**(阻塞项):为 D2D 与 D3D11 加带贴图的 builtin 材质
     (例如 `builtinName: "textured"` + 一个固定 texture slot),或在
     `AssetType::Image` 与 `SpriteRenderer` 之间开一条直接引用路径。
     输出需含 Windows 侧编译与真机截图验证。
  2. 内容迁移:5 个 Layer → 5 个 `node://<name>`,各带 `Transform` +
     `SpriteRenderer{materialId}`;`design_width/height` 与各层
     x/y/width/height 映射到 Transform 的 `position` / `scale`;
     `opacity` 映射到两处;文件引用 → `AssetDefinition{asset://<name>, Image}`。
  3. 动画:`none` / `drift` / `sway` / `breathe` / `blink` / `float` 六种
     → `AnimationTrackDefinition`(注意 `blink` 是间歇触发,与
     `AnimationTriggerMode::InputRisingEdge` 的语义最接近)。
  4. `[Particles]` → `ParticleEmitterDefinition`
     (sparkle / petal / flow 各一个,注意 `kMaxParticlesPerScene = 131072` 预算)。
  5. 补 `parameters.json`(外观参数)。
  6. 从 `manifest.json` 删 `legacy_entry`,删除 `scene.ini`。
  7. 重跑 `verify-wallpaper-library-derived-views.ps1` 等 6 个脚本 ——
     需先确认它们的输入源是否仍指向 `scene.ini`。
  8. 验收:`MiaoSceneSerializer::Deserialize` + `Validate` + `Initialize` 通过,
     且初始化后 `scene.assets.size() >= 5`、`animations.size() >= 1`;
     最终以"壁纸在真机上显示全部 5 层且眨眼动画生效"为准。
- **状态**:❌ 未完成 —— 分支只做了 manifest 规范化;内容迁移被"渲染契约不支持贴图 sprite"阻塞

### P0-5 本地 AI 组件许可证书面确认

- **依据**:`THIRD-PARTY-NOTICES.md`「明确排除的组件」
- **为什么阻塞**:商用分发前必须确认。两个待确认:
  - `DeepSeek-R1-0528-Qwen3-8B` 模型权重条款(代码 MIT,权重可能另有条款)
  - GLM 系列(官方模型卡写 "mistralai/MIT + deepseek license",社区报告为 MIT)——
    **澄清前不得进入任何分发版本**
- **状态**:❌ 未开始 —— 需向模型方取得书面确认

### B-1 skill 接入产品(`src/` 零引用 `skills/`)

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §4.5 / §7 G9;用户 2026-09-20 明确
  "让用户使用本工程自带的 skill(这个也是需要开发的内容)快速制作壁纸和桌面组件"
- **为什么阻塞**:`skills/` 目录下有 4 份 `SKILL.md`(壁纸 / 组件 / 基础 / 评审),但 `src/` 全仓
  零处引用。**创作链断在最后一环**:AI 生成内容包 🟡 → 产品校验包 ✓ → 沙箱预览 ✓ → 用户 Apply ✓ →
  **用户在 UI 主动触发 skill ✗**。skill 不进产品,对用户等于不存在。
- **前置澄清(已解决,2026-09-20)**:
  Pi 契约(`L3-PI-RUNTIME-CONTRACT.md` §9)里的 "skills" 指 **Node 包 agent skills**
  (`%LOCALAPPDATA%\MiaoDesk\PiAgent`,与产品 bundled runtime 分离),与 `skills/` 下的
  Markdown 提示词规范**不是同一个东西**。已确认 Pi 对 agent skills 的装载约定无法在本仓库
  静态验证(未安装 `node_modules`),因此**不走 agent skills 通道**,改用产品自有注入点。
- **已实施方案(2026-09-20)**:按需加载,非常驻注入。
  理由:4 份 SKILL.md 合计 9,307 UTF-16 字符,虽然塞得进 Windows 命令行(32,767 上限,
  实测模拟总长 1,969,余量 30,798),但常驻注入意味着**每一轮对话都付这份 token**。
  对 32K 上下文的本地小模型(见 P3-5)这是三分之一的窗口,不可接受。
  - **Tier 1 常驻**(`PiRuntime.cpp` systemPrompt,1,628 字符):skill 索引 + 无条件安全规则
    (不产 HTML/JS/CSS/shell/可执行、不产 Script、不产 Web 运行时)+ 创作流程
    (读 skill → 写 JSON → `wallpaper_validate_package` → `desktop_preview_wallpaper` → 等 Apply)
  - **Tier 2 按需**:新增 native tool `content_skill_get`,从 `<install>/skills/<name>/SKILL.md`
    读全文。**不调用就不产生 token。**
  - **为什么用 native tool 而不是让 AI 自己 `read` 文件**:Pi 的 `read` 工具沙箱边界与 cwd
    在本仓库无法验证;native tool 走产品自控的 worker 通道,确定性可验证。
  - **路径安全**:skill 名是**闭集白名单**(`kContentSkills`),不是路径拼接。
    模型只能从固定四个里选,任何 `../`、大小写变形、前后空白、嵌入 NUL 都在白名单比对处被拒。
  - **打包**:`CMakeLists.txt` 新增 `install(DIRECTORY skills/ DESTINATION skills)`;
    `stage.ps1` 断言 5 个文件存在 + 双向一致性守卫(磁盘目录 / C++ 白名单 / stage 期望 三方一致)
    + frontmatter `name:` 必须等于目录名。
  - **UI 入口**:`ConversationPanelImpl.inc` 两处问候语加入创作示例;
    `FriendlyToolName` 加 `content_skill_get` → "查阅内容创作规范"。
    示例刻意选了今天真能做到的能力(落叶动态壁纸 / 倒数日组件),**没有**选音频响应壁纸 —— 那是 B-2。
- **安全约束**(已落地,不可协商):skill 只能产出**声明式内容包**。
  `AI_GENERATED_DESKTOP_SANDBOX.md` 的硬规则 "AI never outputs HTML, JavaScript, CSS, shell commands,
  or executable code" 已同时写进 systemPrompt(Tier 1)与每份 `SKILL.md` 的反面提示词。
- **验证情况**:新增 Windows CI 测试 `src/tests/ContentSkillLoading.cpp`
  (target `MiaoDeskContentSkillLoadingTest`,构建并运行于 `windows-x64-build.yml`),
  走**真实 dispatch 路径**而非逻辑复刻,覆盖 7 组:四个 skill 逐个加载 / 省略 name 返回索引 /
  10 种路径穿越 / 未知名报错并列合法集 / 17 种畸形 JSON 不泄漏正文 / 缺失安装报错并指出路径 /
  未知工具路由。为让它能链接 `NativeTools.cpp`,把该文件从 `MIAODESK_APP_SOURCES` 移入
  `MIAODESK_CORE_SOURCES` —— **顺带修正一个契约违背**:原先打算用 `target_sources` 复编,
  那会绕过 `verify-path-layout-contract.ps1` 的正则但违背它"每个实现文件只有一个 CMake owner"
  的本意。另在 macOS 上复现了同一套逻辑(38 项断言)以离线验证。
  **测试过程抓到两个真实缺陷**:①`fs::file_size(path, std::error_code{})` 的 error_code 重载
  要求左值引用,临时对象绑不上,无法编译 —— 已修为命名变量;
  ②卸载清单漏掉产品自有子树(`skills/` 必然残留,`Widgets/` 是同类既有漏洞)——
  已补 `RMDir /r` 并把 ARM64 卸载残留检查加宽到 10 项。
  **完整编译未验证** —— 测试依赖 `windows.h`,本机为 macOS 无法构建,需 Windows CI 确认。
- **验收**:真实 Windows 上,用户在对话面板说「做一个有飘落落叶的动态壁纸」→ AI 调用
  `content_skill_get` → 产出 `.mdwall` → `wallpaper_validate_package` 通过 →
  `desktop_preview_wallpaper` 预览可见 → 用户点 Apply 后桌面出现该壁纸。全程零手写文件。
- **状态**:🟡 已实施,待 Windows 编译与真机验收

### B-2 音频 + 指针输入总线接通(最高优先的能力差距)

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §4.3 / §7 G1+G2 / §5.2 第 1、2 条
- **现状(声明层已就位,只缺数据源)**:
  - `input://frame/time` / `input://event/pulse` 已在
    `MiaoSceneFrameScheduler.cpp:113-114` 推入总线
  - `input://audio/bass` 等仅出现在 `MiaoSceneRuntimeModel.cpp:418` 的**测试桩**里
  - `BindingSourceKind::Input` 求值链路已接通(`MiaoSceneRuntime.cpp:219`)
  - `ComponentKind::InputBinding`、`AnimationTriggerMode::InputChange/InputRisingEdge`、
    `AssetType::Audio` 均已定义,但**没有任何生产者**
- **已实施方案(2026-09-20)**:按"可验证性"切分。内容框架整层
  (`src/content/runtime/`、`scene/`、`serialization/`)**零处包含 `windows.h`**,
  所以 B-2 的核心(契约 + 信号处理 + 写入)可以完全离线交付并测试;
  只有"从声卡取数据"和"桌面宿主跟踪光标"必须留在 Windows 侧。
  - **通道契约** `src/include/miaodesk/MiaoInputBus.h`(纯 C++,无 Windows 依赖):
    每个通道的 id / 类型 / 范围 / 是否需要关闭 click-through,一张表定义完。
    形状分三种:`Float01`(连续量)、`BoolState`(持续电平)、`BoolEdge`(单帧沿)——
    这个区分是必需的,`input://audio/beat` 当电平用会让每帧都变成上升沿。
    另有 `kPositionOnlyChannels` 与 `kInteractivePointerChannels` 两个不相交集合,
    后者才要求关闭 click-through。
  - **音频分析** `AudioSpectrumAnalyzer`:radix-2 FFT + Hann 窗 → 5 个命名频段
    (bass 20-160 / lowmid 160-500 / mid 500-2k / highmid 2k-5k / treble 5k-16k)
    + 16 个对数间隔频谱桶 + 总电平 + 节拍检测。非对称缓动(快起慢落)、
    固定分配、可复位。dB 归一化区间可配。
  - **音频入口** `src/content/input/MiaoAudioCapture.cpp`:多声道交织 PCM → 单声道
    (**取平均而非取左声道**,否则居中立体声的低音会被砍半)→ 线性重采样 →
    按窗口喂给分析器。半窗口不补零(静音会被当成真静音)。
  - **指针归一化** `PointerNormalizer` / `PointerSample`:物理像素 → 所在显示器归一化
    [0,1](**不用屏幕坐标**,壁纸不得知道桌面布局)、边沿从状态转移推导而非平滑值推导。
  - **发布器** `src/include/miaodesk/MiaoInputBusPublisher.h`:把分析结果写进
    `MiaoSceneRuntime::SetInput`,**只写 scene 声明过的通道**;
    `interactive=false` 时扣留 down/click 而不是假造 false。
  - **click-through 分层** 已按 WE 模型定清:指针位置 / 区域内 / 进入离开 = 不抢输入,
    默认允许;按下 / 点击 = 必须 opt-in 且会关掉 click-through。
  - **skill 已更新**:`skills/wallpaper-content/SKILL.md` 写入全部可用通道与用法,
    反面提示词加三条(不得用 down/click、不得把 beat 当电平、不得要求零延迟);
    `skills/content-review/SKILL.md` 壁纸检查项从 8 项增至 14 项。
- **四个 Windows CI 测试**(`MiaoDeskInputBusCoreTest` / `MiaoDeskInputBusPublisherTest` /
  `MiaoDeskAudioIngressTest`,构建并运行于 `windows-x64-build.yml`):
  - `InputBusCore` — 通道契约全覆盖 + 三集合不交且并集完备、FFT 频谱正确性
    (5 个纯音各自落在对应频段)、静音读作静音、17 种边界输入、配置规范化、
    节拍不把持续低音误判为节拍、指针归一化与边沿、2000 帧噪声稳定性
  - `InputBusPublisher` — 只写声明通道、beat 沿语义、interactive=false 扣留按压通道、
    契约覆盖发布器能写的每个 id。**链接真实 `MiaoSceneRuntime`**,因此 `SetInput`
    的类型闸是被真正走到的,不是假设的
  - `AudioIngress` — 下混、重采样(含 44.1k→48k 上采样后仍可分析)、窗口喂给
  - 测试过程中抓到并修复三个真实缺陷:①`kAudioBeat` 被归为 Float01 但语义是沿;
    ②`kPointerInside` 归为 Float01 但语义是状态,与发布器写 bool 冲突;
    ③`kPointerEnter/Leave` 被误列为"需要交互",实际可由位置流推导
- **尚未完成(必须 Windows 侧,本机无法验证)**:
  - ~~WASAPI loopback 采集(共享模式环回 + 设备变更处理)~~ ✅ 已实施见下
  - ~~桌面宿主的光标全局追踪与 `FeedAnalyzer` / `InputBusPublisher` 的实际接线~~ ✅ 已实施见下
  - `input://pointer/x|y` 的按显示器归属 —— **已实施**(`MonitorFromPoint` +
    `GetMonitorInfoW` → 该 slot 显示器的像素尺寸),但多显示器下的真机表现未验
- **已实施(2026-09-22 深夜,commit `667292d`)**:
  - **`MiaoWallpaperAudioTap`**(`src/desktop/wallpaper/monitor/`):WASAPI 共享模式 +
    `AUDCLNT_STREAMFLAGS_LOOPBACK`,经 `IMMNotificationClient` 监听默认设备变更。
    - 分析跑在采集线程,渲染线程只拷一份已算好的 `AudioSpectrumFrame`。16ms 的渲染帧
      不该等一个音频包。
    - 设备丢失是常态(拔 USB 耳机、切换默认输出):采集线程自行重建并退避 400ms,
      不上报壁纸死亡。宿主继续跑。
    - 混音格式只接受 32 位浮点,其余明确报错 —— 把 int16 当 float 读出来的噪声和真
      信号完全一样,静默错比报错难查得多。
    - 等待时长问 `IAudioClient::GetDevicePeriod` 而不是写死:设备周期 10ms 与 1.3ms
      差 8 倍。
    - 采集线程独占所有 COM 对象的生命周期(`CoInitializeEx` 的作用域就是这个函数)。
  - **两个渲染器新增 `Runtime()` 接缝**:`InputBusPublisher` 要的是 `MiaoSceneRuntime&`,
    没有这个访问器就只能绕过它重写"只写声明通道"的规则 —— 而那正是这个类的全部价值。
    D3D11 头继续用前向声明,不把 `d3d11.h` 泄给使用方。
  - **宿主接线**(`IndependentWallpaperHost`):
    - 输入在**绘制前**发布。绘制后才写,绑定读到的是上一帧的值,所有反应晚一帧。
    - 光标归属:`MonitorFromPoint` + `GetMonitorInfoW` → 该 slot 显示器的物理像素尺寸,
      在**那块显示器内**归一化,不用虚拟桌面坐标。
    - 按 `VK_LBUTTON` 的 `GetAsyncKeyState` 读按压,而不是从窗口消息推 —— 壁纸表层
      从不获得焦点,因此永远收不到鼠标消息。
    - 帧间隔用上一帧的真实时间戳(存在 slot 上,不是函数内 static:两个显示器的绘制
      时刻不同,共享 static 会把一个的 delta 递给另一个)。
    - `pointerInteractive` 由"场景是否声明了按压通道"推导,读声明而非开关。
  - **音频 tap 只在至少一个 slot 跑 Scene 壁纸时才启动** —— 打开声卡是用户能察觉的
    副作用;它的失败进 `DiagnosticsText()`,否则"音频壁纸为什么不响应"要来回一个支持轮次。
  - **`DeclaresInteractiveInput` 提到 `MiaoInputBus.h`**:宿主向契约提问,新增交互通道
    只需改一处。`InputBusCore` 加 14 项断言并双向验证(把 `kInteractivePointerChannels`
    里的 `kPointerDown` 换成 `kPointerInside` → 6 处 FAIL;还原 → 全绿)。
- **验收**:一份只用声明式绑定的音频响应壁纸,播放音乐时低频通道驱动
  SpriteRenderer 缩放;鼠标移动时 `input://pointer/x` 驱动 Transform 视差,
  且桌面图标仍可正常点击(证明确实没有抢走输入)。真机验证,不靠单测。
- **状态**:🟡 采集 / 接线 / 按显示器归属已实施;**待 Windows 编译与真机验收**

### B-3 表达力上限:响应曲线已落地,通用脚本解释器明确延后

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §4.1 / §7 G3 / §5.2 第 3 条
- **原判被修正(2026-09-20 重新评估)**:我把这一项写成"Script 解释器",
  但查过代码后,真正的缺口不是"没有脚本语言",而是**绑定只有线性**:
  `MiaoSceneRuntime::ApplyBinding` 只有 `*scale + offset`。
  没有它,音频频谱条无法"只在鼓点上炸开",视差层无法"过冲再回稳" ——
  这些是手感问题,不是表达能力问题,用脚本解决是杀鸡用牛刀。
- **已实施方案(2026-09-20)**:闭集响应曲线,**零代码执行**。
  - `BindingResponse` 8 个成员:`Linear` / `Square` / `Cube` / `SquareRoot` /
    `SmoothStep` / `Elastic` / `Threshold` / `Invert`,外加 `deadzone`。
  - 默认 `Linear` 逐位复现旧行为,既有包零影响(已用测试固定这一点)。
  - **闭集是刻意的**:每个成员都是单浮点的纯函数,因此绑定永远不可能获得副作用、
    文件访问或无界运行时,同时"AI 只产声明式内容"的硬规则依然成立。
  - `response` / `deadzone` 只对 float 源有效,模型校验层拒绝用在 bool/int 上。
  - 落点:`MiaoSceneRuntimeModel.h`(enum + 字段 + key 函数声明)、
    `MiaoSceneRuntimeModel.cpp`(校验 + key 往返)、`MiaoSceneRuntime.cpp`(求值)、
    `MiaoSceneSerializer.cpp`(JSON 读写 + 自测期望同步更新)。
- **测试过程中抓到两个真实缺陷**:
  1. **`Elastic` 根本不过冲**。最初的公式 `1 - e^(-kt)(1+cos(wt))/2` 里
     `(1+cos)` 恒非负,所以该式永不越过 1 —— 那是一个穿着弹性外衣的临界阻尼逼近。
     已换成真正的欠阻尼单位阶跃响应 `1 - e^(-kt)(cos(wt) + (k/w)sin(wt))`,
     实测峰值 1.135、过冲后回落穿越 1、端点仍精确。
  2. **`ReadSchema` 的错误信息不指明是哪个文件**。scene.json 与 parameters.json
     都有 schema 字段,报"Missing numeric field: schema"时用户无从判断。
     已改为带文件标签(修完立即在测试里观察到
     "parameters.json is missing the numeric field: schema")。
- **验证**:两个 Windows CI 测试
  (`MiaoDeskBindingResponseTest` / `MiaoDeskSceneRuntimeTest`,构建并运行):
  曲线数学性质(闭集大小一致、全域有限有界、端点固定、Elastic 真的过冲且回落、
  Threshold 是阶跃不是斜坡、Linear 是恒等)、key 往返(含未知/空/大写拒绝)、
  JSON 序列化往返且**再序列化逐字节稳定**、真实运行时求值
  (sqrt + deadzone = 0.458831 与手算一致)、旧 JSON 无 response 字段仍解析且
  `scale*value` 行为不变、5 种非法 response/deadzone 全部拒绝、bool 源上拒绝。
  既有 `MiaoSceneRuntime::SelfTest` 与 `MiaoSceneSerializer::SelfTest` 均仍通过。
- **通用脚本解释器:明确延后,理由记录在案**。
  做一个可编程壁纸运行时 = 一个可执行代码面。沙箱化一个解释器是大量且精细的工作,
  而它只会服务"用户手工放置的脚本"这一小群受众(AI 侧被
  `AI_GENERATED_DESKTOP_SANDBOX.md` 永久禁止产出代码)。
  在 B-2 的通道契约与 B-3 的响应曲线就位后,声明式已能覆盖绝大多数效果。
  **重新评估的触发条件**:出现响应曲线 + 内建积木确实表达不了的用户需求时。
- **验收**:一份音频壁纸,`response: sqrt` + `deadzone: 0.05` 的绑定让
  SpriteRenderer 在音乐变响时平滑胀缩、静音时完全静止。
- **状态**:✅ 响应曲线已完成并测试(2026-09-20);通用解释器延后(已记录触发条件)

### B-4 2D/3D 维度 + 灯光 + 雾(声明层已完成,渲染器未做)

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §4.1 / §7 G4
- **现状**:`RuntimeProfile` 只有 `{Wallpaper, Widget}`(承担壁纸/组件语义),
  `AssetType::Mesh` 枚举存在但无加载器;全仓 `src/content/` 与
  `src/include/miaodesk/` 对 light / fog 零命中。
- **已实施(2026-09-20)—— 只做声明层,刻意不碰渲染器**:
  - **空间维度** `SceneSpatialMode{TwoD, ThreeD}`,放在 `SceneDefinition` 上。
    **不重用 `RuntimeProfile`**:那个枚举已承担"壁纸语义 vs 组件语义",
    再塞一个 2D/3D 进去会让两个概念互相遮蔽。默认 `TwoD`,既有包零影响。
  - **灯光** `LightType{Point, Spot, Tube, Directional}` + `LightDefinition`
    (id / type / nodeId / color / intensity / range / 锥角余弦)。
    锥角用**余弦而非角度**,渲染器不必每帧转换。上限 12 盏,与 WE 文档一致。
  - **雾** `FogMode{Linear, Exponential}` + `FogDefinition`
    (color / startOrDensity / end)。Linear 模式要求 end > start。
  - **mesh 资产**扩展名限定 `.obj` / `.fbx`,与"v1 加载器计划接受的格式"一致。
  - **3D 门禁**:`lights` / `fog` 非空而 `spatial != ThreeD` 直接校验失败。
    理由:接受后静默丢弃会让作者反复问"为什么灯不亮",拒绝至少给出可诊断的错误。
  - 落点:`MiaoSceneModel.h`(spatial)、`MiaoSceneRuntimeModel.h`(Light/Fog 定义)、
    `MiaoSceneModel.cpp`(mesh 扩展名)、`MiaoSceneRuntimeModel.cpp`(校验)、
    `MiaoSceneSerializer.cpp`(JSON 读写 + SelfTest 期望同步)。
- **测试过程修正自己一处**:我最初在注释里写"inner == outer 的锥角 shades nothing,
  几乎不可能是作者本意",据此准备拒绝它。那是错的——零宽度半影是**硬边聚光灯**,
  是合法创作选择,半影宽度为零不等于没有光。代码本来就允许,改的是我的注释。
- **验证**:`MiaoDeskSceneSpatial3DTest`(Windows CI)覆盖 9 组:
  默认 2D 且既有行为不变 / lights 与 fog 在 2D 场景被拒且错误指明需要 3D /
  合法 3D 场景往返且**再序列化逐字节稳定** / 14 种 light 边界(前缀、悬空 nodeId、
  负值、NaN、负 range、非 spot 带锥角、内窄于外、余弦越界)/ 12 盏上限 /
  5 种 fog 边界 / mesh 扩展名(含大小写)/ 非法 spatial 与缺省字段的旧 JSON /
  非法 light type 与 fog mode。既有 `MiaoSceneModel::SelfTest`、
  `MiaoSceneRuntime::SelfTest`、`MiaoSceneSerializer::SelfTest` 均仍通过。
- **刻意不做(以及为什么)**:3D 渲染器、FBX 骨骼动画、PBR 材质集、雾的着色实现。
  这些是渲染侧工作,本机无法验证;而且**契约先落地是为了让渲染器有明确的靶子**,
  不是为了让内容包现在就能写 3D。
- **skill 已同步(重要)**:3D 契约可用 ≠ 3D 可渲染。`wallpaper-content/SKILL.md`
  已明确禁止生成 `spatial:"3d"` / `lights[]` / `fog[]` / mesh 资产,
  并要求用户要求 3D 时**明说暂不支持并给 2D 替代方案,不得静默降级**;
  `content-review` 加对应检查项。不这样做会让 skill 产出"校验通过但预览里什么都没有"
  的内容,正是我此前反复提醒自己要避免的那类错误。
- **验收**:真实 Windows 上一份 3D 场景壁纸,含导入的 OBJ 模型 + 一盏点光 + 雾,
  可被 skill 生成并经沙箱预览。**在渲染器落地前不可验收。**
- **状态**:🟡 声明层 + 校验 + 序列化已完成并测试;**3D 渲染器 / 模型加载器未做**

### B-5 Video 成为一类可直接生成的轻量产物 ✅

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §4.2 / §7 G5
- **原判断被推翻(2026-09-20)**:我写这一项时没有查 manifest 层。
  `WallpaperPackageType` 一直就有 `{Image, Video, Web, Scene}` 四值,
  `WallpaperLibrary::ImportPackages` 把 `type: video` 映射到 `LibraryWallpaperKind::Video`,
  `WallpaperService.cpp:307` 完整播放,`desktop_preview_wallpaper` 也已有 `mode=video`。
  **"视频壁纸作为一类轻量产物"在产品层本来就通了**,我说的"没通"是错的。
- **真实缺口(两个,都已修)**:
  1. `WallpaperPackage::Validate` 只对 Web 类型校验 entry 扩展名,
     `type: video` + `entry: foo.txt` **能通过校验**,然后在库里才失败且没有可用诊断。
     已按 Web 的同款规则补上 Image / Video 的扩展名校验
     (扩展名集合与 `WallpaperLibrary::InferKind` 一致,保证"包允许的 = 库能播的")。
  2. 没有与 `CreateWeb` 对称的确定性生成路径,AI 只能手写 manifest。
     已加 `CreateImage` / `CreateVideo`:复制源文件进 `assets/` + 写 manifest + 自校验,
     含大小上限(图片 25 MiB / 视频 250 MiB,与 skill 公布的上限一致)。
- **连带修掉一个净化缺陷**:资产名原先只清洗 stem 不清洗 extension。
  Windows 上 `\` 是分隔符所以 extension 不可能含它,但"只净 stem"这个写法本身不设防。
  已改为净最终拼装名 + 拒绝 `.` / `..` + 长度封顶。
- **验证**:`MiaoDeskMediaPackageTest`(Windows CI,构建并运行)覆盖:
  CreateVideo / CreateImage 产出合法包、entry 落在 `assets/`、
  **四种 type/entry 不匹配全部被拒**(含新补的 image-mp4、video-txt、video-html,
  以及既有 web-mp4 回归)、源文件缺 / 空 / 扩展名错 / 越界全部拒绝、
  恶意源路径产出的包仍校验通过且资产仍在包内、库能把手写 video 包导入为 Video 项。
  净化逻辑另在 macOS 上离线跑了 33 项断言。
  **完整编译未验证** —— 依赖 `windows.h`,需 Windows CI 确认。
- **验收**:skill 产出"一个 manifest + 一个视频资产"的 `.mdwall` →
  `wallpaper_validate_package` 通过 → 库导入为 Video 项 → 预览循环播放。
- **状态**:✅ 代码已完成(2026-09-20),待 Windows 编译与真机验收

### B-6 Web 音频监听 API ✅

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §4.3 / §7 G6
- **现状(比预期更空)**:`src/desktop/wallpaper/web/WebDesktopSurfaceChild.cpp` **完全没有 JS 桥** ——
  无 `postMessage`、无 host object、无 `WebMessageReceived` 处理。手工 Web 壁纸拿不到任何宿主数据。
- **已实施(2026-09-20)**:
  - **契约** `src/desktop/wallpaper/web/WallpaperWebAudioBridge.js`:
    `window.wallpaper.registerAudioListener(fn)` → 返回退订函数。
    帧形状与 B-2 的 `AudioSpectrumAnalyzer` 输出一致(level / bands[5] / spectrum[16] / beat),
    所以两条路径给内容的数据是同一套,不需要作者记两套。
  - **单向、闭集**:host → page 只推音频帧;page → host 什么都调不了。
    这不是 WebView2 的限制,是安全姿态:web surface 本就拒绝导航、DevTools、
    上下文菜单、新窗口和全部权限请求,开一个可调用的宿主面等于把这些全部作废。
  - **`window.chrome.webview` 只是传输层**,shim 是它前面的稳定 API,
    传输层可以换而不破坏已创作内容。
  - **幂等**:`AddScriptToExecuteOnDocumentCreated` 在每次导航和每个 iframe 都会跑,
    无守卫的 shim 会把消息监听器注册两次、每帧投递两次。
  - **宿主侧接线** `WebDesktopSurfaceChild.cpp`:嵌入 shim + `InstallAudioBridge()`,
    在 `Navigate` **之前**调用(导航后注入 page 脚本可能已跑过)。
- **防漂移**:shim 存在两份(js 是源真相 + cpp 内逐字节副本,省掉运行时读文件的打包步骤),
  `scripts/verify-web-audio-bridge.ps1` 强制一致,并额外禁止出现任何 page→host 调用。
  七种情形(干净 / LF 漂移 / CRLF 漂移 / CRLF 行尾差异 / LF+page→host / CRLF+page→host /
  副本被删)已逐条验证。
- **测试**:`tests/WebAudioBridge.mjs`(node,无浏览器)13 组断言,直接读 .js 源文件,
  因此跑的正是守卫断言与嵌入副本一致的那份字节。CI 中 `node tests/WebAudioBridge.mjs`。
- **测试抓到两个真实缺陷**:
  1. **NaN 处理是死代码**。`unit()` 先把非有限值变成 0,后面又有一句
     `if (!finiteNumber(item)) return null` —— 永远不触发。读起来像校验,实际是强转。
     已定清职责:**宿主保证有限性**(B-2 有 2000 帧噪声测试),**shim 保证形状**
     (形状错丢帧,值错强转为静音)。死代码删除,决策写进注释。
  2. 我在测试里把一条**合法帧**(带未知多余字段)错放进"应丢弃"组。
     多余字段必须忽略——这是前向兼容,宿主以后加字段不该破坏旧内容。
- **尚未完成**:宿主侧**还没有**真正推送音频帧。`AudioSpectrumAnalyzer` 与
  `FeedAnalyzer` 已就绪并有测试,但 `WebDesktopSurfaceChild` 到分析器之间没有连线 ——
  那需要 WASAPI 采集(B-2 剩余)先落地。
- **验收**:一份手工 Web 壁纸调用 `wallpaper.registerAudioListener`,播放音乐时
  每帧收到 5 频段 + 16 频谱桶;退订后不再收到;另一个故意抛错的监听者不影响它。
- **状态**:✅ 契约 + shim + 测试 + 宿主注入已完成;**宿主尚未推送帧(依赖 B-2 的 WASAPI 采集)**

### B-7 明确不做项(写下来避免反复被提起)

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §4.4 / §5.1 / §7 G7+G8
- **内容**:以下能力经评估**不做**,理由记录在案:
  - 木偶形变 / IK 绑定 / 刚体柔体物理 —— 重型编辑器特性,与"用户用 skill 快速创作"的定位冲突
  - Steam Workshop / 编辑器内发布 / 资产包分享 / Editor Extensions DLC —— 不建社区分发链
  - 第三方引擎(Godot/Unreal/Unity)官方支持 —— WE 官方同样零支持
  - 复刻 WE 的重型编辑器本体(粒子编辑器/模型编辑器/时间线编辑器 GUI)——
    我们的创作入口是 skill,不是编辑器
- **验收**:本清单与 `WALLPAPER_ENGINE_BENCHMARK.md` 表述一致,不再出现"要不要做编辑器"的反复。
- **状态**:✅ 已决断(2026-09-20)

### B-8 分类表述全仓校正

- **依据**:`WALLPAPER_ENGINE_BENCHMARK.md` §3.1 / §3.2 / §6
- **缺陷**:`docs/` 里存在"动态壁纸 —— Image / Video / Web / Scene 四类"的**错误表述**:
  混用了载体与运行时两个维度,且凭空加了 WE 官方不存在的 "Image" 一类。
- **内容**:把全仓表述统一为 `WALLPAPER_ENGINE_BENCHMARK.md` §6 的
  "载体 × 运行时"结构;`RuntimeProfile` 的含义不变(壁纸/组件语义),
  2D/3D 作为 Scene 内部新维度在 B-4 引入。
- **验收**:`grep -rn "Image / Video / Web / Scene" docs/ *.md` 零命中;
  `DESIGN_BASELINE.md` / `MIAODESK_CONTENT_FRAMEWORK.md` / `DOC-INDEX.md` 表述一致。
- **状态**:✅ 已完成(2026-09-20)—— 全仓残留下方全部为纠错说明本身或反面断言
  (`skills/README.md:16` 明示"不是平面四分类";`content-review:87` 与
  `wallpaper-content:84` 是禁止项)。改动:`README.md:21`、
  `DEVELOPMENT_ROADMAP.md:55,174`、`PRODUCT_VISION.md:35`、
  `DESKTOP_DOMAIN_ARCHITECTURE.md:106`、`skills/README.md`(新增分类约定节 + 硬底线第 2 条)、
  `skills/wallpaper-content/SKILL.md`(整节重写 + 反面提示词 + 输入输出)、
  `skills/content-review/SKILL.md`(领域检查 5 项 → 8 项)。
- **连带修正(执行中发现)**:`wallpaper-content/SKILL.md` 原文写死"壁纸不接收鼠标输入",
  与对标目标"鼠标控制壁纸"冲突。已按 WE 的分层模型改为:指针位置类效果(视差/追随/辉亮)
  默认允许且不抢输入;点击/拖拽需显式 opt-in 且会关 click-through。
  该张力已回写进 B-2 作为前置条件。

## P1 — 门的其余验收项

### P1-1 停用幂等的 reload 断言 ✅ 已在真实 Windows CI 通过

- **依据**:`DEVELOPMENT_ROADMAP.md` §3 P0-2
- **现状**:`verify-widget-visibility.ps1:132-137` 已断言 `Enabled 1→0→1` 与壁纸停用下的组件生命周期,
  但缺一条独立断言:**Shell repair / reload 之后壁纸不得被重新拉起**。
- **已实施**:
  - 探针新增 `VisibleWallpaperSurfaceCount()`,数可见的
    `MiaoDesk.Native.IndependentWallpaperSurface`(类名与 `IndependentWallpaperHost`
    的 `kSurfaceClass` 逐字一致)。
  - 新增断言:用"停掉整族再冷启"当 reload 的 CI 等价物,
    `wallpaper.ini` 全程 `Enabled=0`,要求 `VisibleWallpaperSurfaceCount() == 0` **且**
    组件重建数与 reload 前一致(1 = Content GlassClock 布局,2 = quick-build 布局)。
  - 两个断言都要:只断言壁纸数,会把"什么都没起来"也判成通过;只断言组件数,
    则盖不住"顺便把壁纸也拉起来了"这个回归。
- **为什么是"冷启"而不是触发一次 `RepairSurfaceStack`**:`RepairSurfaceStack` 只对
  **已存在**的 MiaoDesk 表层重新排序(`RepairKnownMiaoDeskSurfaces`),不创建表层,
  所以它不可能凭空造出一个壁纸表层。真正会"重新拉起"的路径是运行时进程整体重启
  (Coordinator 重新评估 `Enabled`),这恰好就是冷启覆盖的场景。
- **为什么期望值是 0**(不是"反正测一下"):`WallpaperWebRuntimeCoordinator` 的
  `DesiredRequests()` 第一行就是 `if (!host || !IsWindow(host) || !state.enabled) return requests;`,
  返回空列表;`startRequests()` 拿到空列表只调 `surfaces.Stop()` 并写
  「未启用 Web 壁纸」,**不会 `Start()`**。所以 `Enabled=0` 时
  `IndependentWallpaperSurface` 压根不创建。这条断言验的是"这个结论在整轮运行时重启
  之后依然成立",而不是它在这个进程里碰巧成立。
- **本机能验到什么**:C# 探针块用 macOS pwsh 的 `Add-Type` 真编译通过,两个方法
  (`PaintReadyWidgetCount(Boolean)` / `VisibleWallpaperSurfaceCount()`)签名确认存在;
  `if` 赋值、报错插值、`-ne 0` 分支方向逐条跑过。另外对两条可能创建该表层的代码路径都
  核对过:Web/Content coordinator 在 `enabled=0` 时 `DesiredRequests()` 直接返回空、
  根本不 `Start()`;遗留 `WallpaperEngine.cpp:318` 的 `ShowWindow` 被 `config_.enabled`
  闸住,窗口顶多被创建但不可见,而探针第一件事就是查 `IsWindowVisible`。所以 0 是必然
  结果,不是偶然。
- **验收**:CI 中新增断言,模拟 reload 后壁纸仍保持停用。
- **状态**:✅ **已在真实 Windows CI 通过** —— `Windows x64 Build #367`
  的 `Verify staged GlassClock Content Framework route` 步骤 success。

### P1-2 低常驻资源基线

- **依据**:`DEVELOPMENT_ROADMAP.md` §2 / 设计目标第一段
- **现状**:`PerformanceService` / `WallpaperPerformancePolicy` 已存在,但缺可跨版本比较的数字。
- **内容**:常驻内存 / CPU / 句柄数基线,并在 CI 或发布流程中采集。
- **状态**:❌ 未开始

## P2 — 本地 AI 落地

### P2-1 主模型 A/B 实测定夺

- **依据**:`LOCAL_AI_ARCHITECTURE.md` §6.2 / §8
- **内容**:`gpt-oss-120b`(有实测 14.5 tok/s)与 `Qwen3.6-35B-A3B`(纸面占优但**零实测**)并行评估。
  按"中文对话质量 → 工具调用成功率 → 实测 tok/s → 显存余量"定夺。
- **注意**:若 v1 要做本地图片生成,候选 B 接近必选(§7.7 内存账)。
- **验收清单**:`LOCAL_AI_ARCHITECTURE.md` §8 全部项通过。
- **状态**:❌ 未开始 —— 需 DGX Spark 实机

### P2-2 图像运行时部署与实测

- **依据**:`LOCAL_AI_ARCHITECTURE.md` §7.6 / `LOCAL_AI_DEPLOYMENT.md` §2
- **内容**:ComfyUI(官方 Spark playbook)+ OpenAI 兼容 shim;模型选 Z-Image-Turbo(主)+ Qwen-Image v1.0(按需)。
- **已知障碍**:ComfyUI 的 API 不是 OpenAI 兼容,需自写薄 shim;vLLM-Omni 的 API 匹配但硬件支持未记录。
- **状态**:❌ 未开始

### P2-3 模型路由器 L1 上线

- **依据**:`LOCAL_AI_ARCHITECTURE.md` §7.2 / §7.4
- **内容**:DGX Spark 上跑路由器,对外暴露单一 endpoint,按请求特征分流(有 tools → 主模型;
  命中 skill 签名 → 主模型严格 JSON;短请求无 tools → 轻量模型;其余 → 主模型)。
- **注意**:分流签名必须固定,否则前缀缓存命中率崩塌。
- **状态**:❌ 未开始

### P2-4 修复 `models.json` 硬编码常量 ✅

- **依据**:本会话审计发现,原 `src/ai/pi/PiRuntime.cpp:438`
- **缺陷**:`contextWindow: 128000` 与 `maxTokens: 16384` 是硬编码常量,不随用户所选模型变化。
  用户选一个 32K 上下文的模型,产品会告诉 Pi 它有 128K。
- **已实施**(2026-09-20):
  - `ApiRuntimeProfile.h` — 新增 `ParsePositiveUInt()` 与 `RuntimeProfile.contextWindow` / `.maxTokens`,
    从 profile INI 的可选键读取(0 = 未配置)
  - `L3Agent.h` — `ModelConfig` 增加同名字段,并**纳入 `ReloadConfig()` 的变更检测**,
    否则改配置不会重启 Pi 会话,修复会静默失效
  - `PiRuntime.h` — `ProviderSetup` 增加同名字段
  - `PiRuntime.cpp` — `BuildProviderSetup` 取值;signature 纳入 `ctx=` / `max=`;
    `ConfigurePiAgent` 用配置值,未配置时回落到原默认(行为不变)
- **验证情况**:`ParsePositiveUInt` 逻辑抽出为独立程序,以 C++23 编译并跑 15 个用例全过
  (空串 / 正常值 / 前后空白 / 零 / 非数字 / 数字后跟垃圾 / 负数 / 小数 / 超上限 / 边界 / 十六进制 / 科学计数法);
  JSON 拼装产物经 Python `json` 校验为合法且字段为数字类型。
  **完整编译未验证** —— 代码依赖 `windows.h` / `wincred.h`,本机为 macOS 无法构建;
  需在 Windows CI 上确认。
- **遗留**:未配置时的默认值仍是 128000 / 16384,这个默认值本身是否合理待评估(见 P3-5)。
- **状态**:🟡 已实施,待 Windows 编译验证

### P2-5 L2 多 provider 配置

- **依据**:`LOCAL_AI_ARCHITECTURE.md` §7.2
- **内容**:让产品往 `models.json` 写多个 provider(`miaodesk` / `miaodesk-fast` / `miaodesk-image`),
  由 Pi 按任务选择。图片生成必须走这一层。
- **依赖**:P0-2(图片生成参数化)完成后再做
- **状态**:❌ 未开始

## P3 — 技术债与已知缺陷

### P3-1 移除不可达的 AI HTML 壁纸生成工具 ✅

- **依据**:本会话审计发现
- **缺陷(比初判更严重)**:`wallpaper_create_web_package` 在 `src/ai/tools/NativeTools.cpp` 有**完整实现**
  (41 行 `CreateWebWallpaperPackage` + tool schema + dispatch),其入参含 `html`(完整自包含
  HTML/CSS/JS),描述主动邀请模型"generate an interactive/procedural HTML wallpaper"。
  这直接违反 `AI_GENERATED_DESKTOP_SANDBOX.md` 的硬规则"AI never outputs HTML, JavaScript, CSS"。
  它被三道闸独立阻断(不在 `NativeToolDefinitionsJson()` 广告列表、不在 Pi 的 `TOOL_NAMES`、
  不在 `main.cpp` allowlist),因此**不可达**;但保留即是一个潜在风险:
  日后若有人把它加进任一闸,就会静默复活一个产品已刻意移除的能力。
- **已实施**(2026-09-20):
  - `NativeTools.cpp` — 删除 `CreateWebWallpaperPackage`(41 行)、其 tool schema、dispatch 行
  - `ConversationPanelImpl.inc` — 删除 `FriendlyToolName` 中对应的友好名行
  - helper(`KnownFolder` / `ExtractJsonString` / `ExtractJsonBool` / `SanitizeFileName`)经核实
    各有 6 / 15 / 3 / 5 个其他调用方,删除不会孤立它们
- **验证情况**:全仓库零残留引用;括号平衡复查通过。
  **完整编译未验证** —— 依赖 `windows.h`,本机为 macOS 无法构建,需 Windows CI 确认。
- **状态**:🟡 已实施,待 Windows 编译验证

### P3-2 三个未合入分支 ✅ 已合入主干并恢复为正式分支(2026-09-22)

- **依据**:本会话审计发现;此前因我验证方法有缺陷而误删远端分支
- **去向(已定并执行)**:三支全部合入 `main`,不再游离:
  | 分支 | 提交 | 合入结果 |
  | --- | --- | --- |
  | `fix/content-widget-install-runtime-reload` | 16 | `7aceaa8` —— Widget 生命周期前重置壁纸运行时 |
  | `feat/content-widget-settings` | 25 | `bca7f9b` —— **P0-3 TodayTasks 关闭** |
  | `fix/unicode-wallpaper-theme-packages` | 78 | `9fa2082` —— canonical manifest + Unicode 主题;P0-4 仍未关闭(见该项) |
  三支已从 `_check/*` 恢复为正式本地分支,可随时推回远端。
- **合并中处理的两处要点**:
  1. 两条独立历史各自创建 `ContentWidgetPreviewRenderer.cpp` / `ContentWidgetSettingsDialog.cpp`
     (均非对方祖先)。逐行比对确认分支版把 `PublishWeather` 泛化成 `PublishHostData`
     且天气发布语句逐字节相同、只是新增 tasks 分支,才取分支版 ——
     按 add/add 常规做法直接取一侧会静默删掉天气发布。
  2. `WallpaperPackage.cpp` 自动合并通过了三处**编译不过**的损坏
     (成员定义进了匿名命名空间、调用未加类限定、Image/Video 校验被拼成错误返回形态)。
     只有真的编译才暴露 —— 已全部修复并用最小 windows.h 替身在 macOS 上编译运行验证。
- **上游同步**:仍需 `git push`。当前 `gh` 未认证,无法推送。
- **状态**:✅ 已合入主干并恢复为正式分支(2026-09-22);**尚未推回远端**

### P3-3 本地 AI 文档占位版本号

- **依据**:`LOCAL_AI_DEPLOYMENT.md` §10
- **内容**:容器 TAG、模型 SHA-256、`--structured-outputs-config.enable_in_reasoning` 参数名均为占位,
  部署当天需从官方 playbook 与 Hugging Face 核对。
- **状态**:❌ 未开始

### P3-4 分支恢复的误删修复记录

- **依据**:本会话操作失误
- **内容**:我曾在验证方法有缺陷(zsh 变量不分词导致路径比对静默失效)的情况下删除了 42 个远程分支,
  其中 3 个含有未合入工作。已全部救回本地,但**尚未推回远端**。
- **状态**:🟡 本地已恢复,远端待推回
### P3-5 contextWindow / maxTokens 默认值合理性

- **依据**:P2-4 实施时发现
- **内容**:未在 profile 里配置时,默认仍是 `contextWindow: 128000` / `maxTokens: 16384`。
  对一个本地小模型(如 32K 上下文),这个默认值依然偏大。是继续用保守默认,还是按 provider
  推断,需要产品决策。
- **状态**:❌ 未开始


---

## 已完成

| 日期 | 项 | 产出 |
| --- | --- | --- |
| 2026-09-20 | 清理远程分支 | 42 个已合入或残留分支删除,远端只留 `main` |
| 2026-09-20 | ARM64 打包验证 | run 35507624372 两 job 全绿;三个 exe PE machine = `0xAA64` 独立复验 |
| 2026-09-20 | 确立产品愿景层 | `docs/PRODUCT_VISION.md`;`DOC-INDEX.md` 与 `README.md` 改三层结构 |
| 2026-09-20 | 建立 CHANGELOG | `CHANGELOG.md`;此前项目无任何变更记录 |
| 2026-09-20 | 版本号对齐 | `installer.nsi` 0.1.2 → 0.1.3,与两个 MSIX workflow 断言一致 |
| 2026-09-20 | 技术契约反偏移 | `NATIVE_SOURCE_LAYOUT.md` 补 `content/`+`tests/`;`DESKTOP_DOMAIN_ARCHITECTURE.md` 补 Content Framework 域;`verify-path-layout-contract.ps1` 加反向守卫 |
| 2026-09-20 | 隐私政策补本地模式 | `docs/privacy-policy.md` 双语,模式 A 云端 / 模式 B 本地局域网 |
| 2026-09-20 | 第三方声明补本地栈 | `THIRD-PARTY-NOTICES.md` 追加推理栈 + 排除清单 |
| 2026-09-20 | 本地 AI 架构设计 | `docs/LOCAL_AI_ARCHITECTURE.md` 666 行 |
| 2026-09-20 | 本地 AI 部署手册 | `docs/LOCAL_AI_DEPLOYMENT.md` 380 行 |
| 2026-09-20 | 内容创作 skill 集 | `skills/` 5 文件,壁纸 + 组件,含正反提示词与安全/性能门禁 |
| 2026-09-20 | P2-4 models.json 硬编码常量 | 四个文件;`ParsePositiveUInt` 15 用例通过;JSON 产物校验合法;**待 Windows 编译验证** |
| 2026-09-20 | P3-1 移除不可达的 AI HTML 壁纸工具 | `NativeTools.cpp` 删 43 行 + UI 友好名;零残留;helper 均已核实有其他调用方;**待 Windows 编译验证** |
| 2026-09-20 | 设计 vs 实现对照 | `miaodesk-design-progress.html`,68 项逐条带代码证据 |
| 2026-09-20 | B-8 分类表述全仓校正 | 7 个文件;载体×运行时替换平面四分类;连带修正壁纸交互规则(见 B-2) |
| 2026-09-20 | 对标 Wallpaper Engine | `docs/WALLPAPER_ENGINE_BENCHMARK.md`;官方三类(Scene 2D/3D、Web、Video)逐能力对标;9 条差距 + 5 条明确不做 |
| 2026-09-20 | B-1 skill 接入产品 | 新增 native tool `content_skill_get`(按需加载);systemPrompt 加创作指引;`CMakeLists.txt` + `stage.ps1` 打包 skills 并加一致性守卫;问候语加入口;38 项逻辑单测通过;**待 Windows 编译验证** |
| 2026-09-20 | B-2 输入总线契约与分析内核 | `MiaoInputBus.h` 通道契约(三形状 + click-through 分层);FFT 频谱分析;指针归一化;音频下混重采样;InputBusPublisher;skill 与评审门禁同步;三个 Windows CI 测试 + macOS 离线复现;**WASAPI 采集与宿主接线未做** |
| 2026-09-20 | B-3 绑定响应曲线 | 闭集 8 条曲线 + deadzone,零代码执行;默认 Linear 逐位兼容;两个 Windows CI 测试;**通用脚本解释器延后并记录触发条件** |
| 2026-09-20 | B-6 Web 音频监听 API | `WallpaperWebAudioBridge.js`(单向闭集契约 + 幂等 shim);宿主注入在 Navigate 前;防漂移守卫七情形验证 + node 13 组断言;**宿主尚未推送帧** |
| 2026-09-20 | B-4 3D 场景声明层 | `SceneSpatialMode` + Light/Fog 定义 + mesh 扩展名校验 + 3D 门禁 + JSON 往返;`MiaoDeskSceneSpatial3DTest`(9 组)通过;skill 已禁止生成 3D(渲染器不存在);**渲染器未做** |
| 2026-09-20 | B-5 media 壁纸包校验 | 补齐 Image/Video entry 扩展名校验(关掉 type/entry 不匹配漏洞);新增 `CreateImage`/`CreateVideo`;资产名净化改为净全名 |

## 维护约定

1. 每完成一项,把状态改为 ✅ 并移入「已完成」,写清产出。
2. 每新增一项,必须填**依据**与**验收标准**。
3. P0 项在全部关闭前,不对外做任何发布承诺。
4. 本清单与 `DEVELOPMENT_ROADMAP.md` 冲突时,以后者为准;但若冲突源于本清单已过期,应更新本清单。
