#include "miaodesk/MiaoAssetDatabase.h"

#include "miaodesk/MiaoContentPackage.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

std::uint64_t HashFile(const fs::path& path, bool* ok) {
    constexpr std::uint64_t kOffset = 1469598103934665603ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (ok) *ok = false;
        return 0;
    }
    std::uint64_t hash = kOffset;
    char buffer[64 * 1024];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const auto count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= kPrime;
        }
    }
    if (ok) *ok = input.eof();
    return hash;
}

void AddDependent(MiaoAssetRecord* record, std::wstring id) {
    if (!record || id.empty()) return;
    if (std::find(record->dependents.begin(), record->dependents.end(), id) == record->dependents.end())
        record->dependents.push_back(std::move(id));
}

void CollectPropertyAssetReference(
    const PropertyDefinition& property,
    std::wstring_view dependentId,
    std::unordered_map<std::wstring, MiaoAssetRecord>* records) {
    if (property.type != PropertyType::AssetReference) return;
    const auto* reference = std::get_if<AssetReference>(&property.defaultValue);
    if (!reference || reference->id.empty()) return;
    const auto it = records->find(reference->id);
    if (it != records->end()) AddDependent(&it->second, std::wstring(dependentId));
}

} // namespace

bool MiaoAssetDatabase::Build(
    const fs::path& packageRoot,
    const SceneRuntimeDefinition& runtime,
    std::wstring* error) {
    if (error) error->clear();
    std::wstring runtimeError;
    if (!MiaoSceneRuntimeModel::Validate(runtime, &runtimeError))
        return Fail(error, L"Cannot build asset database from invalid runtime: " + runtimeError);

    std::unordered_map<std::wstring, MiaoAssetRecord> next;
    for (const auto& asset : runtime.scene.assets) {
        fs::path relative(asset.source);
        fs::path resolved;
        if (!MiaoContentPackage::ResolvePackagePath(packageRoot, relative, &resolved, error)) return false;
        std::error_code ec;
        if (!fs::exists(resolved, ec) || !fs::is_regular_file(resolved, ec))
            return Fail(error, L"Asset file does not exist: " + asset.id + L" -> " + relative.wstring());
        const auto size = fs::file_size(resolved, ec);
        if (ec) return Fail(error, L"Cannot read asset size: " + asset.id);
        bool hashOk = false;
        const auto hash = HashFile(resolved, &hashOk);
        if (!hashOk) return Fail(error, L"Cannot hash asset: " + asset.id);

        MiaoAssetRecord record;
        record.id = asset.id;
        record.type = asset.type;
        record.relativePath = std::move(relative);
        record.resolvedPath = std::move(resolved);
        record.contentHash = hash;
        record.size = size;
        if (!next.emplace(record.id, std::move(record)).second)
            return Fail(error, L"Duplicate asset id while building database: " + asset.id);
    }

    for (const auto& shader : runtime.scene.shaders) {
        const auto it = next.find(shader.assetId);
        if (it != next.end()) AddDependent(&it->second, shader.id);
    }
    for (const auto& material : runtime.materials) {
        for (const auto& texture : material.textures) {
            const auto it = next.find(texture.asset.id);
            if (it != next.end()) AddDependent(&it->second, material.id);
        }
        for (const auto& property : material.properties)
            CollectPropertyAssetReference(property, material.id, &next);
    }
    for (const auto& node : runtime.scene.nodes) {
        for (const auto& component : node.components) {
            for (const auto& property : component.properties)
                CollectPropertyAssetReference(property, component.id, &next);
        }
    }

    for (auto& [id, record] : next)
        std::sort(record.dependents.begin(), record.dependents.end());

    records_ = std::move(next);
    return true;
}

const MiaoAssetRecord* MiaoAssetDatabase::Find(std::wstring_view id) const noexcept {
    const auto it = records_.find(std::wstring(id));
    return it == records_.end() ? nullptr : &it->second;
}

std::vector<std::wstring> MiaoAssetDatabase::ChangedAssetsComparedTo(const MiaoAssetDatabase& previous) const {
    std::vector<std::wstring> changed;
    changed.reserve(records_.size());
    for (const auto& [id, record] : records_) {
        const auto it = previous.records_.find(id);
        if (it == previous.records_.end() || it->second.contentHash != record.contentHash ||
            it->second.size != record.size || it->second.relativePath != record.relativePath) {
            changed.push_back(id);
        }
    }
    for (const auto& [id, record] : previous.records_) {
        (void)record;
        if (!records_.contains(id)) changed.push_back(id);
    }
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    return changed;
}

void MiaoAssetDatabase::Clear() noexcept {
    records_.clear();
}

std::size_t MiaoAssetDatabase::Size() const noexcept {
    return records_.size();
}

bool MiaoAssetDatabase::SelfTest() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"MiaoDesk-AssetDatabase-SelfTest.mdwall";
    fs::remove_all(root, ec);
    fs::create_directories(root / L"assets", ec);
    if (ec) return false;
    {
        std::ofstream output(root / L"assets" / L"image.bin", std::ios::binary | std::ios::trunc);
        output << "asset-v1";
        if (!output) return false;
    }

    SceneRuntimeDefinition runtime;
    runtime.scene.id = L"scene://asset-self-test";
    runtime.scene.kind = ContentKind::Wallpaper;
    runtime.scene.rootNodeId = L"node://root";
    SceneNodeDefinition rootNode;
    rootNode.id = L"node://root";
    rootNode.components.push_back(SceneComponentDefinition{
        L"component://root/sprite",
        ComponentKind::SpriteRenderer,
        {PropertyDefinition{L"image", PropertyType::AssetReference, AssetReference{L"asset://image"}}},
    });
    runtime.scene.nodes.push_back(std::move(rootNode));
    runtime.scene.assets.push_back(AssetDefinition{L"asset://image", AssetType::Image, L"assets/image.bin"});
    runtime.profile = RuntimeProfile::Wallpaper;
    runtime.materials.push_back(MaterialDefinition{
        L"material://builtin/sprite", MaterialModel::Builtin, L"sprite", L"", L"", {},
        {MaterialTextureBinding{L"InputTexture", AssetReference{L"asset://image"}}},
    });

    std::wstring error;
    MiaoAssetDatabase first;
    if (!first.Build(root, runtime, &error) || first.Size() != 1) {
        fs::remove_all(root, ec);
        return false;
    }
    const auto* record = first.Find(L"asset://image");
    if (!record || record->dependents.size() != 2) {
        fs::remove_all(root, ec);
        return false;
    }

    MiaoAssetDatabase same;
    if (!same.Build(root, runtime, &error) || !same.ChangedAssetsComparedTo(first).empty()) {
        fs::remove_all(root, ec);
        return false;
    }
    {
        std::ofstream output(root / L"assets" / L"image.bin", std::ios::binary | std::ios::trunc);
        output << "asset-v2";
    }
    MiaoAssetDatabase changed;
    const bool ok = changed.Build(root, runtime, &error) && changed.ChangedAssetsComparedTo(first) == std::vector<std::wstring>{L"asset://image"};
    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::content
