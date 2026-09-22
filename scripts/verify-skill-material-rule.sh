#!/usr/bin/env bash
# 校验 skills/ 里关于 sprite 材质的规则,与渲染器实际执行的规则一致。
#
# 为什么需要它:`skills/content-package-basics/SKILL.md` 原先只写了一句
# "material 优先引用 builtin 模型(builtinName)",而渲染器实际只接受
# `builtinName: "solidColor"` 一个值,并且 programmable material 只有 D3D11
# 能画、texture + material 同时写会被拒。结果是 skill 在教作者写**渲染器会拒绝
# 的包** —— 而用户用本工程 skill 制作壁纸正是产品目标,规范与实现脱节,
# 创作链就断在最后一环。
#
# 补上规则之后又有一个新问题:它们是几处手写文本。这个门把"手写"变成可检查的:
#   * 代码侧唯一真源是 MiaoSpriteMaterialPolicy.cpp —— 从它**读出**支持的 builtin 名,
#     不在这里再抄一份。抄的那份会在改名那天悄悄说相反的话。
#   * **每条规则都按小节比**,不是全文搜关键词。第一版按全文搜,把正面提示词里的
#     "只有 builtinName:"solidColor" 一种能用"整条删掉后门仍然报绿 —— 因为
#     "solidColor" 这个词在反面提示词里还活着。一个能被无关上下文满足的检查,
#     等于没检查,而且它给出的"✅"比没有门更坏。
#   * 输出比对了多少条。零条比对不可能报绿(2026-09-22 verify-workflow-paths.sh
#     空洞模式的教训)。
#
# 它不解析 Markdown、不校验措辞,只钉住那些一旦脱节就会让 skill 教出坏包的、
# 可枚举的事实。
#
# **它证不了什么(写下来,免得把"门绿了"当成"skill 写对了")**:
# 判据是关键词是否出现在指定小节,所以"删掉这句话、但同一个词还在该小节别处
# 合法出现"是抓不到的。实测确认过两种情况:把「只有 builtinName:"solidColor"
# 一种能用」整句删掉、但该小节还剩「`solidColor` 的 `color` 属性会给贴图染色」
# —— 门仍然绿。这类改写要人工发现。
# 它抓得住的:代码侧改名(最要命的一种,会静默拒绝全部合法内容)、规则整段消失、
# 规则被挪到别的小节、清单项被删。这几类恰好是"忘了同步"最常呈现的形状。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
POLICY="$ROOT/src/content/render/MiaoSpriteMaterialPolicy.cpp"
BASICS="$ROOT/skills/content-package-basics/SKILL.md"
REVIEW="$ROOT/skills/content-review/SKILL.md"

python3 - "$POLICY" "$BASICS" "$REVIEW" <<'PY'
import glob
import re
import sys

policy_path, basics_path, review_path = sys.argv[1:4]
policy = open(policy_path, encoding='utf-8').read()

problems = []
checks = 0


def sections(path):
    """按 ^## 标题切开 SKILL.md。规则按小节比对,才不会被别的上下文满足。

    切法必须真的切开:第一版用 partition 循环,结果除最后一节外每一节都拿到
    剩下的全部文本 —— 于是"正面提示词里必须有 solidColor"这条永远被
    "反面提示词里也有 solidColor"满足,门给出稳定的假绿。切完必须自证:
    每个小节的文本不能再含下一个小节的标题。
    """
    text = open(path, encoding='utf-8').read()
    parts = re.split(r'(?m)^## ', text)
    out = {'__front__': parts[0]}
    for part in parts[1:]:
        title = part.split('\n', 1)[0].strip()
        if title in out:
            problems.append('{} 有重复的小节标题「{}」。'.format(path, title))
            continue
        out[title] = part
    cross = [t for t in ('正面提示词', '反面提示词') if t in out]
    if len(cross) == 2:
        for a, b in ((cross[0], cross[1]), (cross[1], cross[0])):
            if '\n' + b in out[a]:
                problems.append('{} 的「{}」小节含有「{}」的内容 —— 小节没被切开,'
                                '按小节比对的规则会互相满足。'.format(path, a, b))
    return out


basics = sections(basics_path)
review = sections(review_path)


def expect(section_map, path, section, needle, why):
    global checks
    checks += 1
    if section not in section_map:
        problems.append("{path} 里没有小节「{section}」。规则该待的地方没了,"
                        "—— {why}".format(path=path, section=section, why=why))
        return
    if needle not in section_map[section]:
        problems.append("{path} 的「{section}」小节没有规定「{needle}」—— {why}".format(
            path=path, section=section, needle=needle, why=why))


# 代码侧只认一个 builtin 名。从代码读出来,不在这里重复写。
names = sorted(set(re.findall(r'builtinName\s*==\s*L"([^"]+)"', policy)))

