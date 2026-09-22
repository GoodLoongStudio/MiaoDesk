#!/usr/bin/env bash
# 在 macOS 上真正编译并运行那些只依赖 content/ 纯逻辑层的测试目标。
#
# 为什么需要它:CMake 里这些目标链接 MiaoDeskCore,而 MiaoDeskCore 需要 Windows。
# 先前我在本机只做过 -fsyntax-only,于是"编译通过"被当成了"测试通过";而 CI 里
# 这些步骤其实一直是被跳过的。两者叠加,让这批测试从未真正验证过行为。
#
# 做法:挑出 content/ 下不依赖 Windows 头的源文件,连同测试一起链接。少数还需要
# windows.h 的目标用 scripts/windows-shim(一个最小替身)补齐。
#
# 这不是 CI 的替代品,Windows 专属路径仍然只有 CI 能覆盖。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
cd "$ROOT/src" || exit 2

CXX=${CXX:-clang++}
STD="-std=c++2b -Iinclude -Wno-deprecated-declarations"
SHIM="$HERE/windows-shim"

# 只保留不依赖 Windows 头的实现,否则单个文件就能让整个链接失败。
SRCS=()
for f in $(find content -name '*.cpp' | sort); do
  if $CXX $STD -fsyntax-only "$f" 2>/dev/null; then SRCS+=("$f"); fi
done
echo "可链接的实现源文件:${#SRCS[@]} 个"

# 源码必须用绝对路径喂给编译器:CMake 就是这么做的,于是 __FILE__ 是绝对路径;
# BuiltinWallpaperPackages.cpp 靠 __FILE__ 向上找 assets/wallpapers(不用 cwd ——
# verify-path-layout-contract.ps1 禁止源码依赖当前工作目录)。传相对路径会让
# __FILE__ 变成相对路径,那个查找就失败。
# 测试用 wmain 作入口(Windows 控制台程序),macOS 上需要一个 main 转接。
cat >/tmp/wmain_shim.cpp <<'SHIM'
extern int wmain();
int main() { return wmain(); }
SHIM

PASS=0; FAIL=0; SKIP=0

run() {  # run <目标名> <测试源文件名> <额外源文件...>
  local name="$1" file="$2"; shift 2
  printf '%-26s ' "$name"
  if ! $CXX $STD -I"$SHIM" -O1 -o "/tmp/run_$name" /tmp/wmain_shim.cpp \
       "$ROOT/src/tests/$file" "$@" "${SRCS[@]}" 2>/tmp/build_"$name"; then
    echo "BUILD FAIL"
    grep -E "error:" /tmp/build_"$name" | head -4 | sed 's/^/      /'
    FAIL=$((FAIL+1)); return
  fi
  local out rc=0
  out=$("/tmp/run_$name" 2>&1) || rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "PASS"
    printf '%s\n' "$out" | grep -E "ALL CHECKS|PASSED|FAIL|failure" | tail -3 | sed 's/^/      /'
    PASS=$((PASS+1))
  else
    echo "RUN FAIL (rc=$rc)"
    printf '%s\n' "$out" | tail -12 | sed 's/^/      /'
    FAIL=$((FAIL+1))
  fi
}

echo
echo "--- 纯逻辑(content/ 子集,无 Windows 依赖)---"
# SpriteMaterialPolicy 是唯一能同时钉住两个渲染后端的地方:D3D11 那份本机编译不了，
# 但'什么形态能画、什么形态该被拒'是纯逻辑，于是后端口径的一致性能每轮都验。
# WebAudioEnvelope 是 host->page 音频信封:它把 content/ 的 AudioSpectrumFrame 变成
# shim 会接受的 JSON。整条链没有一行 Windows 代码,所以能在本机跑;
# 另一半(页面侧 shim)由 tests/WebAudioEnvelopeParity.mjs 一起对。
for t in WebAudioEnvelope InputBusPublisher AudioIngress BindingResponse MiaoSceneRuntimeTest SceneSpatial3D InputBusCore PointerAttribution SpriteTextureContract SceneTextureFixture BuiltinWallpaperPackages SpriteMaterialPolicy SceneSerializerSelfTest ContentSelfTests; do
  run "$t" "$t.cpp"
done

echo
echo "--- 只能在 Windows 上验证的目标 ---"
echo "  MediaWallpaperPackageTest       跳过(CI-only)"
echo "  ContentWebReplacementContinuity 跳过(CI-only)"
echo "  ContentSkillLoading             跳过(CI-only)"
echo "      共同原因:三者都 include WallpaperLibrary.h 或 NativeTools.h,而那两条链都会"
echo "      拉到 UnicodeProfileFile.h:72 的 static_assert(sizeof(wchar_t) == 2)"
echo "      (Windows 配置持久化要求 UTF-16 wchar_t)。macOS 的 wchar_t 是 4 字节,"
echo "      这是产品自身的设计约束,不是替身的缺陷 —— 绕过它去换取离线验证,正是 shim"
echo "      共同原因:三者都 include WallpaperLibrary.h 或 NativeTools.h,而那两条链都会"
echo "      拉到 UnicodeProfileFile.h:72 的 static_assert(sizeof(wchar_t) == 2)"
echo "      (Windows 配置持久化要求 UTF-16 wchar_t)。macOS 的 wchar_t 是 4 字节,"
echo "      这是产品自身的设计约束,不是替身的缺陷 —— 绕过它去换取离线验证,正是 shim"
echo "      头部写着的'替身缺陷伪装成产品缺陷'。这三个目标只有 CI 能覆盖。"
echo "  SceneD2DRendererTest           跳过(CI-only)"
echo "  SceneD3D11Test                 跳过(CI-only)"
echo "      这两个的理由**不一样**,分开写:"
echo "        SceneD2DRendererTest  真要 D2D1 设备 + WIC 位图回读,换不了替身。"
echo "        SceneD3D11Test        里面四个自测其实全是纯逻辑(sizeof 比较、路径字符串、"
echo "                             尺寸算术、矩阵乘法),一个都没碰 D3D11 设备 —— "
echo "                             但它们的实现躺在 include 了 d3d11.h 的 .cpp 里,"
echo "                             所以只能连在这些 TU 能编译的地方。"
echo "      这一条我第一版写成了'要真实 D3D11 设备',是**错的**:那只对 D2D 那条成立。"
echo "      写错理由比不写更麻烦 —— 它会让人以为这里需要一个 GPU,"
echo "      而真正的改进方向是把那几个纯逻辑自测搬到不含 d3d11.h 的文件里去。"
SKIP=$((SKIP+5))

[ "$FAIL" -eq 0 ]
