#!/usr/bin/env bash
# CCA-06:运行时上限优先于文档副本。
#
# 计划验收里有一句是"运行时没有提供的 CPU/GPU/任务操作能力不能靠虚构 binding 伪造"。
# 这句话以前**没有任何执行点**:规范(skills/)、契约文档(docs/MIAO_CONTENT_PACKAGE_V1.md)
# 和代码是几份各自手写的文本,于是文档可以宣称一个运行时根本没有的数据能力,而两边
# 都不报错。
#
# 实测到的那个例子:`content-package-basics/SKILL.md` 把 capabilities 举例成
# "clock.read / weather.read / audio.read"。而 `MiaoContentCapabilityBroker::
# RequiredCapability` 只认识 time. / weather. / tasks. 三个前缀 —— **没有 audio.**。
# 内容能拿到的音频是**输入通道** `input://audio/*`,它不需要、也没有对应的能力声明。
# 后果不是包被拒(loader 只查 capability id 的字符集),而是更坏的两种:
#   · 作者声明了 audio.read 就以为音频已经授权,接着写一个 audio.* 数据绑定 ——
#     被 broker 以 "not supported by the capability broker" 拒掉,而他会以为是别的问题;
#   · 声明了,它完全无效,而模型告诉用户"音频已启用"。
# 一个虚构的能力比一个缺失的能力更难发现:缺失会亮红,虚构只会静悄悄地不工作。
#
# 判据从代码**读出**,不是再抄一份:
#   · 数据能力:从 MiaoContentDataBinding.cpp 的 RequiredCapability 读 (前缀 → capability);
#   · 输入通道:从 MiaoInputBus.h 的 ChannelShape 表读(input://... 的封闭集合)。
# 抄一份清单到脚本里,改名那天它就会悄悄说相反的话 —— 2026-09-22
# verify-skill-material-rule.sh 已经吃过这个亏,这里不重犯。
#
# 检查范围:所有**会教到作者**的文本 —— skills/、docs/MIAO_CONTENT_PACKAGE_V1.md,
# 以及仓库自带的样例 manifest 与自测夹具。代码自己说给自己听的不查
# (src/content/binding/MiaoContentDataBinding.cpp 是唯一真源,查它等于查它自己)。
#
# **它证不了什么(写下来,免得把"门绿了"当成"文档写对了")**:
# 它只比对 id。文档可以把真能力说错用途(例如说 weather.read 需要网络授权),或者
# 只字不提某个真能力 —— 这两种它都抓不到,要人工读。
# 它抓得住的:虚构一个能力、虚构一个输入通道、把已下线的名字留在文本里。这恰好是
# "忘了同步"最常呈现的形状。
# 还有一处已知盲区:同一行里出现否定词(不是 / 不提供 / 别写 ……)时,这个名字算
# "被点名否掉",不计入。这是为了不把 "audio.read 不是能力" 这类说明报成违规 ——
# 一处假失败比没有门更坏。代价是 "写 audio.read 会让它失效" 这种**夹在否定句里的
# 真违规**也会被跳过。实测确认过这一种,要人工读。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
import os, re, sys

root = sys.argv[1]

def read(rel):
    with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as f:
        return f.read()

# --- 1) 真源一:数据能力(前缀 → capability) --------------------------------
binding = read('src/content/binding/MiaoContentDataBinding.cpp')
capabilities = set(re.findall(
    r'if\s*\(\s*StartsWith\(\s*dataPath\s*,\s*L"([^"]+)"\s*\)\s*\)\s*return\s+L"([^"]+)"',
    binding))
if not capabilities:
    print('❌ 无法从 src/content/binding/MiaoContentDataBinding.cpp 解析 RequiredCapability')
    print('   (StartsWith(dataPath, L"...") return L"...") 形状变了,闸门需要跟着代码改)')
    sys.exit(2)
prefix_to_capability = {prefix: capability for prefix, capability in capabilities}
capability_set = set(prefix_to_capability.values())

# --- 2) 真源二:输入通道 -----------------------------------------------------
bus = read('src/include/miaodesk/MiaoInputBus.h')
channels = set(re.findall(r'inline constexpr std::wstring_view\s+k\w+\s*=\s*L"(input://[^"]+)"', bus))
if not channels:
    print('❌ 无法从 src/include/miaodesk/MiaoInputBus.h 解析输入通道表')
    print('   (ChannelShape 依赖这些常量;一个都没有说明形状变了)')
    sys.exit(2)

# 常量表的另一面:ChannelShape 必须**真的分支到**每一个通道。它比较的是常量名
# (kAudioBeat 这些),不是字面量,所以这里按 ChannelShape 体内出现的数组名反查:
# 往常量表里加一个通道却忘了进 ChannelShape,门照样绿 —— 而那个通道生产者写不进、
# 场景声明它就是无效。
shape_body = bus[bus.find('inline bool ChannelShape('):]
shape_end = shape_body.find('\n}\n')
if shape_end == -1:
    print('❌ 无法从 MiaoInputBus.h 切出 ChannelShape 的函数体')
    sys.exit(2)
shape_body = shape_body[:shape_end]
formally = set()
for m in re.finditer(r'channel\s*:\s*(k\w+)', shape_body):
    arr_name = m.group(1)
    arr = re.search(r'inline constexpr std::wstring_view\s+' + arr_name +
                    r'\s*\[\s*\]\s*=\s*\{(.*?)\}', bus, re.S)
    if not arr:
        print(f'❌ ChannelShape 用到数组 {arr_name},但头部找不到它的定义')
        sys.exit(2)
    for name in re.findall(r'\bk(\w+)\b', arr.group(1)):
        formally.add('k' + name)
