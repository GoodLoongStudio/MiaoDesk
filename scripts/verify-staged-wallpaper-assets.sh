#!/usr/bin/env bash
# 校验 packaging/windows/stage.ps1 的手写壁纸资产清单与包内真实引用的资产一致。
#
# 为什么需要它:`stage.ps1` 用一份**手写**的文件列表断言"打包产物里必须有这些文件"。
# 而这份清单是这段流程里唯一逐文件列举资产的地方,读它的人会把它当成"这就是全部资产"。
# 哪天有人删掉一张图、或者给某个包加一张,列表不会响 —— 而"打包出来的产品里少一张壁纸
# 图层"恰恰是 P0-4 记录过的那类静默失败。
#
# 判据不是又抄一份清单,而是从每个包**真正生效的入口**推出应有的集合,再与 stage.ps1
# 里出现的路径比对。这样新增或删除资产只改一处,两边就不会分叉。
#
# ---------------------------------------------------------------------------
# 2026-09-22:这个门此前只看 scene.json,而产品从不读它。
#
# `WallpaperPackage.cpp` 里入口是这么定的:
#     legacy_entry = ExtractJsonString(json, "legacy_entry");
#     entry        = !legacy_entry.empty() ? legacyEntry : ExtractJsonString(json, "entry");
# 也就是 **legacy_entry 优先**。而三个包的 manifest.json 全都同时写着
# `"entry": "scene.json"` 与 `"legacy_entry": "scene.ini"` —— 于是产品三个包全都加载
# scene.ini,`entry` 指向的 scene.json 一个都没被读过。
#
# 后果:这个门按 scene.json 的 assets[] 推集合,于是
#   · MiaoCloud 的 scene.json 恰好好填了 5 个资产 → 被覆盖(而且是因为两份恰好一致,
#     不是因为门读对了文件);
#   · NeonCity 与 MysticMoon 的 scene.json 是空壳、0 个资产 → 门对它们的 10 个真实
#     资产**一无所知**。少任何一个文件,这门照样全绿。
# 也就是说三个包里两个包的资产完全没有检查兜底,而门自己报告"✅ 覆盖了每个资产"。
#
# 修法不是给 scene.json 补资产(那是 P0-4 的迁移工作),而是让门和产品读同一个文件:
# 按 manifest 解出生效入口,再按入口的种类取资产 —— scene.json 走 assets[].source,
# scene.ini 走 [Layer*] 的 file。生效入口会连同结果一起打印,这样"门在检查哪个文件"
# 不再是隐含假设。
# 2026-09-24:三个内置动态壁纸完成 Scene Runtime 迁移后,产品包不再允许
# legacy_entry / scene.ini 回流。历史 INI 只保留在 tests/fixtures 作为迁移证据。
# 这道门因此也承担一个 release invariant: shipped .mdwall 必须只有 canonical entry。
# ---------------------------------------------------------------------------
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
import configparser
import json
import os
import re
import sys

root = sys.argv[1]
stage_ps1 = os.path.join(root, 'packaging', 'windows', 'stage.ps1')
wallpapers = os.path.join(root, 'assets', 'wallpapers')
BUILTIN_SCENE_PACKAGES = {
    'MiaoCloud.mdwall',
    'NeonCity.mdwall',
    'MysticMoon.mdwall',
}

problems = []


