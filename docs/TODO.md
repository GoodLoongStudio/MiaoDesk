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
4. **"只有 Windows 能跑"里裹着的往往是纯逻辑。** `MiaoSceneD2DRenderer::SelfTest`
   三个阶段连续两轮在 Windows CI 上红着,报的是"返回 false",而带标签的 Step 又因为
   我自己紧接着推送、被 concurrency cancel 掉,始终没读到原因。停下来在本机按同样的
   方式把那个包写一遍,根因三分钟就出来了:三个子包一个 `manifest.json` 都没写,
   `MiaoContentPackage::Load` 在第一行就拒了 —— 一步都没走到绘制。附带还挖出两条
   (manifest 声明了 parameters.json 就得有这个文件;manifest 的 kind 与 scene 的 kind
   必须一致,而且目录扩展名还得和 kind 匹配)。三条全是五十行以内的纯逻辑判断,
   只因为唯一能走到它的测试是 Windows-only 的,就花了三轮 90 秒的 CI。
   **可迁移的判据:看到"WIC / D2D / 真机才能验",先问一句"这个断言断言的是绘制,
   还是包的形状 / 数据结构 / 算术?"后者几乎总能在本机验。**
5. **推送频率会吃掉诊断。** 工作流的 concurrency 组是 `cancel-in-progress`,
   连着推两个提交,前一个的 build 会被 cancel 成 `cancelled` —— 于是那一轮猩红的
   Step 注解永远读不到。改了代码要等一轮跑完再改下一轮,尤其是当上一轮正是为了拿诊断。
6. **本地脚本本身也会报假绿。** 我那个"编译并运行测试"的助手脚本最后一句是 `echo`,
   把测试自己的退出码吃掉了;而编译失败时它会跑去跑上一个二进制,于是一份
   `ALL CHECKS PASSED` 是残留产物打印的。Gate 的退出码必须显式 `exit $rc`,
   失败路径上不能留下上一次的二进制。
7. **闸门自己也会有"什么都没验到"的模式。** `verify-workflow-paths.sh` 只解析
   `run: |` 多行块,不认单行的 `run: .\script.ps1`。后果是对某些工作流它一个引用都
   抽不到,于是**一条都没比对就报绿**。2026-09-22 就是靠这个空洞,三个从来没被任何
   步骤调用过的闸门脚本(`derived-view-gate` / `derived-views` /
   `theme-canonical-gate`)一直留在 paths 列表里而没人发现;连带的第二个缺口
   (`build-windows-arm64-exe.yml` 跑 `generate-miaomiao-icon.ps1` 却没把它列进
   paths)也同样被藏在里面。修好之后第一个跑出来的就是它自己。
   **判据:闸门的"通过"要能回答"我刚才比对了多少条"。零条比对不可能是通过。**
8. **一个探针只能代表它单独在场的那个东西。** `MiaoSceneD2DRenderer` 的 SelfTest 里
   "纯色 sprite 落在中心、圆角让四角留黑"这条断言,一直读的是**时钟字形的墨**:
   同一场景还有居中的白色 TextRenderer,而 `"HH:MM 晴"` 在 18px / 64px 盒子里比盒子
   宽、会 word-wrap 成两行,第一行落在 x∈[9.7,54.3]、y∈[10.4,32] —— 正好盖住断言里
   那个 `(17,17)` 探针。sprite 一直画得对。教训不是"探针选错了位置",而是
   **取样几何必须和受测对象单独绑在一起**:要验 sprite 的圆角,就得有一个没有文本的
   场景。混在一起的场景只能验"有东西画出来了"。
8. **失败自报比分阶段人工复现更值。** 那条断言第二次红的时候我没有继续猜,而是先
   在本机把它的**输入**全验了一遍(材质解析、绑定值、transform、以及按渲染器公式
   反算三个探针应处的明暗)。输入侧全对,就一步把范围缩到"只剩 Windows 上的绘制"。
   再往下,给断言加了无条件 dump 和 y=32 边缘扫描 —— 于是下一轮的失败自带结论。
   **"把断言改成会解释自己的"通常比"再猜一个修法"便宜。**

9. **两个后端各写一份规则,等于写了两份规则。** 给 D3D11 补贴图 sprite 时,顺手把
   仓库里八份 `scene.json` 的 sprite 形态全列出来对了一遍,结果发现 MiaoCloud 的五个
   图层是 `texture` + 无 `materialId` —— D2D 画得出来,D3D11 连包都加载不了
   ("does not resolve a material")。同类别的分叉还有三格(materialId 指向不存在的
   material、可编程材质无 texture、programmable + texture 两边都要 t0)。
   根因不是某一处写错,而是**同一个判断在两个文件里各有一份**。
   现在它是一份实现(`MiaoSpriteMaterialPolicy.cpp`),两个后端调用它,唯一允许的差异
   (`backendHasShaderPath`)是显式传进去的,并且被测试钉住:任何两个后端都接受的形态
   必须解析到同一个 path。
   **可迁移的判据:凡是"两个后端/两个平台各判一次"的逻辑,先问它们判的是不是同一件事;
   是,就合并成一份,并且给合并后的那份写一个 parity 断言。**
   这次也顺手照见一条:验证合并后的规则时,我把本次要修的 bug 原样注入回去
   (D3D11 上"texture + 空 materialId"被拒),2 项断言立刻红。**闸门注入已知失效后
   真的会响,才是闸门。**

10. **行尾差异会让门在本机全绿、Windows 全红,而报的是无关的嫌疑对象。**
    `c55ef67` 的 `canonical-derived-view-gate` 在 Windows CI 上红,报的是
    "RecentlyUsed() implementation not found." —— 而那个函数在同一个提交里刚刚修好,
    515/533/550 三处 `IsLibraryUiVisible(item) continue` 都在。把源文件转成 CRLF
    就在本机复现出完全相同的消息:那些门按 `\n` 定位"函数结尾 + 一个空行",
    `\n}\n\n` 在 CRLF 下永远匹配不上(空行是 `\r\n\r\n`)。
    教训不在正则,而在**仓库存 LF(`.gitattributes` 的 `* text=auto`)、runner 检出 CRLF**,
    所以这类门只可能在 Windows 上坏。修法是在**读入处归一化**,而不是改十几处正则。

11. **看门脚本自己也要先跑通一次,否则"静默"和"还在跑"完全一样。**
    我那个轮询 CI 的看门脚本,把 `python3 -c '...'` 嵌在 bash 单引号里,里面写的是
    `\"html_url\"` —— 单引号不转义,于是 Python 每轮都 SyntaxError,而脚本一句输出都没有。
    我隔一段时间去看它的输出文件,看到的是空的,判断成"还在跑"。它已经在 md 里写过
    "覆盖率:只 grep 成功标记的话,崩掉是静默的",结果同一个错以另一种形式又犯一次。
    现在:看门脚本单独放一个 `.py` 文件(不再嵌套引号),并且每次 poll 失败都打
    `ERROR  poll failed:` —— **让失败自己留痕**,而不是靠人去猜沉默意味着什么。
    附带代价:那个坏脚本每小时 60 次的配额被它自己烧光了。

12. **"闸门绿了"最容易被误读成"它证明的那件事成立"。** 写 skill 漂移门时连中三次同类:
    ①第一版按**全文**搜关键词,把正面提示词里"只有 `builtinName:"solidColor"` 一种能用"
    整句删掉,门仍然报绿 —— 因为"solidColor"这个词在反面提示词里还活着。
    ②按小节切分的实现写错了(partition 循环让除最后一节外每节都拿到剩下的全部文本),
    于是"正面小节必须有 X"永远被"反面小节也有 X"满足。
    ③切成小节之后同类仍在:同一个小节里换个说法,关键词照样命中。
    没有去堆一个能覆盖措辞的解析器(那是把工具做成编译器),而是**把上限写进脚本头部
    和它的输出** —— 成功信息现在逐条列出"只证明了这些",并显式写"这是已知上限,不是通过"。
    **判据:一个闸门的输出,能不能让外人正确说出它证明了什么、没证明什么?
    不能,那它的 ✅ 比没有门更坏 —— 它会把"没验"洗成"验过"。**
    另:这条规则我在教训 2 里已经写过("闸门写完必须注入一个已知失效"),这次仍然
    三个版本里只有一个真的会响。**要执行,不是记录。**

