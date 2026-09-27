#!/usr/bin/env bash
# CCA-06 / CCA-14:skill 里指给别人看的路径必须真的在那儿。
#
# skills/README.md 一直写着"`assets/widgets/GlassClock.mdwidget/` — 可参照的完整示例"。
# 那个路径在**仓库里**是对的,在**安装出来的产品里**不存在:根 CMakeLists 装的是
#
#     install(DIRECTORY assets/widgets/ DESTINATION Widgets)
#
# 于是用户在一台装好的机器上照这句话找 `assets/widgets/...`,什么都找不到 ——
# 而这句话是 README 里唯一指向"照着做就行"的实物范本的一行。
# 计划 CCA-06 的"新增引用资源纳入打包与一致性检查"和 CCA-14 的"技能引用资源随包完整"
# 说的就是这一类:引用一个装不进包、或装到另一个名字下的路径。
#
# 判据两半,缺一不可:
#   1. **存在性** —— skill 与 README 里出现的每个仓库相对路径,必须在仓库里真的存在。
#      这一类最常发生在:文件被移动或重命名,而文档里的引用不会响。指向一个不存在的
#      范本比没有范本更坏:照着做的人会以为自己少了一步。
#   2. **安装布局一致性** —— 只要一个引用指向了某个被 install(DIRECTORY …) 装进产品的
#      目录,同一篇文档必须也说出它在安装布局下的名字。只写仓库路径,用户照着找就是空的。
#      安装目的地从 CMakeLists **读出**,不在脚本里再抄一份 —— 抄的那份会在改名那天
#      悄悄说相反的话。
#
# 它只认这些形状:反引号里的、以已知目录名开头的相对路径。所以它不是 Markdown 解析器,
# 也不校验措辞。
#
# **它证不了什么**:它不检查"这个范本本身是不是一个好范本",也不检查引用它的那句话
# 是否解释了该怎么用。它抓得住:路径指向不存在的东西、引用了一个装不进包的路径却说成
# 是安装后的位置、改了 DESTINATION 而文档没跟。这几种恰好是"忘了同步"最常呈现的形状。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
import os, re, sys

root = sys.argv[1]

# --- 1) 安装布局:从 CMakeLists 读出 ----------------------------------------
# install(DIRECTORY "<repo path>/" DESTINATION "<name>"). 文件名跨行,所以先按
# "install(DIRECTORY" 切段再在段内找两个实参。
with open(os.path.join(root, 'CMakeLists.txt'), encoding='utf-8') as handle:
    cmake = handle.read()
mapping = {}
for block in re.split(r'install\(\s*DIRECTORY', cmake)[1:]:
    source = re.search(r'"([^"]+/)"', block)
    dest = re.search(r'DESTINATION\s+"([^"]+)"', block)
    if not source or not dest:
        continue
    repo = re.sub(r'^\$\{[^}]+\}/', '', source.group(1)).strip('/')
    if repo:
        mapping[repo] = dest.group(1).strip('/')
if not mapping:
    print('❌ 无法从 CMakeLists.txt 解析任何 install(DIRECTORY ... DESTINATION ...)')
    print('   (闸门以安装布局为判据,读不出来就不能假装看过)')
    sys.exit(2)

print('安装布局(来自 CMakeLists.txt):')
for repo in sorted(mapping):
    print(f'   {repo}/  →  {mapping[repo]}/')

# 仓库里实际存在的顶层名,用来认出引用里的仓库相对路径。
known_prefixes = ('assets/', 'skills/', 'config/', 'docs/', 'tests/')

def exists(rel):
    return os.path.exists(os.path.join(root, rel))

# --- 2) 扫 skills/ 与几份用户会读的文档 ------------------------------------
targets = []
skills = os.path.join(root, 'skills')
if os.path.isdir(skills):
    for dirpath, _dirs, files in os.walk(skills):
        for name in files:
            if name.endswith('.md'):
                targets.append(os.path.relpath(os.path.join(dirpath, name), root))
for rel in ('README.md', 'docs/DOC-INDEX.md'):
    if os.path.isfile(os.path.join(root, rel)):
        targets.append(rel)

path_shape = re.compile(r'\b((?:' + '|'.join(known_prefixes).replace('.', r'\.') +
                        r')[A-Za-z0-9._\-/]*)')

problems = []
missing = 0
total = 0
for rel in sorted(set(targets)):
    with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as handle:
        text = handle.read()
    # 只看反引号跨度里的引用:那才是"指给别人看的一个路径"。正文里顺口提到 docs/
    # 或 config/ 不算范本引用 —— 否则一串假失败之后,真的那一条也没人看了。
    for span in re.finditer(r'`([^`\n]+)`', text):
        for m in path_shape.finditer(span.group(1)):
            ref = m.group(1).strip('/')
            line = text.count('\n', 0, span.start()) + 1
            total += 1
            if not exists(ref):
                missing += 1
                problems.append(
                    f'{rel}:{line} 引用了不存在的路径 `{ref}`\n'
                    f'      → 指向一个不存在的范本比没有范本更坏:照着做的人会以为自己少了一步。')
                continue
            # 被 install(DIRECTORY ...) 装进产品的,必须也说得出安装布局下的名字。
            owner = next((repo for repo in mapping
                          if ref == repo or ref.startswith(repo + '/')), None)
            if owner is None:
                continue
            install_name = mapping[owner]
            # 出现即算:后面跟着 '/' 或 '\' 或直接结束都行。写成 `skills/` 也就是
            # 说到了安装名 —— 第一版的后视把 '/' 也排掉了,于是 " skills/ " 这种
            # 正确写法被判成"一次都没提到",稳稳的假失败。
            if not re.search(r'(?<![\w-])' + re.escape(install_name) + r'(?![\w])', text):
                problems.append(
                    f'{rel}:{line} 引用了 `{ref}`,但它装在安装产品里的名字是 '
                    f'`{install_name}/`,本文档一次都没提到过\n'
                    f'      → 用户在一台装好的机器上照仓库路径找,什么都找不到。'
                    f'见 CMakeLists.txt 的 install(DIRECTORY {owner}/ DESTINATION {install_name})。')


print()
print(f'核对了 {len(set(targets))} 个文本,{total} 处路径引用')
if total == 0:
    print('❌ 一处路径引用都没比到 —— 形状或文本变了,这不是通过')
    sys.exit(2)

if problems:
    print()
    print(f'❌ {len(problems)} 处引用问题:')
    for problem in problems:
        print(f'      · {problem}')
    sys.exit(1)

print()
print('✅ skill 与 README 引用的路径都真实存在,且被装进产品的路径也说得出安装布局下的名字')
sys.exit(0)
PY
