# scripts/

闸门与工具。分两类:

- **`.ps1`** —— 在 CI 的 Windows runner 上运行。
- **`.sh`** —— 在开发机上运行(作者在 macOS)。它们不是 CI 的替代品,而是在推送前
  先把最便宜的一类错误挡掉,把 CI 留给只有 Windows 才能回答的问题。

## 开发机上先跑这七个

```bash
bash scripts/verify-windows-syntax.sh        # 交叉编译全部独立 TU,查类型/成员是否真存在
bash scripts/run-pure-logic-tests.sh         # 真实编译并运行不依赖 Windows 的测试目标
bash scripts/verify-skill-allowlist.sh       # content_skill_get 白名单 与 skills/ 目录双向比对
bash scripts/verify-no-conflict-markers.sh   # 拒绝未解决的合并冲突标记
bash scripts/verify-cmake-covers-sources.sh  # 磁盘上的 .cpp 是否真的被 CMake 编译
bash scripts/verify-cmake-target-hygiene.sh  # CMake 目标结构:顺序 / 清单 / 链接 / 单一 owner
bash scripts/verify-workflow-paths.sh        # 工作流的 paths 过滤是否覆盖它自己跑的文件
bash scripts/verify-media-package-offline.sh # CreateVideo/CreateImage + Validate(CI 第 1 节)
bash scripts/verify-scene-fixture-parity.sh  # 两份贴图场景 fixture 是否已分叉
```

依赖:`brew install mingw-w64`(`verify-windows-syntax.sh` 用)、`clang++`
(`run-pure-logic-tests.sh` / `verify-media-package-offline.sh` 用)、`cmake`
(`verify-cmake-covers-sources.sh` 用)。

其中四个(冲突标记 / CMake 收录 / 工作流 paths / skill 白名单)连同样两个 node 闸门一起,
由 `.github/workflows/repo-hygiene.yml` 在 CI 里跑 —— 它们不需要 Windows,也不依赖构建
能否通过,所以不该被构建类工作流挡住。

### 为什么需要它们

2026-09-21 四个 CI 工作流同时挂掉,四处编译错误的共同点不是逻辑错,而是那批代码
**从没经过任何认 Windows 头文件的编译器**。此前本机验证只覆盖两类东西:从 C++
里剥出来单独编译的逻辑片段,和不含 `windows.h` 的纯逻辑测试。两者都看不见
MSVC 才能看见的类型错误。

事后补的闸门都有明确的边界,不假装自己比 CI 强:

| 闸门 | 能答 | 不能答 |
| --- | --- | --- |
| `verify-windows-syntax.sh` | 这个类型/成员存不存在;模板能不能推导 | Windows SDK 齐不齐;MSVC 与 mingw 的差异 |
| `run-pure-logic-tests.sh` | 这 6 个纯逻辑测试目标的行为对不对 | 任何需要 Windows 的目标 |
| `verify-skill-allowlist.sh` | 白名单与 `skills/` 是否一致 | CI 上真实的注入效果 |
| `verify-no-conflict-markers.sh` | 仓库里有没有未解决的冲突标记 | 无 |
| `verify-cmake-covers-sources.sh` | 磁盘上的 `.cpp` 是否真的被 CMake 编译 | CMakeLists 本身的意图是否合理 |
| `verify-cmake-target-hygiene.sh` | 目标顺序 / foreach 清单一致 / 每个可执行目标都有链接 / MSVC 选项齐全 / 每个 `.cpp` 只有一个 owner | 链接到的库是否真是它需要的那个;`LINKS_NOTHING` 里的登记是否仍然成立 |
| `verify-workflow-paths.sh` | 工作流的 `paths` 过滤是否覆盖它自己跑的文件 | 过滤模式是否过宽(过宽不算错) |
| `verify-scene-fixture-parity.sh` | 两份贴图场景 fixture(真渲染用的那份 / 本机验 schema 的那份)是否分叉 | Windows 那份是否真能画出来 —— 那只有 CI 能答 |
| `verify-media-package-offline.sh` | `CreateVideo`/`CreateImage` + `Validate`(CI 测试第 1 节) | 见下面「各闸门的覆盖边界」 |

九个 CMake 测试目标里,三个原本只有 CI 能覆盖,共同原因是它们 include
`WallpaperLibrary.h` 或 `NativeTools.h`,而那两条链都会拉到 `UnicodeProfileFile.h:72` 的
`static_assert(sizeof(wchar_t) == 2)`(Windows 配置持久化要求 UTF-16 `wchar_t`),macOS 的
`wchar_t` 是 4 字节。这是产品设计约束,不为离线验证去绕过它(也别用 `-fshort-wchar`,
见规矩第 7 条)。

其中 `MediaWallpaperPackageTest` 的**第 1 节**已经能离线跑了 —— 那一节只依赖
`WallpaperPackage.cpp`,不需要 `WallpaperLibrary`。见
`scripts/verify-media-package-offline.sh`。

## 写闸门的规矩(都是踩出来的)

1. **写完必须注入一个已知失效,确认它真的会响。** 三个闸门里有三个自己试过假绿:
   过滤器用 `startswith('error:')` 匹配 gcc 输出(而 gcc 的行以路径开头)、comm 的列
   搞反导致一侧永远漏报、正则把 `was not declared` 写成 `was not been declared`。
   干净状态下这些都看起来完全正常。
