#!/usr/bin/env bash
# 校验:每个工作流的 paths 过滤覆盖它自己会执行/读取的所有仓库文件。
#
# 为什么需要它:同一个缺口今天撞了三次 ——
#   1. windows-x64-build 跑 node tests/*.mjs,但 paths 里没有 tests/**
#   2. (同类)scripts/verify-web-audio-bridge.ps1 被 windows-x64-build 执行,
#      但 paths 里只有两个具体的 scripts/*.ps1
# 后果都一样:改了那个文件,没有任何一项被过滤条件列中的内容变动,于是唯一验证它的
# 工作流根本不触发 —— 改完等于没验。
#
# 做法:从每个工作流的 run: 块里抽出它引用的仓库相对路径,和 on.<event>.paths 的
# 过滤模式比对。抽不到引用的(纯命令如 cmake/node)跳过。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2

python3 - "$ROOT" <<'PY'
import glob, os, re, sys

root = sys.argv[1]

def parse_workflow(path):
    text = open(path, encoding='utf-8').read()
    lines = text.split('\n')
    # 找 on: 下的 push/pull_request 的 paths 列表(共用一个扁平列表就够,两处不一致
    # 本身也是问题,另行报告)
    patterns, in_paths, base = [], False, None
    for line in lines:
        if re.match(r'^\s+(push|pull_request|workflow_dispatch):\s*$', line):
            in_paths = False
            continue
        if re.match(r'^\s+paths:\s*$', line):
            in_paths, base = True, 4   # 列表项至少比 paths: 再深一级
            continue
        if in_paths:
            m = re.match(r'^\s+-\s+(\S+)\s*$', line)
            if m and (len(line) - len(line.lstrip())) >= base:
                patterns.append(m.group(1))
            elif line.strip() and not line.strip().startswith('#'):
                in_paths = False
    # run: 块。两种写法都要认:
    #   run: |                多行,后面整段都是命令
    #   run: .\scripts\x.ps1    单行,GitHub 同样会执行它
    # 只认 `run: |` 是这里原来的实现,后果是这个闸门对整个 workflow 抽不到任何引用,
    # 于是一条都没比对就报绿 —— 2026-09-22 就是靠这个空洞,让三个从来没被调用过的
    # 闸门脚本一直留在 paths 列表里而没人发现(它们全写成单行 run:)。
    runs = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == 'run: |':
            b = len(line) - len(line.lstrip())
            body, k = [], i + 1
            while k < len(lines) and (len(lines[k]) - len(lines[k].lstrip()) > b or not lines[k].strip()):
                body.append(lines[k]); k += 1
            runs.append('\n'.join(body))
        elif stripped.startswith('run:'):
            command = stripped[len('run:'):].strip()
            if command:
                runs.append(command)
    return patterns, runs, text

def referenced_files(run_text):
    r"""抽出 run 块里引用的仓库相对路径。

    两处细节都是被漏报教出来的:
      * 必须先剥掉 ./ 或 .\ 再判断是不是相对路径。CI 里写的是
        `.\scripts\verify-web-audio-bridge.ps1`,先判 startswith('.') 会把它
        整条跳过 —— 而那恰好是这个闸门本来要抓的情况。
      * 扩展名交替里 json 必须排在 js 前面,否则 CMakePresets.json 会被截成
        CMakePresets.js,凭空多出一个不存在的文件。
    """
    found = set()
    for m in re.finditer(r'([A-Za-z0-9_./\\-]+\.(?:ps1|mjs|sh|py|json|js|ts))', run_text):
        p = m.group(1).replace('\\', '/')
        p = re.sub(r'^\./', '', p)
        if p.startswith(('.', '$')) or re.match(r'^[A-Za-z]:', p):
            continue
        if not re.match(r'^[A-Za-z0-9_][A-Za-z0-9_./-]*$', p):
            continue
        found.add(p)
    return found

def matches(patterns, rel):
    import fnmatch
    for pat in patterns:
        pat = pat.rstrip('/').strip('"\'')
        if not pat:
            continue
        if rel == pat:
            return True
        if fnmatch.fnmatch(rel, pat):
            return True
        if fnmatch.fnmatch(rel, pat + '/*'):
            return True
        # 目录前缀匹配:tests/** 命中 tests/a.mjs
        if pat.endswith('/**') and rel.startswith(pat[:-3] + '/'):
            return True
    return False

problems = 0
for wf in sorted(glob.glob(os.path.join(root, '.github/workflows/*.yml'))):
    name = os.path.basename(wf)
    if name in ('repo-hygiene.yml', 'no-conflict-markers.yml'):
        continue  # 无 paths 过滤,每次推送都跑
    patterns, runs, text = parse_workflow(wf)
    if not patterns:
        print(f'{name}: 无 paths 过滤(每次推送都跑),跳过')
        continue
    refs = set()
    for r in runs:
        refs |= referenced_files(r)
    # 只保留仓库里真实存在的
    refs = {r for r in refs if os.path.exists(os.path.join(root, r))}
    missing = sorted(r for r in refs if not matches(patterns, r))
    if missing:
        print(f'{name}:')
        print(f'   paths 过滤: {", ".join(patterns)}')
        print(f'   ❌ 执行/读取了但过滤未覆盖的文件:')
        for m in missing:
            print(f'        {m}')
        problems += 1

if problems:
    print()
    print(f'{problems} 个工作流存在这个缺口。改这些文件不会触发验证它们的工作流。')
    sys.exit(1)
print()
print('✅ 所有工作流的 paths 过滤都覆盖了它引用的仓库文件')
sys.exit(0)
PY
