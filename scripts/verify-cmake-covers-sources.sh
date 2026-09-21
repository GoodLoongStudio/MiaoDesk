#!/usr/bin/env bash
# 校验 src/ 下每个"该被编译"的 .cpp 真的会被 CMake 编译。
#
# 为什么需要它:2026-09-22 四个打包工作流挂在
#   MiaoSceneD3D11Renderer.obj : error LNK2019: unresolved external symbol
#     MiaoD3D11TextureLoader::LoadImageW / ::SelfTestPathPolicy
# 根因不是签名不匹配,而是 content/render/d3d11/MiaoD3D11TextureLoader.cpp 没进
# MIAODESK_*_SOURCES:文件在磁盘上、语法没问题、只是从来没人编译它,于是那两个符号
# 在链接期不存在。
#
# 这个类别落在所有现有闸门的盲区:语法闸门扫"磁盘上有什么",照样编译它,看不出它
# 没被收录;CI 只在链接期才报,而一轮要 90 秒;也没人会因为"少列一个文件"在评审里
# 注意到。而这里的检查只需要比对两个清单。
#
# 用 CMake 自己的输出做判据,而不是正则解析 CMakeLists.txt:
#   -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 会生成 compile_commands.json,里面是每一个
#   真正参与编译的 TU。问 CMake 要答案,比猜它Lists怎么写更可靠 —— 正则那版就曾把
#   tests/*.cpp(用 add_executable 编,不在 set() 里)和实现单元(#include 进别的
#   .cpp)误报成漏编,还因为遇到第一个右括号就截断而把一个合法文件判成"不存在"。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "向 CMake 询问实际编译清单(compile_commands.json)…"
if ! cmake -S "$ROOT" -B "$WORK/build" \
        -DCMAKE_VS_PLATFORM_NAME=x64 \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >"$WORK/cmake.log" 2>&1; then
  echo "❌ CMake Configure 失败。这本身就是问题 —— 下面是 CMake 的日志:" >&2
  tail -25 "$WORK/cmake.log" >&2
  exit 2
fi

if [ ! -f "$WORK/build/compile_commands.json" ]; then
  echo "❌ CMake Configure 成功了,但没有生成 compile_commands.json" >&2
  exit 2
fi

python3 - "$SRC" "$WORK/build/compile_commands.json" <<'PY'
import json, os, re, sys

src, cc_path = sys.argv[1], sys.argv[2]
entries = json.load(open(cc_path, encoding='utf-8'))

# CMake 给出的是绝对路径,统一换成相对 src/ 的形式,才能和磁盘清单对齐。
compiled = set()
for e in entries:
    p = os.path.abspath(e['file']).replace('\\', '/')
    if p.startswith(src + '/'):
        compiled.add(p[len(src) + 1:])

# 实现单元:被别的 .cpp 在自己的编译单元里 #include 进来。它们不是独立 TU,
# 要求它们单独被 CMake 编译是错的。
included = set()
for dirpath, _dirs, files in os.walk(src):
    for name in files:
        if not name.endswith(('.cpp', '.h', '.inl')):
            continue
        try:
            text = open(os.path.join(dirpath, name), encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for m in re.finditer(r'^\s*#\s*include\s+"([^"]+\.cpp)"', text, re.M):
            included.add(m.group(1).replace('\\', '/'))

on_disk = set()
for dirpath, _dirs, files in os.walk(src):
    for name in files:
        if name.endswith('.cpp'):
            rel = os.path.relpath(os.path.join(dirpath, name), src).replace('\\', '/')
            on_disk.add(rel)

missing = sorted(f for f in on_disk
                 if f not in compiled and os.path.basename(f) not in
                 {os.path.basename(x) for x in included})

print(f"磁盘上的 .cpp:          {len(on_disk)}")
print(f"CMake 实际编译的 TU:    {len(compiled)}")
print(f"被 #include 的实现单元: {len(included)}({', '.join(sorted(included)) or '无'})")

if missing:
    print()
    print("❌ 这些 .cpp 在磁盘上但 CMake 不编译它们。它们不会进任何目标,"
          "别的代码引用其符号时会在链接期报 LNK2019 / LNK1120:")
    for f in missing:
        print(f"      {f}")
        if os.path.basename(f) in {os.path.basename(x) for x in included}:
            # 只有当某个 #include 它的文件本身被 CMake 编译时,这才是"实现单元"而
            # 不是漏编。此时正确的做法恰恰是不动 CMake。
            print(f"      ⚠ 它同时也是被别的 .cpp #include 的实现单元。"
                  f"如果 #include 它的那个文件已被 CMake 编译,这不算漏编 ——")
            print(f"        但要小心:再把它也加进 CMake 会让每个定义出现两次,"
                  f"链接期报 LNK2005。")
    print()
    print("修法:把它加进对应的 MIAODESK_*_SOURCES 清单,或用 add_executable 建目标。")
    sys.exit(1)

print()
print("✅ 磁盘上每个 .cpp 要么被 CMake 编译,要么是被 #include 的实现单元")
sys.exit(0)
PY
