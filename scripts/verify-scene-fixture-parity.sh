#!/usr/bin/env bash
# 校验两处贴图场景 fixture 是逐字节一致的。
#
# 为什么需要它:MiaoSceneD2DRenderer.cpp 的 SelfTest(Windows-only,真实渲染、读回像素)
# 和 tests/SceneTextureFixture.cpp(纯逻辑,每一轮都跑)各带一份同一个场景 JSON。分成
# 两份是故意的 —— 前者要真画出来,后者要在本机就能验 fixture 的 schema 是否写对 ——
# 但两份这就要求它们永不分叉。
#
# 分叉的后果很具体且很难发现:本机那份永远绿着,而 Windows 那份只有在跑真渲染时才可能
# 因为一个字段名写错而失败。届时报的是"绘制失败",没有任何线索指向 fixture。
# 这个脚本把"两份必须一致"变成一条本地可跑的硬检查。
#
# 判据是提取出来的字面量本体,不是文件名或行号:
#   渲染器:constexpr std::string_view texturedScene = R"json(...)json";
#   测试:  constexpr std::string_view kTexturedScene = R"json(...)json";
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RENDERER="$ROOT/src/content/render/d2d/MiaoSceneD2DRenderer.cpp"
FIXTURE="$ROOT/src/tests/SceneTextureFixture.cpp"

python3 - "$RENDERER" "$FIXTURE" <<'PY'
import re, sys

renderer, fixture = sys.argv[1], sys.argv[2]

def extract(path, variable):
    text = open(path, encoding='utf-8').read()
    pattern = (r'constexpr\s+std::string_view\s+' + re.escape(variable)
               + r'\s*=\s*R"json\((.*?)\)json"')
    m = re.search(pattern, text, re.S)
    if not m:
        sys.exit(f"❌ 在 {path} 里找不到变量 {variable} 的 R\"json(...)json\" 字面量。"
                 f"改名字的话这个闸门也要跟着改,不能静默失效。")
    return m.group(1)

renderer_scene = extract(renderer, 'texturedScene')
fixture_scene = extract(fixture, 'kTexturedScene')

# Compare the JSON, not the source formatting. The two literals sit at different
# nesting depths, so their indentation differs by design; what must match is the
# payload. Normalising per line (and dropping blank lines) keeps the gate about drift
# rather than about whitespace.
def normalise(text):
    return '\n'.join(l.strip() for l in text.splitlines() if l.strip())

a, b = normalise(renderer_scene), normalise(fixture_scene)

if a != b:
    print("❌ 两份贴图场景 fixture 不一致(按去缩进后的 JSON 比较)。")
    print()
    la, lb = a.splitlines(), b.splitlines()
    shown = 0
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else '(缺行)'
        y = lb[i] if i < len(lb) else '(缺行)'
        if x != y:
            print(f"  第 {i + 1} 行不同:")
            print(f"    MiaoSceneD2DRenderer.cpp: {x}")
            print(f"    SceneTextureFixture.cpp: {y}")
            shown += 1
            if shown >= 12:
                print("    ...")
                break
    print()
    print("  MiaoSceneD2DRenderer.cpp 那份是唯一会被真渲染跑到的;")
    print("  SceneTextureFixture.cpp 那份负责在本机验 fixture 的 schema 是否写对。")
    print("  两边改任何一处,另一处必须同步 —— 只改一边的后果是:本机那份永远绿着,")
    print("  Windows 那份跑真渲染时才因为一个字段名写错而失败,报的却是'绘制失败'。")
    sys.exit(1)

print(f"✅ 两份贴图场景 fixture 一致({len(b)} 字符,{len(b.splitlines())} 行 JSON)")

# 顺带确认被拒绝的那份也在:它是"非白色 tint 必须被拒"这条约束的 fixture。
# 少了的后果是那条断言在本机和 CI 都不再跑,而没人会发现。
renderer_text = open(renderer, encoding='utf-8').read()
if 'constexpr std::string_view tintedScene' not in renderer_text:
    sys.exit("❌ MiaoSceneD2DRenderer.cpp 里没有了 tintedScene fixture。"
             "'非白色 tint 作用于贴图 sprite 必须被拒'这条断言已经没有 fixture 了。")
print("✅ tintedScene(非白色 tint 拒绝 fixture)仍在")

sys.exit(0)
PY
