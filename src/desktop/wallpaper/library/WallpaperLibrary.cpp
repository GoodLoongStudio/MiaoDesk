#include "miaodesk/WallpaperLibrary.h"
#include "miaodesk/AppPaths.h"
#include "miaodesk/BuiltinWallpaperCatalog.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoContentPackageManager.h"
#include "miaodesk/MiaoSceneSerializer.h"
#include "miaodesk/UnicodeProfileFile.h"
#include "miaodesk/WallpaperPackage.h"

#include <windows.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <system_error>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kItemPrefix[] = L"Item.";
constexpr UINT kThumbnailWidth = 320;
constexpr UINT kThumbnailHeight = 180;

void SetError(std::wstring* error, std::wstring value) {
    if (error) *error = std::move(value);
}

fs::path DefaultLibraryRoot() {
    return paths::WallpaperLibraryRoot();
}

unsigned long long NowUnixSeconds() {
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

unsigned long long EarliestNonZero(unsigned long long a, unsigned long long b) {
    if (a == 0) return b;
    if (b == 0) return a;
    return std::min(a, b);
}

std::wstring SanitizeText(std::wstring value) {
    for (auto& ch : value) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return value;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                             value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            value.data(), static_cast<int>(value.size()), result.data(), required) != required)
        return {};
    return result;
}

fs::path NormalizedAbsolute(const fs::path& value) {
    std::error_code ec;
    fs::path absolute = fs::absolute(value, ec);
    if (ec) absolute = value;
    absolute = absolute.lexically_normal();
    fs::path canonical = fs::weakly_canonical(absolute, ec);
    return ec ? absolute : canonical;
}

bool SamePath(const fs::path& a, const fs::path& b) {
    const std::wstring left = NormalizedAbsolute(a).wstring();
    const std::wstring right = NormalizedAbsolute(b).wstring();
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool PathIsInside(const fs::path& candidate, const fs::path& root) {
    const std::wstring value = Lower(NormalizedAbsolute(candidate).wstring());
    std::wstring base = Lower(NormalizedAbsolute(root).wstring());
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/') base.push_back(fs::path::preferred_separator);
    return value.size() >= base.size() && value.compare(0, base.size(), base) == 0;
}

fs::path ManagedPackageRoot(const fs::path& source, const fs::path& packageDirectory) {
    if (source.empty()) return {};
    const fs::path libraryRoot = NormalizedAbsolute(packageDirectory);
    fs::path current = NormalizedAbsolute(source);
    std::error_code ec;
    if (fs::is_regular_file(current, ec)) current = current.parent_path();
    ec.clear();
    while (!current.empty() && PathIsInside(current, libraryRoot) && !SamePath(current, libraryRoot)) {
        if (Lower(current.extension().wstring()) == L".mdwall") return current;
        const fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return {};
}

std::wstring MakeId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return L"wallpaper-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(GetCurrentProcessId());
    }
    wchar_t text[64]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    std::wstring result;
    result.reserve(36);
    for (const wchar_t ch : std::wstring_view(text)) {
        if (ch != L'{' && ch != L'}' && ch != L'-') result.push_back(ch);
    }
    return Lower(result);
}

std::wstring SectionName(std::wstring_view id) {
    return std::wstring(kItemPrefix) + std::wstring(id);
}

std::wstring ReadProfileText(const fs::path& manifest, const std::wstring& section,
                             const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section.c_str(), key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), manifest.c_str());
    return buffer.data();
}

unsigned long long ReadProfileU64(const fs::path& manifest, const std::wstring& section,
                                  const wchar_t* key, unsigned long long fallback = 0) {
    const auto value = ReadProfileText(manifest, section, key, L"");
    if (value.empty()) return fallback;
    wchar_t* end = nullptr;
    const unsigned long long parsed = _wcstoui64(value.c_str(), &end, 10);
    return end == value.c_str() ? fallback : parsed;
}

bool WriteProfileText(const fs::path& manifest, const std::wstring& section,
                      const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(section.c_str(), key, value.c_str(), manifest.c_str()) != FALSE;
}

std::vector<std::wstring> EnumerateSections(const fs::path& manifest) {
    std::vector<std::wstring> sections;
    DWORD capacity = 65536;
    for (; capacity <= 1024 * 1024; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD written = GetPrivateProfileSectionNamesW(buffer.data(), capacity, manifest.c_str());
        if (written == 0) return sections;
        if (written >= capacity - 2) continue;
        for (const wchar_t* cursor = buffer.data(); *cursor; cursor += std::wcslen(cursor) + 1)
            sections.emplace_back(cursor);
        return sections;
    }
    return sections;
}

std::wstring DefaultTitle(const fs::path& path) {
    std::wstring title = path.stem().wstring();
    if (title.empty()) title = path.filename().wstring();
    return title.empty() ? L"未命名壁纸" : title;
}

bool SaveHBitmapAsPng(HBITMAP bitmap, const fs::path& path) {
    if (!bitmap) return false;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.GetAddressOf())))) return false;

    ComPtr<IWICBitmap> source;
    if (FAILED(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha,
                                                source.GetAddressOf()))) return false;

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf()))) return false;
    if (FAILED(frame->Initialize(properties.Get()))) return false;

    UINT width = 0;
    UINT height = 0;
    if (FAILED(source->GetSize(&width, &height)) || width == 0 || height == 0) return false;
    if (FAILED(frame->SetSize(width, height))) return false;
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&pixelFormat))) return false;
    if (FAILED(frame->WriteSource(source.Get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    return SUCCEEDED(encoder->Commit());
}

bool GenerateShellThumbnail(const fs::path& source, const fs::path& destination) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(item.GetAddressOf())))) return false;

    ComPtr<IShellItemImageFactory> imageFactory;
    if (FAILED(item.As(&imageFactory))) return false;

    HBITMAP bitmap = nullptr;
    const SIZE requested{static_cast<LONG>(kThumbnailWidth), static_cast<LONG>(kThumbnailHeight)};
    const HRESULT hr = imageFactory->GetImage(
        requested,
        static_cast<SIIGBF>(SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT),
        &bitmap);
    if (FAILED(hr) || !bitmap) return false;

    const bool saved = SaveHBitmapAsPng(bitmap, destination);
    DeleteObject(bitmap);
    return saved;
}

