# 对标 Wallpaper Engine — 能力差距分析

- 状态:活文档,随开发更新
- 建立:2026-09-20
- 上游:`PRODUCT_VISION.md` · `DESIGN_BASELINE.md` · `DEVELOPMENT_ROADMAP.md`
- 研究来源:Wallpaper Engine 官方站 / 官方 designer docs / 官方 help / Steam 商店页与全部 105 条更新公告 / Wikipedia(2026-09)

---

## 1. 为什么要有这份文档

`DESIGN_BASELINE.md` 要求"所有功能都对照三个用户界面来设计",但没有回答一个问题:
**我们的动态桌面,到底要做到多强?**

本文件给出答案的度量基准:Wallpaper Engine(下称 WE)是这个品类里唯一被大规模验证的产品。
对标它不是照抄它 —— 第 5 节会说明两者目标用户不同,有几个能力 WE 根本没有。
但 WE 定义了这个品类的**能力天花板**,低于它的地方就是 MiaoDesk 的欠账。

**规则**:
1. 本文件只记录**有来源的事实**与**有代码证据的现状**,不写愿望。
2. 每一条差距必须能指出 MiaoDesk 侧的代码位置或"零引用"证据。
3. 差距进入 `TODO.md` 时必须带本文件小节号作为依据。

---

## 2. Wallpaper Engine 的官方分类(权威)

WE 官方 designer docs 定义**三种创作类型**:

| 类型 | 定义(官方措辞) | 技术 | 用户可否创作 |
| --- | --- | --- | --- |
| **Scene** | "based on images created with the Wallpaper Engine Editor" | WE 自己的实时渲染器(DirectX 11) | 是(核心) |
| **Web** | "custom-programmed and based on web technologies such as HTML, CSS and JavaScript" | 内嵌浏览器(patch notes 提 CEF) | 是 |
| **Video** | "based on pre-rendered .mp4 video files" | 视频播放 | 是(经编辑器上传) |
| **Application** *(遗留)* | 任意 `.exe` 作为壁纸 | 外部 Windows 程序 | **已从公开 Workshop 移除** |

两个容易被误解的点:

- **没有独立的 "Picture" 类型。** 静态图片在编辑器里导入后**就是 scene 壁纸**。
- **"Scene" 不是一类,是一个族。** 官方文档明确:"Wallpaper Engine differentiates between 2D and 3D scenes."
  3D scene 有自由 3D 摄像机与透视;2D scene 默认关闭透视、编辑器以 2D 为中心。**两者都接受 3D 模型。**

Application 类型的退场值得记录:WE **2.8.42(约 2026-06)** 以恶意软件风险为由
("executable files cannot be reliably secured")**永久移除**了公开 Workshop 的 Application 壁纸
(约占全部壁纸 0.5%),本地仍可运行。

---

## 3. MiaoDesk 的现有分类(代码事实)

MiaoDesk 的分类是**两个正交维度**,不是一个平面列表:

```cpp
// src/include/miaodesk/MiaoSceneModel.h:11
enum class ContentKind {          // 载体维度:这个东西放在桌面的哪一层
    Wallpaper,                    //   壁纸层:在桌面图标之下,默认点击穿透
    Widget,                       //   组件层:在桌面图标之上,可交互
};

// src/include/miaodesk/MiaoContentModel.h:15
enum class ContentRuntimeKind {   // 运行时维度:用什么引擎解释它
    Scene,                        //   声明式 Scene(组件/材质/动画/粒子/后处理/shader/绑定)
    Web,                          //   内嵌浏览器
};
```

即 `ContentKind × ContentRuntimeKind` = 2×2 矩阵,`.mdwall` / `.mdwidget` 是它的两种打包形态。

### 3.1 我此前的说法错在哪

我在设计进度审计里写过"动态壁纸 —— Image / Video / Web / Scene 四类"。这是错的,三处:

