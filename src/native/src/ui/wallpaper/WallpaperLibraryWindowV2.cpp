#include "turingdesk/WallpaperLibraryWindow.h"
#include "turingdesk/DesktopAiSettingsPage.h"
#include "turingdesk/DesktopControlService.h"
#include "turingdesk/DesktopWidgetController.h"
#include "turingdesk/RuntimeLogger.h"
#include "turingdesk/LiveLogWindow.h"
#include "turingdesk/NativeWidgetPainter.h"
#include "turingdesk/NativeWidgetPreset.h"
#include "turingdesk/NativeWeatherService.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kWindowClass[] = L"TuringDesk.Native.DesktopLibrary";
constexpr wchar_t kGridClass[] = L"TuringDesk.Native.DesktopLibraryGrid";

constexpr int kSearchId = 6101;
constexpr int kAddId = 6102;
constexpr int kNavInstalledId = 6110;
constexpr int kNavWidgetsId = 6111;
constexpr int kNavAiId = 6116;
constexpr int kWallpaperGridId = 6120;
constexpr int kWidgetGridId = 6121;
constexpr int kTargetComboId = 6130;
constexpr int kApplyId = 6131;
constexpr int kFavoriteId = 6132;
constexpr int kRemoveId = 6133;
constexpr int kWidgetCreateId = 6140;
constexpr int kWidgetToggleId = 6141;
constexpr int kWidgetRemoveId = 6142;
constexpr int kWidgetRefreshId = 6143;
constexpr int kWebUrlId = 6150;
constexpr int kWebConfirmId = 6151;
constexpr int kWebCancelId = 6152;
constexpr int kWallpaperToggleId = 6160;
constexpr int kOpenLogsId = 6170;

constexpr UINT kMenuImportFile = 6201;
constexpr UINT kMenuImportWeb = 6202;
constexpr UINT kMenuApply = 6210;
constexpr UINT kMenuFavorite = 6211;
constexpr UINT kMenuRemove = 6212;
constexpr UINT kMenuWidgetGlassClock = 6220;
constexpr UINT kMenuWidgetTodayTasks = 6221;
constexpr UINT kMenuWidgetWeatherGlass = 6222;
constexpr UINT kMenuWidgetAuto = 6223;
constexpr UINT kMenuWidgetToggle = 6230;
constexpr UINT kMenuWidgetRemove = 6231;

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

int RectWidth(const RECT& rect) {
    return std::max(1, static_cast<int>(rect.right - rect.left));
}

int RectHeight(const RECT& rect) {
    return std::max(1, static_cast<int>(rect.bottom - rect.top));
}