13. **一个从来没跑过的门,会攒下不止一层 bug,而它报的错指向完全无关的地方。**
    `1455b4c` 把 CRLF 修掉之后,`canonical-derived-view-gate` 还是红的,但报错从
    "RecentlyUsed() implementation not found" 变成 "Missing IsLibraryUiVisible() gate."
    —— 我据此判断"CRLF 修错了",**错了**。两层叠着,修好一层下一层才露出来。
    真正叠了三层:
      ① CRLF 未归一化;
      ② 单引号正则写成双反滑线 —— PowerShell 的转义符是反引号不是反斜杠,
         单引号里 `\\(` 原样进正则,被 .NET 读成"一个字面反斜杠 + 一个捕获组的开始",
         于是这条 pattern 在找一个**签名里带反斜杠**的函数;
      ③ `Get-FunctionBlock` 返回 `$match.Value`(string),四个调用点却都写 `$block.Value` ——
         PS7 上 `("hello").Value` 不报错,静默返回空串,于是每条断言都拿空文本比,
         稳定地报 "Search() must use shared canonical gate."
    最坏的是第 ③ 层:**它把门自己的缺陷伪装成产品缺陷**,而产品那段代码是对的。
    而这个门从 `61a638f` 到 `c55ef67` 第一次被工作流调用之间,一次都没成功过。
    **可迁移的判据:一个门的报错在点名别处时,先问"这个门自己跑通过吗?"
    没跑通过过的门,它说的每一个字都还不能信。**
    修完之后按教训 2 补了注入验证:分别从 RecentlyUsed / Favorites / Search 里
    删掉闸门行,四个门各自报错且点名那个函数;CRLF 检出上 5 个步骤全绿。

14. **"从来没跑过的步骤"会一个接一个地藏在最先失败的那一步后面,而且症状指向别处。**
    `canonical-derived-view-gate` 这一轮连修四次才绿,每一层都是"之前的步骤失败了,
    所以我一次都没跑过":
      ① CRLF 未归一化(教训 10);
      ② 单引号正则双反斜杠(教训 13);
      ③ `Get-FunctionBlock` 返回 string,调用点 `.Value` 在 PS7 上静默给空串;
      ④ 第 5 步 `Join-Path $RUNNER_TEMP ...` —— 裸写 `$RUNNER_TEMP` 是未定义的
         PowerShell 变量,不是环境变量;GitHub 把 RUNNER_TEMP 放在进程**环境**里,
         PowerShell 要 `$env:RUNNER_TEMP` 才读得到。于是 `Join-Path $null` 抛
        "Cannot bind argument to parameter 'Path' because it is null."
    **定位它靠的是一个朴素办法:让每一步自报姓名。** 给四个门加 `::notice::GATE-START /
    ::notice::GATE-OK`、失败加 `::error::GATE <名> -> <异常>` 之后,annotations 一眼
    就给出答案:四个门全是 GATE-OK,第 5 步连 GATE-START 都没有 —— 于是范围立刻缩到
    "它在调用门之前就死了"。此前我只有"整个 gate 跑了 18 秒"这一个信号。
    **匿名 API 读得到 check-run 的 annotations,读不到 job log。所以诊断信息要主动
    写进 annotations,不能指望去翻日志。**

15. **"SelfTest 存在"和"SelfTest 在跑"是两件事,而仓库里躺了一片没人调用的。**
    清点时发现三份:**`MiaoSceneD3D11Renderer::SelfTest`**(Windows 侧聚合器)零调用方,
    而它内部 `MiaoRenderGraph::SelfTest() && MiaoPostProcessCompiler::SelfTest() && ...`
    那七项**纯逻辑**自测因此也跟着从未执行 —— 合计约 290 行断言,讲的是渲染图、
    后处理编译、shader ABI 契约、GPU 参数打包、粒子运行时;外加
    `MiaoSceneRuntimeModel::SelfTest`(147 行,构建/参数/输入解析)连那个聚合器都没包含,
    也是零调用方。加上 `MiaoSceneSerializer::SelfTest`(120 行,粒子发射器)一共三处。
    同一类此前已犯过:D2D 的 SelfTest 150 行真实像素断言也是零调用方,接进 CI 后
    第一轮就抓出六个测试自身的缺陷。
    **判据:每加一个 SelfTest,同时给它一个调用方(测试目标 + runner + CI 步骤),
    并且注入失效证明它真的会响。** 这次两处都做了:
    删掉 `ValidateParticleEmitter` 的上限判断 → `SceneSerializerSelfTest` 立刻 FAIL;
    禁用 `MiaoPostProcessCompiler` 里一行校验 → `ContentSelfTests` 立刻 FAIL。
    还原后各自复绿。三处接完之后本机纯逻辑测试 11 → 13。
    顺带记录一个**不是**缺陷的发现:`MiaoParticleSerializer::SelfTest` 是
    `return true;` 的桩。它旁边真正该被覆盖的(`DeserializeEmitters` 只是转调
    `MiaoSceneRuntimeModel::Validate`)已经由新测试覆盖,所以桩保持原样。

16. **不要把"逐字节相等"的门,架在由超越函数算出的产物上。**
    MiaoCloud 的动画迁到关键帧轨之后,`generate-miao-cloud-scene.py --check`
    在 Linux CI 上连红三轮,而本机(Python 3.14 与 3.9 都试过)全绿。
    根因:`math.sin` / `math.cos` 的结果**依赖平台的 libm**,glibc 与 macOS 的 sin
    可能差 1 ulp,而 `json.dumps` 会把这个末位差原样写进 scene.json ——
    于是"在 macOS 上生成、在 Linux 上校验"必红,差异却是 1e-16 的相对量。
    **判据:凡是要提交进仓库、又被逐字节门守着的小数,先问它是怎么算出来的。
    乘加除是 IEEE 精确舍入、与平台无关;sin/cos/sqrt/exp/log 不是。**
    修法是把采样值按固定小数位 round(这里 6 位,即 1e-6 px,比一个像素还小七个
    数量级,远小于声明的 0.306px 误差上界),**不是**把门的容差放宽 ——
    放宽容差会让门再也抓不住真的分叉。
    顺带:breathe 的误差随后刚好压在解析上界上被判 FAIL,那是**上界算漏了一项**
    (还有 round 带来的 1e-6),不是迁移变差了。上界也要跟着写全。
    以及:我一开始连 `time` 也 round 了,结果末键向上进位越过 duration,被
    `Validate` 整scene 拒掉("Animation keyframe time is outside the track duration")。
    time 由乘除得来,本来就不需要 round。**"顺手一起 round"不是无害的。**

