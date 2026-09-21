#!/usr/bin/env bash
# 校验 packaging/windows/stage.ps1 的手写壁纸资产清单与包内真实引用的资产一致。
#
# 为什么需要它:`stage.ps1` 用一份**手写**的文件列表断言"打包产物里必须有这些文件",
# 其中包含四项 MiaoCloud 的图片(background.jpg / cat.png / tail.png / blink.png)。
# 而 MiaoCloud 的 scene.json 引用**五**张 —— 漏了 assets/cloud.png。
#
# 今天这不是一个发货缺陷:真正的拷贝发生在 CMake 的
# `install(DIRECTORY assets/wallpapers/ DESTINATION Wallpapers)`,整目录拷贝,
# cloud.png 会进去。所以缺的只是"断言清单"里的一项。
#
# 但它正是那种会变成缺陷的缺口:那份列表是这段流程里唯一逐文件列举资产的地方,
# 读它的人会把它当成"这就是全部资产"。哪天有人删掉一张图、或者给某个包加一张,
# 列表不会响 —— 而"打包出来的产品里少一张壁纸图层"恰恰是 P0-4 刚刚记录过的那类
# 静默失败(NeonCity / MysticMoon 干脆没有任何资产文件)。
#
# 判据不是又抄一份清单,而是从 **scene.json 的 assets[].source** 推出来应有的集合,
# 再与 stage.ps1 里出现的路径比对。这样新增或删除资产只改一处,两边就不会分叉。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
import json, os, re, sys

root = sys.argv[1]
stage_ps1 = os.path.join(root, 'packaging', 'windows', 'stage.ps1')
wallpapers = os.path.join(root, 'assets', 'wallpapers')

problems = []

stage_text = open(stage_ps1, encoding='utf-8').read()
# stage.ps1 的路径用 Windows 反斜杠写在带引号的字符串里。
asserted = set(re.findall(r"'([^']*Wallpapers[^']*)'", stage_text))


def windows_relative(path):
    return path.replace('/', '\\')


# 每个 .mdwall 包,按它自己 scene.json 的声明推出应有的资产路径。
declared = {}
for name in sorted(os.listdir(wallpapers)):
    package = os.path.join(wallpapers, name)
    scene = os.path.join(package, 'scene.json')
    if not os.path.isfile(scene):
        continue
    try:
        doc = json.load(open(scene, encoding='utf-8'))
    except (OSError, ValueError) as exc:
        problems.append(f"{name}/scene.json 读不了:{exc}")
        continue
    sources = []
    for asset in doc.get('assets', []):
        source = asset.get('source')
        if not source:
            continue
        sources.append((source, asset.get('id', '?')))
    declared[name] = sources

for name, sources in sorted(declared.items()):
    for source, asset_id in sources:
        expected = f"Wallpapers\\{name}\\{windows_relative(source)}"
        if expected not in asserted:
            problems.append(f"{name} 的资产 {source}(id={asset_id})"
                            f"没有出现在 stage.ps1 的断言清单里 —— 它不会被检查到是否真的"
                            f"进了打包产物。应补:{expected}")

# 反向:清单里有、包里没有声明的路径(不是错误,但要报出来 —— 它说明清单在描述一个
# 已不存在的资产,或者清单比 scene.json 更权威,两种情况都需要人确认)。
asserted_asset_paths = {p for p in asserted if re.search(r'\.mdwall\\assets\\', p)}
for path in sorted(asserted_asset_paths):
    parts = path.split('\\')
    if len(parts) < 4:
        problems.append(f"stage.ps1 里的壁纸资产路径形状不认识:{path}")
        continue
    name = parts[1]
    # parts[2:] 而不是 parts[3:]:相对包的路径还要带上 assets/ 这一层目录,
    # 否则 'background.jpg' 永远匹配不上 scene.json 里的 'assets/background.jpg',
    # 每一条既有断言都会被误报成"没有引用它"。
    source = '/'.join(parts[2:])
    if name not in declared:
        problems.append(f"stage.ps1 断言了 {path},但 assets/wallpapers/{name} 不存在或没有 scene.json")
        continue
    if not any(source == s for s, _ in declared[name]):
        problems.append(f"stage.ps1 断言了 {path},而 {name} 的 scene.json 并没有引用它 "
                        f"(删了资产却忘了删断言,还是清单比 scene.json 更权威?)")

print(f"壁纸包:               {len(declared)} 个({', '.join(sorted(declared))})")
total = sum(len(v) for v in declared.values())
print(f"scene.json 声明资产:  {total} 个")
print(f"stage.ps1 断言路径:   {len(asserted)} 条,其中壁纸资产 {len(asserted_asset_paths)} 条")

if problems:
    print()
    print(f"❌ {len(problems)} 处打包断言清单与 scene.json 不一致:")
    for p in problems:
        print(f"      · {p}")
    print()
    print("  真正的拷贝是 CMake 的 install(DIRECTORY assets/wallpapers/ ...)(整目录),")
    print("  所以这些只是断言缺口,不是今天的发货缺陷。但 stage.ps1 是唯一逐文件列举")
    print("  资产的地方,缺口会让'少一张壁纸图'这种事没有检查兜底。")
    sys.exit(1)

print()
print("✅ stage.ps1 的断言清单覆盖了每个 scene.json 声明的资产")
sys.exit(0)
PY
