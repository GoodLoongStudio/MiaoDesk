#pragma once
#include "miaodesk/MiaoSearchGeneration.h"
#include "miaodesk/MiaoGozRecovery.h"
#include "miaodesk/SearchTypes.h"
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace miaodesk {

// L2 file-search adapter. MiaoDesk owns UI/ranking while the pinned goz
// runtime owns the NTFS MFT + USN index and authenticated named-pipe protocol.
class GozSearch {
public:
    GozSearch();

    bool Available() const;
    bool Query(HWND replyWindow, const std::wstring& query, DWORD maxResults = 12) const;
    std::vector<SearchResult> QuerySync(const std::wstring& query, DWORD maxResults = 12) const;
    bool HandleCopyData(const COPYDATASTRUCT* copyData, std::vector<SearchResult>& results,
                        bool* querySucceeded = nullptr) const;
    bool SelfTest() const;
    void Shutdown() const;

    static constexpr DWORD kReplyId = 0x5444475A; // TDGZ

private:
    // 代号追踪器是纯逻辑(见 MiaoSearchGeneration.h),本机测得动;原先这里是一个
    // 裸的 std::atomic_uint64_t,`Claim`/`ShouldDeliver`/`Invalidate` 三件事散在
    // .cpp 的三行里,而那三行是用户连敲搜索框时唯一在保护结果正确性的东西。
    struct SharedState {
        SearchGenerationTracker generation;
    };

    static std::wstring FindClientBinary();
    static bool PipeAvailable();
    // outcome 可空。空表示"这一轮我不关心结论"(SelfTest 之类);
    // 非空时它收到这次恢复实际发生了什么 —— 判定在 MiaoGozRecovery。
    //
    // 保持 static 而不是改成 const 成员:`Query` 在一个 detach 出去的线程
    // lambda 里调它,而那个 lambda 刻意不捕 `this`(它要比实例活得更久)。
    // 把结论写成出参,静态函数就够,不必为它改线程模型。
    static bool EnsurePipeAvailable(
        DWORD waitMs, goz_recovery::GozRecoveryOutcome* outcome = nullptr);

    std::shared_ptr<SharedState> state_;

    // 最近一次恢复尝试的结论。worker 线程写(Query 的 detach lambda)、UI 线程读
    // (SearchWindow 拼提示行)。刻意不是 atomic:它是**诊断快照**而不是状态机输入 ——
    // 没有任何一条路径会因为它从 A 变成 B 就改变行为。要做成原子,就得连带处理
    // "读到的是哪一次的",而这里没有那种并发:一次查询最多触发一次恢复,
    // 而查询本身已经被 generation 串行化了。
    // 判定的纯逻辑在 MiaoGozRecovery。
    mutable goz_recovery::GozRecoveryOutcome lastRecovery_{
        goz_recovery::GozRecoveryOutcome::NotNeeded};

public:
    goz_recovery::GozRecoveryOutcome LastRecovery() const noexcept { return lastRecovery_; }
};

} // namespace miaodesk