17. **"多提交了一个文件"是一整类没有任何闸门在看的缺陷 —— 因为现有闸门全都只问
    "这里的东西对不对",没有一条问"这里有没有不该在的东西"。**
    2026-09-22,`scripts/__pycache__/generate-miao-cloud-scene.cpython-314.pyc`
    跟着一个文档闸门的提交进了库。彼时有**十四个**闸门,无一报警:原生源码那条只扫
    `src/` 的形状(而 pyc 在 `scripts/`),暂存资产那条只管 `assets/*.mdwall`,
    CMake 那条问"CMake 编了什么"而不是"多出来了什么"。
    根因是 `.gitignore` 有 .NET / Node / CMake / IDE / OS / logs 各节,
    **唯独没有 Python** —— 而 `scripts/*.py` 早就在跑了,只是从没人在 `git add -A`
    之后看过一眼暂存区。
    **判据:每一步 `git add -A` 之后,暂存区里都可能混进工具链的副产物。
    加门时问的不是"我要查的那条规则有没有被违反",而是"这一类错误,
    现有门里有没有任何一条看得见"。**
    写这道门时我自己先犯了同一个毛病的变体:第一版按"所有二进制扩展名"扫,
    当场误报三个**故意**提交的二进制 —— vendored 的 `WebView2LoaderStatic.lib`
    与 `downloads/store/` 下的 Store 分发包。它们的引入提交本来就写明了意图,
    README 也登记了。所以判据必须区分**工具链顺带产生的副产物**(永远无可辩解,按名字一票否决)
    与**刻意引入的依赖/分**(正当,但要登记理由)。
    第三版才落到对的形状:**按区域登记**。由 git 自己判定哪些被跟踪文件是二进制
    (`git ls-files` 减去 `git grep -I` 的补集,共 24 个),再要求每一个都落在登记过的
    目录前缀下 —— `assets/`(产品图片)、`runtime/*/{node,goz}/`(锁版本的 vendored 运行时)、
    `third_party/webview2/lib/`、`downloads/store/` 等 9 个区域,每个都注明引入它的提交号。
    区域级比逐文件 allowlist 少一层维护,又比扩展名白名单多一层保证:新出现的区域会红,
    不管里面装的是什么扩展名(实测:一个新 `.zip` 落在未登记目录 → 红)。
    顺带被注入测试逼出一个真缺陷:第一版把解释器缓存也放在 `binaries` 里查,于是判据
    依赖了 git 的二进制启发式(靠 NUL/长度)。注入一个**不含 NUL**的假 `.pyc`,连打两轮
    都是绿的。改成按名字判之后这条路堵上了 —— **「必然成立的规则」不该架在启发式上。**

现在有**十五个**本机闸门,新增 C++ 或改动 CI 脚本后先跑:

| 闸门 | 命令 | 覆盖 | 不覆盖 |
| --- | --- | --- | --- |
| 交叉语法 | `scripts/verify-windows-syntax.sh` | 全部独立 TU 的类型/成员是否真存在 | Windows SDK、MSVC 与 mingw 的差异 |
| 纯逻辑测试 | `scripts/run-pure-logic-tests.sh` | 13 个测试目标真编译并运行通过 | 任何需要 Windows 的目标 |
| CMake 收录 | `scripts/verify-cmake-covers-sources.sh` | 磁盘上每个 `.cpp` 是否真的被 CMake 编译 | CMakeLists 的意图是否合理 |
| CMake 目标结构 | `scripts/verify-cmake-target-hygiene.sh` | 目标顺序 / foreach 一致 / 每个可执行目标都有链接 / MSVC 选项齐全 / 每个 `.cpp` 只有一个 owner | 链的库是否真是它需要的那个 |
| 原生源码形状 | `scripts/verify-native-source-hygiene.sh` | 源码是否依赖 cwd、是否绕过共享 AppPaths、目录形状、CMake 源文件是否都在 | 按反斜杠比对的目录 allowlist(那是 Windows 才成立的) |
| **本机产物入库** | `scripts/verify-no-build-artifacts.sh` | 版本库里有没有解释器缓存(**按名字判**);git 判为二进制的 24 个文件是否都落在 9 个登记区域;`.gitignore` 是否真的挡住缓存;登记区域是否已空(表过期) | 内容恰好是纯文本的 `.a` 落在 `src/` 下(那是源码形状门的事);未跟踪的产物;登记二进制的内容是否仍最新 |
| 冲突标记 | `scripts/verify-no-conflict-markers.sh` | 仓库里有没有未解决的冲突标记 | 无 |
| skill 白名单 | `scripts/verify-skill-allowlist.sh` | `kContentSkills` 与 `skills/` 是否一致 | CI 上真实的注入效果 |
| 工作流 paths | `scripts/verify-workflow-paths.sh` | 每个工作流的 `paths` 过滤是否覆盖它自己跑的文件 | 过滤模式是否过宽 |
| 媒体包离线 | `scripts/verify-media-package-offline.sh` | `CreateVideo`/`CreateImage` + `Validate`(CI 测试第 1 节) | 第 5 节;以及测试自己写 fixture 的方式 |
| 场景 fixture 一致性 | `scripts/verify-scene-fixture-parity.sh` | 贴图 fixture 的 scene / manifest / parameters 两份没有分叉 | Windows 那份是否真能画出来 |
| MiaoCloud 几何 | `python3 scripts/generate-miao-cloud-scene.py --check` | scene.json 与 scene.ini 逐字节一致 + 每层逆合成 assert | 动画与粒子(刻意未迁移) |
| 打包资产断言 | `scripts/verify-staged-wallpaper-assets.sh` | `stage.ps1` 的资产断言清单覆盖每个 scene.json 声明的资产 | `stage.ps1` 之外的拷贝路径是否完整 |
| **渲染后端一致性** | `MiaoDeskSpriteMaterialPolicyTest`(在 `run-pure-logic-tests.sh` 与 Windows CI 里) | D2D 与 D3D11 对"哪个 SpriteRenderer 能画"判断一致;10 个形态 × 2 个后端,含必须被拒的那些 | HLSL 与真实绘制(只有 Windows 能编译/跑) |
| **MiaoCloud 动画保真** | `python3 scripts/verify-miao-cloud-animation-parity.py` | 迁移后的关键帧轨逐点复现 scene.ini 的解析式运动,误差 ≤ 解析上界 | 粒子(刻意未迁移);真机观感 |
| **skill 材质规则** | `scripts/verify-skill-material-rule.sh` | `skills/` 是否说到渲染器真正执行的 sprite 材质规则(名字从代码读出,不手抄) | 措辞改写;同一个词在小节别处仍命中的情况 |
| 壁纸库派生视图(4 个 .ps1) | `packaging/windows/verify-wallpaper-library-*.ps1` | `WallpaperLibrary.cpp` 的 `RecentlyUsed`/`Favorites` 等派生视图仍是"用户可见"的那一份 | CRLF 之外的形状(已在读入处归一化) |

其中除交叉语法与纯逻辑测试外,都由 `.github/workflows/repo-hygiene.yml` 在 CI 跑 —— 它们不需要 Windows、也不依赖
构建能否通过,所以不该被构建类工作流挡住。

`MediaWallpaperPackageTest` 明确只能由 CI 覆盖:它链接 `WallpaperLibrary.cpp` →
`UnicodeProfileFile.h:72` 有 `static_assert(sizeof(wchar_t) == 2)`(Windows 配置持久化
要求 UTF-16 `wchar_t`),而 macOS 的 `wchar_t` 是 4 字节。这是产品设计约束。

**"待 Windows 编译"与"待真机验收"是两件事,不要互相顶替。** 前者问的是"能不能编过、
断言跑没跑",后者问的是"用户桌面上看到的是不是对的"。
`023aa299`(全绿)之后,凡是文件逐字节未变的项,前一个问题已经有答案,
不再写"未验证"。
2026-09-22 顺着这条把 B-1 / B-5 / P2-4 / P3-1 / P3-3 五处已经失效的"待 Windows 编译"
标记按证据改掉了 —— **把已验证的写成未验证,和把未验证的写成已验证同样失真。**

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

