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
