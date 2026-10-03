import io, os, re, sys

ROOT = '/Volumes/Ext/Projects/MiaoDesk'
SRC = ROOT + '/src'

# 故意分离生命周期的登记:相对 src 的路径 -> 它靠什么停。
# 值必须是一个真的出现在文件里的符号名:只写"设计如此"不算,必须能指出那个
# 真的会让它停下来的东西。登记过期(符号被改名/删掉)时门会说"登记已过期"。
# 值 = (定义这个 stop event 的符号, 那个事件的真名)。
# 登记**故意查两样**:符号必须在文件里,而符号定义的必须是那个真名。只查符号名的话,
# "把事件名改了、留着一个名字没变的常量"会一路绿灯 —— 而那时父进程死后没有任何人
# 能拦下这个孤儿。变异检测逮到的:只改字面量不改符号,门仍绿。
# 值 = (定义这个 stop event 的符号, 那个事件的真名)。
# 登记**故意查两样**:符号必须在文件里,而符号定义的必须是那个真名。只查符号名的话,
# "把事件名改了、留着一个名字没变的常量"会一路绿灯 —— 而那时父进程死后没有任何人
# 能拦下这个孤儿。变异检测逮到的:只改字面量不改符号名,门仍绿。
# 这里的字符串是**普通字符串而不是正则**:第一版写成了正则,把不属于反斜杠的点也转义了,
# 于是永远匹配不上,门把三个正确登记的站点全判成"登记已过期"。
SEPARATE = {
    'app/main.cpp': ('kHarnessBackgroundStopEvent', 'Local\\MiaoDesk.Native.Harness.Background.Stop'),
    'harness/HarnessHost.cpp': ('kBackgroundStopEventName', 'Local\\MiaoDesk.Native.Harness.Background.Stop'),
    'desktop/wallpaper/runtime/WallpaperEntry.cpp': ('stopEventName', None),
}
NOTES = {
    'app/main.cpp': '后台 Harness 所有者:自带单实例 mutex 与 stop event,设计上就是另一个进程的生命周期',
    'harness/HarnessHost.cpp': '同上(与 app/main.cpp 是两份相同的实现,本身也该合并)',
    'desktop/wallpaper/runtime/WallpaperEntry.cpp': '壁纸 helper:自带 HelperSpec::mutexName / stopEventName,同上',
}

def strip_line_comments(text):
    out = []
    i, n = 0, len(text)
    in_block = False
    while i < n:
        if in_block:
            j = text.find('*/', i)
            if j == -1:
                out.append(' ' * (n - i)); i = n
            else:
                out.append(' ' * (j + 2 - i)); i = j + 2
            in_block = '*/' not in text[:0]  # placeholder
            in_block = False if j == -1 else True
            if j == -1: in_block = False
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i)
            if j == -1:
                out.append(' ' * (n - i)); break
            out.append(' ' * (j + 2 - i)); i = j + 2; continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            if j == -1: break
            out.append(' ' * (j - i)); i = j; continue
        ch = text[i]
        if ch in '"\'':
            j = i + 1
            while j < n:
                if text[j] == '\\': j += 2; continue
                if text[j] == ch: j += 1; break
                j += 1
            out.append(text[i:j]); i = j; continue
        out.append(ch); i += 1
    return ''.join(out)

