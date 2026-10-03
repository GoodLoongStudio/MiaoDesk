#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/NativeWidgetPreset.h"

#include <algorithm>
#include <cwctype>

namespace miaodesk::wallpaper {
namespace {}

std::wstring NativeSingletonKey(const DesktopWidget& widget) {
    // 只有 Native 预设才有"同一台显示器只一个"的约束。Content 组件允许同一定义
    // 多开(用户可以在三块屏幕上各放一个天气组件),所以它们不参与去重。
    if (widget.kind != DesktopWidgetKind::Native ||
        !IsNativePresetSource(widget.source.wstring()))
        return {};

    // monitorId 为空表示主显示器。它是键的一部分:同一个 preset 在不同的屏幕上
    // 各放一个是允许的,那正是多显示器用户期望的行为。
    std::wstring key = widget.source.wstring();
    key += L"|";
    key += widget.monitorId.empty() ? L"<primary>" : widget.monitorId;
    // 大小写不敏感:monitorId 来自不同的采集路径(SetupAPI、GDI、注册表),
    // 同一个物理显示器可能被拼成 "\\\\.\\DISPLAY1" 与 "\\\\.\\display1"。
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return key;
}

bool SameNativeSingleton(const DesktopWidget& left, const DesktopWidget& right) noexcept {
    const std::wstring leftKey = NativeSingletonKey(left);
    if (leftKey.empty()) return false;
    return leftKey == NativeSingletonKey(right);
}

} // namespace miaodesk::wallpaper