1. **混用了两个维度。** "Image/Video/Web/Scene" 里,Video/Web/Scene 是运行时维度,而 WE 里 Image 根本不是一个类型 ——
   它把"静态图"和"动态场景"归到了同一个 Scene 类型下。MiaoDesk 同理:静态图是 Scene 的一个特例,不是一类。
2. **凭空加了 "Image" 一类。** 代码里不存在它,WE 官方也不存在它。
3. **漏了真正的第二个维度。** `ContentKind{Wallpaper, Widget}` 才是 MiaoDesk 与 WE 差异最大的地方
   (见第 5 节),而我原来的四分类完全没体现。

### 3.2 正确的表述

> MiaoDesk 的桌面内容 = **载体**(壁纸 / 组件)× **运行时**(Scene / Web)。
> 壁纸侧的 Scene 运行时内部再分**静态图 / 动态场景 / 视频 / 3D** 等表现形态,
> 这些是 Scene 的**配置档**,不是并列的顶层类型。

`RuntimeProfile` 目前只有 `{Wallpaper, Widget}`(见 `MiaoSceneModel.h:16`),
它承担的是"壁纸语义 vs 组件语义",**不是** WE 的"2D vs 3D"。两者不要混。

---

## 4. 逐能力对标

图例:✓ 已有 · ◐ 有模型/骨架但未接通 · ✗ 无 · — WE 也没有

### 4.1 创作表达力(Scene)

| WE 能力 | MiaoDesk 现状 | 证据 | 结论 |
| --- | --- | --- | --- |
| 2D / 3D 场景区分 | ◐ | `SceneSpatialMode{TwoD, ThreeD}` 已加在 `SceneDefinition` 上(B-4);渲染器仍只做 2D | **契约已有,渲染器 2D** |
| 3D 模型导入(FBX 骨骼/顶点动画,OBJ) | ✗ | `AssetType::Mesh` 枚举存在,mesh 扩展名已限定 `.obj`/`.fbx`(B-4),但**无任何模型加载器** | **缺加载器** |
| 专用模型编辑器 + PBR 材质 | ◐ | `MaterialDefinition` 有 `MaterialModel`/纹理槽/自定义 property(`MiaoSceneRuntimeModel.h:107`),无 PBR 参数集 | 部分 |
| 灯光(点/聚光/管状/平行,上限 12) | ◐ | `LightType` 四种 + `LightDefinition` 已定义并有校验(B-4);缺着色实现 | **契约已有,缺实现** |
| 雾 | ◐ | `FogMode{Linear, Exponential}` + `FogDefinition` 已定义并有校验(B-4);缺着色实现 | 同上 |
| 刚体/柔体物理(jiggle bone) | ✗ | 无 | 缺 |
| 粒子系统 + 专用编辑器 | ✓ | `ParticleEmitterDefinition`(`MiaoSceneRuntimeModel.h:220`)+ `MiaoD3D11ParticleRenderer` + `MiaoParticleSerializer` + `MiaoParticleRuntime` | 已有 |
| 时间线动画(关键帧 + 缓动) | ✓ | `AnimationTrackDefinition` / `AnimationKeyframeDefinition`,含 `Once/Loop/PingPong` 与 5 种缓动(`MiaoSceneRuntimeModel.h:195-215`) | 已有 |
| 逐层视差 | ◐ | `Transform` 组件 + `input://event/pulse` 输入通道已定义,无数据源驱动 | 见 4.3 |
| 木偶形变 + IK 绑定 | ✗ | 无 | 缺 |
| 自定义 shader 编程 | ✓ | `ShaderStage{Vertex,Pixel,Compute}` + `MiaoPostProcessCompiler` + `MiaoPostProcessShaderLibrary` + `MiaoRenderGraph` | 已有 |
| 后处理特效库 | ✓ | `PostProcessEffectKind` 枚举(`MiaoSceneRuntimeModel.h:58-67`) | 已有 |
| SceneScript(类 JS 脚本) | ◐ | `ComponentKind::Script` 与 `AssetType::Script` 存在,`src/content/` 无脚本解释器。**已按别的方式补齐**:声明式绑定加闭集响应曲线(8 条 + deadzone),覆盖绝大多数效果,且不引入代码执行面;通用解释器延后 | **缺解释器,手感已补** |
| 用户可调属性(颜色/滑杆/下拉/文本/贴图/快捷键) | ✓ | `ContentParameterType{Bool,Int,Float,String,Color,Enum,Asset}` 7 种(`MiaoContentModel.h:20`) | 已有 |
| 资源/特效复用与分享 | ◐ | 包级复用有(`ContentPackageManager`),无跨包资产包 | 部分 |

