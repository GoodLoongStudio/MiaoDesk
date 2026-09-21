#!/usr/bin/env bash
# 校验 src/CMakeLists.txt 的若干结构性不变式。这些不变式此前没有任何闸门在看,
# 而 2026-09-22 一天之内在本仓库踩了三次:
#
#   1. add_executable 追加在 foreach(target ...) 之后
#      → Configure 期就报 "Cannot specify compile definitions for target X which is
#        not built by this project";拖到编译期表现为 "Cannot open include file",
#        两处相隔一百多行,极难反推。犯了两次(9/21、9/22)。
#   2. add_executable 有声明但没有 target_link_libraries
#      → 编译通过、**链接才失败**(LNK2019 / LNK1120)。本机交叉语法门只做
#        -fsyntax-only,link 阶段根本不存在于本机,所以这一类错只能等一轮 90 秒
#        的 Windows CI。而它报的是 "unresolved external symbol MiaoSceneModel::Validate",
#        没有人会从那儿反推到"少写了一行链接"。
#   3. 同一个 .cpp 在两处源清单里出现
#      → scripts/verify-path-layout-contract.ps1 的 "one CMake owner" 规则。
#        那个脚本是 Windows 侧的(它按反斜杠比对 allowlist,在 macOS 上会假阳性),
#        所以本机一直没人跑得动它;于是这条例子在两轮提交之间一直是裸的。
#
# 本闸门把这三条(以及两条集合一致性)变成本机十秒级、可跑的检查。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CMAKE="$ROOT/src/CMakeLists.txt"

if [ ! -f "$CMAKE" ]; then
  echo "❌ 找不到 $CMAKE" >&2
  exit 2
fi

python3 - "$CMAKE" <<'PY'
import re, sys

path = sys.argv[1]
text = open(path, encoding='utf-8').read()
lines = text.splitlines()
problems = []

def lineno(pos):
    return text.count('\n', 0, pos) + 1

# --- 收集事实 ---------------------------------------------------------------
targets = {}      # name -> (kind, line)
for m in re.finditer(r'^add_(executable|library)\(\s*([A-Za-z0-9_]+)', text, re.M):
    kind, name = m.group(1), m.group(2)
    if name in targets:
        problems.append(f"目标 {name} 被声明了两次(第 {targets[name][1]} 行与第 {lineno(m.start())} 行)")
    targets[name] = (kind, lineno(m.start()))

foreach_line = None
m = re.search(r'^foreach\(target\b', text, re.M)
if m:
    foreach_line = lineno(m.start())

linked = set()
for m in re.finditer(r'^target_link_libraries\(\s*([A-Za-z0-9_]+)', text, re.M):
    linked.add(m.group(1))

msvc_options = set()
in_msvc = False
for i, line in enumerate(lines, 1):
    if re.match(r'^if\s*\(\s*MSVC\s*\)', line):
        in_msvc = True
        continue
    if in_msvc and re.match(r'^endif\s*\(', line):
        in_msvc = False
    if in_msvc:
        mm = re.match(r'^\s*target_compile_options\(\s*([A-Za-z0-9_]+)', line)
        if mm:
            msvc_options.add(mm.group(1))

# 源清单里的 .cpp 行:缩进 + 一个相对路径 + 行尾。这正是
# verify-path-layout-contract.ps1 用来判"多个 CMake owner"的同一形状。
source_owner = {}
for i, line in enumerate(lines, 1):
    mm = re.match(r'^\s+([A-Za-z0-9_./-]+\.cpp)\s*$', line)
    if not mm:
        continue
    rel = mm.group(1)
    if rel in source_owner:
        problems.append(f"{rel} 出现在两处源清单里(第 {source_owner[rel]} 与第 {i} 行):"
                        f"每个实现文件只能有一个 CMake owner —— 要共享就建成库目标。")
    source_owner[rel] = i