def strip_ps_comments_and_scan_quotes(text):
    """Every single-quoted PowerShell string literal in the file, comments removed.

    The previous implementation was one regex over the raw file:
        re.findall(r"'([^']*Wallpapers[^']*)'", text)
    That matches *any* two apostrophes with Wallpapers between them — including
    the gap between the end of one list item and the start of the next, when a
    comment in that gap happens to mention Wallpapers. The asset list's own
    comment does (it explains `install(DIRECTORY assets/wallpapers/ DESTINATION
    Wallpapers)`), so the first quoted path after it was swallowed whole and
    never checked.

    It went unnoticed for a while because stage.ps1 happened to repeat one of
    those paths in a second hand-written list. Deriving that second list from the
    staged tree (rather than naming 4 of 15 images) removed the duplicate and the
    hole became visible — which is the point: a defect masked by a coincidence is
    still a defect.

    So walk the file and track PowerShell's own string/comment states. `#` starts
    a line comment only outside a string; `<# ... #>` is a block comment; `''`
    inside a single-quoted string is an escaped apostrophe, not the end of it.
    """
    literals = []
    i, n = 0, len(text)
    state = 'code'          # code | sq | dq | block
    start = 0
    while i < n:
        ch = text[i]
        if state == 'block':
            if text.startswith('#>', i):
                state = 'code'
                i += 2
                continue
            i += 1
            continue
        if state == 'sq':
            if ch == "'":
                if i + 1 < n and text[i + 1] == "'":
                    i += 2
                    continue
                literals.append(text[start + 1:i])
                state = 'code'
            i += 1
            continue
        if state == 'dq':
            if ch == '"':
                state = 'code'
            i += 1
            continue
        # code
        if text.startswith('<#', i):
            state = 'block'
            i += 2
            continue
        if ch == "'":
            state = 'sq'
            start = i
            i += 1
            continue
        if ch == '"':
            state = 'dq'
            i += 1
            continue
        if ch == '#':
            j = text.find('\n', i)
            i = n if j == -1 else j
            continue
        i += 1
    return {lit for lit in literals if 'Wallpapers' in lit}

stage_text = open(stage_ps1, encoding='utf-8').read()
asserted = strip_ps_comments_and_scan_quotes(stage_text)


def windows_relative(path):
    return path.replace('/', '\\')


def load_json(path):
    with open(path, encoding='utf-8') as fh:
        return json.load(fh)


def effective_entry(package_dir):
    """The entry the product will actually load, and why.

    Mirrors WallpaperPackage.cpp: legacy_entry wins when present. Reading
    manifest.json's "entry" directly — which is what this gate used to do — is a
    second opinion about which file is authoritative, and the product does not
    share it.
    """
    manifest = load_json(os.path.join(package_dir, 'manifest.json'))
    legacy = (manifest.get('legacy_entry') or '').strip()
    if legacy:
        return legacy, 'legacy_entry'
    return (manifest.get('entry') or '').strip(), 'entry'


def declared_assets(package_dir, entry):
    """(source, why) pairs for every asset the effective entry references."""
    full = os.path.join(package_dir, entry)
    if entry.lower().endswith('.json'):
        doc = load_json(full)
        pairs = []
        for asset in doc.get('assets', []):
            source = asset.get('source')
            if source:
                pairs.append((source.replace('\\', '/'), asset.get('id', '?')))
        return pairs, 'scene.json assets[].source'
    if entry.lower().endswith('.ini'):
        ini = configparser.ConfigParser()
        # read_string on the text we read ourselves, not read(): read() silently
        # ignores a file it cannot open and returns what it did read, so a wrong
        # path here would produce an empty layer list that looks exactly like
        # "this package declares no layers". A loud failure is the only useful one.
        with open(full, encoding='utf-8') as fh:
            ini.read_string(fh.read(), source=full)
        pairs = []
        for section in ini.sections():
            if not section.startswith('Layer'):
                continue
            value = ini[section].get('file')
            if value:
                pairs.append((value.replace('\\', '/'), section))
        return pairs, 'scene.ini [Layer*] file'
    return [], 'unrecognised entry extension'