### 4.2 Web 与 Video

| WE 能力 | MiaoDesk 现状 | 证据 | 结论 |
| --- | --- | --- | --- |
| Web 壁纸(HTML/CSS/JS + CEF) | ✓ | `ContentRuntimeKind::Web` + `MiaoContentPackage` 的 web 入口 | 已有 |
| Web 内嵌视频限制 `.webm/.ogg/.ogv` | — | 产品侧无同等限制,不构成差距 | — |
| Video 壁纸(mp4/WebM/avi/m4v/mov/wmv) | ◐ | `ComponentKind::VideoRenderer` + `AssetType::Video` 存在,**但没有把"单视频壁纸"作为一类可直接生成的轻量产物** | 半通 |
| Application 壁纸(任意 exe) | — | WE 已自己移除,不追 | — |

### 4.3 音频响应与鼠标交互

这是 WE 的**一等公民**,也是 MiaoDesk 目前最大的空洞。

| WE 能力 | MiaoDesk 现状 | 证据 | 结论 |
| --- | --- | --- | --- |
| 场景音频可视化(含专辑封面/媒体播放数据) | ◐ | 分析内核已完成(`AudioSpectrumAnalyzer`:`src/include/miaodesk/MiaoInputBus.h`),**缺 WASAPI loopback 采集与宿主接线** | **分析已就绪,采集未做** |
| SceneScript `AudioBuffers` | ✗ | 依赖 4.1 的 Script 解释器 | 缺 |
| Web 音频监听 API(`window.wallpaperRegisterAudioListener`,每声道 64 频段) | ◐ | 契约与 shim 已完成(`WallpaperWebAudioBridge.js`,B-6);**宿主尚未推送帧**,且只推 5 频段 + 16 频谱桶而非每声道 64 频段 | **契约已有,推送未接线** |
| 鼠标控制壁纸(商店页明示) | ◐ | `PointerNormalizer` 已完成,`ComponentKind::InputBinding` 与 `AnimationTriggerMode::InputChange/InputRisingEdge` 已定义;缺宿主光标追踪接线 | **归一化已就绪,接线未做** |
| 鼠标交互粒子 | ◐ | 粒子系统 + 指针通道均已就位,缺宿主光标追踪接线 | 同上 |
| 鼠标附着灯光 | ✗ | 依赖 4.1 灯光 | 缺 |

MiaoDesk 的输入总线**声明层已经就位**,这是好消息:

```cpp
// src/content/runtime/MiaoSceneFrameScheduler.cpp:113-114
definition.inputs.push_back(InputChannelDefinition{std::wstring(kFrameTimeInput), PropertyType::Float, 0.0});
definition.inputs.push_back(InputChannelDefinition{L"input://event/pulse", PropertyType::Bool, false});

// src/content/runtime/MiaoSceneRuntimeModel.cpp:523(测试桩)
runtime.inputs.push_back(InputChannelDefinition{L"input://audio/bass", PropertyType::Float, 0.0});
```

`BindingSourceKind::Input` 的求值链路也在 `MiaoSceneRuntime.cpp:257` 接通了。
**缺的只是:谁往这些通道里写真实数据。**

### 4.4 分发与生态

| WE 能力 | MiaoDesk 现状 | 结论 |
| --- | --- | --- |
| Steam Workshop(核心功能,非附加) | ✗ | 不做,见 5.2 |
| 编辑器内一键发布(公开/好友/私有) | ✗ | 不做 |
| 资产包分享 | ✗ | 不做 |
| 免费 DLC(Editor Extensions:ML 深度图) | ✗ | 不做 |
| 第三方引擎(Godot/Unreal/Unity)官方支持 | ✗ | WE 也无,不追 |