### P0-4 壁纸 `.mdwall` dogfood 补齐 🟡 部分完成(新增一个此前未记录的硬阻塞)

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
  3. ~~动画~~ ✅ **已迁移(2026-09-22)**。六种里五种落到关键帧轨,`background`
     是 `none`。两个此前没写入记录的发现:
     - **`blink` 不是间歇触发,是方波。** 原判断它与 `InputRisingEdge` 最接近
       是错的:legacy 是 `cycle = fmod(t+phase, blinkInterval)`,
       `cycle > blinkDuration` 时隐藏 —— "可见 duration 秒、隐藏其余"自我循环,
       与输入无关。它的 `rotation` 也不是动画:`speed=0` 使
       `wave = sin(phase) = sin(pi/2) = 1`,角度恒为 `rotation_amplitude`,
       写成一条永不变的轨反而误导,所以那是静态值。
     - **李萨如的两根轴必须拆到父子两个节点。** drift 的 y 用 `speed*0.77`、
       sway 用 `speed*0.81`,与 x 频率不同;而一条轨只能动一个完整属性,
       `position` 是 vec2 且 `PropertyAddress` 没有 `.x/.y` 寻址。
       节点变换沿 parentId 链连乘,父节点动 y、子节点动 x,合成即原曲线。
       代价:5 个图层节点变成 9 个(多 3 个 axis-y 父节点)。
     - 采样数取 16/周期。均匀采样 + 线性插值的最大误差是解析解
       `A*(1-cos(pi/15))`,在最大幅值 14px 上 0.306px —— 亚像素。
       这个数由 `scripts/verify-miao-cloud-animation-parity.py` 逐点复核
       (按引擎自己的 easing 与局部时间代码求值,不是按我的理解),
       已接进 `repo-hygiene.yml`。把采样数改成 4 复现过它报 6 项超界。
  4. 粒子:`[Particles]` → `ParticleEmitterDefinition` —— **刻意不做**。
     legacy 的粒子不是声明式的:`LayeredSceneRenderer.h:273-314` 按索引
     过程式生成(逐索引正弦抖动、`kPi` 拱形、`i%5` 分频的五类)。
     `[Particles]` 段里只有三个计数和两个不透明度,**没有每粒子的
     初速度/寿命/尺寸/颜色来源**。把发射器参数编出来等于替用户编一份视觉 ——
     正是这个仓库反复拒绝的"校验通过但桌面上不是你想要的东西"。
     已由 `BuiltinWallpaperPackages` 把 `emitterCount == 0` 连同理由钉住。
  5. ~~补 `parameters.json`~~ **不适用**。`MiaoContentPackage::Load` 只在
     `manifest.parameters` 非空时才要求这个文件存在(`MiaoContentPackage.cpp:461`
     的 `if (!manifest.parameters.empty())`),而 MiaoCloud 的 manifest 没有
     `parameters` 键 —— 没有参数要暴露,也没有文件要补。动画的振幅/速度/相位
     已是轨道里的定值,不是用户可调项。若将来想让用户改外观(比如"云飘多快"),
     那是**新增产品能力**,不属于 P0-4 的迁移范围。
  6. 从 `manifest.json` 删 `legacy_entry`,删除 `scene.ini`。
  7. 重跑 `verify-wallpaper-library-derived-views.ps1` 等 6 个脚本 ——
     需先确认它们的输入源是否仍指向 `scene.ini`。
  8. 验收:`MiaoSceneSerializer::Deserialize` + `Validate` + `Initialize` 通过,
     且初始化后 `scene.assets.size() >= 5`、`animations.size() >= 1`
     —— 这两条**已满足并由 `MiaoDeskBuiltinWallpaperPackagesTest` 每轮钉住**
     (5 资产 / 8 轨,推导写在测试注释里);
     最终"壁纸在真机上显示全部 5 层且眨眼动画生效"仍需真机。
  ## 新增:第三个阻塞 —— 两个包根本没有美术资源(2026-09-22 发现)

  走 `MiaoContentPackage::Load → MiaoSceneSerializer::DeserializePackage →
  MiaoSceneRuntimeModel::Validate → MiaoSceneRuntime::Initialize →
  MiaoAssetDatabase::Build` 这条链跑三个真包(新增
  `MiaoDeskBuiltinWallpaperPackagesTest`,纯逻辑,每轮都跑)时暴露:

  - **NeonCity.mdwall 与 MysticMoon.mdwall 里没有任何资产文件。** 目录下只有
    `manifest.json` / `scene.ini` / `scene.json` 三个文件。
  - 它们的 `scene.ini` 各声明 5 个图片图层(`assets/background.jpg`、
    `assets/city_glow.png` …、`assets/moon_glow.png` …)。
  - 而 git 历史里从来没有这两个路径下 `assets/*` 的记录(`git log --all` 为空),
    那些文件名在全仓库也搜不到。
  - 只有 **MiaoCloud** 真的带着自己的 5 张图(background.jpg / cloud.png /
    tail.png / cat.png / blink.png)。

  所以对那两个包,P0-4 的"内容迁移"不是写 scene.json 的问题 —— **没有素材可写**。
  这不属于开发工作能闭合的范围,需要补美术资产。`MiaoCloud.mdwall` 的 scene.json
  已填成 5 层,那两个仍是空壳,并且这件事被测试钉成了断言,不会被当成一次性发现。

  另注:`LayeredSceneRenderer.h::LoadBitmap` 在文件缺失时返回 false,而
  `DrawLayer` 是 `if (!layer.bitmap) return;` —— 即**缺图的图层被静默跳过**。
  这解释了那两个包今天在桌面上为什么"看起来还在跑":它们本来就在静默缺图。

  ## 已落地(2026-09-22)

  上面第三节的 D2D 渲染侧改动(见"实测证据(二)"之后的更新)+ MiaoCloud 内容迁移:

  - `scripts/generate-miao-cloud-scene.py` 从 `scene.ini` 生成 `scene.json`。
    几何映射由脚本算而非手写,并对每层做一次逆合成 assert 回原矩形;
    它当场抓到了"先 round 再 verify 会让宽度差 1e-3"。
    映射:`scale = (w/dw, h/dh)`;
    `position = (x - dw/2·(1-sx), y - dh/2·(1-sy))`。
  - `MiaoCloud.mdwall/scene.json`:1 个 root + 5 个图层节点,5 个 Image 资产,
    5 个 sprite 各带 `texture` 资产引用。
  - `MiaoDeskBuiltinWallpaperPackagesTest` 把三个包走完整链并断言。

  ## 已在真实 Windows CI 上验证(2026-09-22,`4b19282`)

  **`Windows x64 Build` 全绿:31 个步骤 success、0 失败、0 条 `::error::` 注解。**
  其中第 21 步 `Render a textured sprite through the real D2D backend` 通过 ——
  这是 `MiaoSceneD2DRenderer::SelfTest()` **第一次真正被执行**(在那之前它一个调用方
  都没有),于是贴图路径第一次有了执行级证据。实测值:

      centre       (32,32) BGRA = 204,102,51  覆盖率 1.000   ← solidColor(0.2,0.4,0.8)
      inner corner (16,16) BGRA =   0,  0, 0  覆盖率 0.000   ← 圆角外
      outer corner ( 2, 2) BGRA =   0,  0, 0  覆盖率 0.000
      y=32 扫描: 16..48 亮、其余暗             ← 0.5 缩放的精确范围

  即:sprite 的位置、缩放、颜色、圆角全部正确;贴图 sprite 也在同一个 SelfTest 里
  画出了包内 PNG 的颜色。另外两条拒绝路径(tint 作用于贴图、spatial:3d)
  都在 Windows 上验过会拒绝且报错点名组件/场景。

  这一段值得记的是**过程**:SelfTest 跑起来之后连续红了六轮,而六轮的根因全在
  **测试自己的管道**上,渲染器每次都是对的:
  1. 子包一个 `manifest.json` 都没写 → `Load` 第一行就拒;
  2. manifest 的 kind 与 scene 不一致、目录扩展名还得和 kind 匹配;
  3. 像素探针 `(17,17)` 取在圆角弧的**抗锯齿带**上(弧外 1.07px,覆盖率 9.3%),
     而判据是「任一通道 > 8」—— 正确的渲染器永远不可能让它通过;
  4. 断言在 `EndDraw()` **之前**读像素,读到的是上一帧的残留画面;
  5. 诊断行被 CI 步骤的 `Select-String` 过滤器(`FAIL|rror|...`)整行滤掉,
     白跑一轮什么也没读到;
  6. 带标签的 Step 第一次跑之前,报错只有一句"返回 false"。
  真正起作用的三个动作:把断言拆成会自报名字的 Step、**在本机先验断言输入**
  (材质/绑定/transform/按渲染器公式反算探针明暗)、以及上一轮那次文本覆盖的算术。

  ## 仍未完成 / 未验证

  - **真机桌面验收未做**:多显示器 / 多 DPI / click-through / 资源占用仍要真机。
    Windows CI 证明的是"渲染器在离屏位图上画对了",不是"用户桌面上看到对了"。
  - **非白色 `tint` 作用于贴图 sprite 在 D2D 后端被显式拒绝**(报错点名组件)。
    原因与三种被否的权宜做法见渲染器注释。
  - **NeonCity / MysticMoon:缺 10 张美术资产**(上面第一节)。
  - **动画与粒子按设计留空,且被测试显式记录为缺口**:`scene.ini` 的六种动画全是
    解析式正弦(`drift` 还让 y 轴用 `speed*0.77` 的另一个周期),场景动画是线性
    关键帧轨;要在"完全复现"与"循环处连续"之间取舍属于要看真实桌面效果的决定。
  - **`legacy_entry` 刻意保留**:切入口需要真机验收,不在一台编译不了的机器上猜。
    (原清单第 7 条"重跑那 6 个脚本、确认输入源是否仍指向 scene.ini"**已完成**:
    逐个查过,`verify-wallpaper-library-*` 那四个读的是 `WallpaperLibrary.cpp` 而不是包,
    `verify-wallpaper-theme-canonical-gate.ps1` 只读 `manifest.json` 的 id/kind/runtime,
    没有一个读 `scene.ini`。所以删 `legacy_entry` 与 `scene.ini` 不会破坏它们。
    —— 但那个闸门此前从未被调用过,已接进工作流并改成 push + PR 都触发。)
  - ~~D3D11 后端仍没有经 `texture` 属性的贴图路径~~ **这条已过期,2026-09-22 更正**。
    它在 `0937328` 就落了地:`MiaoBuiltinTextured` 像素着色器、按 sprite 自己的
    `texture` assetReference 取图(`input.textureAssetId = texture.id`)、绑 t0、
    经 `ResolveSpriteDrawPath` 与 D2D 共用同一份策略。留着"没有这条路径"的记录
    比没有记录更糟 —— 它会让人去重写一份已经存在、而且已经被共享策略钉住的代码。
    真正仍未做的是**执行**:没有任何一步把一个贴图 sprite 真的渲染过 D3D11 路径,
    HLSL 只在被 `D3DCompile` 编译这个意义上成立过。
