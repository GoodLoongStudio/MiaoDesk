#include "miaodesk/MiaoLibraryRowFilter.h"

#include <algorithm>
#include <cwctype>

namespace miaodesk::library_row {
namespace {

std::wstring TrimText(std::wstring value) {
    const auto notSpace = [](wchar_t ch) { return !std::iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring LowerText(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool OnlyWhitespace(std::wstring_view value) {
    for (const wchar_t ch : value) {
        if (!std::iswspace(ch)) return false;
    }
    return true;
}

// 一个能放进消息里的短 ID。空 ID 也要有个说法,不然那句话读起来缺一块。
std::wstring IdForMessage(std::wstring_view id) {
    const std::wstring trimmed(id);
    return trimmed.empty() ? std::wstring(L"(无 ID)") : trimmed;
}

} // namespace

const wchar_t* RowSkipReasonName(RowSkipReason reason) noexcept {
    switch (reason) {
    case RowSkipReason::None: return L"None";
    case RowSkipReason::MissingId: return L"MissingId";
    case RowSkipReason::MissingKind: return L"MissingKind";
    case RowSkipReason::UnknownKind: break;
    }
    return L"UnknownKind";
}

bool IsKnownLibraryKind(std::wstring_view rawKind) noexcept {
    const std::wstring normalized = LowerText(TrimText(std::wstring(rawKind)));
    return normalized == L"image" || normalized == L"video" || normalized == L"web" ||
           normalized == L"scene";
}

RowSkipReason ClassifyLibraryRow(std::wstring_view id, std::wstring_view rawKind) noexcept {
    if (id.empty()) return RowSkipReason::MissingId;
    if (rawKind.empty() || OnlyWhitespace(rawKind)) return RowSkipReason::MissingKind;
    if (!IsKnownLibraryKind(rawKind)) return RowSkipReason::UnknownKind;
    return RowSkipReason::None;
}

std::wstring DescribeRowSkip(std::wstring_view id, std::wstring_view rawKind,
                             RowSkipReason reason) noexcept {
    switch (reason) {
    case RowSkipReason::None:
        return {};
    case RowSkipReason::MissingId:
        return L"库记录没有 ID,已跳过(这是配置损坏,不是版本差异)";
    case RowSkipReason::MissingKind:
        return L"库记录 " + IdForMessage(id) + L" 没有 Kind 字段,已跳过(这是配置损坏)";
    case RowSkipReason::UnknownKind:
        break;
    }
    // 版本差异:说清是哪个词、以及本构建认识哪些,用户才知道该升级还是该改配置。
    return L"库记录 " + IdForMessage(id) + L" 的 Kind “" + std::wstring(rawKind) +
           L"” 本版本不认识,已跳过(可能是旧版本写入或将来版本写入;本版本认识 image/video/web/scene)";
}

} // namespace miaodesk::library_row
