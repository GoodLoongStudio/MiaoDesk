#include "miaodesk/MiaoGozRecovery.h"

namespace miaodesk::goz_recovery {

GozServiceState DescribeGozServiceState(std::uint32_t rawState) noexcept {
    // 这些数值来自 Win32 的 SERVICE_STATUS_PROCESS。写在这里而不是在调用点,
    // 是为了让L"数值与名字对不上"这件事有一个地方可以被测。
    switch (rawState) {
        case 1: return GozServiceState::Stopped;         // SERVICE_STOPPED
        case 2: return GozServiceState::StartPending;    // SERVICE_START_PENDING
        case 3: return GozServiceState::StopPending;     // SERVICE_STOP_PENDING
        case 4: return GozServiceState::Running;         // SERVICE_RUNNING
        case 5: return GozServiceState::ContinuePending; // SERVICE_CONTINUE_PENDING
        case 6: return GozServiceState::PausePending;    // SERVICE_PAUSE_PENDING
        case 7: return GozServiceState::Paused;          // SERVICE_PAUSED
        default: break;
    }
    return GozServiceState::Unknown;
}

const char* ToString(GozServiceState state) noexcept {
    switch (state) {
        case GozServiceState::Unknown: return "unknown";
        case GozServiceState::Stopped: return "stopped";
        case GozServiceState::StartPending: return "start-pending";
        case GozServiceState::StopPending: return "stop-pending";
        case GozServiceState::Running: return "running";
        case GozServiceState::ContinuePending: return "continue-pending";
        case GozServiceState::PausePending: return "pause-pending";
        case GozServiceState::Paused: return "paused";
    }
    return "unknown";
}

const char* ToString(GozRecoveryOutcome outcome) noexcept {
    switch (outcome) {
        case GozRecoveryOutcome::NotNeeded: return "not-needed";
        case GozRecoveryOutcome::ServiceMissing: return "service-missing";
        case GozRecoveryOutcome::AccessDenied: return "access-denied";
        case GozRecoveryOutcome::StatusUnknown: return "status-unknown";
        case GozRecoveryOutcome::StartIssued: return "start-issued";
        case GozRecoveryOutcome::AlreadyStarting: return "already-starting";
        case GozRecoveryOutcome::StartRefused: return "start-refused";
        case GozRecoveryOutcome::Recovered: return "recovered";
        case GozRecoveryOutcome::TimedOut: return "timed-out";
    }
    return "unknown";
}

GozRecoveryOutcome DecideGozRecovery(bool pipeUpAtEntry,
                                     bool scmOpened,
                                     bool serviceOpened,
                                     GozServiceState state,
                                     bool startIssued,
                                     bool pipeUpAtExit) noexcept {
    // 管道本来就在:什么都不该做,也不该报。
    if (pipeUpAtEntry) return GozRecoveryOutcome::NotNeeded;

    // 打不开 SCM 或服务:这是权限/安装问题,不是L"再等等"。
    if (!scmOpened || !serviceOpened) return GozRecoveryOutcome::ServiceMissing;

    // 状态查不到会一路落到函数末尾的 StatusUnknown —— 没有为它单写一个分支,
    // 是因为这里加 `if (state == Unknown) return StatusUnknown;` 是**不可达的
    // 冗余**:变异检测把那条早退删掉之后测试依然全绿。留着只会让人以为
    // L"查不到状态"在这里被特殊处理过,而其实没有。
    // 不猜、也不乱拉是靠L"下面每一支都只认自己那一个状态"做到的:Stopped 分支
    // 不会因为 state 是 Unknown 就放行。

    // 服务正自己在起(或正在停):不要再叫一次 StartService ——
    // 对 START_PENDING 叫会拿到 ERROR_SERVICE_ALREADY_RUNNING,而对
    // STOP_PENDING 叫会被拒绝,两次都只会把结论搅浑。等就行。
    if (state == GozServiceState::StartPending) {
        // 只有真等到了才算 AlreadyStarting。等不满就归 TimedOut ——
        // 第一版这里无条件返回 AlreadyStarting,于是L"服务一直在起、等到超时"
        // 也被记成成功,而 GozRecoverySucceeded 说它不是成功,两边矛盾。
        return pipeUpAtExit ? GozRecoveryOutcome::AlreadyStarting : GozRecoveryOutcome::TimedOut;
    }
    if (state == GozServiceState::StopPending) return GozRecoveryOutcome::TimedOut;
    if (state == GozServiceState::Running) {
        // 服务说在跑,管道却不通:这不是启动问题。等一等,但不该报L"启动失败"。
        return pipeUpAtExit ? GozRecoveryOutcome::Recovered : GozRecoveryOutcome::TimedOut;
    }
    if (state == GozServiceState::Paused) {
        // 暂停中的服务不会应管道,而 StartService 对它是错的(那是 ResumeService 的活)。
        return GozRecoveryOutcome::StartRefused;
    }

    // 只有 Stopped 才该拉。拉了没拉起来(调用方看到 StartServiceW 返回 FALSE)
    // 与拉都没拉是两回事,后者不该报L"启动被拒绝"。
    if (state == GozServiceState::Stopped) {
        if (!startIssued) return GozRecoveryOutcome::StartRefused;
        return pipeUpAtExit ? GozRecoveryOutcome::Recovered : GozRecoveryOutcome::TimedOut;
    }

    return GozRecoveryOutcome::StatusUnknown;
}

bool GozRecoverySucceeded(GozRecoveryOutcome outcome) noexcept {
    return outcome == GozRecoveryOutcome::NotNeeded ||
           outcome == GozRecoveryOutcome::Recovered ||
           outcome == GozRecoveryOutcome::AlreadyStarting;
}

bool GozRecoveryNeedsNotice(GozRecoveryOutcome outcome) noexcept {
    return outcome == GozRecoveryOutcome::ServiceMissing ||
           outcome == GozRecoveryOutcome::AccessDenied ||
           outcome == GozRecoveryOutcome::StatusUnknown ||
           outcome == GozRecoveryOutcome::StartRefused ||
           outcome == GozRecoveryOutcome::TimedOut;
}

std::wstring ExplainGozRecovery(GozRecoveryOutcome outcome) {
    switch (outcome) {
        case GozRecoveryOutcome::NotNeeded: return L"";
        case GozRecoveryOutcome::ServiceMissing:
            return L"文件索引服务没有安装。应用搜索仍然可用；文件搜索需要安装 gozd 服务。";
        case GozRecoveryOutcome::AccessDenied:
            return L"没有权限启动文件索引服务。应用搜索仍然可用；请用管理员身份运行一次。";
        case GozRecoveryOutcome::StatusUnknown:
            return L"读不到文件索引服务的状态。应用搜索仍然可用；这次无法自动恢复它。";
        case GozRecoveryOutcome::StartIssued:
            return L"文件索引服务正在启动。";
        case GozRecoveryOutcome::AlreadyStarting:
            return L"文件索引服务正在自己启动，已等待它。";
        case GozRecoveryOutcome::StartRefused:
            return L"文件索引服务启动被拒绝（可能被禁用）。应用搜索仍然可用。";
        case GozRecoveryOutcome::Recovered: return L"";
        case GozRecoveryOutcome::TimedOut:
            return L"文件索引服务这一次没有就绪。应用搜索仍然可用；稍后再试。";
    }
    return L"";
}

} // namespace miaodesk::goz_recovery
