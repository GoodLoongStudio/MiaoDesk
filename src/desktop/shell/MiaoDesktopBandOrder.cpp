#include "miaodesk/MiaoDesktopBandOrder.h"

namespace miaodesk {
namespace {

// 收集阶段:和 RepairRoleOrder 一样,只收"已知表面",且不可见的壁纸层直接跳过。
std::vector<const DesktopBandSurface*> Collect(const std::vector<DesktopBandSurface>& surfaces) {
    std::vector<const DesktopBandSurface*> known;
    for (const auto& surface : surfaces) {
        // 组件层可见与否都参与(隐藏的组件仍然占着 z-order 上它该在的位置);
        // 壁纸层只在可见时参与。这条是规则,见头文件。
        if (surface.role != DesktopBandRole::Widget && !surface.isVisible) continue;
        known.push_back(&surface);
    }
    return known;
}

} // namespace

bool DesktopBandOrderSatisfied(const std::vector<DesktopBandSurface>& surfaces,
                               DesktopBandMode mode) noexcept {
    const bool raised = mode == DesktopBandMode::Raised;
    const std::vector<const DesktopBandSurface*> known = Collect(surfaces);
    if (known.empty()) return true;  // 没有已知表面:没什么可违反的,修它反而空转

    bool seenIconLayer = false;
    bool seenWallpaper = false;
    for (const auto* surface : known) {
        // 1. 不是子窗口就根本不在桌面上。
        if (!surface->isChild) return false;

        if (surface->role == DesktopBandRole::Widget) {
            // 2. layered 必须与模式相反。
            if (surface->isLayered == raised) return false;
            // 3. 组件必须排在图标层/壁纸层之前。
            if (raised ? (seenIconLayer || seenWallpaper) : seenWallpaper) return false;
            continue;
        }

        if (raised && surface->role == DesktopBandRole::IconLayer) {
            // 4. 图标层必须排在壁纸层之前。
            if (seenWallpaper) return false;
            seenIconLayer = true;
            continue;
        }

        // 5. 壁纸层必须同时 layered 且 transparent。
        if (!surface->isLayered || !surface->isTransparent) return false;
        seenWallpaper = true;
    }
    return true;
}

} // namespace miaodesk
