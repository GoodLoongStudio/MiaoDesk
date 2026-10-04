#pragma once
#include "miaodesk/MiaoSearchGeneration.h"
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
    static bool EnsurePipeAvailable(DWORD waitMs);

    std::shared_ptr<SharedState> state_;
};

} // namespace miaodesk
