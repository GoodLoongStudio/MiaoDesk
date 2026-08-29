#pragma once
#include "miaodesk/SearchTypes.h"
#include <cstddef>
#include <string>
#include <vector>

namespace miaodesk {

class AppSearch {
public:
    struct Entry {
        std::wstring name;
        std::wstring target;
        std::wstring keywords;
    };

    void BuildIndex();
    std::vector<SearchResult> Query(const std::wstring& query, std::size_t maxResults = 8) const;
    std::size_t Count() const noexcept { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

} // namespace miaodesk
