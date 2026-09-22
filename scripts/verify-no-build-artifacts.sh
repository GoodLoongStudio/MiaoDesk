#!/usr/bin/env bash
# 仓库里不得躺「本机生成的构建/解释器产物」。
#
# 为什么需要它:2026-09-22,`scripts/__pycache__/generate-miao-cloud-scene.cpython-314.pyc`
# 跟着一个文档闸门的提交进了版本库。它是一次 `python3 scripts/generate-miao-cloud-scene.py`
# 的副产品 —— CPython 把字节码缓存写在源文件旁边,而 .gitignore 里一条 Python 的规则都没有,
# 于是 `git add -A` 就把它连着正事一起收走了。
#
# 事发当时仓里有 **十四**个闸门,无一报警,而且不是巧合:
#   verify-native-source-hygiene   只扫 src/ 下的源码形状,而 pyc 躺在 scripts/
#   verify-staged-wallpaper-assets 只管 assets/ 里 .mdwall 的暂存内容
#   verify-cmake-covers-sources    问「CMake 编了什么」,不问「多出来了什么」
# 它们全都只回答「这里的东西对不对」,没有一条回答「这里有没有不该在的东西」。
#
# 判据用 `git ls-files`,不看工作区:`git status` 只在改动时显眼,而这类文件一旦提交
# 就长期安静地待着,直到某天有人发现仓库里有个二进制文件,才问「这是谁提交的」。
# 问版本库「它跟踪了什么」,比扫磁盘更接近真正的问题。
#
# 两版判据的演化,记在这里免得再走一遍:
#   第一版「扫所有二进制扩展名」→ 当场误报三个**故意**提交的二进制
#     (vendored 的 WebView2LoaderStatic.lib、downloads/store/ 下的 Store 分发包)。
#     它们的引入提交本来就写明了意图,README 也登记了。把红灯挂在正确代码上,
#     比不设门更糟:它会训练人忽略这道门。
#   第二版「按扩展名判 + 逐文件 allowlist」→ 仍然不对。`git ls-files -I` 判出的
#     二进制其实有 **24 个**,其中一半是 .png/.jpg/.zip —— 按扩展名白名单反着来,
#     永远漏掉下一类,而漏掉的那类恰恰是没人看过的。
#   现在这版「按 region 判」:由 git 自己判定哪些被跟踪文件是二进制,再要求每一个
#     都落在**登记过的区域**里。`assets/` 允许出现任意图片(那是产品内容),
#     `src/` 出现一个 .a 永远不对。区域级登记比文件级少一层维护,又比扩展名白名单
#     多一层保证 —— 新出现的区域会红,不管它装的是什么扩展名。
# 边界(写清楚,免得把覆盖说大了):
#   · 第一档(解释器缓存)按**名字**判,不依赖 git 的二进制启发式;
#   · 第二档只覆盖 **git 自己判为二进制**的文件。一个内容恰好是纯文本的 `.a`
#     落在 `src/` 下,这道门看不见它 —— 那是源码形状门的事,不是产物门的事。
#   · 未跟踪的产物一律不管(那是 .gitignore 的职责),但门会校验 .gitignore 真的挡住了。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2

python3 - "$ROOT" <<'PY'
import os, subprocess, sys

root = sys.argv[1]
problems = []

def git(*args):
    r = subprocess.run(['git', *args], cwd=root, capture_output=True, text=True)
    if r.returncode not in (0, 1):          # 1 = "no match",合法
        raise SystemExit(f'git {" ".join(args)} failed: {r.stderr.strip()}')
    return r.stdout

# --- 让 git 自己判定二进制 -------------------------------------------------
# 不猜扩展名:`git grep -I -e '' --name-only` 列出全部**文本**文件,补集即二进制。
# 这是 git 自己的判断,与它在 diff 里显示 "Binary files differ" 用的是同一套判据。
text = set(git('ls-files').split('\n')) - {''}
text = {f for f in text if f in set(git('grep', '-I', '--name-only', '-e', '', '--').split('\n'))}
tracked = [l for l in git('ls-files').split('\n') if l]
binaries = sorted(set(tracked) - text)

# --- 第一档:解释器缓存,永远无可辩解 ---------------------------------------
# 这一档**按名字判,不经过 git 的二进制判定**。原本把它也放在 `binaries` 里查,
# 结果注入测试连打两次都是绿的 —— 原因是我写的测试文件碰巧不含 NUL 字节,
# 而 git 判二进制靠的正是 NUL/长度(`git grep -I` 的那套启发式)。
# 这不是测试的运气问题,是判据依赖了不该依赖的东西:`__pycache__/`、`*.pyc`
# 这些形状**按名字就该一票否决**,和一个 .pyc 里有没有 NUL 没有任何关系。
# 真 .pyc 当然全是 NUL,但把"必然成立"建立在启发式上,等于给门留了一条
# "生成一个没有 NUL 的 .pyc" 这种无聊但真实存在的绕法。
interp = [f for f in tracked
          if '/__pycache__/' in f'/{f}' or f.endswith(('.pyc', '.pyo', '.pyd', '.class'))]
