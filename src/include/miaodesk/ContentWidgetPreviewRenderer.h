#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "miaodesk/DesktopWidgetController.h"
#include "miaodesk/DesktopWidgetStore.h"

namespace miaodesk::wallpaper {

// Management-page preview for Scene Content widgets. It deliberately resolves
// the same package, effective instance parameters and host-provided data as the
// desktop Content host so the library card is a real preview rather than a
// second hand-authored representation.
class ContentWidgetPreviewRenderer {
public:
    ContentWidgetPreviewRenderer();
    ~ContentWidgetPreviewRenderer();

    ContentWidgetPreviewRenderer(const ContentWidgetPreviewRenderer&) = delete;
    ContentWidgetPreviewRenderer& operator=(const ContentWidgetPreviewRenderer&) = delete;

    bool Draw(
        HDC dc,
        const RECT& bounds,
        const DesktopWidget& widget,
        desktop::DesktopWidgetController& controller,
        std::wstring* error = nullptr);

    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::wallpaper
