#!/usr/bin/env bash
# 给 C++<->JS 音频契约门注入已知失效,确认它真的会响,而且是为对的原因响。
#
# 为什么必须看"报的什么错":只判断 rc != 0 是不够的 —— 把 header 改到编译不过,
# 测试同样以非零退出,但那时它证明的是"编译器在工作",不是"契约门在工作"。
# 上一版就是这么跑的,三条里两条显示"报了别的错",等于没有证据。
set -u
H=src/include/miaodesk/WallpaperWebAudioEnvelope.h
cp "$H" /tmp/env.h.bak
restore() { cp /tmp/env.h.bak "$H"; }
trap restore EXIT

run() {  # label, expect-substring, mutation-file
    local label="$1" expect="$2" mut="$3"
    restore
    python3 "$mut"
    local out rc
    out=$(node tests/WebAudioEnvelopeParity.mjs 2>&1); rc=$?
    restore
    if [ "$rc" -eq 0 ]; then
        printf 'FAIL  %-34s 注入了失效却仍然通过\n' "$label"; return 1
    fi
    if echo "$out" | grep -q '编译失败'; then
        printf 'FAIL  %-34s 编译失败 —— 那是编译器在工作,不是契约门\n' "$label"; return 1
    fi
    if echo "$out" | grep -q "$expect"; then
        printf 'PASS  %-34s 命中「%s」\n' "$label" "$expect"
    else
        printf 'FAIL  %-34s 失败了但不是因为这个原因:\n' "$label"
        echo "$out" | grep -m2 'FAIL' | sed 's/^/        /'
        return 1
    fi
}

cat > /tmp/m1.py <<'PY'
p = 'src/include/miaodesk/WallpaperWebAudioEnvelope.h'
s = open(p, encoding='utf-8').read()
old = '<< (frame.beat ? "true" : "false") << "}}";'
new = '<< (frame.beat ? 1 : 0) << "}}";'
assert old in s
open(p, 'w', encoding='utf-8').write(s.replace(old, new))
PY

cat > /tmp/m2.py <<'PY'
p = 'src/include/miaodesk/WallpaperWebAudioEnvelope.h'
s = open(p, encoding='utf-8').read()
old = '",\\"bands\\":['
new = '",\\"band\\":['
assert old in s, 'bands literal not found'
open(p, 'w', encoding='utf-8').write(s.replace(old, new))
PY

cat > /tmp/m3.py <<'PY'
p = 'src/include/miaodesk/WallpaperWebAudioEnvelope.h'
s = open(p, encoding='utf-8').read()
old = 'for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {'
new = 'for (std::size_t i = 0; i + 1 < frame.spectrum.size(); ++i) {'
assert old in s
open(p, 'w', encoding='utf-8').write(s.replace(old, new))
PY

cat > /tmp/m4.py <<'PY'
p = 'src/include/miaodesk/WallpaperWebAudioEnvelope.h'
s = open(p, encoding='utf-8').read()
old = 'text << std::fixed << std::setprecision(4) << unit(value);'
new = 'text << std::fixed << std::setprecision(1) << unit(value);'
assert old in s
open(p, 'w', encoding='utf-8').write(s.replace(old, new))
PY

echo "=== 注入已知失效 ==="
fails=0
run 'beat 发 1/0 而非布尔'      'beat: 收到'                /tmp/m1.py || fails=$((fails+1))
run '字段名 bands -> band'      '信封被接受'                /tmp/m2.py || fails=$((fails+1))
run 'spectrum 少发一条'         '信封被接受'                /tmp/m3.py || fails=$((fails+1))
run '精度降到一位小数'          'bands\[1\]'               /tmp/m4.py || fails=$((fails+1))

echo
echo "=== 还原后复跑(必须全绿) ==="
restore
node tests/WebAudioEnvelopeParity.mjs 2>&1 | tail -2
echo "--- $fails 个注入用例未按预期响 ---"
exit $fails