# 故意不链任何库的可执行目标。列在这里而不是默认放过:一个"忘了写
# target_link_libraries"的目标与一个"按设计什么都不链"的目标,在 CMakeLists 里长得
# 一模一样。登记时必须说明它为什么敢不链 —— 两者共同的判据是"只 include 不产生
# 外部符号的头"。
LINKS_NOTHING = {
    # MiaoInputBus.h 是纯 C++,既不需要 Windows 也不需要任何库 —— 这正是它能在
    # 每一轮构建里跑的原因。
    'MiaoDeskInputBusCoreTest',
    # 同 MiaoDeskInputBusCoreTest:只 include MiaoInputBus.h,复刻宿主的归属算术。
    'MiaoDeskPointerAttributionTest',
}

# --- 规则 ------------------------------------------------------------------
if foreach_line is None:
    problems.append("CMakeLists.txt 里没有 foreach(target ...) 循环,无法沿用既有模式?")
else:
    # foreach(...) 单行形式,取出其中的目标名。
    fm = re.search(r'^foreach\(target\s+(.*?)\)\s*$', text, re.M)
    if not fm:
        problems.append("foreach(target ...) 不是单行形式,本闸门无法解析 —— 请改成单行或同步更新本闸门。")
        foreach_targets = set()
    else:
        foreach_targets = {t.strip() for t in fm.group(1).split() if t.strip()}

    for name, (kind, line) in sorted(targets.items(), key=lambda kv: kv[1][1]):
        if line > foreach_line:
            problems.append(f"{kind} {name}(第 {line} 行)声明在 foreach(第 {foreach_line} 行)之后。"
                            f"CMake 顺序执行,循环里的 target_include_directories 会在 Configure 期失败,"
                            f"拖到编译期才表现为找不到头文件。移到循环之前。")

    for name in sorted(foreach_targets - set(targets)):
        problems.append(f"foreach 里的 {name} 不是本文件声明的目标(拼写错误或已删除)。")
    for name in sorted(set(targets) - foreach_targets):
        problems.append(f"{name} 没有出现在 foreach(target ...) 里 —— 它拿不到 include/miaodesk 与"
                        f" WIN32_LEAN_AND_MEAN 等宏,正确性完全靠巧合。")

# 静态库不链接:它自己的符号由最终的可执行目标解析。只有可执行目标需要这一条。
for name, (kind, line) in sorted(targets.items(), key=lambda kv: kv[1][1]):
    if kind != 'executable':
        continue
    if name in linked or name in LINKS_NOTHING:
        continue
    problems.append(f"可执行目标 {name}(第 {line} 行)没有 target_link_libraries。它能编译,但链接着它用到"
                    f"的符号会变成 LNK2019/LNK1120 —— 而本机语法门只做 -fsyntax-only,看不见这一类错"
                    f"(2026-09-22 SpriteTextureContractTest 正是这样红了一轮 Windows CI)。"
                    f" 若确实按设计什么都不链,请把它登记进本闸门的 LINKS_NOTHING 并说明理由。")

for name in sorted(msvc_options - set(targets)):
    problems.append(f"MSVC 编译选项段里的 {name} 不是本文件声明的目标。")
for name in sorted(set(targets) - msvc_options):
    problems.append(f"{name} 在 MSVC 段里没有 target_compile_options —— 于是它在 MSVC 下"
                    f"缺 /W4 /permissive- /utf-8 /EHsc,而其它目标都有。")

# --- 报告 ------------------------------------------------------------------
print(f"目标声明:           {len(targets)} 个")
print(f"根目录 CMakeLists:  {path}")
print(f"foreach 位于:       第 {foreach_line} 行" if foreach_line else "foreach:            未找到")
print(f"foreach 内目标:     {len(foreach_targets)} 个")
print(f"有 target_link:     {len(linked)} 个")
print(f"有 MSVC 选项:       {len(msvc_options)} 个")
print(f"源清单行:           {len(source_owner)} 个 .cpp")

if problems:
    print()
    print(f"❌ {len(problems)} 处 CMake 目标结构问题:")
    for p in problems:
        print(f"      · {p}")
    sys.exit(1)

print()
print("✅ 目标顺序、目标清单一致、每个目标都有链接与 MSVC 选项、每个 .cpp 只有一个 owner")
sys.exit(0)
PY
