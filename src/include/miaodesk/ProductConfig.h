#pragma once

#include "miaodesk/AppPaths.h"

#include <array>

namespace miaodesk::product_config {

// Shipped product defaults are read-only. Missing, malformed or inaccessible
// configuration always falls back to the behavior compiled into this build.
inline bool StoreDemoScopeEnabled() noexcept {
    constexpr bool kFallback = true;
    try {
        const auto path = paths::ProductConfigFile();
        if (path.empty()) return kFallback;
        if (GetPrivateProfileIntW(L"Product", L"Schema", 0, path.c_str()) != 1)
            return kFallback;
        std::array<wchar_t, 16> value{};
        GetPrivateProfileStringW(
            L"Demo", L"StoreScopeEnabled", L"", value.data(),
            static_cast<DWORD>(value.size()), path.c_str());
        if (value[0] == L'0' && value[1] == L'\0') return false;
        if (value[0] == L'1' && value[1] == L'\0') return true;
        return kFallback;
    } catch (...) {
        return kFallback;
    }
}

} // namespace miaodesk::product_config