if interp:
    problems.append("版本库里跟踪了解释器缓存(运行 scripts/*.py 的副产物):\n    "
                    + "\n    ".join(interp))
other = [f for f in binaries if f not in interp]

# --- 区域登记表 -------------------------------------------------------------
# 「前缀 -> 为什么这个前缀下允许出现二进制」。不是「这里有个例外所以跳过检查」,
# 而是「这个区域有书面理由」;真要改这个表的人,必须连理由一起改。
# 依据:verify-native-source-hygiene 的 appdata_allow 同一约定。
REGIONS = [
    ('assets/',
     '产品内容:应用图标、壁纸包内的贴图。图片就是产品本身,每张都由引入提交登记。'),
    ('docs/design/',
     '设计参考图的冻结快照,评审要对着看(7b6516a "Freeze Search Bar visual reference")。'),
    ('packaging/store/assets/',
     'Store 展示素材,由 e20114b 一起生成并登记。'),
    ('downloads/store/',
     'Store 提交通道分发的已签名安装包。它们必须可复现地拿到,a008aaa / b44bf19。'),
    ('runtime/x64/node/',
     'vendored 的 Node 运行时,锁版本,构建时不得联网下载(0c84de5)。'),
    ('runtime/arm64/node/',
     '同上,ARM64 侧(ca77e57)。'),
    ('runtime/x64/goz/',
     'vendored 的 goz 二进制(0c84de5)。'),
    ('runtime/arm64/goz/',
     '同上,ARM64 侧(f4ea8e2)。'),
    ('third_party/webview2/lib/',
     'vendored WebView2 SDK 静态库,没有包管理器在管,链接必须(ce9b855)。README 登记。'),
]

unregistered = [f for f in other
                if not any(f.startswith(p) for p, _ in REGIONS)]
if unregistered:
    problems.append(
        "版本库里跟踪了位于未登记区域的二进制:\n    " + "\n    ".join(unregistered) +
        "\n\n  二进制只有两种来源:工具链顺带产生的副产物,或某个提交刻意放进来的。"
        "\n  若它是刻意引入的,请把它的目录连同「引入它的提交号 + 为什么它必须在库里」"
        "\n  一起补进本闸门的 REGIONS —— 登记本身就是「我们知道它在这儿,并且记得为什么」。"
        "\n  特别注意:上面这个清单是 git 按 NUL/长度自己判出来的,不是按扩展名猜的,"
        "\n  所以新扩展名同样会被抓住。")

# --- 登记的二进制清单必须仍然准确 -----------------------------------------
# 反过来也查一遍:如果某个登记区域里一个二进制都不剩了,说明表过期了 ——
# 过期登记比没有登记更危险,因为它让人以为这里仍然被看着。
for prefix, _why in REGIONS:
    if not any(f.startswith(prefix) for f in binaries):
        problems.append(f"登记区域 {prefix} 下已无任何二进制,登记表过期了,请删掉这一行。")

# --- .gitignore 必须真的挡住解释器缓存 ------------------------------------
if not os.path.isfile(os.path.join(root, '.gitignore')):
    problems.append("仓库没有 .gitignore,`git add -A` 会把工作区所有产物一起收走。")
else:
    probe = 'scripts/__pycache__/generate-miao-cloud-scene.cpython-314.pyc'
    r = subprocess.run(['git', 'check-ignore', '-q', probe], cwd=root)
    if r.returncode != 0:
        problems.append(
            ".gitignore 挡不住 scripts/__pycache__/*.pyc —— 下一条生成的本机字节码仍会被提交。"
            "\n    补一条:scripts/__pycache__/(或全局 __pycache__/ + *.py[cod])")

# --- 报告 ------------------------------------------------------------------
print(f"被跟踪文件总数:      {len(tracked)}")
print(f"其中 git 判为二进制: {len(binaries)}")
print(f"登记区域:            {len(REGIONS)} 个")

if problems:
    print()
    print(f"❌ {len(problems)} 处问题:")
    for p in problems:
        print(f"      · {p}")
    sys.exit(1)

print()
print("✅ 版本库里没有解释器缓存,所有二进制都落在登记区域,.gitignore 覆盖了缓存")
sys.exit(0)
PY