bool WriteUtf8File(const fs::path& path, std::string_view value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    return output.good();
}

bool IsLibraryUiVisible(const WallpaperLibraryItem& item) {
    // Managed built-in themes are presented only under canonical content:<id>
    // identities. Keep exact shipped scene-* rows in storage for compatibility,
    // but never surface them as duplicate cards in Library search/browse UI.
    return FindLegacyBuiltinWallpaper(item.id) == nullptr;
}

} // namespace

WallpaperLibrary::WallpaperLibrary() : root_(DefaultLibraryRoot()) {}
WallpaperLibrary::WallpaperLibrary(fs::path root) : root_(std::move(root)) {}

bool WallpaperLibrary::Load(std::wstring* error) {
    SetError(error, L"");
    items_.clear();
    std::error_code ec;
    fs::create_directories(root_, ec);
    fs::create_directories(MediaDirectory(), ec);
    fs::create_directories(PackageDirectory(), ec);
    fs::create_directories(ThumbnailDirectory(), ec);
    if (ec) {
        SetError(error, L"无法创建壁纸库目录：" + root_.wstring());
        return false;
    }

    const fs::path manifest = ManifestPath();
    if (!text::EnsureUtf16LeProfileFile(manifest, error)) return false;
    for (const auto& section : EnumerateSections(manifest)) {
        if (section.rfind(kItemPrefix, 0) != 0) continue;
        WallpaperLibraryItem item;
        item.id = section.substr(std::size(kItemPrefix) - 1);
        item.kind = ParseKind(ReadProfileText(manifest, section, L"Kind", L"unknown"));
        item.title = ReadProfileText(manifest, section, L"Title", L"");
        item.source = ReadProfileText(manifest, section, L"Source", L"");
        item.thumbnail = ReadProfileText(manifest, section, L"Thumbnail", L"");
        item.favorite = ReadProfileText(manifest, section, L"Favorite", L"0") == L"1";
        item.managedCopy = ReadProfileText(manifest, section, L"ManagedCopy", L"0") == L"1";
        item.importedUnixSeconds = ReadProfileU64(manifest, section, L"Imported", 0);
        item.lastUsedUnixSeconds = ReadProfileU64(manifest, section, L"LastUsed", 0);
        if (item.title.empty()) item.title = item.source.empty() ? L"未命名壁纸" : DefaultTitle(item.source);
        if (!item.id.empty() && item.kind != LibraryWallpaperKind::Unknown) items_.push_back(std::move(item));
    }

    const auto loadedItems = items_;
    for (const auto& legacy : loadedItems) {
        if (legacy.kind != LibraryWallpaperKind::Scene || legacy.source.empty()) continue;
        if (Lower(legacy.source.wstring()).rfind(L"content:", 0) == 0) continue;
        const fs::path packageRoot = ManagedPackageRoot(legacy.source, PackageDirectory());
        if (packageRoot.empty()) continue;
        content::LoadedMiaoContentPackage package;
        std::wstring packageError;
        if (!content::MiaoContentPackage::Load(packageRoot, &package, &packageError) ||
            package.manifest.kind != content::ContentKind::Wallpaper ||
            package.manifest.runtime != content::ContentRuntimeKind::Scene) continue;
        content::SceneRuntimeDefinition runtime;
        if (!content::MiaoSceneSerializer::DeserializePackage(package, &runtime, &packageError)) continue;
        const std::wstring stableId = content::MiaoContentPackageManager::MakeSource(package.manifest.id);
        if (stableId.empty()) continue;
        const auto legacyIndex = FindIndex(legacy.id);
        if (!legacyIndex) continue;
        const auto stableIndex = FindIndex(stableId);
        WallpaperLibraryItem migrated = stableIndex ? items_[*stableIndex] : items_[*legacyIndex];
        migrated.favorite = migrated.favorite || legacy.favorite;
        migrated.importedUnixSeconds = EarliestNonZero(migrated.importedUnixSeconds, legacy.importedUnixSeconds);
        migrated.lastUsedUnixSeconds = std::max(migrated.lastUsedUnixSeconds, legacy.lastUsedUnixSeconds);
        migrated.id = stableId;
        migrated.kind = LibraryWallpaperKind::Scene;
        migrated.source = NormalizedAbsolute(packageRoot);
        migrated.managedCopy = true;
        if (!SaveItem(migrated, error)) return false;
        if (_wcsicmp(legacy.id.c_str(), stableId.c_str()) != 0) {
            const std::wstring oldSection = SectionName(legacy.id);
            if (!WritePrivateProfileStringW(oldSection.c_str(), nullptr, nullptr, manifest.c_str())) {
                SetError(error, L"删除旧版 Scene 壁纸记录失败");
                return false;
            }
        }
        if (stableIndex && *stableIndex != *legacyIndex) {
            items_[*stableIndex] = migrated;
            const auto removeIndex = FindIndex(legacy.id);
            if (removeIndex) items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
        } else {
            const auto replaceIndex = FindIndex(legacy.id);
            if (replaceIndex) items_[*replaceIndex] = migrated;
        }
    }

    if (!DiscoverPackages(error)) return false;

    // Historical builds could persist source-empty built-in scene-* rows in
    // library.ini.  Collapse only exact shipped legacy identities and only
    // after the canonical .mdwall package has been discovered and validated.
    // This is a Library/UI cleanup boundary only; monitor assignments are not
    // read or written here and intentionally remain scene-* during migration.
    const auto discoveredItems = items_;
    for (const auto& legacy : discoveredItems) {
        if (legacy.kind != LibraryWallpaperKind::Scene || !legacy.source.empty()) continue;
        if (!FindLegacyBuiltinWallpaper(legacy.id)) continue;
        const std::wstring_view canonicalView = CanonicalBuiltinWallpaperSource(legacy.id);
        if (canonicalView.empty()) continue;
        const std::wstring canonicalId(canonicalView);
        const auto canonicalIndex = FindIndex(canonicalId);
        if (!canonicalIndex) continue;
        const WallpaperLibraryItem canonical = items_[*canonicalIndex];
        if (canonical.kind != LibraryWallpaperKind::Scene || canonical.source.empty()) continue;
        const fs::path packageRoot = ManagedPackageRoot(canonical.source, PackageDirectory());
        if (packageRoot.empty()) continue;
        content::LoadedMiaoContentPackage package;
        std::wstring packageError;
        if (!content::MiaoContentPackage::Load(packageRoot, &package, &packageError) ||
            package.manifest.kind != content::ContentKind::Wallpaper ||
            package.manifest.runtime != content::ContentRuntimeKind::Scene ||
            content::MiaoContentPackageManager::MakeSource(package.manifest.id) != canonicalId) continue;
        content::SceneRuntimeDefinition runtime;
        if (!content::MiaoSceneSerializer::DeserializePackage(package, &runtime, &packageError)) continue;

        WallpaperLibraryItem merged = canonical;
        merged.favorite = merged.favorite || legacy.favorite;
        merged.importedUnixSeconds = EarliestNonZero(merged.importedUnixSeconds, legacy.importedUnixSeconds);
        merged.lastUsedUnixSeconds = std::max(merged.lastUsedUnixSeconds, legacy.lastUsedUnixSeconds);
        if (!SaveItem(merged, error)) return false;
        const std::wstring oldSection = SectionName(legacy.id);
        if (!WritePrivateProfileStringW(oldSection.c_str(), nullptr, nullptr, manifest.c_str())) {
            SetError(error, L"删除旧版内置主题记录失败");
            return false;
        }
        const auto currentCanonicalIndex = FindIndex(canonicalId);
        if (currentCanonicalIndex) items_[*currentCanonicalIndex] = merged;
        const auto legacyIndex = FindIndex(legacy.id);
        if (legacyIndex) items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(*legacyIndex));
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, manifest.c_str());

    std::sort(items_.begin(), items_.end(), [](const WallpaperLibraryItem& a, const WallpaperLibraryItem& b) {
        if (a.lastUsedUnixSeconds != b.lastUsedUnixSeconds) return a.lastUsedUnixSeconds > b.lastUsedUnixSeconds;
        return a.importedUnixSeconds > b.importedUnixSeconds;
    });
    return true;
}

