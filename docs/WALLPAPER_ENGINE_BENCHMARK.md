# Wallpaper Engine 能力对标与 MiaoDesk 建设基准

- 更新：2026-10-03；替代 2026-09-20 的过期差距快照，旧内容由 Git 追溯。
- 目标：动态壁纸达到 Wallpaper Engine 水平，是专业版完成条件。
- 上游：[产品愿景](PRODUCT_VISION.md)、[总体规划](PROFESSIONAL_DESKTOP_PLAN.md)；任务状态在 [持续开发面板](CONTINUOUS_DEVELOPMENT_BOARD.md)。
- 本次核对官方资料与代码，未做两款产品同机实测，不宣称已达标。

## 1. 官方依据

Wallpaper Engine 文档列出粒子、时间线、音频响应、鼠标视差、SceneScript 与自定义 Shader 等能力。[官方概览](https://docs.wallpaperengine.io/en/scene/overview.html)

其效果库包括水流/波纹、植物摆动、局部动态、深度视差、色彩与光效，体现了成熟作品对组合能力的要求。[官方特效目录](https://docs.wallpaperengine.io/en/scene/effects/overview.html)

粒子支持纹理、运动与鼠标/音频响应；3D 模型支持模型、纹理和动画导入。[粒子说明](https://docs.wallpaperengine.io/en/scene/particles/introduction.html)、[3D 模型说明](https://docs.wallpaperengine.io/en/scene/models/introduction.html)

全屏应用场景可以停止壁纸并释放内存。[性能策略](https://help.wallpaperengine.io/en/performance/game.html)

对照测试另记录实际安装版本、设置与素材。网页说明不能代替效果和性能证据。社区规模、专有格式兼容和编辑器 UI 一致不作为完成门。

## 2. 当前基础与专业目标

| 能力 | 当前代码基础 | 待完成 | 任务 |
| --- | --- | --- | --- |
| 2D、图层/素材、参数 | Scene、D2D/D3D11、资产/参数已有 | 遮罩/混合/文字/矢量组合审计、比例与预览一致性 | WALL-01 |
| 深度/视差/形变 | 变换、输入、绑定已有 | 深度图、局部形变、边缘处理、效果配方 | WALL-02 |
| 动画 | 关键帧、循环、缓动、事件已有 | 状态组合、循环接缝、参数过渡、暂停恢复 | WALL-03 |
| 粒子/后处理/Shader | Particle、Render Graph、D3D11 模块已有 | 多 Pass/组合效果按后端核验，扩充效果库与预算 | WALL-04、ADV-04 |
| 音频/鼠标 | IndependentWallpaperHost 已发布；MiaoWallpaperAudioTap 已采集 | 跨屏归属、设备变化、暂停、质量、组合响应 | DESK-03/04、WALL-05 |
| Web 音频 | WebDesktopSurfaceChild 已推帧 | 真实页面收帧、接口/性能、暂停采集策略 | DESK-05、WALL-05 |
| 图片/视频/Web | WIC、Media Foundation、WebView2 与包入口已有 | 格式能力、循环/音画同步、恢复、质量档 | WALL-06 |
| 多屏/电源/自动规则 | Shell、monitor、performance/automation 已有 | 独立/跨屏布局、全屏/省电/锁屏与长期运行 | P0、PERF、WALL-06 |
| 官方包迁移 | 三个包已用 scene.json | 正式运行与真机效果/性能 | DESK-01 |
| 3D 网格/相机/材质/灯光/雾 | 声明与校验已有，未形成完整 3D 产品链 | 加载、真实渲染、动画、预算与作品 | ADV-01/02 |
| 骨骼/形变/物理 | 无完整产品链证据 | 运行时表达、参数、角色与交互形变作品 | ADV-03 |
| 程序化行为 | 闭集响应曲线/事件已有 | 有界行为组合、程序化视觉与隔离 | WALL-03、ADV-04 |
| AI 创作 | Skills、Creator、预览/候选/应用已有 | 能力知识、设计/素材/观察/修复和保留集 | CAP、AIP |

代码入口：`src/content/`、`src/desktop/wallpaper/`、`src/desktop/control/`；官方包：`assets/wallpapers/`。

## 3. 分类与范围

内容按载体 Wallpaper/Widget 与运行时契约 Scene/Web 区分。实际播放路径还包括图片解码和视频播放；轻量视频包复用现有支持，不因术语统一强迫视频进入完整 Scene。

Web 壁纸用于手工/第三方内容，当前 AI 默认产物为声明式 Scene 与受控素材。Widget 默认 Native；不能从模型存在 Web 枚举推导 Web Widget 已支持。

3D、形变、骨骼与受限物理从本次起进入专业版必达阶段，可以晚于基础版，但不得再以“重型编辑器特性”排除。引擎表达力与编辑器形态分别设计。

## 4. 达标方法

1. 按总体规划固定代表集、等价素材、复杂度与质量档，每类分别判定。
2. 同一 Windows 参考机比较构图、动态层次、节奏、交互、循环、可调性、稳定与资源成本。
3. 视觉盲评和性能测量分开；缺 WE 安装环境时只可完成自测。
4. 公开内容框架能复现效果，AI 可检索能力并制作/续改；官方专用效果不算作者能力达标。
5. S3 只声明所列 2D/2.5D 范围通过；完整专业目标还需 S6 高级集与 S7 证据。

指标与样本量见 [总体规划第 7 节](PROFESSIONAL_DESKTOP_PLAN.md#7-专业级验收)。

## 5. 旧差距迁移

| 旧 ID | 校正 | 当前任务 |
| --- | --- | --- |
| G1/G2 | 音频/指针接线已有，待运行与质量验证 | DESK-03/04、WALL-05 |
| G3 | 通用 VM 未实现；优先有界行为，程序化扩展按隔离门推进 | WALL-03、ADV-04 |
| G4 | 3D 声明已有，完整渲染进入专业版 | ADV-01/02 |
| G5 | 视频轻量生成/校验已有，撤回“半通” | WALL-06 |
| G6 | Web API/推帧已有，待真实使用验证 | DESK-05、WALL-05 |
| G7 | 形变/骨骼/物理纳入表现力目标 | ADV-03 |
| G8 | 社区规模不作门；资产/配方复用与包交付纳入 | CAP-02/03、PRO-04 |
| G9 | Skill 已接入，下一步为准确知识与可量化创作质量 | CAP-01/02、AIP |

旧“AI 尚未接入”“WASAPI 未做”“音频桥未推帧”等现状描述已撤回。校正实现状态不等于真机签收。
