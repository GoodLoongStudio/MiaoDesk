#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <optional>

#include "miaodesk/WallpaperAutomation.h"
#include "miaodesk/WallpaperLibrary.h"

namespace miaodesk::wallpaper {

class WallpaperAutomationWindow {
public:
    using CaptureProfileCallback = std::function<std::optional<WallpaperProfile>(const std::wstring& name)>;
    using DecisionCallback = std::function<void(const AutomationDecision&)>;

    WallpaperAutomationWindow();
    ~WallpaperAutomationWindow();

    WallpaperAutomationWindow(const WallpaperAutomationWindow&) = delete;
    WallpaperAutomationWindow& operator=(const WallpaperAutomationWindow&) = delete;

    bool Show(HINSTANCE instance,
              WallpaperAutomationStore* automation,
              WallpaperLibrary* library,
              CaptureProfileCallback captureProfile,
              DecisionCallback applyDecision,
              // 关闭后键盘该交还给哪个界面(见 WallpaperApplicationRulesWindow::Show)。
              HWND restoreFocus = nullptr);
    void Close();
    void Refresh();
    bool Visible() const noexcept;
    HWND Window() const noexcept;
    // 本窗口内嵌的"应用程序规则"窗口:它是独立的顶层窗口,关窗即隐藏。
    HWND RulesWindow() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::wallpaper
