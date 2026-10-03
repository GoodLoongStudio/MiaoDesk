#!/usr/bin/env bash
# 每个 shell 闸门脚本自己必须能解析。
#
# 为什么需要它:2026-09-22 发现 `run-pure-logic-tests.sh` —— 本机跑 14 个纯逻辑测试的
# 那个脚本 —— 从写出起**从来没有可能失败过**。第 98 行一句 echo 的字符串没有闭合,
# 于是从那一行往后的所有内容(包括最后一行决定退出码的 `[ "$FAIL" -eq 0 ]`)都被吞进
# 同一个字符串,从不作为命令执行。脚本照样打印 14 行 PASS 并退出 0。
#
# 这不是"少写一个引号"。它落在一个**已经记录过的类别**里,只是换了个形状:
# TODO 教训 6 记的是"助手脚本最后一句 echo 吃掉了测试的退出码";这一次是
# **一个未闭合的引号把执行流整段吞掉**。注入验证:把 `MiaoInputBus.h` 的一个频段边界
# 从 160 Hz 改成 320 Hz,修好之后脚本退出 1(InputBusCore RUN FAIL);而在修好之前
# 同样的注入下它退出 0。
#
# 只查一件事:能不能解析。不能解析 = 脚本后半段一件事都没发生,而它打印的东西
# 看起来完全正常。别的地方(退出码、比对条数)由脚本自己和其它门负责 ——
# 这里不写一个"看起来在查、实际什么都不做"的分支。
#
# 2026-10-04 扩到 scripts/*.py:`verify-builtin-wallpaper-animation-parity.py` 这个
# python 门已经在仓库里跑了一年,而这道门只看 .sh —— 一个 python 门写坏了语法,
# 这里照样报"全部可以解析",只有 CI 那一轮才发现。同一条理由,换了个扩展名而已。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2

status=0
count=0
for script in scripts/*.sh; do
    [ -f "$script" ] || continue
    count=$((count + 1))
    if ! bash -n "$script" 2>/tmp/parse_err; then
        echo "❌ $script 无法解析 —— 它后面的内容一件都不会执行,不管它打印什么:"
        sed 's/^/      /' /tmp/parse_err
        status=1
    fi
done

if [ "$count" -eq 0 ]; then
    echo "❌ 一个 .sh 都没扫到 —— 零条比对不可能是通过" >&2
    exit 1
fi

if [ "$count" -eq 0 ]; then
    echo "❌ 一个 .sh 都没扫到 —— 零条比对不可能是通过" >&2
    exit 1
fi

for script in scripts/*.py; do
    [ -f "$script" ] || continue
    count=$((count + 1))
    if ! python3 -m py_compile "$script" 2>/tmp/parse_err; then
        echo "❌ $script 语法错误 —— 它一件事都不会跑,而调用方看到的就是一次崩溃:"
        sed 's/^/      /' /tmp/parse_err
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "✅ $count 个闸门脚本(shell + python)全部可以解析"
fi
exit "$status"
