#!/usr/bin/env bash
# 拒绝未解决的 git 合并冲突标记进入仓库。
#
# 为什么需要它:2026-09-22 发现 packaging/windows/stage.ps1 里留着
# `<<<<<<< HEAD` / `=======` / `>>>>>>> _check/fix/unicode-wallpaper-theme-packages`
# 三行冲突标记,是我早前合分支时留下的。后果是三个打包工作流(x64 Package / MSIX /
# ARM64 Package)全部在 stage 阶段语法失败 —— 而 C 编译器看不见 .ps1,人也很难在一个
# 248 行的脚本里注意到三行尖括号。
#
# 这个闸门便宜到不值得讨论:git grep 一遍,零依赖,秒级返回。
#
# 只匹配 `<<<<<<< ` 和 `>>>>>>> `,不单独匹配 `=======`:markdown 的 setext 二级
# 标题下划线就是七个等号,单匹配必然误报。冲突一定同时出现 <=<<<<<<< 和 >>>>>>>。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2

PATTERN='^(<<<<<<< |>>>>>>> )'
FOUND=0

if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
  # 必须覆盖两类文件:git 跟踪的,以及**还没跟踪的**。
  # 只扫 git grep 会漏掉新文件,而"冲突标记随新文件进仓库"正是最常见的情形 ——
  # 写完还没 commit 时它还没进 index,那时候才是最该拦住它的时刻。
  #
  # --exclude-standard 让 .gitignore 里排除的 build 产物 / node_modules 不参与扫描,
  # 所以不需要手工维护目录黑名单。
  if git grep -n -I -E "$PATTERN" -- .; then
    FOUND=1
  fi
  untracked=$(git ls-files --others --exclude-standard 2>/dev/null)
  if [ -n "$untracked" ]; then
    if printf '%s\n' "$untracked" | xargs -r grep -n -I -E "$PATTERN" 2>/dev/null; then
      FOUND=1
    fi
  fi
else
  # 没有 git 时退化成普通扫描,跳过明显不需要看的目录。
  while IFS= read -r line; do
    printf '%s\n' "$line"
    FOUND=1
  done < <(grep -rn -I -E "$PATTERN" \
             --exclude-dir=.git --exclude-dir=node_modules --exclude-dir=build . 2>/dev/null)
fi

if [ "$FOUND" -ne 0 ]; then
  cat >&2 <<'MSG'

❌ 发现未解决的合并冲突标记(上面是 file:line:内容)。

每行 `<<<<<<< <ref>` / `=======` / `>>>>>>> <ref>` 都来自一次没有解完的合并。
PowerShell、markdown、JSON、CMake 这些文件不进 C++ 编译器,所以它们带着冲突标记
也能一路绿灯,直到某个只有这些文件的工作流在运行期炸开 —— 2026-09-22 三个打包工作流
就是这么挂的。

处理:git mergetool 解掉,或手动选定一侧后删掉三行标记,然后重新编译/解析一次。
MSG
  exit 1
fi

echo "✅ 没有未解决的合并冲突标记"
exit 0
