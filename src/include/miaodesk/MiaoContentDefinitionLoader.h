#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "miaodesk/MiaoContentModel.h"
#include "miaodesk/MiaoContentPackage.h"

namespace miaodesk::content {

// Converts a validated .mdwall/.mdwidget package into the host-facing immutable
// ContentDefinition. Runtime mutable state remains in ContentInstance.
class MiaoContentDefinitionLoader {
public:
    static bool Load(
        const std::filesystem::path& packageRoot,
        ContentDefinition* definition,
        std::wstring* error = nullptr);

    static bool FromPackage(
        const LoadedMiaoContentPackage& package,
        ContentDefinition* definition,
        std::wstring* error = nullptr);

    static bool ParseParameterSchema(
        std::string_view parameterJsonUtf8,
        ContentParameterSchema* schema,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace miaodesk::content
