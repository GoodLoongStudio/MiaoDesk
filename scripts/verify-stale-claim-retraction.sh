#!/usr/bin/env bash
# CCA-14:撤回过的断言不许再回来。
#
# 计划原文:"修正仍称 AI 不能生成组件的旧表述"。BASE-03 已经修过**三处**同一句的
# 副本(AI_GENERATED_DESKTOP_SANDBOX.md、PiNativeToolsExtension.cpp、PiRuntime.cpp),
# 但漏了第四处,而它恰恰是最容易被当成契约读的那一处:
#   docs/L3-PI-RUNTIME_CONDITION.md → docs/L3-PI-RUNTIME-CONTRACT.md:91
# 2026-09-28 才在 CCA-14 里发现并撤回。
#
# 为什么值得一个门:这句话错得很"讲得通"。它说的是"没有**工具**能改现役组件状态",
# 听上去是一句安全承诺;但它把 Content Creator 一起否掉了 —— 而用户主动发起的
# `✨ AI 制作组件` 正是要产出 `.mdwidget` 内容包。一句错的约束比没有约束更坏:
# 它会让人据此砍掉一个已经实现的功能,或者拒绝修一个本可以修的问题。
#
# 判据:
#   1. 那几句被判定为错的原文,**不允许再出现在任何活文档里**。允许出现在
#      更正记录里的文件必须显式列出并写明理由 —— 删掉它们会让"我们曾经这么说过、
#      现已撤回"失去可追溯性,那不是变干净,是变健忘。
#   2. 更正记录本身必须还在:AI_GENERATED_DESKTOP_SANDBOX.md 的三面对照表、
#      以及"AI 输出是数据,不是代码"这条跨三面的不变式。表里的代码引用
#      (DesktopWidgetTools.cpp:107 等)由 verify-doc-code-citations.sh 单独核对。
#
# 两侧都要比到东西,零比对不可能报绿。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
import os, sys

root = sys.argv[1]

# 判定为错的原文。它们是整句,不是关键词:"组件"这个词本身没有错,
# 错的是把"读不到"说成"不能做出来"。
STALE = (
    '不能创建或修改组件',
    '不能生成组件',
    '不能生成/预览组件',
    'Widget 是 Native-only preset',
    'AI 只能读取其状态',
    'cannot preview, generate, or apply widgets',
)

# 这些文件是**更正记录**,不是活的断言。每一条都得说清为什么允许。
# 这是 2026-09-22 verify-native-source-hygiene.sh 里 appdata_allow 的同一个做法:
# 例外必须显式、可数、带理由,否则它会静悄悄地长成一个窟窿。
RETRACTION_RECORD = {
    'docs/AI_GENERATED_DESKTOP_SANDBOX.md': '更正记录本体:顶部 Corrected 段与三面对照表',
    'docs/TODO.md': 'BASE-03 的"现状/依据"栏:记录当时哪里还在说错',
    'docs/DOC-INDEX.md': 'DOC-INDEX 的摘要:说明该文档是更正记录',
    'docs/CONTENT_CREATOR_AGENT_PLAN.md': 'CCA-14 的实施条目本身在描述要修的对象',
    'docs/L3-PI-RUNTIME-CONTRACT.md': '本节自己声明的撤回说明(同一段的撤回句)',
}

# 更正记录必须仍然成立的形状。
MUST_HAVE = {
    'docs/AI_GENERATED_DESKTOP_SANDBOX.md': [
        ('| Pi tool surface', '三面对照表的 Pi 工具面'),
        ('| AI Content Creator', '三面对照表的 Content Creator 面'),
        ('| Formal widget APIs', '三面对照表的正式 API 面'),
        ('AI output is data, not code', '跨三面的不变式'),
    ],
}


def read(rel):
    try:
        with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as handle:
            return handle.read()
    except OSError:
        return None


print('比对撤回过的断言(活的文档里不许再出现):')
problems = []
compared = 0
allowed = 0
for dirpath, dirs, files in os.walk(os.path.join(root, 'docs')):
    dirs[:] = [d for d in dirs if d != 'history']
    for name in files:
        if not name.endswith('.md'):
            continue
        rel = os.path.relpath(os.path.join(dirpath, name), root).replace(os.sep, '/')
        text = read(rel)
        if text is None:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            for phrase in STALE:
                if phrase not in line:
                    continue
                compared += 1
                if rel in RETRACTION_RECORD:
                    allowed += 1
                    continue
                problems.append(
                    f'{rel}:{lineno} 仍写着被撤回的断言:`{phrase}`\n'
                    f'      → 它把 Content Creator 一起否掉了。更正记录见 '
                    f'docs/AI_GENERATED_DESKTOP_SANDBOX.md。\n'
                    f'      → 如果这确实是更正记录而非活的断言,请把它加进 '
                    f'scripts/verify-doc-retraction-claims.sh 的 RETRACTION_RECORD 并写明理由。')
print(f'   {compared} 处提到,其中 {allowed} 处属于显式登记的更正记录')

print()
print('核对更正记录本身仍然成立:')
present = 0
for rel, needles in MUST_HAVE.items():
    text = read(rel)
    if text is None:
        problems.append(f'{rel} 不存在 —— 更正记录不能只剩一句"曾经错",要留下对的那份')
        continue
    for needle, what in needles:
        present += 1
        if needle not in text:
            problems.append(f'{rel} 缺少 {what}(找不到 `{needle}`)—— 撤回不等于改写,'
                            f'对的那一面必须仍然写在那里')

print(f'   {present} 处关键表述')

if compared == 0 or present == 0:
    print('❌ 一处都没比到 —— 文本或正则变了,这不是通过')
    sys.exit(2)

if problems:
    print()
    print(f'❌ {len(problems)} 处问题:')
    for problem in problems:
        print(f'      · {problem}')
    sys.exit(1)

print()
print('✅ 撤回过的断言没有回到活的文档里,更正记录本身仍完整')
sys.exit(0)
PY
