#!/usr/bin/env bash
# 校验 `文档里写的符号` 与 `它引用的那一行` 是否还对得上。
#
# 为什么需要它:verify-doc-code-citations.sh 只查"这一行还在不在"。2026-09-22 做过一次
# 全量人工核对,把 docs/ 里每条引用连同文档上下文打出来和真实那一行对照,26 条能解析的
# 引用里**有 10 条指错了地方**,而那道门十条全放行 —— 因为那些行都存在。典型:
#   · `MiaoSceneRuntimeModel.h:166` 被引作"每 emitter 粒子 ≤ 65536",实际那一行是
#     一条关于灯光节点的注释,常量在 :257;
#   · `PiRuntime.cpp:413` 被引作 PiRuntime::ConfigurePiAgent,实际那一行在
#     PiRuntime::Status() 里;
#   · `WallpaperService.cpp:307` 被引作"完整播放 video",实际那是 Scene 分支的报错返回。
#
# 那 10 条里有 8 条有一个共同点:引用前面不远处就写着它讲的那个符号,比如
# `MaterialDefinition` 有 ...(`MiaoSceneRuntimeModel.h:73`)。既然符号就在句子里,
# 就可以机器核对 —— 这就是本闸门做的事:取引用点之前那段文字里的标识符,
# 验证它确实出现在被引用的那一行附近。
#
# 边界(刻意不做的):
#   · 只处理"引用**同一行**、且在引用点之前有反引号标识符"的那些。剩下的
#     (例如跨行的 "`MediaWallpaperPackageTest` ... →\n`UnicodeProfileFile.h:72`")没有
#     可靠的符号可核,仍然要靠人读。这一行的分界是被两个假阳性逼出来的,理由写在下面。
#   · 允许 ±3 行的窗口:作者常常引用声明的前一行注释,或结构体的中间一行。
#     窗口是本闸门唯一可调的地方,调大就会开始漏,调小就会误报。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2

python3 - "$ROOT" <<'PY'
import os, re, subprocess, sys

root = sys.argv[1]
docs = os.path.join(root, 'docs')
CITE = re.compile(r'([A-Za-z0-9_]+\.(?:cpp|h|inc)):(\d+)(?:-(\d+))?')
# 反引号里的标识符:允许 A::B、A<T>、以及裸标识符;不接括号,免得把 LoadDefault() 的
# 调用形态和定义形态混起来判断。
TICK = re.compile(r'`([A-Za-z_][A-Za-z0-9_:]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)`')
WINDOW = 3          # 前后各放宽几行

tracked = subprocess.run(['git', 'ls-files'], cwd=root, capture_output=True, text=True).stdout.split()
by_name = {}
for t in tracked:
    by_name.setdefault(os.path.basename(t), t)

checked = 0
problems = []
for name in sorted(os.listdir(docs)):
    if not name.endswith('.md'):
        continue
    text = open(os.path.join(docs, name), encoding='utf-8').read()
    for m in CITE.finditer(text):
        stem = m.group(1)
        first = int(m.group(2))
        last = int(m.group(3)) if m.group(3) else first
        path = by_name.get(stem)
        if not path:
            continue
        try:
            lines = open(os.path.join(root, path), encoding='utf-8', errors='replace').read().split('\n')
        except OSError:
            continue
        if last > len(lines):
            # 越界不归本门管:那是 verify-doc-code-citations.sh 的职责,它会报"这一行不存在"。
            # 这里跳过,免得同一件事两处报,还报得不一样。
            continue

        # 只看**同一行**、且在引用点之前的反引号符号。
        #
        # 这一行限制不是图省事,是被两个假阳性逼出来的。第一版往前看 80 个字符、
        # 跨行,于是把两种完全正当的写法判成错:
        #   · "`MediaWallpaperPackageTest` 明确只能由 CI 覆盖:它链接 ... →\n"
        #     "`UnicodeProfileFile.h:72` 有 `static_assert(...)`" —— 句子的主语是那个
        #     测试名,而这条引用讲的是 static_assert,符号在上一行;
        #   · "`ParticleEmitterDefinition` —— **刻意不做**。\nlegacy 的粒子不是声明式的:"
        #     "`LayeredSceneRenderer.h:273-314` 按索引..." —— 符号被点名是为了说它
        #     **没有**被用,引用指向的正是那份没用它实现的旧代码。
        # 两处的符号与引用都不在同一行;而所有真阳性(表格行内、同一句括号前)都在同一行。
        # 所以按行切,是这两类之间唯一干净的分界。
        line_start = text.rfind('\n', 0, m.start()) + 1
        before = text[line_start:m.start()]
        ticks = [t for t in TICK.findall(before)
                 if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*', t)]
        if not ticks:
            continue
        symbol = ticks[-1]
        # A::B 取最后一段:MiaoContentPackage::Load 的定义行写作 MiaoContentPackage::Load,
        # 但有些地方只写 Load;两段都可能出现,取最后一段最稳。
        needle = symbol.split('::')[-1]
        if len(needle) < 6:
            continue        # 太短的标识符撞车概率高,不判

        lo = max(0, first - 1 - WINDOW)
        hi = min(len(lines), last + WINDOW)
        window = '\n'.join(lines[lo:hi])
        checked += 1
        if needle not in window:
            near = [f"{i+1}: {l.strip()[:70]}" for i, l in enumerate(lines)
                    if needle in l][:2]
            problems.append(
                f"{name}: {stem}:{first}{'-' + str(last) if last != first else ''} "
                f"讲的是 `{symbol}`,但引用行 ±{WINDOW} 内找不到 `{needle}`。\n"
                f"      引用行实际是: {lines[first-1].strip()[:80]}\n"
                + (f"      该符号其实在: {near[0]}" if near else f"      全文件都找不到 `{needle}`"))

print(f"可核对的引用(引用点前有符号): {checked}")
if problems:
    print()
    print(f"❌ {len(problems)} 处引用讲的符号与所指行不符:")
    for p in problems:
        print(f"      · {p}")
    sys.exit(1)
print("✅ 每条引用讲的符号都出现在它所指的那一行附近")
sys.exit(0)
PY
