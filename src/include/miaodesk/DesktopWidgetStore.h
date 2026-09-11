#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/NativeWidgetPreset.h"

namespace miaodesk::wallpaper {

enum class DesktopWidgetKind {
    Native,
    Content,
    Unknown,
};

// NativeWidgetHost is currently compiled with the staged Content dogfood flag.
// In that one translation unit only, treat Content as runtime-equivalent to
// Native for the existing host gate. Persistence and every other caller still
// retain the real Content kind. NativeWidgetPreset.h then resolves the staged
// content:<definitionId> source to the matching host route.
#if defined(MIAODESK_WIDGET_HOST_CONTENT_DOGFOOD)
constexpr bool operator==(DesktopWidgetKind lhs, DesktopWidgetKind rhs) noexcept {
    const int left = static_cast<int>(lhs);
    const int right = static_cast<int>(rhs);
    const int native = static_cast<int>(DesktopWidgetKind::Native);
    const int content = static_cast<int>(DesktopWidgetKind::Content);
    if ((left == native && right == content) || (left == content && right == native)) return true;
    return left == right;
}

constexpr bool operator!=(DesktopWidgetKind lhs, DesktopWidgetKind rhs) noexcept {
    return !(lhs == rhs);
}
#endif

struct DesktopWidget {
    std::wstring id;
    DesktopWidgetKind kind{DesktopWidgetKind::Native};
    std::wstring title;
    std::filesystem::path source;
    std::wstring monitorId;
    float x{0.68f};
    float y{0.05f};
    float width{0.28f};
    float height{0.18f};
    bool enabled{true};
};

class DesktopWidgetStore {
public:
    DesktopWidgetStore();
    explicit DesktopWidgetStore(std::filesystem::path root);

    bool Load(std::wstring* error = nullptr);
    bool Save(std::wstring* error = nullptr) const;

    const std::vector<DesktopWidget>& Items() const noexcept;
    std::optional<DesktopWidget> Find(std::wstring_view id) const;

    std::optional<DesktopWidget> Upsert(DesktopWidget widget, std::wstring* error = nullptr);
    std::optional<DesktopWidget> CreateManagedNative(
        NativeWidgetPreset preset,
        std::wstring title = {},
        std::wstring monitorId = {},
        float x = 0.68f,
        float y = 0.05f,
        float width = 0.28f,
        float height = 0.18f,
        std::wstring* error = nullptr);
    bool Remove(std::wstring_view id, std::wstring* error = nullptr);

    const std::filesystem::path& Root() const noexcept;
    std::filesystem::path ManifestPath() const;
    std::filesystem::path PackageDirectory() const;

    static DesktopWidget Normalize(DesktopWidget widget);
    static std::wstring MakeId();
    static const wchar_t* KindKey(DesktopWidgetKind kind) noexcept;
    static DesktopWidgetKind ParseKind(std::wstring_view value) noexcept;
    static bool SelfTest();

private:
    std::optional<std::size_t> FindIndex(std::wstring_view id) const noexcept;

    std::filesystem::path root_;
    std::vector<DesktopWidget> items_;
};

} // namespace miaodesk::wallpaper