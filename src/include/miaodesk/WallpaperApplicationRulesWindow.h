#pragma once

#include <windows.h>
#include <memory>

namespace miaodesk::wallpaper {

class WallpaperApplicationRulesWindow {
public:
    WallpaperApplicationRulesWindow();
    ~WallpaperApplicationRulesWindow();

    WallpaperApplicationRulesWindow(const WallpaperApplicationRulesWindow&) = delete;
    WallpaperApplicationRulesWindow& operator=(const WallpaperApplicationRulesWindow&) = delete;

    // restoreFocus 是"关闭后键盘该回哪里"的界面(通常是打开本窗口的那个窗口)。
    // 本窗口是关窗即隐藏的,而隐藏持有焦点的窗口会让焦点落到 Z 序里的下一个窗口,
    // 经常是桌面 —— 用户的下一串按键就进了别的应用程序。传 nullptr 则不交还。
    bool Show(HINSTANCE instance, HWND restoreFocus = nullptr);
    void Close();
    void Refresh();
    bool Visible() const noexcept;
    HWND Window() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::wallpaper
