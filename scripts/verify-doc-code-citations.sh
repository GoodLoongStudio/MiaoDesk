#!/usr/bin/env bash
# 校验 docs/ 里"源文件名加行号"形式的代码引用仍然指向真实存在的行。
#
# 为什么需要它:文档里的 file:line 引用会**静默腐烂**。代码一改行号就偏,
# 偏了几行之后读者按图索骥会落到一段无关的代码上 —— 而没有任何测试会发现,
# 因为文档不是代码。这类错误的特征是:看起来有依据,实际上依据已经错了。
#
# 2026-09-22 跑过一次人工核查,33 处全部有效;那个结果没有意义除非它被钉住。
# 所以做成门:每次改动 src/ 或 docs/ 之后几秒内就能知道有没有引用失效。
#
# 它只查"这一行还在不在",不查"这一行说的是不是那件事" —— 后者要人读。
#
# 这个边界不是理论上的。2026-09-22 做过一次全量人工核对:把 docs/ 里每一条
# `文件.cpp:行号` 引用连同它的文档上下文,与实际那一行打印出来对照,26 条能解析的引用里
# **有 10 条指错了地方** —— 而这 10 条这道门全是放行的(因为那些行都存在)。典型:
#   · MiaoSceneRuntimeModel.h:166 被引作"每 emitter 粒子 ≤ 65536",实际那一行是一条
#     关于灯光节点的注释,常量在 :257;
#   · PiRuntime.cpp:413 被引作 ConfigurePiAgent,实际那是 PiRuntime::Status() 里的,
#     函数在 :429;
#   · WallpaperService.cpp:307 被引作"完整播放" video,实际那是 Scene 分支的报错返回,
#     Video 分支在 :323。
# 已全部按核对结果改正,但**这道门不会发现下一次漂移** —— 它管"还在不在",不管"说的是不是"。
# 所以:改一个 .cpp 的行数结构时,顺手 grep 一下 docs/ 里有没有引它。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

# 文件名 -> 路径。同名文件取最短路径的那个(src 下没有同名不同义的现状;
# 真出现歧义时这个脚本会报出来,而不是悄悄选一个)。
index = {}
for p in (root / "src").glob("**/*"):
    if p.is_file() and p.suffix in (".cpp", ".h", ".inc"):
        index.setdefault(p.name, []).append(p)

ambiguous = {k: v for k, v in index.items() if len(v) > 1}
if ambiguous:
    for name, paths in sorted(ambiguous.items()):
        print("  ? 同名文件有 %d 个,引用时按最短路径取:%s" % (len(paths), name))
        for p in paths:
            print("      %s" % p)

checked = 0
problems = []

for doc in sorted((root / "docs").glob("*.md")):
    text = doc.read_text(encoding="utf-8")
    for m in re.finditer(r'([A-Za-z0-9_]+\.(?:cpp|h|inc)):(\d+)', text):
        fname, line = m.group(1), int(m.group(2))
        checked += 1
        paths = index.get(fname)
        if not paths:
            problems.append("%s: %s:%d -> 仓库里没有这个文件" % (doc.name, fname, line))
            continue
        target = min(paths, key=lambda p: len(str(p)))
        total = len(target.read_text(encoding="utf-8", errors="replace").split("\n"))
        if line > total:
            problems.append("%s: %s:%d -> 该文件只有 %d 行(引用已腐烂)"
                            % (doc.name, fname, line, total))

print("核验 docs/ 里的代码引用: %d 处" % checked)
if ambiguous:
    print("同名文件: %d 个(需人工确认引用的是哪一个)" % len(ambiguous))

if problems:
    print()
    print("❌ %d 处引用失效:" % len(problems))
    for p in problems:
        print("      · %s" % p)
    print()
    print("  行号会随代码改动而偏移,而文档不是代码、没有测试覆盖它。")
    print("  失效的引用比没有引用更坏:它让读者以为有依据。")
    sys.exit(1)

print("✅ 全部引用都指向真实存在的行")
sys.exit(0)
PY
