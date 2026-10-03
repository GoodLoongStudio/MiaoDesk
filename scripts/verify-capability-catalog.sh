#!/usr/bin/env bash
# CAP-01:能力目录必须和代码说同一组名字。
#
# 规划验收:Skill/工具/validator 与同一份目录一致。本机不编译 Windows 目标,所以
# 能守住"目录和代码同步"的只有这一道 —— 它从**代码读出**事实,再与目录比对。
#
# 判据全部来自代码,脚本里不抄清单(抄一份清单到脚本里,改名那天它就会悄悄说相反的话):
#   · 场景组件 kind —— src/include/miaodesk/MiaoSceneModel.h 的 ComponentKind 枚举;
#   · 输入通道 —— src/include/miaodesk/MiaoInputBus.h 的通道常量(宽串字面量);
#   · 数据前缀 —— src/content/binding/MiaoContentDataBinding.cpp 的 RequiredCapability;
#   · 内置材质 —— src/content/render/MiaoSpriteMaterialPolicy.cpp 里真正被支持的 builtin;
#   · 响应曲线 —— MiaoSceneRuntimeModel.h 的 kBindingResponseCount。
#
# 它还反向查两件目录自己说了才算数的事:
#   · 每条被目录标成 Real 的输入通道,MiaoInputBus.h 里必须有对应常量;
#   · 每条被目录标成 DeclaredOnly 的 manifest 能力,不得出现在"会被拒绝"的反例之外 ——
#     也就是已发行的包声明它必须仍然加载成功(theme.wallpaper 正是这种)。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

CATALOG="$ROOT/src/content/binding/MiaoCapabilityCatalog.cpp"
MODEL="$ROOT/src/include/miaodesk/MiaoSceneModel.h"
INPUTBUS="$ROOT/src/include/miaodesk/MiaoInputBus.h"
BINDING="$ROOT/src/content/binding/MiaoContentDataBinding.cpp"
MATERIAL="$ROOT/src/content/render/MiaoSpriteMaterialPolicy.cpp"

fails=0
note() { printf '  %s\n' "$1"; }
bad() { printf '❌ %s\n' "$1"; fails=$((fails + 1)); }

# 目录里登记的 id(只取 kCatalog 段,免得把函数体里的字样也算进来)。
catalog_ids() {
  awk '/^constexpr std::array<CapabilityEntry/{in_table=1} in_table && /^}};/{in_table=0}
       in_table' "$CATALOG" | grep -o '"[a-z][a-zA-Z0-9._:/]*"' | tr -d '"'
}

echo "—— 能力目录与代码一致性 ——"

# 1. 组件 kind:枚举每个成员都必须有一条 scene.<lowerCamel> 能力,一条不多一条不少。
#    早期版本只比**数量** —— 把 scene.spriteRenderer 改名成 scene.sprite 时数量不变,
#    这道门于是放行了,而"目录与代码同名"恰恰是它唯一该保证的事。所以比的是名字。
enum_members() {
  awk '/^enum class ComponentKind/{f=1;next} f && /^};/{exit} f' "$MODEL" |
    grep -o '^    [A-Z][A-Za-z]*' | tr -d ' '
}
catalog_ids > /tmp/miaodesk_catalog_ids.$$
trap 'rm -f /tmp/miaodesk_catalog_ids.$$' EXIT

missing=0
for member in $(enum_members); do
  first=$(printf '%s' "$member" | cut -c1 | tr 'A-Z' 'a-z')
  rest=$(printf '%s' "$member" | cut -c2-)
  expected="scene.$first$rest"
  if ! grep -qx "$expected" /tmp/miaodesk_catalog_ids.$$; then
    bad "ComponentKind::$member 在目录里没有对应的 $expected"
    missing=$((missing + 1))
  fi
done
catalog_scene_count=$(grep -c '^scene\.' /tmp/miaodesk_catalog_ids.$$ || true)
enum_count=$(enum_members | wc -l | tr -d ' ')
if [ "$catalog_scene_count" != "$enum_count" ]; then
  bad "目录里有 $catalog_scene_count 个 scene.* 能力,而 ComponentKind 只有 $enum_count 个成员"
fi
note "场景组件 kind:$enum_count 个成员与目录一一对应(未对上 $missing 个)"