const std::vector<WallpaperLibraryItem>& WallpaperLibrary::Items() const noexcept { return items_; }

std::optional<WallpaperLibraryItem> WallpaperLibrary::ImportFile(
    const fs::path& sourcePath, const WallpaperImportOptions& options, std::wstring* error) {
    SetError(error, L"");
    std::error_code ec;
    if (!fs::exists(sourcePath, ec) || !fs::is_regular_file(sourcePath, ec)) {
        SetError(error, L"壁纸源文件不存在：" + sourcePath.wstring());
        return std::nullopt;
    }
    const LibraryWallpaperKind kind = InferKind(sourcePath);
    if (kind == LibraryWallpaperKind::Unknown) {
        SetError(error, L"不支持的壁纸文件类型：" + sourcePath.extension().wstring());
        return std::nullopt;
    }
    const fs::path normalizedSource = NormalizedAbsolute(sourcePath);
    if (const auto existing = FindSourceIndex(normalizedSource)) {
        auto& item = items_[*existing];
        if (!options.title.empty()) item.title = SanitizeText(options.title);
        item.lastUsedUnixSeconds = NowUnixSeconds();
        if (!SaveItem(item, error)) return std::nullopt;
        return item;
    }
    WallpaperLibraryItem item;
    item.id = MakeId();
    item.kind = kind;
    item.title = SanitizeText(options.title.empty() ? DefaultTitle(normalizedSource) : options.title);
    item.favorite = false;
    item.managedCopy = options.managedCopy;
    item.importedUnixSeconds = NowUnixSeconds();
    item.lastUsedUnixSeconds = item.importedUnixSeconds;
    if (options.managedCopy) {
        fs::create_directories(MediaDirectory(), ec);
        const fs::path destination = MediaDirectory() / (item.id + normalizedSource.extension().wstring());
        fs::copy_file(normalizedSource, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SetError(error, L"复制壁纸到托管库失败，error=" + std::to_wstring(ec.value()));
            return std::nullopt;
        }
        item.source = destination;
    } else item.source = normalizedSource;
    GenerateThumbnail(item);
    if (!SaveItem(item, error)) {
        if (item.managedCopy) fs::remove(item.source, ec);
        if (!item.thumbnail.empty()) fs::remove(item.thumbnail, ec);
        return std::nullopt;
    }
    items_.insert(items_.begin(), item);
    return item;
}

bool WallpaperLibrary::UpsertScene(std::wstring id, std::wstring title, std::wstring* error) {
    SetError(error, L"");
    if (id.empty()) { SetError(error, L"Scene ID 不能为空"); return false; }
    id = SanitizeText(std::move(id));
    title = SanitizeText(std::move(title));
    if (const auto index = FindIndex(id)) {
        auto& item = items_[*index];
        item.kind = LibraryWallpaperKind::Scene;
        item.title = title.empty() ? id : title;
        return SaveItem(item, error);
    }
    WallpaperLibraryItem item;
    item.id = std::move(id);
    item.kind = LibraryWallpaperKind::Scene;
    item.title = title.empty() ? item.id : std::move(title);
    item.importedUnixSeconds = NowUnixSeconds();
    if (!SaveItem(item, error)) return false;
    items_.push_back(std::move(item));
    return true;
}