declared = {}
resolved = {}
for name in sorted(os.listdir(wallpapers)):
    package = os.path.join(wallpapers, name)
    if not os.path.isdir(package):
        continue
    manifest = os.path.join(package, 'manifest.json')
    if not os.path.isfile(manifest):
        continue
    try:
        manifest_doc = load_json(manifest)
    except (OSError, ValueError) as exc:
        problems.append(f"{name}/manifest.json 读不了:{exc}")
        continue
    if name in BUILTIN_SCENE_PACKAGES:
        if (manifest_doc.get('legacy_entry') or '').strip():
            problems.append(f"{name} 又声明了 legacy_entry —— 内置壁纸必须只走 canonical scene.json")
        if os.path.isfile(os.path.join(package, 'scene.ini')):
            problems.append(f"{name} 又携带了 scene.ini —— legacy 运行时文件不得重新进入发货包")
    try:
        entry, why = effective_entry(package)
    except (OSError, ValueError) as exc:
        problems.append(f"{name}/manifest.json 读不了:{exc}")
        continue
    if not entry:
        problems.append(f"{name} 的 manifest.json 没有 entry 也没有 legacy_entry")
        continue
    entry_path = os.path.join(package, entry)
    if not os.path.isfile(entry_path):
        problems.append(f"{name} 的生效入口 {entry} 不存在 —— 产品会在这里失败")
        continue
    try:
        pairs, basis = declared_assets(package, entry)
    except (OSError, ValueError) as exc:
        problems.append(f"{name} 的生效入口 {entry} 读不了:{exc}")
        continue
    declared[name] = pairs
    resolved[name] = (entry, why, basis)

for name, pairs in sorted(declared.items()):
    for source, asset_id in pairs:
        expected = f"Wallpapers\\{name}\\{windows_relative(source)}"
        if expected not in asserted:
            problems.append(f"{name} 的资产 {source}(id={asset_id})"
                            f"没有出现在 stage.ps1 的断言清单里 —— 它不会被检查到是否真的"
                            f"进了打包产物。应补:{expected}")

# 反向:清单里有、包里没有声明的路径(不是错误,但要报出来 —— 它说明清单在描述一个
# 已不存在的资产,或者清单比入口文件更权威,两种情况都需要人确认)。
asserted_asset_paths = {p for p in asserted if re.search(r'\.mdwall\\assets\\', p)}
for path in sorted(asserted_asset_paths):
    parts = path.split('\\')
    if len(parts) < 4:
        problems.append(f"stage.ps1 里的壁纸资产路径形状不认识:{path}")
        continue
    name = parts[1]
    # parts[2:] 而不是 parts[3:]:相对包的路径还要带上 assets/ 这一层目录,
    # 否则 'background.jpg' 永远匹配不上入口文件里的 'assets/background.jpg',
    # 每一条既有断言都会被误报成"没有引用它"。
    source = '/'.join(parts[2:])
    if name not in declared:
        problems.append(f"stage.ps1 断言了 {path},但 assets/wallpapers/{name} 不存在或没有 manifest.json")
        continue
    if not any(source == s for s, _ in declared[name]):
        problems.append(f"stage.ps1 断言了 {path},而 {name} 的生效入口并没有引用它 "
                        f"(删了资产却忘了删断言,还是清单比入口文件更权威?)")

print(f"壁纸包:               {len(declared)} 个({', '.join(sorted(declared))})")
print("生效入口(产品真正读的文件):")
for name in sorted(resolved):
    entry, why, basis = resolved[name]
    print(f"  {name:<18} {entry}  [{why} 优先]  <- {len(declared[name])} 个资产,取自 {basis}")
total = sum(len(v) for v in declared.values())
print(f"入口文件声明资产:     {total} 个")
print(f"stage.ps1 断言路径:   {len(asserted)} 条,其中壁纸资产 {len(asserted_asset_paths)} 条")

if problems:
    print()
    print(f"❌ {len(problems)} 处打包断言清单与生效入口不一致:")
    for p in problems:
        print(f"      · {p}")
    print()
    print("  真正的拷贝是 CMake 的 install(DIRECTORY assets/wallpapers/ ...)(整目录),")
    print("  所以这些只是断言缺口,不是今天的发货缺陷。但 stage.ps1 是唯一逐文件列举")
    print("  资产的地方,缺口会让'少一张壁纸图'这种事没有检查兜底。")
    sys.exit(1)

print()
print("✅ stage.ps1 的断言清单覆盖了每个包生效入口声明的资产")
sys.exit(0)
PY
