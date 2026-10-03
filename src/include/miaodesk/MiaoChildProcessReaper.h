#pragma once

// P0-08:子进程必须跟着父进程一起死。
//
// `TerminateProcess(MiaoDesk)` 不跑析构函数、不跑 atexit —— 于是"父进程自己会收拾
// 子进程"这件事在强杀路径上一条都不成立。PiRuntime::CleanupProcess 写得再好,
// 也只是给正常退出路径准备的;强杀之后 Node 会一直活下去,握着一根已经断掉的
// stdin 管子,而没有任何人再去收它。这正是 P0-08 的验收原话:
// "无永久 Node/WebView2/Wallpaper/Harness 孤儿"。
//
// 唯一挡住这件事的是 Windows Job Object:把子进程装进一个带
// `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 的 job,父进程一死(不管怎么死的),
// 内核会连坐杀掉 job 里的全部进程。
//
// 仓库里已经有两次这么做了 —— WebWallpaperHost.cpp 与 HarnessProcessManager.cpp
// 各自 `CreateJobObjectW` + `AssignProcessToJobObject`。本文件把它们共用的那部分收成
// 一处,免得第三个、第四个站点再各写一遍(各写一遍的后果已经能看见:另有一个
// `LaunchBackgroundHarnessOwner` 在 main.cpp 和 HarnessHost.cpp 里存在两份相同的实现)。
//
// 边界:这个头文件**只做"装进去"**,不改变任何调用方原有的优雅退出路径。
// 装失败时函数返回 false 并说清原因 —— 那时行为与今天完全一样,风险是显式的。
#include <windows.h>

#include <string>

namespace miaodesk::child_reaper {

// 进程级唯一的 reaper job。生命周期与进程相同(故意不关句柄 ——
// KILL_ON_JOB_CLOSE 正是在"最后一个句柄关闭"时触发的,那发生在进程退出、
// 内核回收句柄的那一刻,而不是某个析构函数想跑的时候)。
inline HANDLE ReaperJob() {
    static HANDLE job = []() -> HANDLE {
        HANDLE created = CreateJobObjectW(nullptr, nullptr);
        if (!created) return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(created, JobObjectExtendedLimitInformation, &limits,
                                     sizeof(limits))) {
            // 设不上就别用:一个没有 KILL_ON_JOB_CLOSE 的 job 只会让人以为这里有收尸,
            // 而强杀之后子进程照旧活着 —— 那比没有 job 更糟。
            CloseHandle(created);
            return nullptr;
        }
        return created;
    }();
    return job;
}

// 把子进程装进 reaper job。成功返回 true。
//
// 失败的原因会在 reason 里说成人看的一句话。最常见的失败是
// ERROR_ACCESS_DENIED:父进程自己已经在另一个 job 里(调试器、CI、Explorer 的
// Job 都可能),而那个 job 没有 JOB_OBJECT_LIMIT_BREAKAWAY_OK,嵌套不进去。
// 这不是本程序的缺陷,但**必须让调用方知道它没装成**。
inline bool AttachToReaper(HANDLE process, std::wstring& reason) {
    reason.clear();
    if (!process) {
        reason = L"子进程句柄为空";
        return false;
    }
    const HANDLE job = ReaperJob();
    if (!job) {
        reason = L"无法创建带 KILL_ON_JOB_CLOSE 的 job,Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    if (AssignProcessToJobObject(job, process)) {
        reason.clear();
        return true;
    }
    const DWORD code = GetLastError();
    reason = L"无法把子进程装进收尸 job,Win32=" + std::to_wstring(code);
    if (code == ERROR_ACCESS_DENIED) {
        reason += L"（父进程自身已在另一个 job 中且不允许嵌套）";
    }
    return false;
}

} // namespace miaodesk::child_reaper