2. **注入的失效必须真的落进去。** 有两次"测试没被捕到"其实是替换目标字符串不存在,
   tamper 空操作 —— 空操作的测试和通过的闸门站在一起,读起来就是"闸门漏报",而真因
   在测试自己。注入前先 `assert` 锚点确实在文件里,注入后确认文件内容真的变了。
3. **区分 `success` / `failure` / `skipped` / `null`。** 把 CI 里被跳过的步骤读成
   通过,让"17 个验证步骤通过"这个结论完全失实。`conclusion` 为 `null` 等于没跑。
   顺带:`cmd | tail` 之后 `$?` 取到的是 `tail` 的退出码,不是被验程序的,zsh 里尤其
   容易看错。
4. **别硬编码另一个源文件里的常量。** 从源码解析,否则两处迟早对不上,而这种漂移
   在磁盘上看不出来。
5. **替身的缺陷会被伪装成产品缺陷。** `windows-shim/` 的 `MultiByteToWideChar` 一旦
   写成有损窄化,含中文的自检就会假失败。改替身前先怀疑替身。
6. **`std::string` 从 `const char*` 构造会在第一个 NUL 处截断。** 测试里写二进制 fixture
   时 `"\x00\x00\x00\x18ftyp…"` 会变成**空文件**,于是被验代码正确地拒绝"源文件为空",
   正面断言以一种看起来像产品 bug 的方式失败。要用显式长度的构造:
   `std::string(charArray, sizeof(charArray))`。同一 session 里这个坑出现了三次。
7. **`-fshort-wchar` 不能用来绕开 `sizeof(wchar_t)==2`。** 它确实能让那条静态断言过,
   但在 macOS/Darwin 上宽字符字面量按一字节一字(UTF-8)发出、却按 2 字节 `wchar_t` 读,
   于是 `L"视频壁纸…"` 长度从 16 变 38 并夹入 `U+0000`,`printf("%s")` 在第一个 NUL
   截断,得到一个**看似是产品 bug 的假消息**。判定之前先验工具链本身。

## PowerShell 闸门的三个陷阱(2026-09-22 连中三次)

1. **`.\script.ps1` 之后查 `$LASTEXITCODE` 是错的。** `.ps1` 是 PowerShell 脚本,不是
   原生可执行文件,直接调用不设置 `$LASTEXITCODE`;它保留的是之前某个原生命令的值。
   那一步之前没有原生命令时它是 `$null`,而 `$null -ne 0` 恒为真 —— 这一步从写下来就
   不可能通过,与脚本本身过没过无关。要用 `& pwsh -NoProfile -File script.ps1`,它能正确
   传递退出码。
   (整段 `run:` 只有一句直接调用是另一回事:脚本 throw 会让 pwsh 进程非零退出,步骤
   照旧失败,那个形式没问题。)
2. **`$array -notmatch 're'` 是过滤,不是布尔。** `Get-Content -TotalCount 6` 得到
   `Object[]`,`-notmatch` 返回**不匹配的元素**。6 行里只有一行匹配时结果是非空数组、
   恒为真值,`if` 必进。要先 `-join` 成单串,并用 `(?m)` 让 `^`/`$` 按行锚定。
3. **`[Parser]::ParseFile` 通过不等于语义正确。** 它把
   `& script.ps1 -A X $gateOut = -B Y 2>&1` 当合法命令放过 —— PowerShell 对命令参数很
   宽松。改造工作流后要**执行级**验证:造一个假脚本(回显参数、按环境变量决定退出码),
   把步骤原样跑一遍,覆盖成功/失败两条路径。

## 各闸门的覆盖边界

| 闸门 | 覆盖 | 明确不覆盖 |
| --- | --- | --- |
| `verify-media-package-offline.sh` | `CreateVideo`/`CreateImage` + `Validate`,即 CI 测试第 1 节 | 第 5 节(library 导入手写包)—— 它要 `WallpaperLibrary.cpp`,需要真 Windows SDK;以及**测试自己写 fixture 的方式**(驱动自带一份字节逻辑,看不见测试里的) |

## 有一半闸门其实能在 macOS 上跑

不是因为它们是可移植的,而是因为 macOS 装了 `brew install powershell` 之后,PowerShell
脚本可以直接执行。已验证可跑的:

```bash
brew install powershell
pwsh -NoProfile -File scripts/verify-web-audio-bridge.ps1
```

它四个方向都验过:干净树通过;改 `.js` 来源失败;给 `.cpp` 和 `.js` **同时**加一条
`chrome.webview.postMessage`(逐字节仍然一致,只违反只读性)失败;恢复后通过。
正因为两侧都改才过得去第一关,验证的是真正想验证的那条属性。

只解析不执行的话,全部 17 个 `.ps1` 都能用 `[Parser]::ParseFile` 检查语法 —— 手改完
PowerShell 门禁后先跑一遍,一次几秒。

## 其他

- `windows-shim/` —— 最小 `windows.h` / `shlobj.h` 替身,只覆盖真实用到的符号。
  头文件里记着历史教训。
- `check-private-files.ps1` / `verify-path-layout-contract.ps1` —— 发布前检查,CI 运行。
- `verify-web-audio-bridge.ps1` —— 保证 `WebDesktopSurfaceChild.cpp` 里内嵌的音频桥
  与它的真实来源 `WallpaperWebAudioBridge.js` 逐字节一致。
- `generate-*.ps1` —— 图标与商店素材生成。
