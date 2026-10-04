#include "miaodesk/MiaoFileSearchNotice.h"

namespace miaodesk {
namespace {

const std::wstring kNoneTitle;
const std::wstring kInFlightTitle = L"正在搜索文件…";
const std::wstring kNotInstalledTitle = L"文件搜索未连接";
const std::wstring kQueryFailedTitle = L"文件查询失败";

// 每一句说明都不许断言输入没有建立的事实。见头文件里那段关于"已连接"的说明。
const std::wstring kInFlightDetail =
    L"文件索引查询仍在进行；你也可以直接按 Enter 交给妙喵 AI。";
const std::wstring kNotInstalledDetail =
    L"当前仍可搜索应用；按 Enter 可交给妙喵 AI。";
const std::wstring kQueryFailedDetail =
    L"这一次没有拿到文件结果（可能索引服务没起来，也可能这次查询超时）；"
    L"应用搜索仍然可用，按 Enter 可交给妙喵 AI。";

} // namespace

FileSearchNotice DecideFileSearchNotice(bool pending, bool available, bool queryFailed) noexcept {
    // 客户端二进制都不在:这是最根本的一条,先看它。放在 pending 之后会让
    // 没装客户端的机器显示"正在搜索文件…",然后永远等一个不会到来的回包。
    if (!available) return FileSearchNotice::NotInstalled;
    if (pending) return FileSearchNotice::InFlight;
    if (queryFailed) return FileSearchNotice::QueryFailed;
    return FileSearchNotice::None;
}

const std::wstring& FileSearchNoticeTitle(FileSearchNotice notice) noexcept {
    switch (notice) {
        case FileSearchNotice::InFlight: return kInFlightTitle;
        case FileSearchNotice::NotInstalled: return kNotInstalledTitle;
        case FileSearchNotice::QueryFailed: return kQueryFailedTitle;
        case FileSearchNotice::None: break;
    }
    return kNoneTitle;
}

const std::wstring& FileSearchNoticeDetail(FileSearchNotice notice) noexcept {
    switch (notice) {
        case FileSearchNotice::InFlight: return kInFlightDetail;
        case FileSearchNotice::NotInstalled: return kNotInstalledDetail;
        case FileSearchNotice::QueryFailed: return kQueryFailedDetail;
        case FileSearchNotice::None: break;
    }
    static const std::wstring empty;
    return empty;
}

} // namespace miaodesk