# 渲染器里还各有一份同名单词。它们必须与策略里的同名,否则 skill 教的就是错的那个。
render_root = policy_path.rsplit('/', 1)[0]
other_hardcoded = set()
for renderer in (glob.glob(render_root + '/d2d/*.cpp') + glob.glob(render_root + '/d3d11/*.cpp')):
    for hit in re.findall(r'builtinName\s*==\s*L"([^"]+)"', open(renderer, encoding='utf-8').read()):
        other_hardcoded.add(hit)

checks += 1
if other_hardcoded and other_hardcoded != set(names):
    problems.append(
        "渲染器里硬编码的 builtin 名 {},与策略实现的 {} 不一致。两边说的是同一件事,"
        "但现在说成了两回事 —— 任何一侧改名,另一侧会静默拒绝合法内容。".format(
            sorted(other_hardcoded), names))

checks += 1
if len(names) != 1:
    problems.append(
        "策略实现认可的 builtin 名是 {},不是恰好一个。这个门按「恰好一个」写的:"
        "若真的增加了第二个,先把这条规则连同 skills/ 一起改,别让门默默通过。".format(names))

supported = names[0] if len(names) == 1 else None
B = 'content-package-basics'

# --- 正面提示词:作者必须知道的规则 ---
expect(basics, B, '正面提示词', 'texture',
       'sprite 取图的第一条路没写,作者会以为必须走 material')
expect(basics, B, '正面提示词', 'materialId',
       'sprite 取图的第二条路没写')
expect(basics, B, '正面提示词', 't0',
       '两条路为什么互斥(共用 t0)没写,作者不理解为什\n'
       '         么两个都写会被拒')
if supported:
    expect(basics, B, '正面提示词', supported,
           '唯一可用的 builtin 名没有出现 —— 代码只接受 ' + supported +
           ',skill 不写就等于让作者去猜')
expect(basics, B, '正面提示词', 'programmable',
       'programmable material 没提,作者会以为 D2D 也能画')
expect(basics, B, '正面提示词', 'D3D11',
       '没有说明 programmable 只在 D3D11 后端可用')
expect(basics, B, '正面提示词', 'tint',
       'tint 在两个后端上的差异没写,而这是当前唯一一处「同一个包在一个后端'
       '能加载、在另一个被拒」的合规差异')

# --- 反面提示词:作者必须知道的红线 ---
expect(basics, B, '反面提示词', 'programmable material',
       '同时写 programmable material 与 texture 这条红线没有写')
expect(basics, B, '反面提示词', 'tint',
       '贴图 sprite 的非白色 tint 这条红线没有写')

# --- content-review:门禁清单里必须核查得到的项 ---
R = 'content-review'
expect(review, R, '领域检查(按 kind 二选一)', 'texture',
       '清单里没有 texture 项 —— 门禁会放过 materially 违规的包')
expect(review, R, '领域检查(按 kind 二选一)', 'materialId',
       '清单里没有 materialId 项')
expect(review, R, '领域检查(按 kind 二选一)', 'solidColor',
       '清单里没有 builtinName 必须是 solidColor 这一项')
expect(review, R, '领域检查(按 kind 二选一)', 'tint',
       '清单里没有 tint 项')

print("策略认可的 builtin 名:      " + (str(names) if names else '(没读到)'))
print("渲染器里另外硬编码的名字:   " + (str(sorted(other_hardcoded)) if other_hardcoded else '(无)'))
print("按小节比对的规则:           {} 条".format(checks))
print("content-package-basics:     {} 个小节".format(len(basics)))
print("content-review:             {} 个小节".format(len(review)))

if problems:
    print()
    print("❌ {} 处 skills 与渲染器规则不一致:".format(len(problems)))
    for p in problems:
        print("      · " + p)
    print()
    print("  为什么这条门拦得住真问题:skill 是用户制作壁纸时唯一读到的规范。")
    print("  规范说能用、渲染器却拒绝,作者拿到的报错点名的是他完全没写错的东西;")
    print("  反过来规范没说不能用、渲染器却悄悄收下,就是一块没有测试的空地。")
    sys.exit(1)

print()
print("✅ 通过,但只证明下面这些,不证明措辞:")
print("   代码侧认可的 builtin 名与渲染器硬编码一致,且它出现在 basics 的正面提示词里;")
print("   两条取图路径(texture / materialId)、t0 互斥的原因、programmable 仅 D3D11、")
print("   tint 的跨后端差异,都在 basics 的正面提示词里;")
print("   对应红线在 basics 的反面提示词里;texture / materialId / solidColor / tint")
print("   四项在 content-review 的领域检查清单里。")
print("   ⚠ 判据是关键词命中,同小节内换一种说法仍可能命中或漏掉 —— 见脚本头部的")
print("   「它证不了什么」。这是已知上限,不是通过。")
sys.exit(0)
PY
