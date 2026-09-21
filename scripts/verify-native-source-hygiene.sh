#!/usr/bin/env bash
# 把 scripts/verify-path-layout-contract.ps1 里**与平台无关**的那几条搬到本机。
#
# 为什么需要它:2026-09-22,一个Windows-only 的闸门因为一句**注释**而失败了两次提交。
# BuiltinWallpaperPackages.cpp 的注释里写着"这里刻意不用 fs::current_path()",
# 而那个契约是用正则在源码全文里匹配 `\bfs::current_path\s*\(` —— 注释也是源码全文,
# 于是那一行没执行过的文字让 "Verify path layout contract" 红了。
# 那个 .ps1 在 macOS 上没法整段跑(它按反斜杠比对一个 allowlist,而 macOS 的
# $file.FullName 是正斜杠,会给出与产品无关的假阳性),所以本机一直没人跑得动它,
# 这条规则一直是裸的。
#
# 这里只搬那些**不依赖路径分隔符**的检查,因此本机结论与 Windows 一致:
#   · 源码不得依赖当前工作目录
#   · 不得绕过共享 AppPaths 直接摸 LOCALAPPDATA
#   · .cpp 不得直接躺在 src/ 下
#   · src/ 的每个顶层目录必须是登记过的源码域
#   · 源码树内不得有构建产物目录
#   · CMakeLists 里列的每个源文件必须真的存在
# 明确**不搬**那条按反斜杠比对的目录 allowlist —— 那正是 macOS 上假阳性的来源,
# 搬过来就等于在本机伪造一个失败。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"
CMAKE="$SRC/CMakeLists.txt"

python3 - "$SRC" "$CMAKE" "$ROOT" <<'PY'
import os, re, sys

src, cmake_path, root = sys.argv[1], sys.argv[2], sys.argv[3]
problems = []

# --- 1) 不得依赖当前工作目录 ------------------------------------------------
# 与 .ps1 里的 $cwdPatterns 同一组形状。注释也算文本,这一点那个闸门是对的。
cwd_patterns = [
    r'GetCurrentDirectoryW\s*\(',
    r'GetCurrentDirectoryA\s*\(',
    r'SetCurrentDirectoryW\s*\(',
    r'SetCurrentDirectoryA\s*\(',
    r'std::filesystem::current_path\s*\(',
    r'\bfs::current_path\s*\(',
]

# --- 2) 不得绕过共享 AppPaths -------------------------------------------------
appdata_patterns = [
    r'GetEnvironmentVariableW\s*\(\s*L"LOCALAPPDATA"',
    r'FOLDERID_LocalAppData',
]
# .ps1 里用反斜杠书写;本机与 CI 都统一成 POSIX 相对路径再比。
appdata_allow = {
    'src/include/miaodesk/AppPaths.h',
    'src/ai/agent/L3PersistenceSelfTest.cpp',
    'src/desktop/wallpaper/runtime/WallpaperEntry.cpp',
}

CANONICAL_DOMAINS = {'app', 'ai', 'content', 'desktop', 'harness', 'search', 'tests', 'ui', 'include'}
BUILD_ARTIFACT_DIRS = {'CMakeFiles', 'CMakeScripts', 'out', 'build', 'x64', 'arm64',
                       'Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel', '.vs', '.cache'}

source_files = []
for dirpath, _dirs, files in os.walk(src):
    # 不跟进构建产物目录:它们不会被提交,扫进去只会拖慢并且报一堆假问题。
    _dirs[:] = [d for d in _dirs if d not in BUILD_ARTIFACT_DIRS]
    for name in files:
        if name.endswith(('.cpp', '.cc', '.cxx', '.h', '.hpp', '.inc')):
            source_files.append(os.path.join(dirpath, name))

def rel(path):
    return os.path.relpath(path, root).replace('\\', '/')

for path in sorted(source_files):
    relative = rel(path)
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        continue
    for pattern in cwd_patterns:
        m = re.search(pattern, text)
        if m:
            line = text.count('\n', 0, m.start()) + 1
            problems.append(f"{relative}:{line} 依赖当前工作目录(匹配 {pattern!r})——"
                            f"运行期工作目录是不可控的,要用可执行文件相对路径。")
    if relative not in appdata_allow:
        for pattern in appdata_patterns:
            m = re.search(pattern, text)
            if m:
                line = text.count('\n', 0, m.start()) + 1
                problems.append(f"{relative}:{line} 绕过了共享 AppPaths(匹配 {pattern!r})。"
                                f"需要的话请把它登记进 .ps1 与本闸门的 allowlist,并说明理由。")

# --- 3) / 4) / 5) 目录形状 ---------------------------------------------------
for name in sorted(os.listdir(src)):
    dir_path = os.path.join(src, name)
    if not os.path.isdir(dir_path):
        continue
    if name in BUILD_ARTIFACT_DIRS:
        problems.append(f"src/{name} 是构建产物目录,不得进入源码树(构建与暂存都在树外)。")
    if name not in CANONICAL_DOMAINS:
        problems.append(f"src/{name} 不是登记过的源码域。新域必须在 docs/NATIVE_SOURCE_LAYOUT.md "
                        f"的同一个提交里登记,否则布局契约会静默落后于代码。")

stray = [n for n in os.listdir(src)
         if os.path.isfile(os.path.join(src, n)) and n.endswith(('.cpp', '.cc', '.cxx'))]
if stray:
    problems.append(f"实现文件直接躺在 src/ 下({', '.join(sorted(stray))}),必须放进某个源码域。")

# --- 6) CMakeLists 里列的源文件必须存在 --------------------------------------
cmake_text = open(cmake_path, encoding='utf-8').read()
listed = re.findall(r'^\s+([A-Za-z0-9_./-]+\.cpp)\s*$', cmake_text, re.M)
missing = [f for f in listed if not os.path.isfile(os.path.join(src, f))]
if missing:
    problems.append(f"CMakeLists 列了但磁盘上不存在的源文件: {', '.join(sorted(missing))}")

print(f"扫描的源码文件:     {len(source_files)}")
print(f"CMakeLists 源清单:  {len(listed)} 个 .cpp")
print(f"登记过的源码域:     {', '.join(sorted(CANONICAL_DOMAINS))}")

if problems:
    print()
    print(f"❌ {len(problems)} 处原生源码形状/路径问题:")
    for p in problems:
        print(f"      · {p}")
    sys.exit(1)

print()
print("✅ 源码不依赖 cwd、不绕过共享 AppPaths、目录形状合规、CMake 源文件全部存在")
sys.exit(0)
PY