bool WallpaperLibrary::Remove(std::wstring_view id, bool deleteManagedCopy, std::wstring* error) {
    SetError(error, L"");
    const auto index = FindIndex(id);
    if (!index) { SetError(error, L"壁纸库中不存在该项目"); return false; }
    const WallpaperLibraryItem item = items_[*index];
    const std::wstring section = SectionName(item.id);
    if (!WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, ManifestPath().c_str())) {
        SetError(error, L"删除壁纸库记录失败"); return false;
    }
    std::error_code ec;
    if (!item.thumbnail.empty() && PathIsInside(item.thumbnail, ThumbnailDirectory())) fs::remove(item.thumbnail, ec);
    if (deleteManagedCopy && item.managedCopy && !item.source.empty()) {
        if (PathIsInside(item.source, MediaDirectory())) fs::remove(item.source, ec);
        else if (PathIsInside(item.source, PackageDirectory())) {
            const fs::path package = ManagedPackageRoot(item.source, PackageDirectory());
            if (!package.empty()) fs::remove_all(package, ec);
        }
    }
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(*index));
    return true;
}

bool WallpaperLibrary::SetFavorite(std::wstring_view id, bool favorite, std::wstring* error) {
    const auto index = FindIndex(id);
    if (!index) { SetError(error, L"壁纸库中不存在该项目"); return false; }
    items_[*index].favorite = favorite;
    return SaveItem(items_[*index], error);
}

bool WallpaperLibrary::MarkUsed(std::wstring_view id, std::wstring* error) {
    const auto index = FindIndex(id);
    if (!index) { SetError(error, L"壁纸库中不存在该项目"); return false; }
    items_[*index].lastUsedUnixSeconds = NowUnixSeconds();
    if (!SaveItem(items_[*index], error)) return false;
    const WallpaperLibraryItem used = items_[*index];
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(*index));
    items_.insert(items_.begin(), used);
    return true;
}

std::optional<WallpaperLibraryItem> WallpaperLibrary::Find(std::wstring_view id) const {
    if (const auto index = FindIndex(id)) return items_[*index];

    // Compatibility lookup only: legacy built-in scene-* assignments are kept
    // byte-for-byte on disk, while the library/UI may expose only the canonical
    // content:<id> theme package. Resolve the alias after an exact miss so old
    // persisted assignments continue to target the canonical package without
    // rewriting monitor-assignments.ini or duplicating UI entries.
    const std::wstring_view canonical = CanonicalBuiltinWallpaperSource(id);
    if (!canonical.empty() && canonical != id) {
        if (const auto index = FindIndex(canonical)) return items_[*index];
    }
    return std::nullopt;
}

std::vector<WallpaperLibraryItem> WallpaperLibrary::Search(std::wstring_view query) const {
    const std::wstring needle = Lower(std::wstring(query));
    std::vector<WallpaperLibraryItem> result;
    result.reserve(items_.size());
    for (const auto& item : items_) {
        if (!IsLibraryUiVisible(item)) continue;
        if (needle.empty()) {
            result.push_back(item);
            continue;
        }
        const std::wstring haystack = Lower(item.title + L"\n" + item.source.wstring() + L"\n" + KindKey(item.kind));
        if (haystack.find(needle) != std::wstring::npos) result.push_back(item);
    }
    return result;
}

std::vector<WallpaperLibraryItem> WallpaperLibrary::RecentlyUsed(std::size_t limit) const {
    std::vector<WallpaperLibraryItem> result;
    for (const auto& item : items_) {
        // Same gate as Search(): a shipped legacy scene-* row that has been used must not
        // resurface in a UI-derived view. The other three derived views already filtered;
        // these two did not, and the gate that asserts it was only wired to pull_request,
        // so main carried the regression unguarded.
        if (!IsLibraryUiVisible(item)) continue;
        if (item.lastUsedUnixSeconds <= 0) continue;
        result.push_back(item);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.lastUsedUnixSeconds > b.lastUsedUnixSeconds; });
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::vector<WallpaperLibraryItem> WallpaperLibrary::Favorites() const {
    std::vector<WallpaperLibraryItem> result;
    for (const auto& item : items_) {
        // Same canonical gate. Kept as an explicit push rather than a std::copy_if whose
        // predicate returns the favourite flag alone: the derived-view gates reject that
        // form by its literal text, because it reads as "favourite-ness decides
        // visibility". Those gates match raw source, so this comment cannot name the
        // form either — spelling it out here would fail the very gate it describes.
        if (!IsLibraryUiVisible(item)) continue;
        if (item.favorite) result.push_back(item);
    }
    return result;
}

const fs::path& WallpaperLibrary::Root() const noexcept { return root_; }
fs::path WallpaperLibrary::ManifestPath() const { return root_ / L"library.ini"; }
fs::path WallpaperLibrary::MediaDirectory() const { return root_ / L"Media"; }
fs::path WallpaperLibrary::PackageDirectory() const { return root_ / L"Packages"; }

