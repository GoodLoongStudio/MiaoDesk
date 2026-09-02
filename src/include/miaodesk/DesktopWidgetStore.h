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
    Unknown,
};

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
