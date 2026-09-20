#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/MiaoContentModel.h"

namespace miaodesk::content {

struct ResolvedWidgetContent {
    std::filesystem::path packageRoot;
    ContentDefinition definition;
};

// Resolves stable persisted sources such as
//   content:com.goodloong.glass-clock
// to a validated .mdwidget package. Persisted desktop state never stores an
// installation-specific absolute path.
class MiaoWidgetContentCatalog {
public:
    static constexpr std::wstring_view kSourcePrefix = L"content:";

    static bool IsContentSource(std::wstring_view source) noexcept;
    static std::wstring MakeSource(std::wstring_view definitionId);
    static bool ParseSource(
        std::wstring_view source,
        std::wstring* definitionId,
        std::wstring* error = nullptr);

    static bool Resolve(
        std::wstring_view source,
        ResolvedWidgetContent* content,
        std::wstring* error = nullptr);

    // Deterministic resolver used by tests and future Creator/package import.
    // Roots are searched in order; product-owned Widgets should precede mutable
    // user package roots so a user package cannot shadow an official ID.
    static bool ResolveInRoots(
        std::wstring_view source,
        const std::vector<std::filesystem::path>& roots,
        ResolvedWidgetContent* content,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace miaodesk::content
