#!/usr/bin/env bash
# 校验 skills/ 目录 与 NativeTools.cpp / PiNativeToolsExtension.cpp 里的
# content_skill_get 白名单一致。
#
# 为什么值得单独一个闸门:白名单是名字的封闭集合(NativeTools.cpp 的
# kContentSkills),磁盘上是同样的四个目录。任何一边单独变更都会静默失衡 ——
# 多建一个目录而没加进白名单,那个 skill 对 AI 就是不可达的;白名单里有而磁盘没有,
# 用户在对话里点这个 skill 只会拿到"未随产品安装"的报错。两侧都不报错,所以必须比对。
#
# 另外顺带查两件事:
#   * 每个 skill 都有 SKILL.md 且非空;
#   * 每个 SKILL.md 不超过 kContentSkillMaxBytes(content_skill_get 的读取上限)。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NATIVE="$ROOT/src/ai/tools/NativeTools.cpp"
PI_EXT="$ROOT/src/ai/pi/PiNativeToolsExtension.cpp"
SKILLS_DIR="$ROOT/skills"
FAIL=0

# 从 kContentSkills 数组里取名字(只看那一对花括号内部)
ALLOW=$(python3 - "$NATIVE" <<'PY'
import re, sys
text = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r'kContentSkills\s*\[\]\s*=\s*\{(.*?)\}', text, re.S)
if not m:
    print('PARSER_FAILED'); raise SystemExit
names = re.findall(r'L"([^"]+)"', m.group(1))
print('\n'.join(sorted(names)))
PY
)
if [ "$ALLOW" = "PARSER_FAILED" ] || [ -z "$ALLOW" ]; then
  echo "❌ 无法从 $NATIVE 解析 kContentSkills(正则失配,闸门需要跟着代码改)" >&2
  exit 2
fi

# 磁盘上实际存在的 skill(有 SKILL.md 的目录才算)
DISK=$(cd "$SKILLS_DIR" 2>/dev/null && for d in */; do
         [ -f "${d}SKILL.md" ] && basename "$d"
       done | sort)
if [ -z "$DISK" ]; then
  echo "❌ $SKILLS_DIR 下没有任何含 SKILL.md 的 skill 目录" >&2
  exit 1
fi

echo "白名单($(printf '%s\n' "$ALLOW" | wc -l | tr -d ' ') 个):"
printf '%s\n' "$ALLOW" | sed 's/^/   /'
echo "磁盘($(printf '%s\n' "$DISK" | wc -l | tr -d ' ') 个):"
printf '%s\n' "$DISK" | sed 's/^/   /'
echo

# 双向比对。注意 comm 的列:列1=只在 file1,列2=只在 file2,列3=两边都有;
# -23 只留列1,-13 只留列2。两侧都必须用"自己的那一侧"当 file1,否则算出来的是同一个
# 集合(第一版就犯了这个错:ONLY_DISK 用了 -13 配 file1=DISK,得到的是只在 ALLOW 的
# 名字,于是白名单外的 skill 目录永远漏报)。
ONLY_ALLOW=$(printf '%s\n' "$ALLOW" | sort -u | comm -23 - <(printf '%s\n' "$DISK" | sort -u))
ONLY_DISK=$(printf '%s\n' "$DISK" | sort -u | comm -23 - <(printf '%s\n' "$ALLOW" | sort -u))
if [ -n "$ONLY_ALLOW" ]; then
  echo "❌ 白名单里有但磁盘上没有(AI 读到会是「未随产品安装」):"
  printf '      %s\n' $ONLY_ALLOW   # 名字无空格,故意展开
  FAIL=1
fi
if [ -n "$ONLY_DISK" ]; then
  echo "❌ 磁盘上有但白名单里没有(该 skill 对 AI 不可达,白名单外名字一律拒):"
  printf '      %s\n' $ONLY_DISK
  FAIL=1
fi

# 每个 skill 非空、不超过读取上限。
# 上限从 kContentSkillMaxBytes 解析,不硬编码:两处各写一个数字迟早对不上,
# 而"skill 太大被工具拒读"这种失效在磁盘上完全看不出来。
LIMIT=$(python3 - "$NATIVE" <<'PY'
import re, sys
text = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r'kContentSkillMaxBytes\s*=\s*(\d+)\s*\*\s*(\d+)', text)
if m:
    print(int(m.group(1)) * int(m.group(2)))
else:
    m2 = re.search(r'kContentSkillMaxBytes\s*=\s*(\d+)', text)
    print(m2.group(1) if m2 else 'PARSE_FAILED')
PY
)
if [ "$LIMIT" = "PARSE_FAILED" ] || [ -z "$LIMIT" ]; then
  echo "❌ 无法从 $NATIVE 解析 kContentSkillMaxBytes" >&2
  exit 2
fi
echo "content_skill_get 读取上限:$LIMIT 字节"
while IFS= read -r name; do
  [ -n "$name" ] || continue
  f="$SKILLS_DIR/$name/SKILL.md"
  [ -f "$f" ] || continue
  bytes=$(wc -c <"$f" | tr -d ' ')
  if [ "$bytes" -eq 0 ]; then
    echo "❌ $name/SKILL.md 是空文件"
    FAIL=1
  fi
  if [ "$bytes" -gt "$LIMIT" ]; then
    echo "❌ $name/SKILL.md 有 $bytes 字节,超过 content_skill_get 的 $LIMIT 字节上限"
    FAIL=1
  fi
done < <(printf '%s\n' "$ALLOW")

# 索引用 README.md(LoadContentSkill 无参时返回它)
if [ ! -f "$SKILLS_DIR/README.md" ]; then
  echo "❌ 缺 skills/README.md —— 不带 name 调用 content_skill_get 会拿到索引不可用"
  FAIL=1
fi

# Pi 扩展的 system prompt 也把四个名字写给了模型,必须与白名单一致
PI_NAMES=$(grep -oE "content-package-basics|wallpaper-content|widget-content|content-review" "$PI_EXT" 2>/dev/null | sort -u)
MISSING_IN_PI=$(comm -23 <(printf '%s\n' "$ALLOW") <(printf '%s\n' "$PI_NAMES"))
if [ -n "$MISSING_IN_PI" ]; then
  echo "❌ 白名单里有,但 Pi 扩展没告诉模型有这些 skill:"
  printf '      %s\n' $MISSING_IN_PI
  FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
  echo "✅ 白名单与磁盘一致,4 个 SKILL.md 均非空且在上限内,Pi 提示词同步"
  exit 0
fi
exit 1