### 4.5 WE 没有、MiaoDesk 独有的能力

不要因为 WE 没有就低估这些 —— 它们是产品立足点:

| 能力 | MiaoDesk 现状 | 说明 |
| --- | --- | --- |
| **桌面小组件常驻可交互层** | ✓ | WE 全站 58 个 help 页 + 191 个 docs 页 **零次**出现 "widget"。这是 MiaoDesk 的差异化主轴 |
| **AI 生成内容包** | ◐ | `wallpaper_create_web_package` 已按 `AI_GENERATED_DESKTOP_SANDBOX.md` 移除;创作链改为 `skills/` 提示词规范,**尚未接入产品** |
| **本地 AI 驱动全部 AI 功能** | ◐ | 拓扑与部署文档已就绪,`image_generate` 本地化是唯一硬缺口 |
| **包校验 + 沙箱预览 + 一键应用** | ✓ | `wallpaper_validate_package` / `desktop_preview_wallpaper` / 宿主按钮 |
| **点击穿透层级契约** | ✓ | Widget > Desktop Icons > Wallpaper |

---

## 5. 定位结论

### 5.1 两者不是同一个物种

| | Wallpaper Engine | MiaoDesk |
| --- | --- | --- |
| 创作方式 | 人工在**重型编辑器**里做(粒子编辑器/模型编辑器/木偶绑定 IK/时间线/SceneScript) | 用户用**工程自带 skill** 描述需求,AI 产出内容包,产品校验 + 沙箱预览 + 一键应用 |
| 分发方式 | Steam Workshop,超百万条目,全免费 | 本地产出,不建社区 |
| 核心资产 | 编辑器 + 社区生态 | 内容框架 + skill + 本地 AI |
| 安全姿态 | 应用层沙箱(已因恶意软件风险砍掉 Application 类型) | `AI_GENERATED_DESKTOP_SANDBOX.md`:AI 永不输出 HTML/JS/CSS/shell/可执行代码 |

**推论:MiaoDesk 不需要复刻 WE 的重型编辑器。** 复刻它意味着把创作负担转回用户,
这与"让用户用 skill 快速做壁纸和组件"直接矛盾。

### 5.2 但有一条不能退让

**skill 能做出的东西,天花板由 Scene 的表达力决定。**

如果 MiaoDesk 的 Scene 只能做"贴图 + 平移 + 淡入淡出",那么 skill 再聪明,
用户拿到的壁纸也永远比 WE 社区里手工做的差一档。这不是 skill 的问题,是积木不够。

所以对标的重点不是"补编辑器",而是**补齐让声明式 Scene 能表达 WE 级效果的最小积木集**:

1. **音频输入总线**(4.3)—— 音频响应壁纸是 WE 生态里最受欢迎的一类,且完全不需要编辑器,
   声明式绑定 + skill 生成即可覆盖。
2. **鼠标/指针输入总线**(4.3)—— 同上,`InputBinding` 组件已存在,只缺数据源。
3. **Script 解释器**(4.1)—— 这是表达力上限。没有它,Scene 只能做声明式能描述的事。
   但它也是风险最高的一项(见 `AI_GENERATED_DESKTOP_SANDBOX.md`),必须与"AI 不产出代码"的规则共存:
   **脚本只能由用户手工放置,skill/AI 永不生成脚本内容**。
4. **灯光 + 雾 + 3D 模型加载**(4.1)—— 3D 场景的表达力基础,工作量最大,优先级低于前三项。

### 5.3 Web 运行时的定位要说清

`ContentRuntimeKind::Web` 是给**用户手工制作或第三方来源**的内容用的。
它**不是** AI/skill 的产出通道 —— `AI_GENERATED_DESKTOP_SANDBOX.md` 的硬规则
"AI never outputs HTML, JavaScript, CSS" 仍然有效,不可因对标 WE 的 Web 类型而松动。

