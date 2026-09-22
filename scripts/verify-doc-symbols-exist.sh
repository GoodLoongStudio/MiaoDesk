#!/usr/bin/env bash
# docs/ 里提到的 C++ 符号,必须还在源码树里。
#
# 为什么需要它:这是一类与"引用指错行"不同、但同样安静的腐烂 —— 文档讲的是某个函数、
# 某个常量,而那个名字已经被改名或删掉了。读者看到的是一句结构完整、读起来有依据的话,
# 只是它依据的东西不存在。
#
# 2026-09-22 第一次跑就抓到一个真的:`WallpaperLibrary::ImportPackages`。
# TODO.md 用它来解释"视频壁纸在产品层本来就通了",而这个函数在源码里已经不存在了 ——
# 导入路径后来变成了 `ImportFile` / `ImportWebUrl`,那段 type->kind 的映射仍在
# `WallpaperLibrary.cpp:626-627`,只是不在任何叫 ImportPackages 的函数里。
# 也就是说那句话的**结论是对的,依据的名字是错的**。
#
# 与另外两道引用门的分工:
#   verify-doc-code-citations.sh    这一行还在不在
#   verify-doc-citation-symbols.sh  这一行讲的东西在不在这一行
#   本门                        文档讲的那个东西还存不存在
# 三道都绿,才等于"文档引用的代码既在、也对、也还没被删掉"。
#
# 判据与边界(刻意不做的):
#   · 只认反引号里的标识符,且必须含 `::` 或以 .cpp/.h 结尾, Haystack 只用 src/。
#     这个窄判据是量出来的,不是猜的:放宽到"所有反斜号标识符 + 全仓库(排除 docs/)"
#     之后,342 个符号里 28 个找不到(8%),逐条看过之后**绝大多数是正当的**:
#       - `CreateWebWallpaperPackage` / `PublishWeather` —— 文档在讲**被刻意删掉**的代码
#         (P3-1 删了前者,后者被泛化成 PublishHostData)。讲一次删除当然要写被删的名字;
#       - `OPENROUTER_API_KEY` / `tool_call_parser` / `extra_key` —— 环境变量与配置键,
#         不是 C++ 符号;
#       - `Planning` / `WaitingForUser` 之类 —— 文案态名,代码里是另一种写法;
#       - `a1348b06` —— commit 号。
#     换句话说,放宽之后门会红在一堆正确的话上,而这种门红几次就没人看了。
#     所以取窄判据:37 个符号、0 假阳性,并且真的抓到了 ImportPackages 那一处。
#   · 最后一段至少 8 个字符:短标识符(Load、Draw)撞车概率太高。
#   · 取 `A::B` 的最后一段在全文里找:定义处写作 `A::B`,使用处可能只写 `B`,
#     两段都找不到才判缺失。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2

python3 - "$ROOT" <<'PY'
import os, re, subprocess, sys

root = sys.argv[1]
docs = os.path.join(root, 'docs')

TICK = re.compile(r'`([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)`')
MIN_TAIL = 8

code = [f for f in subprocess.run(['git', 'ls-files', 'src', '--'], cwd=root,
                                  capture_output=True, text=True).stdout.split()
        if f.endswith(('.cpp', '.h', '.inc'))]
haystack = []
for f in code:
    try:
        haystack.append(open(os.path.join(root, f), encoding='utf-8', errors='replace').read())
    except OSError:
        pass
source = '\n'.join(haystack)

checked = set()
missing = []
for name in sorted(os.listdir(docs)):
    if not name.endswith('.md'):
        continue
    text = open(os.path.join(docs, name), encoding='utf-8').read()
    for m in TICK.finditer(text):
        sym = m.group(1)
        tail = sym.split('::')[-1]
        if len(tail) < MIN_TAIL:
            continue
        if '::' not in sym and not re.search(r'(?:^|\.)(?:cpp|h)$', sym):
            continue
        if sym.startswith(('http', 'www')):
            continue
        checked.add(sym)
        if tail not in source and sym not in source:
            where = text[max(0, m.start()-60):m.start()].replace('\n', ' ').strip()[-50:]
            missing.append(f"{name}: `{sym}` —— 源码树里找不到。文档上下文: …{where}")

print(f"纳入核对的符号: {len(checked)} 个(源码文件 {len(code)} 个)")
if missing:
    print()
    print(f"❌ {len(missing)} 个符号已被改名或删除,而文档仍在引用:")
    for m in missing:
        print(f"      · {m}")
    print()
    print("   结论可能仍然对,只是依据的名字错了 —— 那就更糟:读者会去找一个不存在的东西。")
    print("   改法要么把名字改成现在的,要么在文档里说明它已不存在。")
    sys.exit(1)
print("✅ 文档提到的每个符号都还在源码树里")
sys.exit(0)
PY