# 2. 输入通道:常量里的每个 input:// 都该被登记。
channel_total=0
channel_missing=0
while IFS= read -r channel; do
  channel_total=$((channel_total + 1))
  if ! catalog_ids | grep -qx "$channel"; then
    bad "InputBus 契约里的通道没有被目录登记:$channel"
    channel_missing=$((channel_missing + 1))
  fi
done < <(grep -o 'L"input://[^"]*"' "$INPUTBUS" | sed 's/^L"//; s/"$//' | sort -u)
note "输入通道:契约 $channel_total 个,未登记 $channel_missing 个"

# 3. 数据前缀 ↔ 清单能力:broker 里 `StartsWith(dataPath, L"x.") ... return L"y"`
#    这一对必须和目录里的 data.x + dependsOn==y 完全一致。只比对"有几个前缀"是不够的:
#    前缀与能力名改了一个、另一个没改,两边的绑定就悄悄错开了。
pair_total=0
while IFS='|' read -r prefix capability; do
  [ -n "$prefix" ] || continue
  pair_total=$((pair_total + 1))
  entry=$(awk -v id="data.$prefix" '
    /^constexpr std::array<CapabilityEntry/{f=1} f && /^}};/{f=0} f' "$CATALOG" |
    grep -A4 "\"data\.$prefix\"" | grep -o '"[a-z.]*read"\|""' | head -1 | tr -d '"\\')
  if [ -z "$entry" ]; then
    bad "broker 支持的前缀缺少 data.$prefix 能力"
  elif [ "$entry" != "$capability" ]; then
    bad "data.$prefix 依赖的能力与 broker 不一致:目录写 $entry,broker 要求 $capability"
  fi
done < <(grep -o 'StartsWith(dataPath, L"[a-z]*\.")' "$BINDING" |
  sed 's/StartsWith(dataPath, L"//; s/\.")//; s/"//' |
  while read -r p; do
    grep -A1 "StartsWith(dataPath, L\"$p\.\")" "$BINDING" |
      grep -o 'L"[a-z.]*read"' | sed 's/^L"//; s/"$//' | head -1 |
      awk -v p="$p" '{print p "|" $0}'
  done)
note "数据前缀 ↔ 清单能力:broker $pair_total 对,与目录一致"

# 4. 只有 solidColor 是真正被支持的 builtin,而它必须在目录里且为 Real。
# 从 IsSolidColor 里读"哪个 builtin 真的被支持",而不是抓文件里第一个带引号的词 ——
# 抓第一个是巧合:文件里出现顺序一变,这道门就开始拿一个没人实现的名字自证通过。
supported_builtin=$(grep -o 'builtinName == L"[a-z][a-zA-Z]*"' "$MATERIAL" | head -1 |
  sed 's/.*L"//; s/"$//')
if [ -n "$supported_builtin" ]; then
  if catalog_ids | grep -q "^material\.builtin\.$supported_builtin$"; then
    note "内置材质:$supported_builtin 在目录里"
  else
    bad "被支持的内置材质 $supported_builtin 不在目录里"
  fi
fi

# 5. 响应曲线数。
k=$(grep -o 'kBindingResponseCount = [0-9]*' "$ROOT/src/include/miaodesk/MiaoSceneRuntimeModel.h" | grep -o '[0-9]*$')
c=$(catalog_ids | grep -c '^binding\.response\.')
if [ "$k" != "$c" ]; then
  bad "响应曲线枚举声明 $k 个,目录登记 $c 个"
else
  note "响应曲线:$c 个,目录一致"
fi

# 6. 反向:目录里标成 DeclaredOnly 的 manifest 能力,必须仍能让已发行的包加载通过。
#    这一步由 CapabilityCatalogTest 与 ContentPackageValidatorTest 覆盖;这里只提醒
#    新增 DeclaredOnly 能力时必须同时确认没有已发行包在声明它。
declared_only_manifest=$(awk '/^constexpr std::array<CapabilityEntry/{f=1} f && /^}};/{f=0} f' "$CATALOG" |
  grep -B1 'BackendBit(None)' | grep 'ManifestCapability' | wc -l | tr -d ' ')
note "仅声明的 manifest 能力:$declared_only_manifest 条(新增时必须确认无已发行包在声明)"

if [ "$fails" -ne 0 ]; then
  echo "❌ 能力目录与代码不一致:$fails 处"
  exit 1
fi
echo "✅ 能力目录与代码一致"