# ChannelShape 里还有两条直接比常量的分支(frame/time 与 event/pulse)。
for name in re.findall(r'if\s*\(\s*id\s*==\s*(k\w+)\s*\)', shape_body):
    formally.add(name)

# 常量名 → 字面量,反查哪些通道没有对应的形状分支。
unshaped = []
for m in re.finditer(r'inline constexpr std::wstring_view\s+(k\w+)\s*=\s*L"(input://[^"]+)"', bus):
    if m.group(1) not in formally:
        unshaped.append(m.group(2))
if unshaped:
    print('❌ MiaoInputBus.h 声明了 ChannelShape 分支不到的输入通道:')
    for c in sorted(unshaped):
        print(f'      · {c} —— 加进了常量表但没进形状表,生产者写不进、场景声明它无效')
    sys.exit(1)

print('真实的数据能力(来自 RequiredCapability):')
for prefix in sorted(prefix_to_capability):
    print(f'   {prefix:<12} → {prefix_to_capability[prefix]}')
print(f'真实的输入通道(来自 MiaoInputBus.h):{len(channels)} 个')
for c in sorted(channels):
    print(f'   {c}')

# --- 3) 会教到作者的文本 ----------------------------------------------------
scope = []
skills_dir = os.path.join(root, 'skills')
if os.path.isdir(skills_dir):
    for dirpath, _dirs, files in os.walk(skills_dir):
        for name in files:
            if name.endswith('.md'):
                scope.append(os.path.relpath(os.path.join(dirpath, name), root))
for rel in ('docs/MIAO_CONTENT_PACKAGE_V1.md', 'docs/MIAODESK_CONTENT_FRAMEWORK.md',
            'docs/DESIGN_BASELINE.md'):
    if os.path.isfile(os.path.join(root, rel)):
        scope.append(rel)
# 仓库自带的样例:它们是"照着做就行"的实物范本。
for base in ('assets/wallpapers', 'assets/widgets'):
    base_path = os.path.join(root, base)
    if not os.path.isdir(base_path):
        continue
    for dirpath, _dirs, files in os.walk(base_path):
        for name in files:
            if name == 'manifest.json':
                scope.append(os.path.relpath(os.path.join(dirpath, name), root))
# 自测夹具:它写出来的东西是"一个合法的包"的活样本。
scope.append('src/content/package/MiaoContentPackage.cpp')

checked_capability = 0
checked_channel = 0
problems = []

# 一行里带否定说法时,那个名字是在被**点名否掉**,不是在声明。
# 不区分的话,"audio.read 不是能力"这种说明文字会被报成"虚构了一个能力" ——
# 一处假失败比没有门更坏:它让人把结论整个关掉。
NEGATIONS = ('不是', '不提供', '不存在', '无效', '没有', '别写', '不要', '不得',
             '不再', '曾经', '不在表内', 'outside the set')

def claimed_in_text(text, start, claimed):
    """这个名字出现在"声明它"的位置,还是"点名否掉它"的位置?"""
    line_start = text.rfind('\n', 0, start) + 1
    line_end = text.find('\n', start)
    if line_end == -1:
        line_end = len(text)
    return not any(word in text[line_start:line_end] for word in NEGATIONS)

for rel in sorted(set(scope)):
    path = os.path.join(root, rel)
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        continue
    for m in re.finditer(r'\b([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)*\.read)\b', text):
        claimed = m.group(1)
        if not claimed_in_text(text, m.start(), claimed):
            continue
        checked_capability += 1
        if claimed not in capability_set:
            line = text.count('\n', 0, m.start()) + 1
            problems.append(f'{rel}:{line} 声明了数据能力 "{claimed}",'
                            f'而 broker 不提供它(真实集合:{", ".join(sorted(capability_set))})。\n'
                            f'      → 虚构一个能力比缺失一个更难发现:缺失会亮红,'
                            f'虚构只会静悄悄地不工作。')
    for m in re.finditer(r'\b(input://[a-z0-9/_.-]+)', text):
        claimed = m.group(1)
        if not claimed_in_text(text, m.start(), claimed):
            continue
        # `input://audio/{level,bass,...}` 这种花括号简写不是通道名,是清单的缩写。
        # 把它当成一个被虚构出来的通道会报一串假失败,而门的结论是给人读的 ——
        # 喊狼来了几次之后,真的那一条也没人看了。
        if claimed.endswith('/') or text[m.end():m.end() + 1] == '{':
            continue
        checked_channel += 1
        if claimed not in channels:
            line = text.count('\n', 0, m.start()) + 1
            problems.append(f'{rel}:{line} 声明了输入通道 "{claimed}",'
                            f'而 MiaoInputBus.h 的闭集里没有它。\n'
                            f'      → 场景声明一个不存在的通道是无效的;'
                            f'要新通道得先在 MiaoInputBus.h 加常量并进 ChannelShape。')

print()
print(f'核对了 {len(set(scope))} 个文本,{checked_capability} 处能力声明、'
      f'{checked_channel} 处输入通道声明')

# 空洞模式:零条比对不可能报绿。
if checked_capability == 0 and checked_channel == 0:
    print('❌ 一处声明都没比到 —— 正则与文本都变了,这不是通过')
    sys.exit(2)

if problems:
    print()
    print(f'❌ {len(problems)} 处规范/文档说出了运行时没有的能力:')
    for p in problems:
        print(f'      · {p}')
    sys.exit(1)

print()
print('✅ 规范、契约文档、样例 manifest 里声名的能力与输入通道都在运行时闭集内')
sys.exit(0)
PY