- **状态**:🟡 渲染侧阻塞的**两个半边**都已落地 —— D3D11 半边在 `0937328`,
  但**只到"本机能验证的那一层"为止**,尚未在 Windows 上编译或运行过。
  落在本机证据范围内的:材质策略合并为一份 + 16 项断言 + 注入已知失效确认闸门会响
  + 12 个纯逻辑测试全过 + 交叉语法门 114 文件零真实错误。
  不在范围内的:**HLSL 由 `D3DCompile` 在运行时编译,只有 Windows CI 能证明它编得过去**;
  真机截图没有人看过。此前"`4b19282`,31 步全绿"是 D2D 半边的实测证据,D3D11 半边没有对应物。
  补充(`9a03f26` 与 `6c8321a`,`0937328`):`build` / `installer` / `package` /
  `package-msix` / `scan` / `verify` 全部 success —— 即**新代码在 Windows 上编译链接通过、
  新测试目标 `MiaoDeskSpriteMaterialPolicyTest` 被构建**,`Verify the two render backends
  agree on which sprites are drawable` 这一步走的是 `windows-x64-build.yml`,它在那几轮
  同为 success。HLSL 仍未被执行(没有真渲染步骤跑到 D3D11 的贴图路径)——
  **"编过去了"不等于"画出来了"**,后者仍然只有真机能给。
  契约校验在 `f41903e`,D2D 绘制与 MiaoCloud 内容迁移在 `6b3677b` 之后陆续落地。
  同一轮还把 `skills/` 的 sprite 材质规则补齐(此前它只写"material 优先引用 builtin",
  在教作者写渲染器会拒的包),并加了 `verify-skill-material-rule.sh` 让规则不脱钩。

  走的正是上面第 1 条里的第二个选项:"在 `AssetType::Image` 与 `SpriteRenderer`
  之间开一条直接引用路径",没有新增 builtin 材质 ——
  `PropertyType::AssetReference` 早就存在,`MiaoAssetDatabase` 也早就在沿组件属性
  收集资产依赖,所以缺的只是一条校验规则和 D2D 的绘制路径。

  已落地:
  - `MiaoSceneModel::Validate` 强制 spriteRenderer 的 `texture`(assetReference)
    非空时必须指向一个真实存在的 `AssetType::Image` 资产,报错点名 asset id。
    (该校验器是加载链的必经点:`Deserialize` → `MiaoSceneRuntimeModel::Validate`
     → `MiaoSceneModel::Validate`)
  - `MiaoD2DTextureLoader`(新)WIC 解码 → `ID2D1Bitmap`,8192/128MiB 上限
    (比 D3D11 的 16384/512MiB 紧,理由写在该 .cpp 里)。
  - `MiaoSceneD2DRenderer` 用 **bitmap brush** 走 FillRectangle/FillRoundedRectangle,
    transform / opacity / cornerRadius 对贴图 sprite 全部继续生效;按 asset id 缓存,
    `Reset()`(即 D2DERR_RECREATE_TARGET 后的重载路径)清空。
  - **真机证据补齐了此前最大的一个洞**:`MiaoSceneD2DRenderer::SelfTest()` 有 150 多行
    真实像素断言,却**从来没有任何地方调用它**。已加 `MiaoDeskSceneD2DRendererTest`
    把它挂进 CMake 与 CI(Windows)。同时补了贴图 sprite 的 fixture 与断言。

  **仍未完成 / 未验证**:
  - D2D 侧的贴图绘制**未在真 Windows 上编译运行过**,只有本机 mingw 交叉语法门 +
    本地 fixture schema 门。真机验证见上面第 8 条。
  - **非白色 `tint` 作用于贴图 sprite 在 D2D 后端被显式拒绝**(报错点名组件),
    不是静忽略。原因:\`ID2D1BitmapBrush\` 没有颜色成员,普通
    \`ID2D1RenderTarget\` 既不能设混合模式也没有 effect API,一条 pass 内无法给位图
    染色。三种权宜做法都被否(理由写在渲染器注释里)。
  - ~~D3D11 后端没有经 \`texture\` 属性的贴图路径~~ **已落地(`0937328`,待 Windows 编译)**:
    新增 \`EngineTexturedPixelShader()\` 采 t0,\`CreateTextures\` 从 sprite 的 texture 资产填 t0,
    \`CreateShaders\` 按 \`textured\` 选 shader;**故意不预乘 alpha**(混合阶段做,shader 里
    再做一次会让透明像素周围出黑边,且 \`MiaoD3D11TextureLoader\` 解的是非 PBGRA)。
    \`tint\` 在两边分叉这一点**依然是分叉,而且是允许的**:D2D 的 \`ID2D1BitmapBrush\`
    没有颜色成员,非白色 tint 显式拒绝;D3D11 的 tint 是 shader 常量,免费。
    content-review 把它当差异记录,别当成 bug。
  - ~~内容迁移(第 2–7 条)~~:第 2 条(几何 + 5 层 + 5 个 Image 资产)已落地,
    第 3 条(动画)已迁移并被逐点复核;第 4 条(粒子)刻意不做、第 5 条不适用;
    第 6 条(`legacy_entry` 切换)
    刻意保留,需真机验收。

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
  **已由 Windows CI 验证编译与运行**:`ContentSkillLoading.cpp` 自 `023aa299`(全绿那次)起
  逐字节未变,而那次构建包含 `Verify content skill loading gate` 这一步并 success,
  所以"能在 Windows 上编过 + 7 组 dispatch 断言真的跑过"已有证据。此前写的
  "完整编译未验证"在那之后已失效。
- **验收**:真实 Windows 上,用户在对话面板说「做一个有飘落落叶的动态壁纸」→ AI 调用
  `content_skill_get` → 产出 `.mdwall` → `wallpaper_validate_package` 通过 →
  `desktop_preview_wallpaper` 预览可见 → 用户点 Apply 后桌面出现该壁纸。全程零手写文件。
- **状态**:🟡 已实施并已通过 Windows 编译与 CI 运行;**仍待真机验收**(验收标准见上一条)

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
- **补上"渲染器要拒绝"这一环(2026-09-22,commit `ed2f381` 之后)—— 一个静默失败**:
  此前整条链是:`spatial:"3d"` + `lights[]` 是**合法内容**,
  `MiaoSceneRuntimeModel::Validate` 会接受,两个渲染器也会 Load 成功,
  然后**按 2D 平着画、把每一盏灯和雾都静默丢掉**。
  也就是说声明层明明已经有"lights/fog 非空而 spatial != ThreeD 直接校验失败"这条
  (理由是"接受后静默丢弃会让作者反复问'为什么灯不亮'"),
  但**反方向**那条没人管:合法的 3D 场景流到一个没有投影、没有深度缓冲、
  没有网格加载器的后端,是静默降级。
  两个渲染器现在都在 `Load` 里拒绝 `spatial:3d`,报错点名场景 id。
  (D3D11 那份报错明说 3D 的"计划归属地"是它、但它的文件里同样没有投影矩阵 ——
  "打算在这儿做"不等于"已经做了"。)宿主会把 Load 失败降级到内置壁纸,
  所以它呈现为一次可见的失败,而不是桌面上少一圈辉光。
  已用产品自己的校验器确认那个 fixture 是合法内容:反序列化 + Validate + Initialize
  全部接受,所以被拒是渲染器的责任,不是校验的功劳。
  **仍未做**:3D 渲染器本体、OBJ/FBX 模型加载器、FBX 骨骼动画、PBR 材质集、
  雾的着色实现。这些仍是渲染侧工作,本机无法验证。
- **状态**:🟡 声明层 + 校验 + 序列化 + 渲染器显式拒绝已完成并测试;
  **3D 渲染器 / 模型加载器未做**

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
  **已由 Windows CI 验证编译与运行**:`MediaWallpaperPackage.cpp` 自 `023aa299` 起未变,
  而 `Verify media wallpaper package gate` 这一步在那次全绿构建里 success。
- **验收**:skill 产出"一个 manifest + 一个视频资产"的 `.mdwall` →
  `wallpaper_validate_package` 通过 → 库导入为 Video 项 → 预览循环播放。
- **状态**:✅ 代码已完成(2026-09-20),已通过 Windows CI 编译与运行;**待真机验收**

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
- **宿主已真正推送帧(2026-09-22)**:当初的阻碍是"`WebDesktopSurfaceChild` 到分析器之间
  没有连线,而那需要 WASAPI 先落地"。B-2 的 `MiaoWallpaperAudioTap` 落地之后这个阻碍
  消失了 —— web 桌面 surface 是**独立进程**,所以它自己持有一个 loopback 客户端,
  不去等独立壁纸宿主喂它。现在是 `SetTimer` 16ms 一拍,读 `LatestFrame`、
  `BuildAudioBridgeEnvelope` 造信封、`PostWebMessageAsJson` 发出去。
  `Pause` 时跳过读帧与投递;tap 起不来不杀 surface ——
  没有采集设备的机器照样要显示网页内容。
- **一处刻意没做,记在这里免得它躲在注释里**:`Pause` 时**没有**停掉 loopback 客户端。
  `MiaoWallpaperAudioTap::Start()` 没有把自己写成可重入,而"暂停/恢复时重启采集线程"
  这条路径在本机没法测 —— 于是代价是一个暂停中的 surface 仍然占着一个 WASAPI
  loopback 客户端(可能挡到别的应用)。正确的修法是补上这条,但要用真机验证,
  不是靠猜。没有写成"暂停即停"那样的注释,因为代码并没有那么做。
- **信封单独做成纯函数**(`src/include/miaodesk/WallpaperWebAudioEnvelope.h`),
  因为 B-6 的契约有两份实现而**没有编译器在检查它们之间的关系**:宿主侧这个构造器,
  页面侧 `normalizeFrame()`。对不上的表现是"页面什么都收不到",而 shim 的丢弃路径是
  静默 `return`。做成纯函数就能在每台机器上测,而不是只在跑 WebView2 的地方测。
  三处容易错的地方都写在头注释里,其中两处已经咬过:
  - **locale**:`ostringstream` 跟全局 locale 走,逗号小数点的机器上会发出 `"level":0,5`,
    页面 `JSON.parse` 抛 —— 而且**只在那台机器上**。函数内 `imbue(std::locale::classic())`。
  - **精度**:第一版注释写"四位小数"而代码没写 `setprecision`,实际落在 6 位。
    注释与代码不符,正是 `verify-doc-code-citations.sh` 那一类问题的人肉版。
- **契约两头对上的验证**:`tests/WebAudioEnvelopeParity.mjs` 编译并运行
  `tests/WebAudioEnvelopeDump.cpp` —— 也就是真的调 `BuildAudioBridgeEnvelope` ——
  把它的真实输出喂给真的 shim,再比对监听者收到的值。4 组样本 × 3 条断言
  (信封被接受 / 值一致 / 退订后不再收到)。已接入 repo-hygiene。
  四向注入验证:beat 发 1/0、字段名改名、spectrum 少发一条、精度降到一位,分别按预期的
  原因变红(`scripts/inject-audio-envelope-failures.sh`)。
- **验收**:一份手工 Web 壁纸调用 `wallpaper.registerAudioListener`,播放音乐时
  每帧收到 5 频段 + 16 频谱桶;退订后不再收到;另一个故意抛错的监听者不影响它。
  前两条已由上面的 node 契约门覆盖;**真机播放音乐仍未验**。
- **一处已知代价,不是疏漏**:宿主不知道页面有没有注册监听者,而契约**刻意**不让
  page→host 说话("page → host 什么都调不了",这是 web surface 拒绝导航/DevTools/
  上下文菜单/新窗口/全部权限请求的那套安全姿态的一部分)。于是没人监听时,
  `PostWebMessageAsJson` 仍然每 16ms 送一帧,由 WebView2 收下再丢掉。
  要消掉它只有两条路:开一条 page→host 通道(否掉整个安全姿态),或者把频率降到
  牺牲 listening 时的平滑度。两条都不划算,所以按现状记在这里 ——
  而不是让下一个人以为这是漏了一处 `if`。
- **状态**:✅ 契约 + shim + 测试 + 宿主注入 + 信封 + 宿主推帧均已完成;
  **仅剩真机验收(播放音乐、听声辨形)**

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
  **已在 Windows CI 上编译**:`Windows x64 Build` #367 / `023aa299` 全绿,而 `PiRuntime.cpp`
  与 `ApiRuntimeProfile.h` 自那时到现在**逐字节未变**(`git diff 023aa299 HEAD` 为空),
  所以那次绿构建覆盖的正是这几行代码。没有独立测试步骤 —— 需要的是"能编过 +
  `ReloadConfig()` 认得这两个字段",后者至今没有断言。
- **遗留**:未配置时的默认值仍是 128000 / 16384,这个默认值本身是否合理待评估(见 P3-5)。
- **状态**:✅ 已实施并通过 Windows 编译;默认值合理性仍开放

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
  **已在 Windows CI 上编译**:`NativeTools.cpp` 自 `023aa299`(全绿那次)起逐字节未变,
  所以那次构建已经证明删除之后仍然编译并链接通过。
- **状态**:✅ 已实施并通过 Windows 编译

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
- **状态**:✅ 已合入主干、恢复为正式分支、并推回远端(2026-09-22)

### P3-3 本地 AI 文档占位版本号

- **依据**:`LOCAL_AI_DEPLOYMENT.md` §10
- **内容**:容器 TAG、模型 SHA-256、`--structured-outputs-config.enable_in_reasoning` 参数名均为占位,
  部署当天需从官方 playbook 与 Hugging Face 核对。
- **结论:按设计延后,不是漏做(2026-09-22 核实)**。`LOCAL_AI_DEPLOYMENT.md` 已经在
  §10「本手册未验证的部分」里逐条列明这三项,§1.1 明写"不要使用 `:latest`;部署完成后
  把实际使用的 TAG 记录到部署台账",§6 清单有"容器 TAG 与模型 SHA-256 已记录到
  部署台账"这一项。也就是说这份文档**刻意不预先钉版本** —— 把没验证过的版本号
  填进去,会把准确的"未验证"变成失真的"已验证",比留着占位更坏。
  真正剩下的是部署当天跑 §6 清单,而那被 DGX Spark 卡住。
- **顺手补了一道门**:`scripts/verify-doc-code-citations.sh`(已接 repo-hygiene)。
  文档里的"源文件加行号"式引用会随代码改动静默腐烂,而现在 33 处全部有效且被钉住。
  它只查"这一行还在不在",不查"这一行说的是不是那件事" —— 这一点写在了脚本头部。
- **状态**:🟡 按设计延后到部署当天(文档已如实标注);引用腐烂风险已用门挡住

### P3-4 分支恢复的误删修复记录

- **依据**:本会话操作失误
- **内容**:我曾在验证方法有缺陷(zsh 变量不分词导致路径比对静默失效)的情况下删除了 42 个远程分支,
  其中 3 个含有未合入工作。已全部救回本地,但**尚未推回远端**。
- **已完成(2026-09-22)**:三个分支已推回远端。
  推之前先验了"推回去不引入任何内容":三个分支各自
  `git rev-list --count main..<branch>` = **0**、且 `git merge-base --is-ancestor <tip> main`
  全部为真 —— 即 tip 早已在 main 历史里,推回去只是把书签复位,不会带来任何
  未合入的提交。三个:`feat/content-widget-settings`(a431f58)、
  `fix/content-widget-install-runtime-reload`(df7d81d)、
  `fix/unicode-wallpaper-theme-packages`(321c39f)。
- **状态**:✅ 已推回远端
### P3-6 D3D11 渲染器的 Windows-only 自测 ✅ 已有调用方并在 Windows CI 执行通过

- **依据**:2026-09-22 清点 SelfTest 调用方时发现(见教训 15)
- **当时的现状**:`MiaoSceneD3D11Renderer::SelfTest()` 零调用方,而它里面这四项是 Windows-only:
  - `MiaoD3D11ParticleRenderer::SelfTest`
  - `MiaoD3D11TextureLoader::SelfTestPathPolicy`
  - `MiaoD3D11RenderTargetPool::SelfTest`
  - 文件内的 `TransformMathSelfTest`
- **第 1 步已完成(2026-09-22):建立 `MiaoDeskSceneD3D11` 库。**
  四个 `.cpp` 从 `MIAODESK_WALLPAPER_SOURCES` 搬到 `MIAODESK_SCENE_D3D11_SOURCES`,
  新库加入公共属性 foreach、MSVC 选项段、`source_group`,并由 `MiaoDeskWallpaper` 链它 ——
  与既有的 `MiaoDeskScene2D` 逐处对称。这一步就是"做法(下一步)"里写的那件事。
  (当初拦路的正是它:建库要改产品主程序的链接结构,而本机编不了 Windows。)
- **这一步在本机验到了哪一层(以及没验到哪一层)**:
  本机跑 CMake Configure 能生成 compile_commands.json,于是把四个 TU 在搬迁**前/后**的
  编译命令逐 token 比对 —— 归一化掉必然不同的产物名之后,**14 个 token 顺序与取值完全一致**。
  唯一真正消失的是 `-isystem third_party/webview2/include`:它们原先作为可执行目标的
  源文件继承了它,搬进库之后不再继承。而这一项的消失是可证明无害的 ——
  四个文件的传递依赖闭包共 19 个头,没有一个引用 WebView2;且 `MiaoDeskScene2D`
  这两个渲染器文件本来就在没有该 include 的情况下编译通过。
  链接侧:`MiaoDeskSceneD3D11Renderer.cpp` 里真的调用了 particle / rendertarget /
  textureloader(分别出现 3 / 4 / 5 次),而 `IndependentWallpaperHost.cpp`
  在 `MIAODESK_WALLPAPER_SOURCES` 里引用 renderer,所以对象链会被拉进最终链接。
  写这一步时被 `verify-cmake-target-hygiene.sh` 当场拦住一次:新库漏了 MSVC 段,
  缺 `/W4 /permissive- /utf-8 /EHsc`。
  **没验到的:MSVC 实际编译与最终链接。** 本机没有 MSVC,compile_commands 的
  toolchain 是 host 默认(那个 `-DCMAKE_VS_PLATFORM_NAME=x64` 在非 Windows 上被忽略),
  它证明的是"CMake 给这四个 TU 的编译环境没变",不是"MSVC 编得过、链得上"。
- **第 2 步已完成(2026-09-22):`MiaoDeskSceneD3D11Test`。** 参考
  `src/tests/SceneD2DRenderer.cpp` 的形状,链 `MiaoDeskSceneD3D11` 而不是再把它的
  `.cpp` 编一遍。调用的四个:
  `MiaoD3D11TextureLoader::SelfTestPathPolicy` / `MiaoD3D11ParticleRenderer::SelfTest` /
  `MiaoD3D11RenderTargetPool::SelfTest` / `MiaoSceneD3D11Renderer::SelfTest`。
  **逐个报而不聚合成一个布尔** —— "D3D11 自测失败"不告诉你是哪一类,
  而四个里三个是公开静态成员、本来就分得开。
  第四个(聚合器)是 `TransformMathSelfTest()` **唯一**的入口:它在
  `MiaoSceneD3D11Renderer.cpp` 里是文件局部的,没有别的路能进去。调它也会重跑
  `ContentSelfTests` 已经在每台机器上覆盖的那七个纯逻辑自测 —— 这点冗余是到达
  `TransformMathSelfTest` 的代价,写进注释了。
- **一处必须更正的表述:这四个自测本身是纯逻辑,一个都没碰 D3D11 设备。**
  逐个读过实现之后:`MiaoD3D11ParticleRenderer::SelfTest` 是 `sizeof` 加两个常量比较;
  `MiaoD3D11TextureLoader::SelfTestPathPolicy` 是三个路径字符串判断;
  `MiaoD3D11RenderTargetPool::SelfTest` 是尺寸算术;`TransformMathSelfTest` 是矩阵乘法。
  它们之所以只能在 Windows 上跑,是因为**实现所在的 `.cpp` include 了 `d3d11.h`**,
  不是因为需要显卡。我先前在 `run-pure-logic-tests.sh` 里写的理由是"要真实 D3D11 设备",
  那是从 D2D 那条照抄过来的,**错**。理由写错的代价很具体:它让人以为这里需要一个 GPU,
  而真正的改进方向是把那几个纯逻辑自测搬进一个不含 `d3d11.h` 的文件,
  让它们回到每台机器都能跑 —— 那才是这一类问题的正解,和教训 15 是同一件事。
- **顺序是按记录执行的,而且记录是对的**:第 1 步的库先要证明 MSVC 编得过、链得上,
  才允许有测试目标依赖它 —— 否则真出链接问题分不清是谁引入的。证据是
  `7109050` / `a1348b06` 两次 `build` 全绿,**然后**才建这个目标。
- **顺带补了两处此前不显眼的漏**:
  1. `MiaoDeskWebAudioEnvelopeTest` **从来没进过 Windows CI 的 `--target` 列表** ——
     它只在 macOS 本地跑过。现在建了也跑了。
  2. `run-pure-logic-tests.sh` 的"只能在 Windows 上验证"清单只列了 3 个,
     而 `SceneD2DRendererTest` 与新的 `SceneD3D11Test` 既不在跑清单也不在跳过清单。
     于是"14 跑 + 3 跳过"看着像覆盖了全部,实际有 19 个目标。改成 5 个并写明原因。
- **四个自测已经真的跑过并返回 true(2026-09-22,`65dbc31e` `build` success,0 条失败标注)**。
  这一步是干净的:两个新步骤都挂在 `windows-x64-build.yml` 的 `build` job 里,
  任一步失败都会 `throw`,所以 job 绿 = 两个 exe 都被找到、都以 0 退出。
  也就是说这四个自测自写下以来**第一次执行**,并且通过。
  一处必须说清的边界:GitHub 的 Windows runner 是虚拟机,D3D11 设备多半由 WARP
  软件光栅器支撑,不是真实 GPU。所以验到的是"设备能建、四条自测的逻辑在 Windows
  原生路径上成立",**不是**"在用户显卡上画面正确"。后者属于 P0-1。
- **状态**:✅ 第 1、2 步均完成并已在 Windows CI 执行通过;**真实 GPU 上的画面仍属 P0-1**

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
| 2026-09-22 | 五处已失效的"待 Windows 编译"标记 | 按证据改掉:`023aa299` 全绿之后文件逐字节未变的项,"能不能编过"已经有答案(B-1 / B-5 / P2-4 / P3-1 / P3-3)。区别:`待真机验收`仍然保留 |
| 2026-09-22 | 自报姓名的 gate 包装 | `Invoke-WallpaperGate`(写 `$RUNNER_TEMP` 文件、每步 dot-source):`::notice::GATE-START/OK` + `::error::GATE <名> -> <异常>`。第一次加 `::error::` 仍然什么都看不到,因为失败在 try/catch **之外** |
| 2026-09-22 | 第 5 步 `$RUNNER_TEMP` → `$env:RUNNER_TEMP` | 裸写环境变量在 pwsh 里是 `$null`,`Join-Path $null` 抛"Path 为 null"。该步自加入起一次都没成功跑过(`9a03f26`) |
| 2026-09-22 | `canonical-derived-view-gate` 首次在 Windows 上通过 | 五步全绿(`9a03f26`)。此前它自 `61a638f` 起从未成功运行过 |
| 2026-09-22 | 派生视图门的三层叠bug | `verify-wallpaper-library-derived-view-runtime.ps1`:单引号正则双反斜杠 + `.Value` 作用在 string 上静默返回空串(`34f839e`) |
| 2026-09-22 | skill 补上 sprite 材质规则 + 漂移门 | `content-package-basics` 正面/反面、`content-review` 清单;`verify-skill-material-rule.sh`(15 条按小节比对,名字从代码读出)。此前 skill 在教作者写渲染器会拒的包 |
| 2026-09-22 | 七项内容层自测首次执行 | `MiaoRenderGraph` / `MiaoPostProcessCompiler` / `MiaoPostProcessShaderLibrary` / `MiaoShaderContract` / `MiaoGpuParameterPacker` / `MiaoParticleRuntime` / `MiaoSceneRuntimeModel` —— 全部只经由一个无人调用的 D3D11 聚合器可达。纯逻辑,已放进 `run-pure-logic-tests.sh`(`ContentSelfTests`) |
| 2026-09-22 | 修正 libm 末位差导致的假红 | 采样值 round 到 6 位;`--check` 改为打印差异;time 不 round(进位会越过 duration)。连红三轮的根因是平台 libm,不是分叉 |
| 2026-09-22 | P3-4 分支推回远端 + P0-4 第 5 条核实关闭 | 三个分支 tip 早已在 main 历史里,`rev-list --count main..b` = 0,推回只是复位书签;`parameters.json` 不适用 —— loader 只在 manifest 声明时才要求它 |
| 2026-09-22 | `__pycache__` 入库 + 新的产物门 | 一个 `cpython-314.pyc` 跟着文档闸门的提交进了库,而 14 个闸门无一报警 —— 它们全都只问"这里的东西对不对",没有一条问"这里有没有不该在的东西"。根因是 `.gitignore` 缺 Python 一节。新版闸门两档:缓存按名字一票否决,其余二进制由 git 自己判定后要求落在 9 个登记区域。六向注入验证(干净绿 / 含 NUL 的 pyc 红 / 不含 NUL 的 pyc 红 / src 下 .a 红 / 未登记目录的新 .zip 红 / 删掉 .gitignore 的 Python 节红)|
| 2026-09-22 | P0-4 动画迁移 + 保真复核 | 4 个动画层 → 8 条关键帧轨;李萨如双轴拆到父子节点靠变换连乘合成;`verify-miao-cloud-animation-parity.py` 按引擎语义逐点比,最大误差 0.306px(上界内)。修正了自己两个错:breathe 的 y 频率与 blink 的相位 |
| 2026-09-22 | P3-6 第 1 步:`MiaoDeskSceneD3D11` 库 | 四个 D3D11 渲染器 `.cpp` 从 wallpaper EXE 源清单搬进新库(与 `MiaoDeskScene2D` 逐处对称),解除"四个 SelfTest 零调用方"的结构性阻碍。本机把四个 TU 搬迁前后的编译命令逐 token 比对:归一化产物名后 14 个 token 完全一致;唯一消失的 webview2 `-isystem` 已用 19 个头的依赖闭包证明无害。被 `verify-cmake-target-hygiene` 拦住一次(漏 MSVC 段)。**MSVC 实编与最终链接仍未验** |
| 2026-09-22 | P3-6 第 2 步:`MiaoDeskSceneD3D11Test` | 四个 Windows-only 自测首次有调用方(逐个报,不聚合成一个布尔);`TransformMathSelfTest` 经聚合器进入 —— 它是文件局部的,没有别的入口。按记录的顺序做的:先有两次 `build` 全绿证明库链接不变,才建依赖它的目标。顺带发现 `MiaoDeskWebAudioEnvelopeTest` 从未进过 Windows CI 的 `--target` 列表,以及本地 runner 的跳过清单漏了两个渲染器目标 |
| 2026-09-22 | B-6 宿主开始真正推送音频帧 | 阻碍解除:B-2 的 WASAPI 已落地,而 web 桌面 surface 是独立进程、自己持 loopback。信封单独做成纯函数 `WallpaperWebAudioEnvelope.h`(locale 逗号小数点 / 精度两处已在注释里写明),并用 `tests/WebAudioEnvelopeParity.mjs` 把 C++ 真实输出喂给真 shim 比对 —— 契约有两份实现而此前没有任何东西检查它们之间是否一致。四向注入验证过门会响。**真机播放音乐仍未验** |
| 2026-09-22 | `MiaoSceneSerializer::SelfTest` 首次被调用 | 120 行断言自始至终没有调用方;多在与粒子发射器预算(65536/131072 —— 正是 content-review 要求作者遵守的那两条)。经注入失效验证会响(`SceneSerializerSelfTest`) |
| 2026-09-22 | C++ 真 bug:`RecentlyUsed`/`Favorites` 漏了"用户可见"闸门 | `WallpaperLibrary.cpp` 三处补 `IsLibraryUiVisible(item) continue`(`c55ef67`)。已在 HEAD 515/533/550 逐行确认 |
| 2026-09-22 | 四个派生视图门 CRLF 脆弱性 | `packaging/windows/verify-wallpaper-library-*.ps1` 读入处归一化行尾(`1455b4c`)。根因:按 `\n` 定位空行/函数结尾,CRLF 下永远匹配不上。本机转 CRLF 复现过与 CI 完全相同的报错消息 |
| 2026-09-22 | 两个渲染后端材质规则合并 | `MiaoSpriteMaterialPolicy.h/.cpp`(共享实现)+ `MiaoDeskSpriteMaterialPolicyTest`(16 项断言,含 parity)。发现并修掉:MiaoCloud 在 D3D11 上因"无 materialId"整个包加载失败(`0937328`) |
| 2026-09-22 | D3D11 贴图 sprite 绘制路径 | `EngineTexturedPixelShader()` + t0 绑定 + 按 `textured` 选 shader;**待 Windows 编译与真机**,HLSL 只有 `D3DCompile` 能验(`0937328`) |
| 2026-09-20 | B-5 media 壁纸包校验 | 补齐 Image/Video entry 扩展名校验(关掉 type/entry 不匹配漏洞);新增 `CreateImage`/`CreateVideo`;资产名净化改为净全名 |

## 维护约定

1. 每完成一项,把状态改为 ✅ 并移入「已完成」,写清产出。
2. 每新增一项,必须填**依据**与**验收标准**。
3. P0 项在全部关闭前,不对外做任何发布承诺。
4. 本清单与 `DEVELOPMENT_ROADMAP.md` 冲突时,以后者为准;但若冲突源于本清单已过期,应更新本清单。
