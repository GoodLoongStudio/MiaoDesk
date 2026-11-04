#pragma once
#include <string>

namespace whaledesk {

enum class ResultKind { App, File, Folder, Answer, Status };

struct SearchResult {
    ResultKind kind{};
    std::wstring title;
    std::wstring subtitle;
    std::wstring target;
    double score{};
};

} // namespace whaledesk