bool WallpaperLibrary::DiscoverPackages(std::wstring* error) {
    std::error_code ec;
    const auto packages = PackageDirectory();
    fs::create_directories(packages, ec);
    if (ec) { SetError(error, L"无法创建壁纸包目录"); return false; }
    for (const auto& entry : fs::directory_iterator(packages, ec)) {
        if (ec) { SetError(error, L"扫描壁纸包目录失败"); return false; }
        if (!entry.is_directory(ec) || Lower(entry.path().extension().wstring()) != L".mdwall") continue;
        content::LoadedMiaoContentPackage canonical;
        std::wstring canonicalError;
        if (content::MiaoContentPackage::Load(entry.path(), &canonical, &canonicalError) &&
            canonical.manifest.kind == content::ContentKind::Wallpaper) {
            LibraryWallpaperKind canonicalKind = LibraryWallpaperKind::Unknown;
            fs::path source;
            if (canonical.manifest.runtime == content::ContentRuntimeKind::Scene) {
                content::SceneRuntimeDefinition runtime;
                if (!content::MiaoSceneSerializer::DeserializePackage(canonical, &runtime, &canonicalError)) continue;
                canonicalKind = LibraryWallpaperKind::Scene;
                source = NormalizedAbsolute(entry.path());
            } else if (canonical.manifest.runtime == content::ContentRuntimeKind::Web) {
                if (!content::MiaoContentPackage::ResolvePackagePath(canonical.root, canonical.manifest.entry, &source, &canonicalError) ||
                    !fs::is_regular_file(source, ec)) { ec.clear(); continue; }
                ec.clear();
                canonicalKind = LibraryWallpaperKind::Web;
                source = NormalizedAbsolute(source);
            } else continue;
            const std::wstring stableId = content::MiaoContentPackageManager::MakeSource(canonical.manifest.id);
            if (stableId.empty()) continue;
            const auto stableIndex = FindIndex(stableId);
            const auto sourceIndex = stableIndex ? std::optional<std::size_t>{} : FindSourceIndex(source);
            WallpaperLibraryItem item;
            if (stableIndex) item = items_[*stableIndex];
            else if (sourceIndex) {
                item = items_[*sourceIndex];
                if (_wcsicmp(item.id.c_str(), stableId.c_str()) != 0) {
                    const std::wstring oldSection = SectionName(item.id);
                    WritePrivateProfileStringW(oldSection.c_str(), nullptr, nullptr, ManifestPath().c_str());
                }
            } else item.importedUnixSeconds = NowUnixSeconds();
            item.id = stableId;
            item.kind = canonicalKind;
            const std::wstring packageName = Utf8ToWide(canonical.manifest.name);
            item.title = SanitizeText(packageName.empty() ? DefaultTitle(entry.path()) : packageName);
            item.source = source;
            item.managedCopy = true;
            item.thumbnail.clear();
            if (!canonical.manifest.preview.empty()) {
                fs::path preview;
                std::wstring previewError;
                if (content::MiaoContentPackage::ResolvePackagePath(canonical.root, canonical.manifest.preview, &preview, &previewError) &&
                    fs::is_regular_file(preview, ec)) item.thumbnail = preview;
                ec.clear();
            }
            if (!SaveItem(item, error)) return false;
            if (stableIndex) items_[*stableIndex] = item;
            else if (sourceIndex) items_[*sourceIndex] = item;
            else items_.push_back(std::move(item));
            continue;
        }

        WallpaperPackageManifest manifest;
        std::wstring packageError;
        if (!WallpaperPackage::Validate(entry.path(), &manifest, &packageError)) continue;
        LibraryWallpaperKind kind = LibraryWallpaperKind::Unknown;
        switch (manifest.type) {
        case WallpaperPackageType::Image: kind = LibraryWallpaperKind::Image; break;
        case WallpaperPackageType::Video: kind = LibraryWallpaperKind::Video; break;
        case WallpaperPackageType::Web: kind = LibraryWallpaperKind::Web; break;
        case WallpaperPackageType::Scene:
        case WallpaperPackageType::Unknown: continue;
        }
        const fs::path source = NormalizedAbsolute(entry.path() / manifest.entry);
        if (FindSourceIndex(source)) continue;
        WallpaperLibraryItem item;
        item.id = L"package-" + Lower(entry.path().stem().wstring());
        if (item.id == L"package-") item.id = MakeId();
        if (FindIndex(item.id)) item.id += L"-" + std::to_wstring(items_.size());
        item.kind = kind;
        item.title = SanitizeText(manifest.title.empty() ? DefaultTitle(entry.path()) : manifest.title);
        item.source = source;
        item.managedCopy = true;
        item.importedUnixSeconds = NowUnixSeconds();
        if (!SaveItem(item, error)) return false;
        items_.push_back(std::move(item));
    }
    return true;
}

fs::path WallpaperLibrary::ThumbnailDirectory() const { return root_ / L"Thumbnails"; }

LibraryWallpaperKind WallpaperLibrary::InferKind(const fs::path& path) noexcept {
    const std::wstring extension = Lower(path.extension().wstring());
    if (extension == L".jpg" || extension == L".jpeg" || extension == L".png" || extension == L".bmp" ||
        extension == L".gif" || extension == L".webp" || extension == L".tif" || extension == L".tiff") return LibraryWallpaperKind::Image;
    if (extension == L".mp4" || extension == L".mov" || extension == L".wmv" || extension == L".m4v" ||
        extension == L".avi" || extension == L".mkv" || extension == L".webm") return LibraryWallpaperKind::Video;
    if (extension == L".html" || extension == L".htm") return LibraryWallpaperKind::Web;
    return LibraryWallpaperKind::Unknown;
}

const wchar_t* WallpaperLibrary::KindKey(LibraryWallpaperKind kind) noexcept {
    switch (kind) {
    case LibraryWallpaperKind::Image: return L"image";
    case LibraryWallpaperKind::Video: return L"video";
    case LibraryWallpaperKind::Web: return L"web";
    case LibraryWallpaperKind::Scene: return L"scene";
    case LibraryWallpaperKind::Unknown: break;
    }
    return L"unknown";
}

LibraryWallpaperKind WallpaperLibrary::ParseKind(std::wstring_view value) noexcept {
    if (_wcsicmp(std::wstring(value).c_str(), L"image") == 0) return LibraryWallpaperKind::Image;
    if (_wcsicmp(std::wstring(value).c_str(), L"video") == 0) return LibraryWallpaperKind::Video;
    if (_wcsicmp(std::wstring(value).c_str(), L"web") == 0) return LibraryWallpaperKind::Web;
    if (_wcsicmp(std::wstring(value).c_str(), L"scene") == 0) return LibraryWallpaperKind::Scene;
    return LibraryWallpaperKind::Unknown;
}

