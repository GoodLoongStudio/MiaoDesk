#!/usr/bin/env bash
# 在 macOS 上真正编译并运行那 5 个 CI 从未跑过的测试目标。
#
# 原因:CMake 里它们链接 MiaoDeskCore,而 MiaoDeskCore 需要 Windows。但它们的实现
# 依赖只落在 content/ 下少数纯逻辑源文件里,把那些单独链接起来即可运行。
# 之前这些目标只做过 -fsyntax-only,等于从未验证过行为。
set -u
cd "$(dirname "$0")/../src" || exit 2

CXX=${CXX:-clang++}
STD="-std=c++2b -Iinclude"

# 挑出不依赖 Windows 头的 content 源文件
SRCS=()
for f in $(find content -name '*.cpp' | sort); do
  if $CXX $STD -fsyntax-only "$f" 2>/dev/null; then SRCS+=("$f"); fi
done
echo "可运行的实现源文件:${#SRCS[@]} 个"
printf '   %s\n' "${SRCS[@]}"

# 这些测试用 wmain 作入口(Windows 控制台程序)。在 macOS 上链接需要一个普通 main
# 转接,否则报 "_main" undefined。
cat >"/tmp/wmain_shim.cpp" <<'SHIM'
extern int wmain();
int main() { return wmain(); }
SHIM

PASS=0; FAIL=0
for t in InputBusPublisher AudioIngress BindingResponse MiaoSceneRuntimeTest SceneSpatial3D; do
  printf '%-22s ' "$t"
  if $CXX $STD -O1 -Wno-deprecated-declarations -o "/tmp/r_$t" "/tmp/wmain_shim.cpp" \
       "tests/$t.cpp" "${SRCS[@]}" 2>/tmp/lk_"$t"; then
    if out=$("/tmp/r_$t" 2>&1); then rc=0; else rc=$?; fi
    if [ "$rc" -eq 0 ]; then
      echo "PASS"
      printf '%s\n' "$out" | sed 's/^/        /' | tail -12
      PASS=$((PASS+1))
    else
      echo "RUN FAIL (rc=$rc)"
      printf '%s\n' "$out" | tail -15
      FAIL=$((FAIL+1))
    fi
  else
    echo "LINK FAIL"
    head -10 /tmp/lk_"$t"
    FAIL=$((FAIL+1))
  fi
done
echo
echo "=== 通过 $PASS / 失败 $FAIL ==="
[ "$FAIL" -eq 0 ]