这条分界必须写进 skill 文档,否则 skill 会被试探着去写 HTML。

---

## 6. 分类重划建议

建议把 `docs/` 里所有"Image/Video/Web/Scene 四类"的表述统一改为:

```
桌面内容 = 载体 × 运行时

载体(ContentKind)
  ├─ 壁纸 Wallpaper      桌面图标之下,默认点击穿透
  └─ 组件 Widget         桌面图标之上,可交互,常驻

运行时(ContentRuntimeKind)
  ├─ Scene               声明式场景;内部按表现形态分为:
  │    ├─ 静态图         单图 + 可选缩放/色彩/视差
  │    ├─ 动态场景       组件 + 动画 + 绑定 + 粒子 + 后处理
  │    ├─ 视频           VideoRenderer 单轨循环
  │    └─ 3D 场景        需 2D/3D profile 区分 + 模型 + 灯光(未实现)
  └─ Web                 内嵌浏览器;仅限用户手工/第三方内容,AI 不得生成
```

同时建议:`RuntimeProfile` 保持"壁纸语义 / 组件语义"的现有含义不变,
2D/3D 作为 Scene 内部的**新维度**引入(名字待定,避免与 `RuntimeProfile` 混淆)。

---

## 7. 差距清单(进入 `TODO.md` 的依据)

按"能不能被 skill 转化为用户可见效果"排序,不按工作量排序:

| # | 差距 | 依据小节 | 用户能否感知 |
| --- | --- | --- | --- |
| G1 | 音频输入总线无数据源(`input://audio/*`) | 4.3 | 能 —— **分析内核已完成(B-2),缺 WASAPI 采集与宿主接线** |
| G2 | 指针/鼠标输入总线无数据源(`input://event/*`、`InputBinding`) | 4.3 | 能 —— **归一化已完成(B-2),缺宿主光标追踪接线** |
| G3 | Script 组件无解释器 | 4.1 | 能 —— **已用闭集响应曲线补上手感(B-3);通用解释器延后并记录触发条件** |
| G4 | 无 2D/3D 场景维度、无灯光、无雾、无模型加载 | 4.1 | 能 —— **维度/灯光/雾契约已完成(B-4);缺 3D 渲染器与模型加载器** |
| G5 | Video 未成为一类可直接生成的轻量产物 | 4.2 | 能 —— **原判断有误,该路径本就基本可用;已补 entry 校验与确定性生成(B-5)** |
| G6 | Web 音频监听 API 缺失 | 4.3 | 弱 —— **契约已完成(B-6);推送未接线,且频段数少于 WE** |
| G7 | 木偶形变 / IK / 物理缺失 | 4.1 | 弱 —— 属重型编辑器特性,与 5.1 定位冲突 |
| G8 | Workshop / 资产包 / DLC 分发链缺失 | 4.4 | 不感知 —— 5.1 定位决定不做 |
| G9 | skill 未接入产品(`src/` 零处引用 `skills/`) | 4.5 | 能 —— **已接入(B-1),待真机验收** |

G7/G8 建议**明确不做**,并写入文档说明理由,避免反复被提起。

G1/G2 的剩余部分是同一件事:**把已就绪的分析接到宿主上**。声明层、分析、归一化、发布器全部
完成并有测试覆盖,缺的只是 WASAPI loopback 采集与桌面宿主的光标全局追踪 —— 这两件都必须在
Windows 侧做,无法离线验证。建议作为同一项实施。

---

## 8. 本文件未验证的部分

- WE 2.8.42 移除 Application 的**确切版本号与日期**来自 Steam 更新公告,未在客户端复现。
- Web 壁纸的鼠标输入:WE 官方文档未覆盖,无法判断是能力缺失还是文档缺失。
- 场景音频可视化指南:官方自述 "still in progress",细节可能已变。
- "over a million" Workshop 条目为商店页措辞,非精确数字。