std::wstring Trim(std::wstring value) {
    const auto notSpace = [](wchar_t ch) { return !iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring WindowText(HWND hwnd) {
    if (!hwnd) return {};
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

const wchar_t* KindLabel(LibraryWallpaperKind kind) {
    switch (kind) {
    case LibraryWallpaperKind::Image: return L"图片";
    case LibraryWallpaperKind::Video: return L"视频";
    case LibraryWallpaperKind::Web: return L"Web";
    case LibraryWallpaperKind::Scene: return L"Scene";
    case LibraryWallpaperKind::Unknown: break;
    }
    return L"桌面";
}

std::wstring DescriptionFor(const WallpaperLibraryItem& item) {
    if (item.kind == LibraryWallpaperKind::Scene) {
        if (_wcsicmp(item.id.c_str(), L"scene-aurora") == 0) return L"流动极光 · 夜空星幕";
        if (_wcsicmp(item.id.c_str(), L"scene-neon") == 0) return L"霓虹幻境 · 赛博公路";
        if (_wcsicmp(item.id.c_str(), L"scene-grid") == 0) return L"深海浪潮 · 澄蓝海底";
        return L"原生 Scene";
    }
    if (item.kind == LibraryWallpaperKind::Video) return L"视频壁纸";
    if (item.kind == LibraryWallpaperKind::Web) return L"Web 壁纸";
    if (item.kind == LibraryWallpaperKind::Image) return L"图片壁纸";
    return L"桌面资源";
}

bool SourceMissing(const WallpaperLibraryItem& item) {
    if (item.kind == LibraryWallpaperKind::Scene) return false;
    if (item.kind == LibraryWallpaperKind::Web) return !WallpaperLibrary::IsTrustedWebUrl(item.source.wstring());
    if (item.source.empty()) return true;
    std::error_code ec;
    return !fs::exists(item.source, ec) || !fs::is_regular_file(item.source, ec);
}

WallpaperSettingsSection SectionForNav(int id) {
    if (id == kNavWidgetsId) return WallpaperSettingsSection::Widgets;
    if (id == kNavAiId) return WallpaperSettingsSection::AI;
    return WallpaperSettingsSection::Installed;
}

void FillSolid(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void FrameSolid(HDC dc, RECT rect, COLORREF color, int thickness = 1) {
    HBRUSH brush = CreateSolidBrush(color);
    for (int i = 0; i < thickness; ++i) {
        FrameRect(dc, &rect, brush);
        InflateRect(&rect, -1, -1);
    }
    DeleteObject(brush);
}

} // namespace

struct WallpaperLibraryWindow::Impl {
    enum class Page { Installed, Widgets, AI };

    HINSTANCE instance{};
    HWND window{};
    HWND title{};
    HWND sectionTitle{};
    HWND search{};
    HWND addButton{};
    std::array<HWND, 3> nav{};
    HWND wallpaperGrid{};
    HWND widgetGrid{};
    HWND status{};
    HWND targetCombo{};
    HWND applyButton{};
    HWND favoriteButton{};
    HWND removeButton{};
    HWND widgetCreateButton{};
    HWND widgetToggleButton{};
    HWND widgetRemoveButton{};
    HWND widgetRefreshButton{};
    HWND webUrl{};
    HWND webConfirm{};
    HWND webCancel{};
    HWND wallpaperToggleButton{};
    HWND logsButton{};

    WallpaperLibrary* library{};
    ApplyCallback applyCallback;
    NavigateCallback navigateCallback;
    std::vector<WallpaperLibraryTarget> targets;
    std::vector<std::wstring> targetIds;
    std::vector<WallpaperLibraryItem> visibleWallpapers;
    std::vector<DesktopWidget> visibleWidgets;
    desktop::WidgetRuntimeHealth widgetHealth;
    desktop::DesktopWidgetController widgetController;
    desktop::DesktopControlService desktopControl;
    NativeWeatherSnapshot widgetWeather;
    std::wstring selectedWallpaperId;
    std::wstring selectedWidgetId;
    Page page{Page::Installed};
    int activeNavId{kNavInstalledId};
    bool webBarVisible{};
    int wallpaperScroll{};
    int widgetScroll{};
    int wallpaperHover{-1};
    int widgetHover{-1};

    HFONT brandFont{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HFONT cardTitleFont{};
    HFONT cardSmallFont{};

    Microsoft::WRL::ComPtr<ID2D1Factory> widgetPreviewFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> widgetPreviewDWrite;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> widgetPreviewTarget;

    ~Impl() {
        if (window && IsWindow(window)) DestroyWindow(window);
        for (HFONT* font : {&brandFont, &titleFont, &bodyFont, &smallFont, &cardTitleFont, &cardSmallFont}) {
            if (*font) DeleteObject(*font);
        }
    }

    UINT Dpi() const {
        const UINT dpi = window ? GetDpiForWindow(window) : USER_DEFAULT_SCREEN_DPI;
        return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
    }

    int S(int px) const { return MulDiv(px, static_cast<int>(Dpi()), USER_DEFAULT_SCREEN_DPI); }

    HFONT MakeFont(int size, int weight, const wchar_t* face = L"Segoe UI Variable Text") const {
        return CreateFontW(-S(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, face);
    }

    void RebuildFonts() {
        for (HFONT* font : {&brandFont, &titleFont, &bodyFont, &smallFont, &cardTitleFont, &cardSmallFont}) {
            if (*font) { DeleteObject(*font); *font = nullptr; }
        }
        brandFont = MakeFont(18, FW_SEMIBOLD, L"Segoe UI Variable Display");
        titleFont = MakeFont(20, FW_SEMIBOLD, L"Segoe UI Variable Display");
        bodyFont = MakeFont(14, FW_NORMAL);
        smallFont = MakeFont(12, FW_NORMAL);
        cardTitleFont = MakeFont(14, FW_SEMIBOLD);
        cardSmallFont = MakeFont(11, FW_NORMAL);
    }

    void ApplyFonts() const {
        auto set = [](HWND control, HFONT font) {
            if (control && font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        };
        set(title, brandFont);
        set(sectionTitle, titleFont);
        set(search, bodyFont);
        set(addButton, bodyFont);
        for (HWND button : nav) set(button, bodyFont);
        for (HWND control : {status, targetCombo, applyButton, favoriteButton, removeButton,
                             wallpaperToggleButton, logsButton,
                             widgetCreateButton, widgetToggleButton, widgetRemoveButton, widgetRefreshButton,
                             webUrl, webConfirm, webCancel}) set(control, bodyFont);
    }

    std::wstring SelectedTargetId() const {
        if (!targetCombo) return {};
        const LRESULT selected = SendMessageW(targetCombo, CB_GETCURSEL, 0, 0);
        if (selected == CB_ERR || selected < 0 || static_cast<std::size_t>(selected) >= targetIds.size()) return {};
        return targetIds[static_cast<std::size_t>(selected)];
    }

    std::wstring FriendlyMonitor(std::wstring_view id) const {
        if (id.empty()) return L"主显示器";
        for (const auto& target : targets) {
            if (_wcsicmp(target.monitorId.c_str(), std::wstring(id).c_str()) == 0) {
                std::wstring label = target.primary ? L"主屏 · " : L"显示器 · ";
                label += target.displayName.empty() ? L"未命名显示器" : target.displayName;
                return label;
            }
        }
        return L"显示器";
    }

    void RebuildTargets() {
        if (!targetCombo) return;
        const std::wstring previous = SelectedTargetId();
        SendMessageW(targetCombo, CB_RESETCONTENT, 0, 0);
        targetIds.clear();
        SendMessageW(targetCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全局 / 当前布局"));
        targetIds.emplace_back();
        int selectedIndex = 0;
        for (const auto& target : targets) {
            std::wstring label = target.primary ? L"主屏 · " : L"显示器 · ";
            label += target.displayName.empty() ? L"未命名显示器" : target.displayName;
            SendMessageW(targetCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            targetIds.push_back(target.monitorId);
            if (!previous.empty() && _wcsicmp(previous.c_str(), target.monitorId.c_str()) == 0)
                selectedIndex = static_cast<int>(targetIds.size() - 1);
        }
        SendMessageW(targetCombo, CB_SETCURSEL, selectedIndex, 0);
    }

    std::optional<WallpaperLibraryItem> SelectedWallpaper() const {
        if (!library || selectedWallpaperId.empty()) return std::nullopt;
        return library->Find(selectedWallpaperId);
    }

    std::optional<DesktopWidget> SelectedWidget() const {
        if (selectedWidgetId.empty()) return std::nullopt;
        for (const auto& widget : visibleWidgets) {
            if (_wcsicmp(widget.id.c_str(), selectedWidgetId.c_str()) == 0) return widget;
        }
        DesktopWidget widget;
        const auto result = widgetController.Find(selectedWidgetId, &widget);
        if (!result.success) return std::nullopt;
        return widget;
    }

    const desktop::WidgetSurfaceHealth* HealthFor(std::wstring_view id) const {
        for (const auto& surface : widgetHealth.surfaces) {
            if (_wcsicmp(surface.widgetId.c_str(), std::wstring(id).c_str()) == 0) return &surface;
        }
        return nullptr;
    }

    void SetStatus(std::wstring text) const {
        if (status) SetWindowTextW(status, text.c_str());
    }

    void SetWallpaperEnabledState(bool enabled) {
        if (!wallpaperToggleButton) return;
        SetWindowTextW(wallpaperToggleButton, enabled ? L"停止壁纸" : L"恢复壁纸");
    }

    void RefreshWallpaperToggle() {
        if (!wallpaperToggleButton) return;
        desktop::DesktopState state;
        const bool enabled = desktopControl.GetState(&state).success ? state.enabled : true;
        SetWallpaperEnabledState(enabled);
    }

    void ToggleWallpaper() {
        desktop::DesktopState state{};
        const bool currentlyEnabled = desktopControl.GetState(&state).success ? state.enabled : true;
        const bool enable = !currentlyEnabled;
        turingdesk::log::Info(L"UI.Library", L"用户点击启停壁纸: 当前状态=" + std::wstring(currentlyEnabled ? L"已启用 (动态壁纸渲染中)" : L"已停用 (原生桌面壁纸)") + L" -> 目标切换为=" + std::wstring(enable ? L"启用壁纸" : L"停止壁纸"));
        const auto result = desktopControl.SetWallpaperEnabled(enable);
        turingdesk::log::Info(L"UI.Library", L"启停壁纸调用结果: " + result.message);
        RefreshWallpaperToggle();
        SetStatus(result.message.empty()
                      ? (enable ? L"壁纸已启用。" : L"壁纸已停用，小组件仍可显示。")
                      : result.message);
    }

    void OpenLogs() {
        turingdesk::log::Info(L"UI.Library", L"用户点击打开实时运行日志面板");
        turingdesk::log::ShowLiveLogConsole(instance, window);
    }

    void RefreshWallpapers() {
        if (!library) return;
        const auto previous = selectedWallpaperId;
        visibleWallpapers = library->Search(WindowText(search));
        if (!previous.empty()) {
            const auto it = std::find_if(visibleWallpapers.begin(), visibleWallpapers.end(), [&](const auto& item) {
                return _wcsicmp(item.id.c_str(), previous.c_str()) == 0;
            });
            if (it == visibleWallpapers.end()) selectedWallpaperId.clear();
        }
        if (selectedWallpaperId.empty() && !visibleWallpapers.empty()) selectedWallpaperId = visibleWallpapers.front().id;
        wallpaperScroll = 0;
        UpdateGridScroll(wallpaperGrid, false);
        UpdateFooter();
        InvalidateRect(wallpaperGrid, nullptr, TRUE);
    }

    void LoadWidgetList() {
        const auto previous = selectedWidgetId;
        std::vector<DesktopWidget> widgets;
        const auto result = widgetController.Refresh(&widgets);
        if (!result.success) {
            visibleWidgets.clear();
            selectedWidgetId.clear();
            SetStatus(result.message.empty() ? L"无法读取小组件。" : result.message);
        } else {
            visibleWidgets = std::move(widgets);
            if (!previous.empty()) {
                const auto it = std::find_if(visibleWidgets.begin(), visibleWidgets.end(), [&](const auto& item) {
                    return _wcsicmp(item.id.c_str(), previous.c_str()) == 0;
                });
                if (it == visibleWidgets.end()) selectedWidgetId.clear();
            }
            if (selectedWidgetId.empty() && !visibleWidgets.empty()) selectedWidgetId = visibleWidgets.front().id;
        }
        widgetScroll = 0;
        UpdateGridScroll(widgetGrid, true);
        UpdateFooter();
    }

    void RefreshWidgetHealth() {
        widgetHealth = {};
        widgetController.RuntimeHealth(&widgetHealth);
        NativeWeatherSnapshot cachedWeather;
        if (NativeWeatherService::ReadCachedSnapshot(&cachedWeather)) widgetWeather = std::move(cachedWeather);
        UpdateFooter();
        InvalidateRect(widgetGrid, nullptr, FALSE);
    }

    void RefreshWidgets() {
        LoadWidgetList();
        RefreshWidgetHealth();
    }

    int GridClientWidth(HWND grid) const {
        RECT rc{};
        GetClientRect(grid, &rc);
        return std::max(1, RectWidth(rc));
    }

    int CardWidth(HWND grid) const {
        const int width = GridClientWidth(grid);
        const int gap = CardGap();
        const int minCard = S(200);
        const int columns = std::max(1, (width + gap) / (minCard + gap));
        return std::max(minCard, (width - gap * (columns + 1)) / columns);
    }

    int CardHeight(HWND grid) const { return MulDiv(CardWidth(grid), 153, 272); }
    int CardGap() const { return S(12); }

    int GridColumns(HWND grid) const {
        const int width = GridClientWidth(grid);
        const int gap = CardGap();
        const int minCard = S(200);
        return std::max(1, (width + gap) / (minCard + gap));
    }

    int GridContentHeight(HWND grid, bool widgets) const {
        const std::size_t count = widgets ? visibleWidgets.size() : visibleWallpapers.size();
        const int columns = GridColumns(grid);
        const int rows = count == 0 ? 0 : static_cast<int>((count + columns - 1) / columns);
        return CardGap() + rows * (CardHeight(grid) + CardGap());
    }

    int& GridScrollRef(bool widgets) { return widgets ? widgetScroll : wallpaperScroll; }
    int& GridHoverRef(bool widgets) { return widgets ? widgetHover : wallpaperHover; }

    void UpdateGridScroll(HWND grid, bool widgets) {
        if (!grid) return;
        RECT rc{};
        GetClientRect(grid, &rc);
        const int pageHeight = RectHeight(rc);
        const int contentHeight = GridContentHeight(grid, widgets);
        int& offset = GridScrollRef(widgets);
        offset = std::clamp(offset, 0, std::max(0, contentHeight - pageHeight));
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = std::max(0, contentHeight - 1);
        info.nPage = static_cast<UINT>(pageHeight);
        info.nPos = offset;
        SetScrollInfo(grid, SB_VERT, &info, TRUE);
    }

    RECT CardRect(HWND grid, int index, bool widgets) const {
        const int columns = GridColumns(grid);
        const int col = index % columns;
        const int row = index / columns;
        const int scroll = widgets ? widgetScroll : wallpaperScroll;
        const int cardW = CardWidth(grid);
        const int cardH = CardHeight(grid);
        const int left = CardGap() + col * (cardW + CardGap());
        const int top = CardGap() + row * (cardH + CardGap()) - scroll;
        return RECT{left, top, left + cardW, top + cardH};
    }

    int HitTest(HWND grid, POINT point, bool widgets) const {
        const int count = static_cast<int>(widgets ? visibleWidgets.size() : visibleWallpapers.size());
        for (int i = 0; i < count; ++i) {
            RECT rect = CardRect(grid, i, widgets);
            if (PtInRect(&rect, point)) return i;
        }
        return -1;
    }

    void DrawWallpaperPreview(HDC dc, const RECT& rect, const WallpaperLibraryItem& item) const {
        COLORREF base = RGB(36, 42, 54);
        COLORREF accent = RGB(92, 135, 255);
        if (item.kind == LibraryWallpaperKind::Scene) {
            if (item.id.find(L"aurora") != std::wstring::npos) { base = RGB(6, 14, 36); accent = RGB(48, 220, 180); }
            else if (item.id.find(L"neon") != std::wstring::npos) { base = RGB(18, 6, 32); accent = RGB(255, 96, 48); }
            else if (item.id.find(L"grid") != std::wstring::npos) { base = RGB(8, 58, 96); accent = RGB(120, 220, 255); }
        } else if (item.kind == LibraryWallpaperKind::Web) {
            base = RGB(25, 46, 72); accent = RGB(76, 170, 235);
        } else if (item.kind == LibraryWallpaperKind::Video) {
            base = RGB(45, 34, 60); accent = RGB(181, 100, 235);
        } else if (item.kind == LibraryWallpaperKind::Image) {
            base = RGB(42, 58, 48); accent = RGB(99, 190, 132);
        }
        FillSolid(dc, rect, base);
        HPEN pen = CreatePen(PS_SOLID, 1, accent);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int step = std::max(S(24), 1);
        for (int x = rect.left; x < rect.right; x += step) {
            MoveToEx(dc, x, rect.top, nullptr); LineTo(dc, x, rect.bottom);
        }
        for (int y = rect.top; y < rect.bottom; y += step) {
            MoveToEx(dc, rect.left, y, nullptr); LineTo(dc, rect.right, y);
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    void DrawWallpaperCard(HDC dc, int index, const RECT& card) {
        if (index < 0 || static_cast<std::size_t>(index) >= visibleWallpapers.size()) return;
        const auto& item = visibleWallpapers[static_cast<std::size_t>(index)];
        const bool selected = _wcsicmp(item.id.c_str(), selectedWallpaperId.c_str()) == 0;
        const bool hover = wallpaperHover == index;

        RECT preview = card;
        preview.bottom -= S(50);
        DrawWallpaperPreview(dc, preview, item);

        RECT textRect{card.left, card.bottom - S(50), card.right, card.bottom};
        FillSolid(dc, textRect, RGB(250, 250, 252));
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, cardTitleFont);
        SetTextColor(dc, RGB(30, 30, 34));
        RECT titleRect{card.left + S(9), card.bottom - S(45), card.right - S(34), card.bottom - S(24)};
        DrawTextW(dc, item.title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        SelectObject(dc, cardSmallFont);
        SetTextColor(dc, RGB(105, 108, 118));
        std::wstring meta = DescriptionFor(item);
        if (SourceMissing(item)) meta += L" · 不可用";
        RECT metaRect{card.left + S(9), card.bottom - S(24), card.right - S(8), card.bottom - S(5)};
        DrawTextW(dc, meta.c_str(), -1, &metaRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        if (item.favorite) {
            SelectObject(dc, cardTitleFont);
            SetTextColor(dc, RGB(230, 170, 24));
            RECT star{card.right - S(30), card.bottom - S(46), card.right - S(6), card.bottom - S(20)};
            DrawTextW(dc, L"★", -1, &star, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, old);

        FrameSolid(dc, card, selected ? GetSysColor(COLOR_HIGHLIGHT) : (hover ? RGB(155, 160, 170) : RGB(222, 224, 230)), selected ? 2 : 1);
    }

    bool EnsureNativeWidgetPreviewRenderer() {
        if (!widgetPreviewFactory) {
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, widgetPreviewFactory.GetAddressOf()))) return false;
        }
        if (!widgetPreviewDWrite) {
            if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                           reinterpret_cast<IUnknown**>(widgetPreviewDWrite.GetAddressOf())))) return false;
        }
        if (!widgetPreviewTarget) {
            const auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96.0f, 96.0f);
            if (FAILED(widgetPreviewFactory->CreateDCRenderTarget(&properties, widgetPreviewTarget.GetAddressOf()))) return false;
        }
        return true;
    }

    bool DrawNativeWidgetPreview(HDC dc, const RECT& bounds, const DesktopWidget& widget,
                                 NativeWidgetPreset preset) {
        if (!EnsureNativeWidgetPreviewRenderer()) return false;
        RECT render = bounds;
        InflateRect(&render, -S(6), -S(6));
        const int availableW = std::max(1, RectWidth(render));
        const int availableH = std::max(1, RectHeight(render));
        const float screenW = static_cast<float>(std::max(1, GetSystemMetrics(SM_CXSCREEN)));
        const float screenH = static_cast<float>(std::max(1, GetSystemMetrics(SM_CYSCREEN)));
        const float aspect = std::clamp((widget.width * screenW) / std::max(1.0f, widget.height * screenH), 0.35f, 4.0f);
        if (static_cast<float>(availableW) / availableH > aspect) {
            const int fittedW = std::max(1, static_cast<int>(std::lround(availableH * aspect)));
            render.left += (availableW - fittedW) / 2;
            render.right = render.left + fittedW;
        } else {
            const int fittedH = std::max(1, static_cast<int>(std::lround(availableW / aspect)));
            render.top += (availableH - fittedH) / 2;
            render.bottom = render.top + fittedH;
        }

        FillSolid(dc, bounds, RGB(248, 250, 253));
        if (FAILED(widgetPreviewTarget->BindDC(dc, &render))) return false;
        widgetPreviewTarget->SetDpi(96.0f, 96.0f);

        const UINT desktopDpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window));
        const float designW = std::max(1.0f, widget.width * screenW * USER_DEFAULT_SCREEN_DPI / static_cast<float>(desktopDpi));
        const float designH = std::max(1.0f, widget.height * screenH * USER_DEFAULT_SCREEN_DPI / static_cast<float>(desktopDpi));
        const float scaleX = static_cast<float>(std::max(1, RectWidth(render))) / designW;
        const float scaleY = static_cast<float>(std::max(1, RectHeight(render))) / designH;
        const float scale = std::max(0.01f, std::min(scaleX, scaleY));

        NativeWidgetPaintContext context{};
        context.target = widgetPreviewTarget.Get();
        context.dwrite = widgetPreviewDWrite.Get();
        context.width = designW;
        context.height = designH;
        context.clearBackground = false;
        if (preset == NativeWidgetPreset::WeatherGlass && widgetWeather.valid) context.weather = &widgetWeather;
        if (preset == NativeWidgetPreset::GlassClock) {
            GetLocalTime(&context.localTime);
            context.hasTime = true;
        }
        widgetPreviewTarget->BeginDraw();
        widgetPreviewTarget->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
        PaintNativeWidgetPreset(context, preset);
        widgetPreviewTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        return SUCCEEDED(widgetPreviewTarget->EndDraw());
    }

    void DrawWidgetCard(HDC dc, int index, const RECT& card) {
        if (index < 0 || static_cast<std::size_t>(index) >= visibleWidgets.size()) return;
        const auto& widget = visibleWidgets[static_cast<std::size_t>(index)];
        const bool selected = _wcsicmp(widget.id.c_str(), selectedWidgetId.c_str()) == 0;
        const bool hover = widgetHover == index;
        const auto* health = HealthFor(widget.id);

        RECT preview = card;
        preview.bottom -= S(50);
        NativeWidgetPreset nativePreset{};
        const bool exactNativePreview = widget.kind == DesktopWidgetKind::Native &&
            ParseNativePreset(widget.source.wstring(), &nativePreset) &&
            DrawNativeWidgetPreview(dc, preview, widget, nativePreset);
        if (!exactNativePreview) {
            FillSolid(dc, preview, RGB(72, 87, 132));
            SetBkMode(dc, TRANSPARENT);
            HGDIOBJ previewOld = SelectObject(dc, titleFont);
            SetTextColor(dc, RGB(250, 252, 255));
            DrawTextW(dc, L"组件", -1, &preview, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, previewOld);
        }

        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, titleFont);

        RECT textRect{card.left, card.bottom - S(50), card.right, card.bottom};
        FillSolid(dc, textRect, RGB(250, 250, 252));
        SelectObject(dc, cardTitleFont);
        SetTextColor(dc, RGB(30, 30, 34));
        RECT titleRect{card.left + S(9), card.bottom - S(45), card.right - S(8), card.bottom - S(24)};
        DrawTextW(dc, widget.title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        SelectObject(dc, cardSmallFont);
        const bool healthy = health && health->renderingHealthy;
        SetTextColor(dc, healthy ? RGB(28, 145, 78) : (widget.enabled ? RGB(205, 115, 40) : RGB(120, 124, 134)));
        std::wstring meta;
        if (!widget.enabled) meta = L"○ 已停用";
        else if (healthy) meta = L"● 运行正常";
        else if (health && !health->detail.empty()) meta = L"● " + health->detail;
        else meta = L"● 等待桌面运行时";
        meta += L" · " + FriendlyMonitor(widget.monitorId);
        RECT metaRect{card.left + S(9), card.bottom - S(24), card.right - S(8), card.bottom - S(5)};
        DrawTextW(dc, meta.c_str(), -1, &metaRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        SelectObject(dc, old);

        FrameSolid(dc, card, selected ? GetSysColor(COLOR_HIGHLIGHT) : (hover ? RGB(155, 160, 170) : RGB(222, 224, 230)), selected ? 2 : 1);
    }

    void PaintGrid(HWND grid, bool widgets) {
        PAINTSTRUCT ps{};
        HDC paintDc = BeginPaint(grid, &ps);
        RECT client{};
        GetClientRect(grid, &client);
        const int width = std::max(1, RectWidth(client));
        const int height = std::max(1, RectHeight(client));
        HDC bufferDc = CreateCompatibleDC(paintDc);
        HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(paintDc, width, height) : nullptr;
        HGDIOBJ oldBitmap = bufferBitmap ? SelectObject(bufferDc, bufferBitmap) : nullptr;
        HDC dc = bufferBitmap ? bufferDc : paintDc;

        FillSolid(dc, client, RGB(255, 255, 255));
        const int count = static_cast<int>(widgets ? visibleWidgets.size() : visibleWallpapers.size());
        for (int i = 0; i < count; ++i) {
            RECT card = CardRect(grid, i, widgets);
            RECT clipped{};
            if (!IntersectRect(&clipped, &card, &client)) continue;
            if (widgets) DrawWidgetCard(dc, i, card);
            else DrawWallpaperCard(dc, i, card);
        }
        if (count == 0) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(115, 118, 128));
            HGDIOBJ old = SelectObject(dc, bodyFont);
            const wchar_t* empty = widgets ? L"还没有小组件。点右下角「新建桌面小组件」选择类型。" : L"桌面库为空。使用右上角“添加”导入壁纸。";
            DrawTextW(dc, empty, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
        }
        if (bufferBitmap) BitBlt(paintDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
        if (oldBitmap) SelectObject(bufferDc, oldBitmap);
        if (bufferBitmap) DeleteObject(bufferBitmap);
        if (bufferDc) DeleteDC(bufferDc);
        EndPaint(grid, &ps);
    }

    void ScrollGrid(HWND grid, bool widgets, int delta) {
        int& offset = GridScrollRef(widgets);
        RECT rc{};
        GetClientRect(grid, &rc);
        const int maxOffset = std::max(0, GridContentHeight(grid, widgets) - RectHeight(rc));
        offset = std::clamp(offset + delta, 0, maxOffset);
        UpdateGridScroll(grid, widgets);
        InvalidateRect(grid, nullptr, TRUE);
    }

    void UpdateFooter() {
        const bool installed = page == Page::Installed;
        const bool widgets = page == Page::Widgets;
        const bool showFooter = installed || widgets;
        ShowWindow(status, showFooter ? SW_SHOW : SW_HIDE);
        ShowWindow(targetCombo, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(applyButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(favoriteButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(removeButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(wallpaperToggleButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetCreateButton, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetToggleButton, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetRemoveButton, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetRefreshButton, widgets ? SW_SHOW : SW_HIDE);

        if (installed) {
            const auto selected = SelectedWallpaper();
            const bool usable = selected && !SourceMissing(*selected);
            EnableWindow(applyButton, usable ? TRUE : FALSE);
            EnableWindow(favoriteButton, selected ? TRUE : FALSE);
            EnableWindow(removeButton, selected && selected->kind != LibraryWallpaperKind::Scene ? TRUE : FALSE);
            if (!selected) SetStatus(L"选择一个桌面；双击卡片可直接应用。");
            else {
                std::wstring text = selected->title + L" · " + KindLabel(selected->kind);
                if (SourceMissing(*selected)) text += L" · 资源不可用";
                SetStatus(std::move(text));
                SetWindowTextW(favoriteButton, selected->favorite ? L"取消收藏" : L"收藏");
            }
        } else if (widgets) {
            const auto widget = SelectedWidget();
            EnableWindow(widgetToggleButton, widget ? TRUE : FALSE);
            EnableWindow(widgetRemoveButton, widget ? TRUE : FALSE);
            if (!widget) {
                SetStatus(L"在桌面按住小组件即可拖动位置；也可在此启用、停用或删除。");
            } else {
                const auto* health = HealthFor(widget->id);
                std::wostringstream text;
                text << widget->title << L" · " << (widget->enabled ? L"已启用" : L"已停用")
                     << L" · " << FriendlyMonitor(widget->monitorId);
                if (widget->enabled && health) {
                    if (health->renderingHealthy) text << L" · 运行正常";
                    else if (!health->detail.empty()) text << L" · " << health->detail;
                    if (!health->recommendedAction.empty()) text << L" · " << health->recommendedAction;
                }
                SetStatus(text.str());
                SetWindowTextW(widgetToggleButton, widget->enabled ? L"停用" : L"启用");
            }
        }
    }

    void SetPage(Page next) {
        page = next;
        const bool installed = page == Page::Installed;
        const bool widgets = page == Page::Widgets;
        const bool ai = page == Page::AI;
        turingdesk::log::Info(L"UI.Library", L"切换选项卡: " + std::wstring(installed ? L"壁纸库" : widgets ? L"组件" : L"API 配置"));
        activeNavId = installed ? kNavInstalledId : widgets ? kNavWidgetsId : kNavAiId;
        SetWindowTextW(sectionTitle, installed ? L"壁纸库" : widgets ? L"组件" : L"API 配置");
        ShowWindow(wallpaperGrid, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetGrid, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(search, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(addButton, installed ? SW_SHOW : SW_HIDE);
        if (!installed && webBarVisible) HideWebBar();
        if (ai) ShowDesktopAiSettingsPage(window);
        else HideDesktopAiSettingsPage(window);
        if (widgets) {
            LoadWidgetList();
            RefreshWidgetHealth();
        }
        UpdateFooter();
        Layout();
        InvalidateRect(window, nullptr, FALSE);
        for (HWND button : nav) InvalidateRect(button, nullptr, FALSE);
    }

    void ApplySelected() {
        const auto selected = SelectedWallpaper();
        if (!selected || SourceMissing(*selected)) {
            turingdesk::log::Warn(L"UI.Library", L"应用壁纸失败：未选中壁纸或资源不存在");
            return;
        }
        const std::wstring targetId = SelectedTargetId();
        turingdesk::log::Info(L"UI.Library", L"用户点击应用壁纸: \"" + selected->title + L"\" (ID=" + selected->id + L", 类型=" + KindLabel(selected->kind) + L"), 目标屏幕=" + (targetId.empty() ? L"全局 (所有显示器)" : (L"指定单屏 ID: " + targetId)));
        if (applyCallback) applyCallback(*selected, targetId);
        else {
            const auto result = targetId.empty()
                ? desktopControl.ApplyLibraryItem(*selected)
                : desktopControl.AssignLibraryItemToMonitor(*selected, targetId, FriendlyMonitor(targetId));
            turingdesk::log::Info(L"UI.Library", L"DesktopControl 应用结果: " + result.message);
        }
        std::wstring ignored;
        if (library) library->MarkUsed(selected->id, &ignored);
        RefreshWallpapers();
        SetStatus(L"已应用到桌面：" + selected->title);
    }

    void ToggleFavorite() {
        const auto selected = SelectedWallpaper();
        if (!selected || !library) return;
        turingdesk::log::Info(L"UI.Library", L"用户点击收藏/取消收藏: \"" + selected->title + L"\", 当前=" + (selected->favorite ? L"已收藏" : L"未收藏"));
        std::wstring error;
        if (!library->SetFavorite(selected->id, !selected->favorite, &error)) {
            turingdesk::log::Error(L"UI.Library", L"收藏操作失败: " + error);
            MessageBoxW(window, error.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWallpaperId = selected->id;
        RefreshWallpapers();
    }

    void RemoveSelected() {
        const auto selected = SelectedWallpaper();
        if (!selected || !library || selected->kind == LibraryWallpaperKind::Scene) return;
        turingdesk::log::Info(L"UI.Library", L"用户请求移除壁纸: \"" + selected->title + L"\" (id=" + selected->id + L")");
        if (MessageBoxW(window, (L"从桌面库移除“" + selected->title + L"”？").c_str(), L"妙喵",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        std::wstring error;
        if (!library->Remove(selected->id, selected->managedCopy, &error)) {
            turingdesk::log::Error(L"UI.Library", L"移除壁纸失败: " + error);
            MessageBoxW(window, error.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
            return;
        }
        turingdesk::log::Info(L"UI.Library", L"成功移除壁纸: " + selected->id);
        selectedWallpaperId.clear();
        RefreshWallpapers();
    }

    void ImportPath(const fs::path& path) {
        if (!library) return;
        turingdesk::log::Info(L"UI.Library", L"用户导入文件: " + path.wstring());
        std::wstring error;
        auto imported = library->ImportFile(path, {}, &error);
        if (!imported) {
            turingdesk::log::Error(L"UI.Library", L"导入失败: " + error);
            MessageBoxW(window, error.empty() ? L"导入失败。" : error.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
            return;
        }
        turingdesk::log::Info(L"UI.Library", L"导入成功: \"" + imported->title + L"\" (id=" + imported->id + L")");
        selectedWallpaperId = imported->id;
        RefreshWallpapers();
        SetStatus(L"已导入：" + imported->title);
    }

    void ImportFile() {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFile = path;
        dialog.nMaxFile = static_cast<DWORD>(std::size(path));
        dialog.lpstrFilter =
            L"壁纸文件\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.tif;*.tiff;*.mp4;*.mov;*.wmv;*.m4v;*.avi;*.mkv;*.webm;*.html;*.htm;*.tdwall\0"
            L"所有文件\0*.*\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&dialog)) ImportPath(path);
    }

    void ShowWebBar() {
        webBarVisible = true;
        ShowWindow(webUrl, SW_SHOW);
        ShowWindow(webConfirm, SW_SHOW);
        ShowWindow(webCancel, SW_SHOW);
        SetFocus(webUrl);
        Layout();
    }

    void HideWebBar() {
        webBarVisible = false;
        ShowWindow(webUrl, SW_HIDE);
        ShowWindow(webConfirm, SW_HIDE);
        ShowWindow(webCancel, SW_HIDE);
        if (webUrl) SetWindowTextW(webUrl, L"");
        Layout();
    }

    void ImportWeb() {
        if (!library) return;
        const std::wstring url = Trim(WindowText(webUrl));
        if (url.empty()) return;
        std::wstring error;
        auto imported = library->ImportWebUrl(url, {}, &error);
        if (!imported) {
            MessageBoxW(window, error.empty() ? L"Web URL 导入失败。" : error.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWallpaperId = imported->id;
        HideWebBar();
        RefreshWallpapers();
        SetStatus(L"已导入 Web 壁纸：" + imported->title);
    }

    void ShowAddMenu() {
        RECT rect{};
        GetWindowRect(addButton, &rect);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuImportFile, L"从文件导入…");
        AppendMenuW(menu, MF_STRING, kMenuImportWeb, L"添加 HTTPS Web 地址…");
        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, rect.right, rect.bottom, 0, window, nullptr);
        DestroyMenu(menu);
    }

    std::wstring PrimaryMonitorId() const {
        for (const auto& target : targets) if (target.primary) return target.monitorId;
        return targets.empty() ? std::wstring{} : targets.front().monitorId;
    }

    void CreateWidgetPreset(desktop::WidgetFixedPreset preset) {
        turingdesk::log::Info(L"UI.Widgets", L"用户创建小组件预设...");
        DesktopWidget created;
        const auto result = widgetController.CreatePreset(preset, PrimaryMonitorId(), &created);
        if (!result.success) {
            turingdesk::log::Error(L"UI.Widgets", L"创建小组件预设失败: " + result.message);
            MessageBoxW(window, result.message.empty() ? L"创建小组件失败。" : result.message.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
            return;
        }
        turingdesk::log::Info(L"UI.Widgets", L"成功创建小组件: \"" + created.title + L"\" (id=" + created.id + L")");
        selectedWidgetId = created.id;
        RefreshWidgets();
        SetStatus(L"已创建桌面小组件：" + created.title);
    }

    void ShowWidgetCreateMenu() {
        RECT rect{};
        GetWindowRect(widgetCreateButton, &rect);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuWidgetGlassClock, L"玻璃时钟");
        AppendMenuW(menu, MF_STRING, kMenuWidgetTodayTasks, L"今日待办");
        AppendMenuW(menu, MF_STRING, kMenuWidgetWeatherGlass, L"玻璃天气");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuWidgetAuto, L"自动轮换下一个");
        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, rect.right, rect.bottom, 0, window, nullptr);
        DestroyMenu(menu);
    }

    void ShowWidgetContextMenu(POINT screen) {
        HMENU menu = CreatePopupMenu();
        const auto current = SelectedWidget();
        const wchar_t* toggle = current && current->enabled ? L"停用" : L"启用";
        AppendMenuW(menu, MF_STRING, kMenuWidgetToggle, toggle);
        AppendMenuW(menu, MF_STRING, kMenuWidgetRemove, L"删除");
        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, window, nullptr);
        DestroyMenu(menu);
    }

    void CreateClockWidget() {
        ShowWidgetCreateMenu();
    }

    void ToggleWidget() {
        const auto current = SelectedWidget();
        if (!current) return;
        const bool target = !current->enabled;
        turingdesk::log::Info(L"UI.Widgets", L"用户切换小组件启停: \"" + current->title + L"\" -> " + std::wstring(target ? L"启用" : L"停用"));
        const auto result = widgetController.SetEnabled(current->id, target);
        turingdesk::log::Info(L"UI.Widgets", L"切换小组件启停结果: " + result.message);
        if (!result.success) {
            MessageBoxW(window, result.message.empty() ? L"更新小组件失败。" : result.message.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWidgetId = current->id;
        RefreshWidgets();
    }

    void RemoveWidget() {
        const auto current = SelectedWidget();
        if (!current) return;
        turingdesk::log::Info(L"UI.Widgets", L"用户请求删除小组件: \"" + current->title + L"\" (id=" + current->id + L")");
        if (MessageBoxW(window, (L"删除小组件“" + current->title + L"”？").c_str(), L"妙喵",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        const auto result = widgetController.Remove(current->id);
        turingdesk::log::Info(L"UI.Widgets", L"删除小组件结果: " + result.message);
        if (!result.success) {
            MessageBoxW(window, result.message.empty() ? L"删除小组件失败。" : result.message.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWidgetId.clear();
        RefreshWidgets();
    }

    void HandleNav(int id) {
        if (id == kNavInstalledId) { SetPage(Page::Installed); return; }
        if (id == kNavWidgetsId) { SetPage(Page::Widgets); return; }
        if (id == kNavAiId) { SetPage(Page::AI); return; }
    }

    void ShowWallpaperContextMenu(POINT screenPoint) {
        const auto selected = SelectedWallpaper();
        if (!selected) return;
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, SourceMissing(*selected) ? MF_STRING | MF_GRAYED : MF_STRING, kMenuApply, L"应用到桌面");
        AppendMenuW(menu, MF_STRING, kMenuFavorite, selected->favorite ? L"取消收藏" : L"收藏");
        if (selected->kind != LibraryWallpaperKind::Scene)
            AppendMenuW(menu, MF_STRING, kMenuRemove, L"移出库");
        TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, screenPoint.x, screenPoint.y, 0, window, nullptr);
        DestroyMenu(menu);
    }

    void Layout() {
        if (!window) return;
        RECT rc{};
        GetClientRect(window, &rc);
        const int width = RectWidth(rc);
        const int height = RectHeight(rc);
        const int sidebarW = S(208);
        const int topH = S(58);
        const int footerH = S(58);
        const int margin = std::max(S(12), MulDiv(width - sidebarW, 16, 1000));
        const bool installed = page == Page::Installed;
        const bool widgets = page == Page::Widgets;
        const int webH = webBarVisible && installed ? S(48) : 0;

        auto place = [&](HWND child, int x, int y, int w, int h) {
            if (!child) return;
            SetWindowPos(child, nullptr, x, y, std::max(1, w), std::max(1, h),
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
        };

        place(title, S(18), S(16), sidebarW - S(36), S(28));
        int navY = topH + S(18);
        for (HWND button : nav) {
            place(button, S(12), navY, sidebarW - S(24), S(38));
            navY += S(42);
        }
        place(logsButton, S(12), height - S(46), sidebarW - S(24), S(32));

        const int contentLeft = sidebarW;
        const int contentWidth = std::max(1, width - contentLeft);
        const int headerLeft = contentLeft + margin;
        const int headerRight = width - margin;
        if (installed) {
            const int addW = S(96);
            const int titleW = std::clamp(contentWidth * 21 / 100, S(112), S(190));
            const int titleRight = headerLeft + titleW;
            const int addLeft = headerRight - addW;
            const int searchLeft = titleRight + S(12);
            const int searchW = std::max(S(120), addLeft - S(12) - searchLeft);
            place(sectionTitle, headerLeft, S(17), titleW, S(30));
            place(search, searchLeft, S(12), searchW, S(34));
            place(addButton, addLeft, S(11), addW, S(36));
        } else {
            place(sectionTitle, headerLeft, S(17), std::max(S(160), contentWidth - margin * 2), S(30));
        }

        if (webBarVisible && installed) {
            const int webTop = topH;
            const int buttonsW = S(202);
            const int urlW = std::max(S(160), contentWidth - margin * 2 - buttonsW - S(12));
            place(webUrl, contentLeft + margin, webTop + S(7), urlW, S(34));
            place(webConfirm, width - margin - S(202), webTop + S(7), S(96), S(34));
            place(webCancel, width - margin - S(98), webTop + S(7), S(98), S(34));
        }

        const int contentTop = topH + webH;
        const int contentBottom = std::max(contentTop, height - footerH);
        const int contentH = std::max(1, contentBottom - contentTop);
        place(wallpaperGrid, contentLeft, contentTop, contentWidth, contentH);
        place(widgetGrid, contentLeft, contentTop, contentWidth, contentH);
        UpdateGridScroll(wallpaperGrid, false);
        UpdateGridScroll(widgetGrid, true);

        const int footerTop = height - footerH;
        if (installed) {
            const int gap = S(6);
            const int actionW = std::max(S(82), MulDiv(contentWidth, 15, 100));
            const int smallW = std::max(S(68), MulDiv(contentWidth, 11, 100));
            const int toggleW = std::max(S(82), MulDiv(contentWidth, 12, 100));
            const int targetW = std::max(S(118), MulDiv(contentWidth, 23, 100));
            const int right = width - margin;
            const int actionTotal = targetW + toggleW + actionW + smallW * 2 + gap * 4;
            const int actionsLeft = right - actionTotal;
            const int statusLeft = contentLeft + margin;
            const int available = actionsLeft - S(10) - statusLeft;
            const int statusW = std::max(0, std::min(S(90), available));
            place(status, statusLeft, footerTop + S(18), statusW, S(26));
            int x = actionsLeft;
            place(targetCombo, x, footerTop + S(11), targetW, S(180)); x += targetW + gap;
            place(wallpaperToggleButton, x, footerTop + S(11), toggleW, S(36)); x += toggleW + gap;
            place(favoriteButton, x, footerTop + S(11), smallW, S(36)); x += smallW + gap;
            place(removeButton, x, footerTop + S(11), smallW, S(36)); x += smallW + gap;
            place(applyButton, x, footerTop + S(11), actionW, S(36));
        } else if (widgets) {
            const int gap = S(6);
            const int buttonW = std::max(S(70), MulDiv(contentWidth, 11, 100));
            const int createW = std::max(S(124), MulDiv(contentWidth, 20, 100));
            const int right = width - margin;
            const int actionTotal = createW + buttonW * 3 + gap * 3;
            const int actionsLeft = right - actionTotal;
            const int statusLeft = contentLeft + margin;
            const int available = actionsLeft - S(10) - statusLeft;
            const int statusW = std::max(0, std::min(S(240), available));
            place(status, statusLeft, footerTop + S(18), statusW, S(26));
            int x = actionsLeft;
            place(widgetCreateButton, x, footerTop + S(11), createW, S(36)); x += createW + gap;
            place(widgetRefreshButton, x, footerTop + S(11), buttonW, S(36)); x += buttonW + gap;
            place(widgetToggleButton, x, footerTop + S(11), buttonW, S(36)); x += buttonW + gap;
            place(widgetRemoveButton, x, footerTop + S(11), buttonW, S(36));
        }

        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
    }

    LRESULT DrawNavButton(const DRAWITEMSTRUCT* draw) {
        if (!draw) return FALSE;
        const bool active = static_cast<int>(draw->CtlID) == activeNavId;
        const COLORREF background = active ? RGB(229, 241, 255) : RGB(241, 247, 255);
        FillSolid(draw->hDC, draw->rcItem, background);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, active ? RGB(35, 105, 225) : RGB(45, 65, 98));
        HGDIOBJ old = SelectObject(draw->hDC, active ? cardTitleFont : bodyFont);
        wchar_t text[64]{};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
        RECT label = draw->rcItem;
        label.left += S(14);
        DrawTextW(draw->hDC, text, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (active) {
            RECT line{draw->rcItem.left, draw->rcItem.top + S(7), draw->rcItem.left + S(3), draw->rcItem.bottom - S(7)};
            FillSolid(draw->hDC, line, GetSysColor(COLOR_HIGHLIGHT));
        }
        if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
        SelectObject(draw->hDC, old);
        return TRUE;
    }

    static LRESULT CALLBACK GridProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
        const bool widgets = GetDlgCtrlID(hwnd) == kWidgetGridId;
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: self->PaintGrid(hwnd, widgets); return 0;
        case WM_SIZE: self->UpdateGridScroll(hwnd, widgets); InvalidateRect(hwnd, nullptr, FALSE); return 0;
        case WM_MOUSEWHEEL: self->ScrollGrid(hwnd, widgets, -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * self->S(72)); return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &info);
            int target = info.nPos;
            switch (LOWORD(wParam)) {
            case SB_LINEUP: target -= self->S(36); break;
            case SB_LINEDOWN: target += self->S(36); break;
            case SB_PAGEUP: target -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: target += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: target = info.nTrackPos; break;
            default: break;
            }
            self->ScrollGrid(hwnd, widgets, target - self->GridScrollRef(widgets));
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int hit = self->HitTest(hwnd, pt, widgets);
            int& hover = self->GridHoverRef(widgets);
            if (hover != hit) { hover = hit; InvalidateRect(hwnd, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            self->GridHoverRef(widgets) = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int hit = self->HitTest(hwnd, pt, widgets);
            if (hit >= 0) {
                if (widgets) self->selectedWidgetId = self->visibleWidgets[static_cast<std::size_t>(hit)].id;
                else self->selectedWallpaperId = self->visibleWallpapers[static_cast<std::size_t>(hit)].id;
                self->UpdateFooter();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (!widgets) {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const int hit = self->HitTest(hwnd, pt, false);
                if (hit >= 0) {
                    self->selectedWallpaperId = self->visibleWallpapers[static_cast<std::size_t>(hit)].id;
                    self->ApplySelected();
                }
            }
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int hit = self->HitTest(hwnd, pt, widgets);
            if (hit < 0) return 0;
            if (widgets) {
                self->selectedWidgetId = self->visibleWidgets[static_cast<std::size_t>(hit)].id;
                self->UpdateFooter();
                POINT screen = pt;
                ClientToScreen(hwnd, &screen);
                self->ShowWidgetContextMenu(screen);
            } else {
                self->selectedWallpaperId = self->visibleWallpapers[static_cast<std::size_t>(hit)].id;
                self->UpdateFooter();
                POINT screen = pt;
                ClientToScreen(hwnd, &screen);
                self->ShowWallpaperContextMenu(screen);
            }
            return 0;
        }
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Impl*>(create->lpCreateParams);
            if (self) {
                self->window = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
        }
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);

        switch (message) {
        case WM_NCHITTEST: {
            const LRESULT nativeHit = DefWindowProcW(hwnd, message, wParam, lParam);
            if (nativeHit != HTCLIENT) return nativeHit;
            break;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (info) {
                info->ptMinTrackSize.x = self->S(640);
                info->ptMinTrackSize.y = self->S(440);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            turingdesk::log::Info(L"UI.Library", L"显示器 DPI 缩放发生变化: 当前 DPI=" + std::to_wstring(self->Dpi()));
            self->RebuildFonts();
            self->ApplyFonts();
            self->Layout();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_SIZE: {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            const wchar_t* stateStr = wParam == SIZE_MAXIMIZED ? L"最大化" : (wParam == SIZE_MINIMIZED ? L"最小化" : L"正常自由缩放");
            turingdesk::log::Info(L"UI.Library", L"设置页面窗口尺寸变更: 宽=" + std::to_wstring(width) +
                                  L", 高=" + std::to_wstring(height) + L", 状态=" + stateStr);
            self->Layout();
            return 0;
        }
        case WM_DRAWITEM: {
            const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (draw && draw->CtlType == ODT_BUTTON && draw->CtlID >= kNavInstalledId && draw->CtlID <= kNavAiId)
                return self->DrawNavButton(draw);
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            if (id == kSearchId && notification == EN_CHANGE) self->RefreshWallpapers();
            else if (id == kAddId && notification == BN_CLICKED) self->ShowAddMenu();
            else if (id >= kNavInstalledId && id <= kNavAiId && notification == BN_CLICKED) self->HandleNav(id);
            else if (id == kApplyId && notification == BN_CLICKED) self->ApplySelected();
            else if (id == kFavoriteId && notification == BN_CLICKED) self->ToggleFavorite();
            else if (id == kRemoveId && notification == BN_CLICKED) self->RemoveSelected();
            else if (id == kWidgetCreateId && notification == BN_CLICKED) self->CreateClockWidget();
            else if (id == kWidgetToggleId && notification == BN_CLICKED) self->ToggleWidget();
            else if (id == kWidgetRemoveId && notification == BN_CLICKED) self->RemoveWidget();
            else if (id == kWidgetRefreshId && notification == BN_CLICKED) self->RefreshWidgets();
            else if (id == kWallpaperToggleId && notification == BN_CLICKED) self->ToggleWallpaper();
            else if (id == kOpenLogsId && notification == BN_CLICKED) self->OpenLogs();
            else if (id == kWebConfirmId && notification == BN_CLICKED) self->ImportWeb();
            else if (id == kWebCancelId && notification == BN_CLICKED) self->HideWebBar();
            else if (id == kMenuImportFile) self->ImportFile();
            else if (id == kMenuImportWeb) self->ShowWebBar();
            else if (id == kMenuApply) self->ApplySelected();
            else if (id == kMenuFavorite) self->ToggleFavorite();
            else if (id == kMenuRemove) self->RemoveSelected();
            else if (id == kMenuWidgetGlassClock) self->CreateWidgetPreset(desktop::WidgetFixedPreset::GlassClock);
            else if (id == kMenuWidgetTodayTasks) self->CreateWidgetPreset(desktop::WidgetFixedPreset::TodayTasks);
            else if (id == kMenuWidgetWeatherGlass) self->CreateWidgetPreset(desktop::WidgetFixedPreset::WeatherGlass);
            else if (id == kMenuWidgetAuto) {
                DesktopWidget created;
                const auto result = self->widgetController.CreateClock(self->PrimaryMonitorId(), &created);
                if (!result.success) {
                    MessageBoxW(self->window, result.message.empty() ? L"创建小组件失败。" : result.message.c_str(), L"妙喵", MB_OK | MB_ICONERROR);
                } else {
                    self->selectedWidgetId = created.id;
                    self->RefreshWidgets();
                    self->SetStatus(L"已创建桌面小组件：" + created.title);
                }
            }
            else if (id == kMenuWidgetToggle) self->ToggleWidget();
            else if (id == kMenuWidgetRemove) self->RemoveWidget();
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                wchar_t path[32768]{};
                if (DragQueryFileW(drop, i, path, static_cast<UINT>(std::size(path))) > 0) self->ImportPath(path);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int sidebarW = self->S(208);
            const int topH = self->S(58);
            const bool showFooter = self->page != Page::AI;
            const int footerH = showFooter ? self->S(58) : 0;
            RECT whole = client;
            FillSolid(dc, whole, RGB(248, 251, 255));
            RECT sidebar{0, 0, sidebarW, client.bottom};
            FillSolid(dc, sidebar, RGB(241, 247, 255));
            if (showFooter) {
                RECT footer{sidebarW, std::max<LONG>(0, client.bottom - footerH), client.right, client.bottom};
                FillSolid(dc, footer, RGB(248, 251, 255));
            }
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 231, 245));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            MoveToEx(dc, sidebarW - 1, 0, nullptr); LineTo(dc, sidebarW - 1, client.bottom);
            MoveToEx(dc, sidebarW, topH - 1, nullptr); LineTo(dc, client.right, topH - 1);
            if (showFooter) {
                const int footerTop = client.bottom - footerH;
                MoveToEx(dc, sidebarW, footerTop, nullptr); LineTo(dc, client.right, footerTop);
            }
            SelectObject(dc, oldPen);
            DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY: self->window = nullptr; return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool CreateWindowUi() {
        INITCOMMONCONTROLSEX common{};
        common.dwSize = sizeof(common);
        common.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&common);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &Impl::WndProc;
        wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        WNDCLASSEXW gridClass{};
        gridClass.cbSize = sizeof(gridClass);
        gridClass.hInstance = instance;
        gridClass.lpfnWndProc = &Impl::GridProc;
        gridClass.lpszClassName = kGridClass;
        gridClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
        gridClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        gridClass.style = CS_DBLCLKS;
        if (!RegisterClassExW(&gridClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        window = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"妙喵",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, S(1160), S(790),
                                 nullptr, nullptr, instance, this);
        if (!window) return false;
        DragAcceptFiles(window, TRUE);

        RebuildFonts();
        auto font = [&](HWND control, HFONT use) {
            if (control && use) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(use), TRUE);
            return control;
        };
        auto label = [&](const wchar_t* text, HFONT use) {
            return font(CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        0, 0, 10, 10, window, nullptr, instance, nullptr), use);
        };
        auto button = [&](const wchar_t* text, int id, DWORD extra = 0, bool visible = true) {
            const DWORD style = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | extra | (visible ? WS_VISIBLE : 0);
            return font(CreateWindowExW(0, L"BUTTON", text, style, 0, 0, 10, 10,
                                        window, ControlId(id), instance, nullptr), bodyFont);
        };

        title = label(L"妙喵", brandFont);
        sectionTitle = label(L"壁纸库", titleFont);
        search = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kSearchId), instance, nullptr), bodyFont);
        SendMessageW(search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索壁纸"));
        addButton = button(L"＋ 添加", kAddId);

        const std::array<const wchar_t*, 3> navLabels{L"壁纸", L"组件", L"API 配置"};
        const std::array<int, 3> navIds{kNavInstalledId, kNavWidgetsId, kNavAiId};
        for (std::size_t i = 0; i < nav.size(); ++i)
            nav[i] = button(navLabels[i], navIds[i], BS_OWNERDRAW);

        wallpaperGrid = CreateWindowExW(0, kGridClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                        0, 0, 10, 10, window, ControlId(kWallpaperGridId), instance, this);
        widgetGrid = CreateWindowExW(0, kGridClass, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN,
                                     0, 0, 10, 10, window, ControlId(kWidgetGridId), instance, this);

        status = label(L"选择一个桌面；双击卡片可直接应用。", smallFont);
        targetCombo = font(CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                           0, 0, 10, 10, window, ControlId(kTargetComboId), instance, nullptr), bodyFont);
        applyButton = button(L"应用到桌面", kApplyId);
        favoriteButton = button(L"收藏", kFavoriteId);
        removeButton = button(L"移出库", kRemoveId);

        widgetCreateButton = button(L"＋ 新建桌面小组件", kWidgetCreateId, 0, false);
        widgetToggleButton = button(L"启用 / 停用", kWidgetToggleId, 0, false);
        widgetRemoveButton = button(L"删除", kWidgetRemoveId, 0, false);
        widgetRefreshButton = button(L"刷新", kWidgetRefreshId, 0, false);

        webUrl = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kWebUrlId), instance, nullptr), bodyFont);
        SendMessageW(webUrl, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"https://example.com/wallpaper"));
        webConfirm = button(L"添加 Web", kWebConfirmId, 0, false);
        webCancel = button(L"取消", kWebCancelId, 0, false);
        wallpaperToggleButton = button(L"停止壁纸", kWallpaperToggleId, 0, false);
        logsButton = button(L"实时日志面板 ↗", kOpenLogsId, 0, true);

        ApplyFonts();
        RebuildTargets();
        RefreshWallpapers();
        SetPage(Page::Installed);
        Layout();
        return true;
    }
};

WallpaperLibraryWindow::WallpaperLibraryWindow() : impl_(std::make_unique<Impl>()) {}
WallpaperLibraryWindow::~WallpaperLibraryWindow() = default;

bool WallpaperLibraryWindow::Show(HINSTANCE instance, WallpaperLibrary* library,
                                  const std::vector<WallpaperLibraryTarget>& targets,
                                  ApplyCallback applyCallback,
                                  NavigateCallback navigateCallback) {
    impl_->instance = instance;
    impl_->library = library;
    impl_->targets = targets;
    impl_->applyCallback = std::move(applyCallback);
    impl_->navigateCallback = std::move(navigateCallback);
    if (!impl_->window && !impl_->CreateWindowUi()) return false;

    // First CreateWindowUi() already loaded local wallpaper list. Re-showing must
    // not refresh wallpaper, probe widget runtime, or touch Explorer attachment.
    impl_->RebuildTargets();
    ShowWindow(impl_->window, SW_SHOWNORMAL);
    SetForegroundWindow(impl_->window);
    return true;
}

void WallpaperLibraryWindow::SetTargets(const std::vector<WallpaperLibraryTarget>& targets) {
    impl_->targets = targets;
    if (!impl_->window || !IsWindow(impl_->window)) return;
    impl_->RebuildTargets();
}

void WallpaperLibraryWindow::Close() {
    if (impl_->window) ShowWindow(impl_->window, SW_HIDE);
}

void WallpaperLibraryWindow::Refresh() {
    if (!impl_->window || !IsWindow(impl_->window)) return;
    impl_->RebuildTargets();
    impl_->RefreshWallpaperToggle();
    impl_->RefreshWallpapers();
    if (impl_->page == Impl::Page::Widgets) impl_->RefreshWidgets();
}

void WallpaperLibraryWindow::SetWallpaperEnabledState(const bool enabled) {
    impl_->SetWallpaperEnabledState(enabled);
}

bool WallpaperLibraryWindow::Visible() const noexcept {
    return impl_->window && IsWindowVisible(impl_->window);
}

HWND WallpaperLibraryWindow::Window() const noexcept {
    return impl_->window;
}

} // namespace turingdesk::wallpaper
