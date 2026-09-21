#!/usr/bin/env bash
# 离线验证 MiaoDeskMediaPackageTest 第 1 节(CreateVideo/CreateImage + Validate)。
#
# 为什么单独搞一个:完整测试被标为 CI-only,因为它链接 WallpaperLibrary.cpp,而那条链
# 需要真正的 Windows SDK(shobjidl + WIC COM)。但第 1 节 —— 也就是 2026-09-22 在
# Windows CI 上失败的那三处断言 —— 只依赖 WallpaperPackage.cpp,不需要 WallpaperLibrary。
# 既然 CI 一轮要 15 分钟而这个检查只要几秒,就把能离线验的那部分单独拉出来。
#
# 覆盖范围请说清楚:只覆盖第 1 节。第 5 节(library 导入手写 video 包)仍然只有 CI 能验,
# 因为这个驱动刻意不链接 WallpaperLibrary,免得为了编译它而把 windows-shim 扩成第二个
# Windows SDK。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
S="$HERE/windows-shim"
DRIVER="$HERE/offline-media-package-check.cpp"

cd "$ROOT/src" || exit 2

CXX=${CXX:-clang++}

echo "编译 WallpaperPackage.cpp + 离线驱动…"
if ! $CXX -std=c++2b -Iinclude -I"$S" -Wno-deprecated-declarations -O1 \
        -o /tmp/offline-media "$DRIVER" \
        desktop/wallpaper/library/WallpaperPackage.cpp 2>/tmp/om_build.log; then
  echo "❌ 编译失败:"
  grep -E "error:" /tmp/om_build.log | head -10
  exit 2
fi

echo "运行:"
LC_ALL=${LC_ALL:-en_US.UTF-8} LANG=${LANG:-en_US.UTF-8} /tmp/offline-media
rc=$?

echo
# 断言与 CI 测试第 1 节逐条对应
if [ $rc -eq 0 ]; then
  echo "✅ CreateVideo / Validate / type=video / entry=assets/clip.mp4 全部成立"
else
  echo "❌ 有断言不成立(见上面 [8]/[9] 两行)"
fi
exit $rc