def main():
    problems = []
    stats = {'job': [], 'bounded': [], 'separate': []}
    for dirpath, dirnames, filenames in os.walk(SRC):
        for name in sorted(filenames):
            if not name.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, ROOT)
            raw = io.open(path, encoding='utf-8', errors='replace').read()
            code = strip_line_comments(raw)
            for m in re.finditer(r'\bCreateProcessW\s*\(', code):
                line = code[:m.start()].count('\n') + 1
                # 站点到所属函数结束:本仓库函数体以列 0 的 '}' 收尾
                j = code.find('\n}\n', m.end())
                window = code[m.start(): j if j > 0 else m.end() + 4000]
                if 'AttachToReaper' in window:
                    # 装了 job 还不够:返回值必须被检查。只看一眼就丢掉返回值的话,
                    # “装不进去”这件事在日志里一个字都不会出现 —— 而那正是最需要
                    # 被看见的情形(父进程自己在调试器/CI 的 job 里,嵌套不进去)。
                    if not re.search(r'if\s*\(\s*!\s*\w+\s*::\s*AttachToReaper|if\s*\(\s*!\s*AttachToReaper', window):
                        problems.append(
                            '%s:%d 调了 AttachToReaper 但没有检查返回值 —— 装不进去时没人知道'
                            % (rel, line))
                    else:
                        stats['job'].append((rel, line))
                    continue
                if 'AssignProcessToJobObject' in window:
                    stats['job'].append((rel, line)); continue
                # 有界同步:必须**先等**、再(失败时)终止。只按"文件里有 TerminateProcess"
                # 判的话,一个把 wait 换掉的站点会被当成安全 —— 而它从来没等过。
                if 'WaitForSingleObject' in window and 'TerminateProcess' in window:
                    if window.index('WaitForSingleObject') < window.index('TerminateProcess'):
                        stats['bounded'].append((rel, line)); continue
                entry = SEPARATE.get(rel[len('src/'):])
                if entry:
                    symbol, literal = entry
                    # C++ 源码里的宽字面量把反斜杠写成两折,先还原成一层再比。
                    value = code
                    if literal:
                        m = re.search(re.escape(symbol) + r'\s*\[\s*\]\s*=\s*L"([^"]*)"', code)
                        if m:
                            value = m.group(1).replace('\\\\', '\\')
                        else:
                            # 不是文件级常量(例如它是结构体字段),就退一步看它是否被
                            # OpenEventW 真的打开 —— 那才是"它真的会停"的证据。
                            value = code if re.search(r'OpenEventW[^;]*' + re.escape(symbol), code) else ''
                    else:
                        value = code if re.search(r'OpenEventW[^;]*' + re.escape(symbol), code) else ''
                    ok = bool(value) and (not literal or value == literal)
                    if not ok:
                        problems.append(
                            '%s:%d 登记的 stop event 证据不存在:%s 没有定义成 "%s",也没有被 OpenEventW 打开'
                            % (rel, line, symbol, literal or symbol))
                    else:
                        stats['separate'].append((rel, line, symbol))
                    continue
                problems.append(
                    '%s:%d CreateProcessW 之后既不收尸也不等;强杀父进程会留下永久孤儿'
                    % (rel, line))

    total = len(stats['job']) + len(stats['bounded']) + len(stats['separate'])
    if total == 0:
        # 一个站点都没扫到 = 扫描本身坏了,而不是"仓里没有子进程"。
        # 一道什么都没查却打印通过的门,与一道好门在通过时长得一模一样 —— 这个仓库里
        # 已经为此栽过好几次(see verify-shell-scripts-parse.sh 的零条比对守卫)。
        print('❌ 一个 CreateProcessW 站点都没扫到 —— 零条比对不可能是通过')
        print('   检查 scripts/verify-child-process-reapers.py 的扫描路径与扩展名。')
        return 1

    print('扫描 src/ 下的 CreateProcessW 站点(注释里的提及不算)…')
    print()
    for rel, line in stats['job']:
        print('  ✅ %s:%d —— 装进收尸 job(KILL_ON_JOB_CLOSE)' % (rel, line))
    for rel, line in stats['bounded']:
        print('  ✅ %s:%d —— 有界同步(当场等 HANDLE,超时/失败即 TerminateProcess)' % (rel, line))
    for rel, line, stop in stats['separate']:
        print('  🟡 %s:%d —— 故意分离的生命周期,靠 %s 停' % (rel, line, stop))
    print()
    print('站点分类:装进 job %d · 有界同步 %d · 故意分离 %d' %
          (len(stats['job']), len(stats['bounded']), len(stats['separate'])))
    if problems:
        print()
        print('❌ 以下进程站点没有收尸:')
        for p in problems:
            print('   - ' + p)
        print()
        print('修法:把 HANDLE 交给 child_reaper::AttachToReaper')
        print('(见 include/miaodesk/MiaoChildProcessReaper.h)。若它确实应当是独立的生命周期,')
        print('就把 stop event 的名字登记到本脚本的 SEPARATE,并说清它是怎么停的。')
        return 1
    print()
    print('✅ 每个 CreateProcessW 站点都有收尸路径(装 job / 有界同步 / 已登记的独立生命周期)')
    return 0

sys.exit(main())