bool WallpaperLibrary::SaveItem(const WallpaperLibraryItem& item, std::wstring* error) {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (ec) { SetError(error, L"无法创建壁纸库目录"); return false; }
    const fs::path manifest = ManifestPath();
    if (!text::EnsureUtf16LeProfileFile(manifest, error)) return false;
    const std::wstring section = SectionName(item.id);
    bool ok = true;
    ok = WriteProfileText(manifest, section, L"Kind", KindKey(item.kind)) && ok;
    ok = WriteProfileText(manifest, section, L"Title", SanitizeText(item.title)) && ok;
    ok = WriteProfileText(manifest, section, L"Source", item.source.wstring()) && ok;
    ok = WriteProfileText(manifest, section, L"Thumbnail", item.thumbnail.wstring()) && ok;
    ok = WriteProfileText(manifest, section, L"Favorite", item.favorite ? L"1" : L"0") && ok;
    ok = WriteProfileText(manifest, section, L"ManagedCopy", item.managedCopy ? L"1" : L"0") && ok;
    ok = WriteProfileText(manifest, section, L"Imported", std::to_wstring(item.importedUnixSeconds)) && ok;
    ok = WriteProfileText(manifest, section, L"LastUsed", std::to_wstring(item.lastUsedUnixSeconds)) && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, manifest.c_str());
    if (!ok) SetError(error, L"写入壁纸库清单失败");
    return ok;
}

bool WallpaperLibrary::GenerateThumbnail(WallpaperLibraryItem& item) {
    if (item.source.empty() || item.kind == LibraryWallpaperKind::Scene || item.kind == LibraryWallpaperKind::Web) return false;
    const fs::path destination = ThumbnailDirectory() / (item.id + L".png");
    if (!GenerateShellThumbnail(item.source, destination)) return false;
    item.thumbnail = destination;
    return true;
}

std::optional<std::size_t> WallpaperLibrary::FindIndex(std::wstring_view id) const {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (_wcsicmp(items_[i].id.c_str(), std::wstring(id).c_str()) == 0) return i;
    }
    return std::nullopt;
}

std::optional<std::size_t> WallpaperLibrary::FindSourceIndex(const fs::path& source) const {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (!items_[i].source.empty() && SamePath(items_[i].source, source)) return i;
    }
    return std::nullopt;
}

