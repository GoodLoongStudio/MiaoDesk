#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

struct MiaoAssetRecord {
    std::wstring id;
    AssetType type{AssetType::Binary};
    std::filesystem::path relativePath;
    std::filesystem::path resolvedPath;
    std::uint64_t contentHash{};
    std::uintmax_t size{};
    std::vector<std::wstring> dependents;
};

class MiaoAssetDatabase {
public:
    bool Build(
        const std::filesystem::path& packageRoot,
        const SceneRuntimeDefinition& runtime,
        std::wstring* error = nullptr);

    const MiaoAssetRecord* Find(std::wstring_view id) const noexcept;
    std::vector<std::wstring> ChangedAssetsComparedTo(const MiaoAssetDatabase& previous) const;
    void Clear() noexcept;
    std::size_t Size() const noexcept;

    static bool SelfTest();

private:
    std::unordered_map<std::wstring, MiaoAssetRecord> records_;
};

} // namespace miaodesk::content
