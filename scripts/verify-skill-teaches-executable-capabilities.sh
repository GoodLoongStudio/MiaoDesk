#!/usr/bin/env bash
# CAP-02:可创作的能力必须被**教到**。
#
# 与 verify-skill-capability-contract.sh 是同一个问题的两个方向:
#   那一份查"教了不存在的能力"(虚构),这一份查"存在的能力没被教"(漏教)。
#
# 为什么漏教值得一道独立的门:它不会让任何东西变红。一个真实存在、写出来就有用的能力,
# Skill 里只字不提 —— 于是 AI 不知道它能用,作者手册里也找不到,而代码里它好好地实现着。
# 实测状态(2026-10-03):四份 Skill 提到 3 个场景组件 kind(spriteRenderer、videoRenderer,
# 后者还是作为禁止项),0 个资产类型。textRenderer 有完整的 D2D 实现、支持 {{data.*}}
# 模板,而没有任何一份 Skill 提到它 —— AI 因此不会用它显示日期或待办文字。
#
# 判据同样从代码读出:能力目录里 aiAuthorable && 可执行 的那些条目,就是"AI 应当会用的"。
# 再往 Skill 里抄一份清单就前功尽弃,所以这里解析 MiaoCapabilityCatalog.cpp 自己。
#
# **它证不了什么**:它只证明名字出现过。Skill 可以把真能力讲错(说 solidColor 有渐变)、
# 或者提一句就过去 —— 这两种它抓不到,要人工读。它抓得住的:新增了一个 AI 可创作的能力,
# 而四份 Skill 一个字都没提。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
import os, re, sys

root = sys.argv[1]

def read(rel):
    with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as f:
        return f.read()

catalog = read('src/content/binding/MiaoCapabilityCatalog.cpp')
table = catalog[catalog.find('constexpr std::array<CapabilityEntry'):
                catalog.find('static_assert(kCatalog.size()')]
if not table:
    print('❌ 无法从 MiaoCapabilityCatalog.cpp 切出能力表')
    sys.exit(2)

# 每条形如 {"id", ver, Carrier, Backends, {支持五态}, ...}
# 只要求:载体属于"作者要写的东西",AI 可创作,且真的可执行。
# CreatorTool 排除:工具由 creator_capabilities_get 自述,不是 Skill 教的内容。
AUTHORABLE_CARRIERS = {
    'ManifestCapability', 'SceneComponent', 'AssetType', 'InputChannel',
    'MaterialModel', 'PostProcessEffect', 'BindingResponse', 'DataPathPrefix',
}
entries = re.findall(r'\{"([^"]+)",\s*\d+,\s*CapabilityCarrier::(\w+),'
                     r'\s*([^,]+),\s*\{([^}]*)\}', table)
required = []
for ident, carrier, backends, support in entries:
    if carrier not in AUTHORABLE_CARRIERS:
        continue
    flags = [v.strip() for v in support.split(',')]
    if len(flags) < 4:
        continue
    declarable, executable, _previewable, ai_authorable = flags[:4]
    if ai_authorable != 'true' or executable != 'true':
        continue
    if 'BackendBit(None)' in backends:
        continue
    required.append(ident)

if not required:
    print('❌ 一条可创作能力都没解析到 —— 表的形状大概变了,闸门需要跟着改')
    sys.exit(2)

skills = []
skills_dir = os.path.join(root, 'skills')
for dirpath, _dirs, files in os.walk(skills_dir):
    for name in files:
        if name == 'SKILL.md':
            skills.append(read(os.path.relpath(os.path.join(dirpath, name), root)))
if not skills:
    print('❌ 没有找到任何 SKILL.md')
    sys.exit(2)
corpus = '\n'.join(skills)

# 目录 id 不是作者敲的名字。scene.spriteRenderer 在 scene.json 里写 spriteRenderer,
# binding.response.linear 的 JSON 键是 linear,material.builtin.solidColor 写 solidColor。
# 所以判"教没教"要按**作者写的那个名字**去找,否则门会永远红,而永远红的门等于没有。
# 每条规则都写清为什么可以这么放宽 —— 放宽就是放过,不能顺手多放。
def authoring_names(ident):
    names = {ident}
    if '://' in ident:
        # input://audio/x 这类在文档里常写成 input://audio/*(整族一起讲)。
        # 只放到"剩最后一段之前"为止:再放宽就等于整族都算教过。
        names.add(ident.rsplit('/', 1)[0])
        return names
    if ident.startswith('data.'):
        # data.time 的作者形态是 time. 前缀(time.*)
        names.add(ident.split('.', 1)[1] + '.')
        return names
    # 其余:最后一段就是作者写的值(binding.response.linear → linear,
    # asset.image → image, scene.spriteRenderer → spriteRenderer)。
    names.add(ident.rsplit('.', 1)[-1])
    return names

missing = [ident for ident in required
           if not any(name and name in corpus for name in authoring_names(ident))]

print("可创作且可执行的能力:%d 条;Skill 未提到:%d 条" % (len(required), len(missing)))
for ident in missing:
    print("  ❌ 没有任何一份 Skill 提到 %s" % ident)
if missing:
    print("   补法:在对应 Skill 的能力卡里加一节,说明它怎么写、限制是什么、")
    print("   以及它在哪个后端可用。不要只把名字塞进一句话 —— 那样门会绿,AI 仍然不会用。")
    sys.exit(1)
print("✅ 每条可创作能力都至少被一份 Skill 提到")
PY