bool WallpaperLibrary::SelfTest() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-WallpaperLibrary-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root, ec);
    if (ec) return false;
    const fs::path sample = root / L"中文壁纸.html";
    { std::ofstream output(sample, std::ios::binary); output << "<html><body>MiaoDesk wallpaper library self-test</body></html>"; }
    WallpaperLibrary library(root / L"Library");
    std::wstring error;
    bool ok = library.Load(&error);
    WallpaperImportOptions unicodeOptions;
    unicodeOptions.title = L"中文壁纸标题";
    auto imported = library.ImportFile(sample, unicodeOptions, &error);
    ok = ok && imported.has_value();
    if (imported) {
        ok = ok && library.SetFavorite(imported->id, true, &error);
        ok = ok && library.MarkUsed(imported->id, &error);
        ok = ok && !library.Search(L"中文壁纸标题").empty();
        ok = ok && library.Favorites().size() == 1;
        ok = ok && !library.RecentlyUsed(1).empty();
    }
    ok = ok && library.UpsertScene(L"scene-aurora", L"妙喵云境", &error);
    constexpr wchar_t kNeonCanonical[] = L"content:com.goodloong.miaodesk.theme.neon-city";
    ok = ok && library.UpsertScene(kNeonCanonical, L"霓虹之城", &error);
    const auto legacyNeonAlias = library.Find(L"scene-neon");
    ok = ok && legacyNeonAlias.has_value();
    if (legacyNeonAlias) {
        ok = ok && legacyNeonAlias->id == kNeonCanonical;
        ok = ok && legacyNeonAlias->title == L"霓虹之城";
    }
    ok = ok && library.Search(L"妙喵云境").empty();
    const auto visibleInitial = library.Search(L"");
    ok = ok && std::none_of(visibleInitial.begin(), visibleInitial.end(), [](const auto& item) {
        return FindLegacyBuiltinWallpaper(item.id) != nullptr;
    });

    const fs::path legacyPackage = library.PackageDirectory() / L"selftest.mdwall";
    ok = ok && WallpaperPackage::CreateWeb(legacyPackage, L"Package Web", "<html><body>package</body></html>", L"self-test", L"MiaoDesk", &error);
    const fs::path canonicalPackage = library.PackageDirectory() / L"canonical-selftest.mdwall";
    fs::create_directories(canonicalPackage, ec); ok = ok && !ec;
    ok = ok && WriteUtf8File(canonicalPackage / L"manifest.json", R"json({"schema":1,"id":"com.goodloong.selftest.scene","name":"Canonical Scene","author":"MiaoDesk","version":"1.0.0","kind":"wallpaper","runtime":"scene","entry":"scene.json","parameters":"parameters.json","capabilities":[]})json");
    ok = ok && WriteUtf8File(canonicalPackage / L"scene.json", R"json({"schema":1,"id":"scene://library-canonical-selftest","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root","nodes":[{"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[{"id":"component://root/transform","kind":"transform","properties":[{"name":"opacity","type":"float","default":1.0}]}]}],"assets":[],"shaders":[],"materials":[],"inputs":[],"bindings":[],"animations":[],"postProcesses":[]})json");
    ok = ok && WriteUtf8File(canonicalPackage / L"parameters.json", R"json({"schema":1,"parameters":[]})json");
    const fs::path canonicalWebPackage = library.PackageDirectory() / L"canonical-web-selftest.mdwall";
    fs::create_directories(canonicalWebPackage, ec); ok = ok && !ec;
    ok = ok && WriteUtf8File(canonicalWebPackage / L"manifest.json", R"json({"schema":1,"id":"com.goodloong.selftest.web","name":"Canonical Web","author":"MiaoDesk","version":"1.0.0","kind":"wallpaper","runtime":"web","entry":"index.html","capabilities":[]})json");
    ok = ok && WriteUtf8File(canonicalWebPackage / L"index.html", "<html><body>MiaoDesk canonical web wallpaper</body></html>");
    const fs::path neonCanonicalPackage = library.PackageDirectory() / L"neon-collapse-selftest.mdwall";
    fs::create_directories(neonCanonicalPackage, ec); ok = ok && !ec;
    ok = ok && WriteUtf8File(neonCanonicalPackage / L"manifest.json", R"json({"schema":1,"id":"com.goodloong.miaodesk.theme.neon-city","name":"霓虹之城主题包","author":"妙桌面","version":"1.0.0","kind":"wallpaper","runtime":"scene","entry":"scene.json","parameters":"parameters.json","capabilities":[]})json");
    ok = ok && WriteUtf8File(neonCanonicalPackage / L"scene.json", R"json({"schema":1,"id":"scene://library-neon-collapse-selftest","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root","nodes":[{"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[{"id":"component://root/transform","kind":"transform","properties":[{"name":"opacity","type":"float","default":1.0}]}]}],"assets":[],"shaders":[],"materials":[],"inputs":[],"bindings":[],"animations":[],"postProcesses":[]})json");
    ok = ok && WriteUtf8File(neonCanonicalPackage / L"parameters.json", R"json({"schema":1,"parameters":[]})json");

    const fs::path libraryManifest = library.ManifestPath();
    const std::wstring stableSection = SectionName(L"content:com.goodloong.selftest.scene");
    ok = ok && WriteProfileText(libraryManifest, stableSection, L"Kind", L"scene");
    ok = ok && WriteProfileText(libraryManifest, stableSection, L"Title", L"Stable Scene");
    ok = ok && WriteProfileText(libraryManifest, stableSection, L"Source", canonicalPackage.wstring());
    ok = ok && WriteProfileText(libraryManifest, stableSection, L"Favorite", L"0");
    ok = ok && WriteProfileText(libraryManifest, stableSection, L"ManagedCopy", L"1");
    ok = ok && WriteProfileText(libraryManifest, stableSection, L"Imported", L"220");
    ok = ok && WriteProfileText(libraryManifest, stableSection, L"LastUsed", L"120");
    const std::wstring legacySection = SectionName(L"legacy-scene");
    ok = ok && WriteProfileText(libraryManifest, legacySection, L"Kind", L"scene");
    ok = ok && WriteProfileText(libraryManifest, legacySection, L"Title", L"Legacy Scene");
    ok = ok && WriteProfileText(libraryManifest, legacySection, L"Source", (canonicalPackage / L"scene.json").wstring());
    ok = ok && WriteProfileText(libraryManifest, legacySection, L"Favorite", L"1");
    ok = ok && WriteProfileText(libraryManifest, legacySection, L"ManagedCopy", L"1");
    ok = ok && WriteProfileText(libraryManifest, legacySection, L"Imported", L"110");
    ok = ok && WriteProfileText(libraryManifest, legacySection, L"LastUsed", L"330");
    const std::wstring legacyNeonSection = SectionName(L"scene-neon");
    ok = ok && WriteProfileText(libraryManifest, legacyNeonSection, L"Kind", L"scene");
    ok = ok && WriteProfileText(libraryManifest, legacyNeonSection, L"Title", L"旧版中文霓虹标题");
    ok = ok && WriteProfileText(libraryManifest, legacyNeonSection, L"Source", L"");
    ok = ok && WriteProfileText(libraryManifest, legacyNeonSection, L"Favorite", L"1");
    ok = ok && WriteProfileText(libraryManifest, legacyNeonSection, L"ManagedCopy", L"0");
    ok = ok && WriteProfileText(libraryManifest, legacyNeonSection, L"Imported", L"105");
    ok = ok && WriteProfileText(libraryManifest, legacyNeonSection, L"LastUsed", L"350");
    const std::wstring unknownEmptySection = SectionName(L"scene-user-custom");
    ok = ok && WriteProfileText(libraryManifest, unknownEmptySection, L"Kind", L"scene");
    ok = ok && WriteProfileText(libraryManifest, unknownEmptySection, L"Title", L"用户中文自定义场景");
    ok = ok && WriteProfileText(libraryManifest, unknownEmptySection, L"Source", L"");
    ok = ok && WriteProfileText(libraryManifest, unknownEmptySection, L"Favorite", L"0");
    const fs::path externalScene = root / L"external.mdwall" / L"scene.json";
    const std::wstring externalSection = SectionName(L"external-scene");
    ok = ok && WriteProfileText(libraryManifest, externalSection, L"Kind", L"scene");
    ok = ok && WriteProfileText(libraryManifest, externalSection, L"Title", L"External Legacy Scene");
    ok = ok && WriteProfileText(libraryManifest, externalSection, L"Source", externalScene.wstring());
    ok = ok && WriteProfileText(libraryManifest, externalSection, L"Favorite", L"0");
    ok = ok && WriteProfileText(libraryManifest, externalSection, L"ManagedCopy", L"0");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, libraryManifest.c_str());

    WallpaperLibrary reloaded(root / L"Library");
    ok = ok && reloaded.Load(&error);
    const auto unicodeScene = reloaded.Find(L"scene-aurora");
    ok = ok && unicodeScene.has_value(); if (unicodeScene) ok = ok && unicodeScene->title == L"妙喵云境";
    ok = ok && !reloaded.Find(L"legacy-scene").has_value();
    ok = ok && reloaded.Find(L"external-scene").has_value();
    ok = ok && reloaded.Find(L"scene-user-custom").has_value();
    ok = ok && reloaded.Search(L"妙喵云境").empty();
    const auto visibleReloaded = reloaded.Search(L"");
    ok = ok && std::none_of(visibleReloaded.begin(), visibleReloaded.end(), [](const auto& item) {
        return FindLegacyBuiltinWallpaper(item.id) != nullptr;
    });
    ok = ok && std::any_of(visibleReloaded.begin(), visibleReloaded.end(), [](const auto& item) {
        return _wcsicmp(item.id.c_str(), L"scene-user-custom") == 0;
    });
    if (imported) {
        const auto unicodeImported = reloaded.Find(imported->id);
        ok = ok && unicodeImported.has_value();
        if (unicodeImported) { ok = ok && unicodeImported->title == L"中文壁纸标题"; ok = ok && SamePath(unicodeImported->source, sample); }
    }
    const auto packageItems = reloaded.Search(L"Package Web");
    ok = ok && !packageItems.empty();
    if (!packageItems.empty()) { ok = ok && reloaded.Remove(packageItems.front().id, true, &error); ok = ok && !fs::exists(legacyPackage); }
    const std::wstring canonicalId = L"content:com.goodloong.selftest.scene";
    auto canonicalItem = reloaded.Find(canonicalId);
    ok = ok && canonicalItem.has_value();
    if (canonicalItem) {
        ok = ok && canonicalItem->kind == LibraryWallpaperKind::Scene;
        ok = ok && SamePath(canonicalItem->source, canonicalPackage);
        ok = ok && canonicalItem->favorite;
        ok = ok && canonicalItem->importedUnixSeconds == 110;
        ok = ok && canonicalItem->lastUsedUnixSeconds == 330;
        ok = ok && reloaded.Search(L"Canonical Scene").size() == 1;
    }
    const auto collapsedNeon = reloaded.Find(L"scene-neon");
    ok = ok && collapsedNeon.has_value();
    if (collapsedNeon) {
        ok = ok && collapsedNeon->id == kNeonCanonical;
        ok = ok && collapsedNeon->title == L"霓虹之城主题包";
        ok = ok && SamePath(collapsedNeon->source, neonCanonicalPackage);
        ok = ok && collapsedNeon->favorite;
        ok = ok && collapsedNeon->importedUnixSeconds == 105;
        ok = ok && collapsedNeon->lastUsedUnixSeconds == 350;
    }
    ok = ok && std::none_of(reloaded.Items().begin(), reloaded.Items().end(), [](const auto& item) {
        return _wcsicmp(item.id.c_str(), L"scene-neon") == 0;
    });
    ok = ok && std::any_of(reloaded.Items().begin(), reloaded.Items().end(), [](const auto& item) {
        return _wcsicmp(item.id.c_str(), L"scene-aurora") == 0;
    });
    ok = ok && std::any_of(reloaded.Items().begin(), reloaded.Items().end(), [](const auto& item) {
        return _wcsicmp(item.id.c_str(), L"scene-user-custom") == 0;
    });
    const std::wstring canonicalWebId = L"content:com.goodloong.selftest.web";
    const auto canonicalWebItem = reloaded.Find(canonicalWebId);
    ok = ok && canonicalWebItem.has_value();
    if (canonicalWebItem) {
        ok = ok && canonicalWebItem->kind == LibraryWallpaperKind::Web;
        ok = ok && SamePath(canonicalWebItem->source, canonicalWebPackage / L"index.html");
        ok = ok && canonicalWebItem->managedCopy;
        ok = ok && reloaded.Search(L"Canonical Web").size() == 1;
    }
    WallpaperLibrary idempotent(root / L"Library");
    ok = ok && idempotent.Load(&error);
    const auto idempotentNeon = idempotent.Find(L"scene-neon");
    ok = ok && idempotentNeon.has_value();
    if (idempotentNeon) {
        ok = ok && idempotentNeon->id == kNeonCanonical;
        ok = ok && idempotentNeon->favorite;
        ok = ok && idempotentNeon->importedUnixSeconds == 105;
        ok = ok && idempotentNeon->lastUsedUnixSeconds == 350;
    }
    ok = ok && std::none_of(idempotent.Items().begin(), idempotent.Items().end(), [](const auto& item) {
        return _wcsicmp(item.id.c_str(), L"scene-neon") == 0;
    });
    const fs::path movedCanonicalPackage = library.PackageDirectory() / L"renamed-canonical.mdwall";
    if (ok) { fs::rename(canonicalPackage, movedCanonicalPackage, ec); ok = !ec; }
    WallpaperLibrary afterMove(root / L"Library");
    ok = ok && afterMove.Load(&error);
    canonicalItem = afterMove.Find(canonicalId);
    ok = ok && canonicalItem.has_value();
    if (canonicalItem) {
        ok = ok && SamePath(canonicalItem->source, movedCanonicalPackage);
        ok = ok && afterMove.Search(L"Canonical Scene").size() == 1;
        ok = ok && afterMove.Remove(canonicalItem->id, true, &error);
        ok = ok && !fs::exists(movedCanonicalPackage);
    }
    const auto webAfterMove = afterMove.Find(canonicalWebId);
    ok = ok && webAfterMove.has_value();
    if (webAfterMove) { ok = ok && afterMove.Remove(webAfterMove->id, true, &error); ok = ok && !fs::exists(canonicalWebPackage); }
    const auto neonAfterMove = afterMove.Find(kNeonCanonical);
    ok = ok && neonAfterMove.has_value();
    if (neonAfterMove) { ok = ok && afterMove.Remove(neonAfterMove->id, true, &error); ok = ok && !fs::exists(neonCanonicalPackage); }
    WallpaperLibrary afterRemoval(root / L"Library");
    ok = ok && afterRemoval.Load(&error);
    ok = ok && afterRemoval.Search(L"Package Web").empty();
    ok = ok && afterRemoval.Search(L"Canonical Scene").empty();
    ok = ok && afterRemoval.Search(L"Canonical Web").empty();
    ok = ok && afterRemoval.Find(L"external-scene").has_value();
    ok = ok && afterRemoval.Find(L"scene-user-custom").has_value();
    if (imported) ok = ok && afterRemoval.Find(imported->id).has_value();
    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::wallpaper
