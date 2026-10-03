#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::wallpaper {

enum class LibraryWallpaperKind {
    Image,
    Video,
    Web,
    Scene,
    Unknown,
};

struct WallpaperLibraryItem {
    std::wstring id;
    LibraryWallpaperKind kind{LibraryWallpaperKind::Unknown};
    std::wstring title;
    std::filesystem::path source;
    std::filesystem::path thumbnail;
    bool favorite{};
    bool managedCopy{};
    // INI 里读出来的 Kind 原文(未归一化)。留着是为了"为什么跳过这一行"
    // 能分清"字段缺失"与"写了本版本不认识的词"—— 一个是损坏,一个是版本差异。
    std::wstring kindText;
    unsigned long long importedUnixSeconds{};
    unsigned long long lastUsedUnixSeconds{};
};

struct WallpaperImportOptions {
    bool managedCopy{};
    std::wstring title;
};

class WallpaperLibrary {
public:
    WallpaperLibrary();
    explicit WallpaperLibrary(std::filesystem::path root);

    bool Load(std::wstring* error = nullptr);
    const std::vector<WallpaperLibraryItem>& Items() const noexcept;

    // 上一轮 Load 中被跳过的行,以及为什么。
    //
    // 为什么要有这个:Load 原先在"ID 空"或"Kind 本版本不认识"时**静默丢行**,
    // 而 Load 照常返回 true —— 于是调用方只问成败,拿着一个悄悄变短的库继续,
    // 用户导入的壁纸就这么不见了,而没有任何地方说为什么。P0-07 的验收是
    // "库状态一致恢复",这里的实际行为是"恢复成一个更短的库并报告成功"。
    //
    // 它**不改变**哪些行进库:那一行仍然不进(要不要进是要动 UI 的决定),
    // 只是让丢失第一次说得出口。理由由 MiaoLibraryRowFilter(纯逻辑,本机有门)给。
    const std::vector<std::wstring>& SkippedRows() const noexcept { return skippedRows_; }
    std::size_t SkippedRowCount() const noexcept { return skippedRows_.size(); }

    std::optional<WallpaperLibraryItem> ImportFile(
        const std::filesystem::path& source,
        const WallpaperImportOptions& options = {},
        std::wstring* error = nullptr);
    std::optional<WallpaperLibraryItem> ImportWebUrl(
        std::wstring url,
        std::wstring title = {},
        std::wstring* error = nullptr);

    bool UpsertScene(std::wstring id, std::wstring title, std::wstring* error = nullptr);
    bool Remove(std::wstring_view id, bool deleteManagedCopy, std::wstring* error = nullptr);
    bool SetFavorite(std::wstring_view id, bool favorite, std::wstring* error = nullptr);
    bool MarkUsed(std::wstring_view id, std::wstring* error = nullptr);

    std::optional<WallpaperLibraryItem> Find(std::wstring_view id) const;
    std::vector<WallpaperLibraryItem> Search(std::wstring_view query) const;
    std::vector<WallpaperLibraryItem> RecentlyUsed(std::size_t limit = 12) const;
    std::vector<WallpaperLibraryItem> Favorites() const;

    const std::filesystem::path& Root() const noexcept;
    std::filesystem::path ManifestPath() const;
    std::filesystem::path MediaDirectory() const;
    std::filesystem::path PackageDirectory() const;
    std::filesystem::path ThumbnailDirectory() const;

    static LibraryWallpaperKind InferKind(const std::filesystem::path& path) noexcept;
    static const wchar_t* KindKey(LibraryWallpaperKind kind) noexcept;
    static LibraryWallpaperKind ParseKind(std::wstring_view value) noexcept;
    static bool IsTrustedWebUrl(std::wstring_view value) noexcept;
    static bool SelfTest();

private:
    bool DiscoverPackages(std::wstring* error);
    bool SaveItem(const WallpaperLibraryItem& item, std::wstring* error);
    bool GenerateThumbnail(WallpaperLibraryItem& item);
    std::optional<std::size_t> FindIndex(std::wstring_view id) const;
    std::optional<std::size_t> FindSourceIndex(const std::filesystem::path& source) const;

    std::filesystem::path root_;
    std::vector<WallpaperLibraryItem> items_;
    // 上一轮 Load 跳过的行及原因。空表示没有跳过。
    std::vector<std::wstring> skippedRows_;
};

} // namespace miaodesk::wallpaper
