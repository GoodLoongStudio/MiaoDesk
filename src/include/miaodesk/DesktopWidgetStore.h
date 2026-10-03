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

// 这个组件在"同一 preset 同一显示器只允许一个实例"这条规则下的键。
// 空串表示不参与去重 —— Content 组件允许同一定义多开,所以只有 Native 预设有这个约束。
//
// 定义在 WidgetIdentityRules.cpp(纯字符串逻辑,不 import Windows 头),于是 P0-04
// "无重复实例"这条验收在本机就能验。原先它是 DesktopWidgetStore.cpp 匿名命名空间里的
// 文件局部符号,而那个文件为了 UTF-16 配置持久化 include 了 windows.h。
std::wstring NativeSingletonKey(const DesktopWidget& widget);

// 两份组件几何/身份是否指向同一个可去重实例。用于在 items_ 里找冲突。
bool SameNativeSingleton(const DesktopWidget& left, const DesktopWidget& right) noexcept;

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
