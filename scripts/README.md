# scripts/

闸门与工具。分两类:

- **`.ps1`** —— 在 CI 的 Windows runner 上运行。
- **`.sh`** —— 在开发机上运行(作者在 macOS)。它们不是 CI 的替代品,而是在推送前
  先把最便宜的一类错误挡掉,把 CI 留给只有 Windows 才能回答的问题。

## 开发机上先跑这三个

```bash
bash scripts/verify-windows-syntax.sh        # 交叉编译全部独立 TU,查类型/成员是否真存在
bash scripts/run-pure-logic-tests.sh         # 真实编译并运行不依赖 Windows 的测试目标
bash scripts/verify-skill-allowlist.sh       # content_skill_get 白名单 与 skills/ 目录双向比对
bash scripts/verify-no-conflict-markers.sh   # 拒绝未解决的合并冲突标记
bash scripts/verify-cmake-covers-sources.sh  # 磁盘上的 .cpp 是否真的被 CMake 编译
```

依赖:`brew install mingw-w64`(`verify-windows-syntax.sh` 用)、`clang++`
(`run-pure-logic-tests.sh` 用)、`cmake`(`verify-cmake-covers-sources.sh` 用)。

后三个另有 `.github/workflows/repo-hygiene.yml` 在 CI 里跑 —— 它们不需要 Windows,
也不依赖构建能否通过,所以不该被构建类工作流挡住。

### 为什么需要它们

2026-09-21 四个 CI 工作流同时挂掉,四处编译错误的共同点不是逻辑错,而是那批代码
**从没经过任何认 Windows 头文件的编译器**。此前本机验证只覆盖两类东西:从 C++
里剥出来单独编译的逻辑片段,和不含 `windows.h` 的纯逻辑测试。两者都看不见
MSVC 才能看见的类型错误。

事后补的三个闸门都有明确的边界,不假装自己比 CI 强:

| 闸门 | 能答 | 不能答 |
| --- | --- | --- |
| `verify-windows-syntax.sh` | 这个类型/成员存不存在;模板能不能推导 | Windows SDK 齐不齐;MSVC 与 mingw 的差异 |
| `run-pure-logic-tests.sh` | 这 6 个纯逻辑测试目标的行为对不对 | 任何需要 Windows 的目标 |
| `verify-skill-allowlist.sh` | 白名单与 `skills/` 是否一致 | CI 上真实的注入效果 |
| `verify-no-conflict-markers.sh` | 仓库里有没有未解决的冲突标记 | 无 |
| `verify-cmake-covers-sources.sh` | 磁盘上的 `.cpp` 是否真的被 CMake 编译 | CMakeLists 本身的意图是否合理 |

`MediaWallpaperPackageTest` 只有 CI 能覆盖 —— 它链接 `WallpaperLibrary.cpp` →
`UnicodeProfileFile.h:72` 的 `static_assert(sizeof(wchar_t) == 2)`(Windows 配置持久化
要求 UTF-16 `wchar_t`),而 macOS 的 `wchar_t` 是 4 字节。这是产品设计约束,不为离线
验证去绕过它。

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
