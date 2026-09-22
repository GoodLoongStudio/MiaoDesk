#!/usr/bin/env bash
# 本地 Windows 语法闸门(交叉编译 -fsyntax-only)。
#
# 背景:2026-09-21 那次 CI 事故,四个工作流挂在同一个错误上——NativeTools.cpp
# 对一个 std::wstring 调 .wstring()。根因不在那一行,而在流程:那批代码从 9/17
# 起就没经过任何"认 Windows 头文件"的编译器,而本机验证只跑过剥离出来的逻辑
# 片段(macOS clang)和不含 windows.h 的纯逻辑测试。两者都看不见 MSVC 才能看见
# 的类型错误。
#
# 这个脚本用 mingw-w64 交叉编译真实源文件。它自带一套 Windows 头,因此能捕获
# "这个成员/这个类型到底存不存在"这一类错误,把最常见的类型错从一轮 90 秒的 CI
# 变成十秒级。
#
# 它不替代 CI:Windows SDK 覆盖面不同,mingw 与 MSVC 的结论仍会有分歧。缺头文件的
# 报错在这里一律算环境噪声(mingw 没有整套 Windows SDK),不计入失败;但下面这些
# 是真错误、必须修:
#   * 非静态 constexpr 数据成员(MSVC 同样报错)
#   * std::max(int, long) 之类的模板推导失败
#   * 名字查不到、类型无此成员
#
# 用法:bash scripts/verify-windows-syntax.sh [--jobs N]
# 依赖:brew install mingw-w64
#
# 兼容 macOS 自带的 bash 3.2:不用 mapfile、不用 nproc、不用关联数组。
set -u

CROSS=${CROSS:-/opt/homebrew/bin/x86_64-w64-mingw32-g++}
if [ ! -x "$CROSS" ]; then
  echo "缺交叉编译器:$CROSS" >&2
  echo "先 brew install mingw-w64" >&2
  exit 2
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
prev=""
for arg in "$@"; do
  case "$prev" in --jobs) JOBS="$arg" ;; esac
  prev="$arg"
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# 与 src/CMakeLists.txt 的 foreach 保持同一套宏;/W4 没有 -Wall 之外的等价物。
# -fms-compatibility 是 clang 独有的,gcc 会直接报 unrecognized,这里不写。
# 单个标量导出:bash 3.2 导不出数组,而转义进 worker 又会把路径里的空格弄坏;
# 这里所有条目本身无空格,标量字符串是最稳的形式。
BASE="-std=c++2b -fsyntax-only -fms-extensions -I$SRC/include -I$ROOT/third_party/webview2/include -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Werror=return-type -Wno-unknown-pragmas"

# src/CMakeLists.txt 的 set_source_files_properties 给个别文件挂了宏,闸门必须
# 照抄:漏掉 MIAODESK_NATIVE_TOOLS_IMPL 时,NativeTools.h 的 inline 兜底实现会和
# NativeTools.cpp 的定义并存,报出假的重定义错误。
per_file_define() {
  case "$1" in
    "$SRC/ai/tools/NativeTools.cpp")            echo " -DMIAODESK_NATIVE_TOOLS_IMPL" ;;
    "$SRC/ai/agent/L3Agent.cpp")                echo " -DMIAODESK_L3_WINHTTP_TRACE" ;;
    "$SRC/ai/pi/PiRuntime.cpp")                 echo " -DMIAODESK_PI_CREDENTIAL_GUARD" ;;
    "$SRC/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp")
                                                echo " -DwWinMain=MiaoDeskWallpaperMain" ;;
    *)                                          echo "" ;;
  esac
}

find "$SRC" -name '*.cpp' | sort >"$WORK/all"

# 有些 .cpp 不是独立编译单元,而是被别的 .cpp 在某个命名空间内部 #include 进去的
# (典型:desktop/wallpaper/legacy/WallpaperEngineProduction.cpp 在
#  namespace miaodesk::wallpaper 内部 #include "WallpaperEngine.cpp")。把它们当独立
# TU 编译会得到假错误——匿名命名空间在真实构建里嵌在 miaodesk::wallpaper 内,单独
# 编译时却落在全局,于是 wallpaper::scenes:: 这种限定名解析不到。
# 这里先识别并排除。
python3 - "$SRC" "$WORK" <<'PY'
import os, re, sys
src, work = sys.argv[1], sys.argv[2]
included = set()
for dirpath, _dirs, files in os.walk(src):
    for name in files:
        if not name.endswith(('.cpp', '.h', '.inl')):
            continue
        path = os.path.join(dirpath, name)
        try:
            text = open(path, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for m in re.finditer(r'^\s*#\s*include\s+"([^"]+\.cpp)"', text, re.M):
            included.add(m.group(1))
all_files = [l.strip() for l in open(os.path.join(work, 'all')) if l.strip()]
kept, skipped = [], []
for f in all_files:
    base = os.path.basename(f)
    if base in included:
        skipped.append(base)
    else:
        kept.append(f)
open(os.path.join(work, 'files'), 'w').write('\n'.join(kept) + '\n')
if skipped:
    print("跳过(被其他 .cpp 在命名空间内 #include,不是独立 TU):")
    for s in sorted(skipped):
        print("   -", s)
PY
COUNT=$(wc -l <"$WORK/files" | tr -d ' ')
echo "交叉语法检查:$COUNT 个源文件 (jobs=$JOBS)"
echo "编译器:$CROSS"
: >"$WORK/failed"

# 单独写成 worker 文件而不是 `bash -s <<EOF`:后者会被 xargs 把文件名拼进参数,
# 实测报 "invalid option"。逐文件宏也在这一步注入。
cat >"$WORK/worker.sh" <<'INNER'
f="$1"
flags="$BASE$(per_file_define "$f")"
if ! out=$("$CROSS" $flags "$f" 2>&1); then
  printf '\n### %s\n%s\n' "${f#$SRC/}" "$(printf '%s\n' "$out" | head -12)" >>"$WORK/failed"
fi
INNER

# per_file_define 要在 worker 里可见,连同 BASE 一起注入它的定义。
python3 - "$WORK/worker.sh" <<'PY'
import sys
path = sys.argv[1]
body = open(path).read()
helper = '''
per_file_define() {
  case "$1" in
    "$SRC/ai/tools/NativeTools.cpp")            echo " -DMIAODESK_NATIVE_TOOLS_IMPL" ;;
    "$SRC/ai/agent/L3Agent.cpp")                echo " -DMIAODESK_L3_WINHTTP_TRACE" ;;
    "$SRC/ai/pi/PiRuntime.cpp")                 echo " -DMIAODESK_PI_CREDENTIAL_GUARD" ;;
    "$SRC/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp")
                                                echo " -DwWinMain=MiaoDeskWallpaperMain" ;;
    *)                                          echo "" ;;
  esac
}
'''
open(path, 'w').write(helper + body)
PY

export CROSS SRC WORK BASE
xargs -P "$JOBS" -n 1 <"$WORK/files" bash "$WORK/worker.sh"

FAILED=$(grep -c '^### ' "$WORK/failed" 2>/dev/null)
FAILED=${FAILED:-0}
if [ "$FAILED" -eq 0 ]; then
  echo
  echo "全部通过"
  exit 0
fi

echo
cat "$WORK/failed"
echo

# mingw 的 WRL 头里没有 Microsoft::WRL::Callback(MSVC 在 wrl/implements.h 提供),
# __assume 也只有 MSVC 的 intrin 提供。这两处叠加它们的级联报错,是 mingw 的缺口
# 而不是代码缺陷;不排掉的话闸门永远红着,而永远红着的闸门等于没有闸门。
# 这里逐条列出并说明,新增的错误类型必须显式登记才能被忽略。
python3 - "$WORK/failed" <<'PY'
import re, sys
KNOWN = [
    (r"'Callback' has not been declared|'Callback' was not declared|WRL.*Callback|Callback.*WRL",
     "mingw 的 wrl 无 Microsoft::WRL::Callback(MSVC 在 wrl/implements.h 提供)"),
    (r"expected primary-expression before '>' token",
     "上一条 Callback 缺失的级联报错"),
    (r"has no member named 'Get'",
     "上一条 Callback 缺失的级联报错(lambda 返回类型塌成空结构)"),
    (r"'__assume' was not declared",
     "__assume 是 MSVC intrin 专属,mingw 用 __builtin_unreachable"),
    # mingw 的 d3d11.h 把 ID3D11View::GetResource 声明成返回 void(真 SDK 返回 HRESULT),
    # 于是 `if (FAILED(view->GetResource(&res)))` 在 mingw 下报 "void value not ignored"。
    # 这一条值得登记而不是顺手绕开:它是**下游头文件的 bug**,写在仓库代码里毫无问题。
    # 绕开的办法是用 MiaoD3D11RenderTarget::Texture() 直接拿纹理 —— 那个访问器就是为此
    # 存在的,而且比 view->GetResource 这条间接路更直接。
    (r"void value not ignored as it ought to be",
     "mingw 的 d3d11.h 把 ID3D11View::GetResource 声明成 void(真 SDK 返回 HRESULT);"
     "改用 MiaoD3D11RenderTarget::Texture()"),
    (r"fatal error:", "mingw 没有整套 Windows SDK,缺头文件属环境噪声"),
]
text = open(sys.argv[1]).read()
# gcc 的行以 "file:line:col: error: ..." 开头,不是以 error: 开头——用 startswith
# 会一条都匹配不上,于是过滤器恒空、闸门永远报绿。必须用子串匹配。
kept = [l for l in text.split('\n') if ' error:' in l or 'fatal error:' in l]
real = []
for line in kept:
    if not any(re.search(p, line) for p, _ in KNOWN):
        real.append(line)
print("=== 非『已知 mingw 缺口』的真实错误(这些才是要修的)===")
for l in real[:40]:
    print("  ", l)
if not real:
    print("   (无)")
print()
print("=== 已知 mingw 缺口统计(已登记,不计入失败)===")
for pat, why in KNOWN:
    n = sum(1 for l in kept if re.search(pat, l))
    if n:
        print(f"   {n:>3} 处  {why}")
        print(f"        匹配: {pat}")
print()
print(f"真实错误行数:{len(real)}")
sys.exit(1 if real else 0)
PY
GATE=$?
echo "闸门结论:$([ $GATE -eq 0 ] && echo '通过(仅剩已登记的 mingw 缺口)' || echo '有真实错误,见上')"
exit $GATE
