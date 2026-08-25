#include "turingdesk/WallpaperLibraryWindow.h"
#include "turingdesk/DesktopWidgetStore.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
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
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kWindowClass[] = L"TuringDesk.Native.DesktopLibrary";
constexpr wchar_t kGridClass[] = L"TuringDesk.Native.DesktopLibraryGrid";

constexpr int kSearchId = 6101;
constexpr int kAddId = 6102;
constexpr int kNavInstalledId = 6110;
constexpr int kNavWidgetsId = 6111;
constexpr int kNavPlaylistsId = 6112;
constexpr int kNavDisplaysId = 6113;
constexpr int kNavRulesId = 6114;
constexpr int kNavPerformanceId = 6115;
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

constexpr UINT kMenuImportFile = 6201;
constexpr UINT kMenuImportWeb = 6202;
constexpr UINT kMenuApply = 6210;
constexpr UINT kMenuFavorite = 6211;
constexpr UINT kMenuRemove = 6212;

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
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
        if (_wcsicmp(item.id.c_str(), L"scene-aurora") == 0) return L"流动极光 · 原生 Scene";
        if (_wcsicmp(item.id.c_str(), L"scene-neon") == 0) return L"霓虹网格 · 原生 Scene";
        if (_wcsicmp(item.id.c_str(), L"scene-grid") == 0) return L"深色网格 · 原生 Scene";
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
    switch (id) {
    case kNavWidgetsId: return WallpaperSettingsSection::Widgets;
    case kNavPlaylistsId: return WallpaperSettingsSection::Playlists;
    case kNavDisplaysId: return WallpaperSettingsSection::Displays;
    case kNavRulesId: return WallpaperSettingsSection::Rules;
    case kNavPerformanceId: return WallpaperSettingsSection::Performance;
    case kNavAiId: return WallpaperSettingsSection::AI;
    default: return WallpaperSettingsSection::Installed;
    }
}

HBITMAP LoadScaledBitmap(const fs::path& path, int width, int height) {
    if (path.empty() || width <= 0 || height <= 0) return nullptr;
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) return nullptr;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.GetAddressOf()))) || !factory) return nullptr;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) || !decoder) return nullptr;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || !frame) return nullptr;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) || !scaler) return nullptr;
    if (FAILED(scaler->Initialize(frame.Get(), static_cast<UINT>(width), static_cast<UINT>(height),
                                  WICBitmapInterpolationModeFant))) return nullptr;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) || !converter) return nullptr;
    if (FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return nullptr;
    }
    const UINT stride = static_cast<UINT>(width * 4);
    const UINT bytes = stride * static_cast<UINT>(height);
    if (FAILED(converter->CopyPixels(nullptr, stride, bytes, static_cast<BYTE*>(bits)))) {
        DeleteObject(bitmap);
        return nullptr;
    }
    return bitmap;
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
    enum class Page { Installed, Widgets };

    HINSTANCE instance{};
    HWND window{};
    HWND title{};
    HWND search{};
    HWND addButton{};
    std::array<HWND, 7> nav{};
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

    WallpaperLibrary* library{};
    ApplyCallback applyCallback;
    NavigateCallback navigateCallback;
    std::vector<WallpaperLibraryTarget> targets;
    std::vector<std::wstring> targetIds;
    std::vector<WallpaperLibraryItem> visibleWallpapers;
    std::vector<DesktopWidget> visibleWidgets;
    std::wstring selectedWallpaperId;
    std::wstring selectedWidgetId;
    Page page{Page::Installed};
    bool webBarVisible{};
    int wallpaperScroll{};
    int widgetScroll{};
    int wallpaperHover{-1};
    int widgetHover{-1};

    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HFONT cardTitleFont{};
    HFONT cardSmallFont{};
    std::unordered_map<std::wstring, HBITMAP> bitmapCache;

    ~Impl() {
        if (window && IsWindow(window)) DestroyWindow(window);
        ClearBitmaps();
        for (HFONT* font : {&titleFont, &bodyFont, &smallFont, &cardTitleFont, &cardSmallFont}) {
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
        for (HFONT* font : {&titleFont, &bodyFont, &smallFont, &cardTitleFont, &cardSmallFont}) {
            if (*font) { DeleteObject(*font); *font = nullptr; }
        }
        titleFont = MakeFont(16, FW_SEMIBOLD, L"Segoe UI Variable Display");
        bodyFont = MakeFont(15, FW_NORMAL);
        smallFont = MakeFont(13, FW_NORMAL);
        cardTitleFont = MakeFont(15, FW_SEMIBOLD);
        cardSmallFont = MakeFont(12, FW_NORMAL);
    }

    void ApplyFonts() const {
        auto set = [](HWND control, HFONT font) {
            if (control && font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        };
        set(title, titleFont);
        set(search, bodyFont);
        set(addButton, bodyFont);
        for (HWND button : nav) set(button, bodyFont);
        for (HWND control : {status, targetCombo, applyButton, favoriteButton, removeButton,
                             widgetCreateButton, widgetToggleButton, widgetRemoveButton, widgetRefreshButton,
                             webUrl, webConfirm, webCancel}) set(control, bodyFont);
    }

    void ClearBitmaps() {
        for (auto& [_, bitmap] : bitmapCache) if (bitmap) DeleteObject(bitmap);
        bitmapCache.clear();
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
        for (const auto& widget : visibleWidgets)
            if (_wcsicmp(widget.id.c_str(), selectedWidgetId.c_str()) == 0) return widget;
        DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) return std::nullopt;
        return store.Find(selectedWidgetId);
    }

    void SetStatus(std::wstring text) const {
        if (status) SetWindowTextW(status, text.c_str());
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

    void RefreshWidgets() {
        DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) {
            SetStatus(error.empty() ? L"无法读取小组件。" : error);
            visibleWidgets.clear();
            selectedWidgetId.clear();
        } else {
            const auto previous = selectedWidgetId;
            visibleWidgets = store.Items();
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
        InvalidateRect(widgetGrid, nullptr, TRUE);
    }

    int CardWidth() const { return S(272); }
    int CardHeight() const { return S(153); }
    int CardGap() const { return S(10); }

    int GridColumns(HWND grid) const {
        RECT rc{};
        GetClientRect(grid, &rc);
        const int width = std::max(1L, rc.right - rc.left);
        return std::max(1, (width - CardGap()) / (CardWidth() + CardGap()));
    }

    int GridContentHeight(HWND grid, bool widgets) const {
        const std::size_t count = widgets ? visibleWidgets.size() : visibleWallpapers.size();
        const int columns = GridColumns(grid);
        const int rows = count == 0 ? 0 : static_cast<int>((count + columns - 1) / columns);
        return CardGap() + rows * (CardHeight() + CardGap());
    }

    int& GridScrollRef(bool widgets) { return widgets ? widgetScroll : wallpaperScroll; }
    int& GridHoverRef(bool widgets) { return widgets ? widgetHover : wallpaperHover; }

    void UpdateGridScroll(HWND grid, bool widgets) {
        if (!grid) return;
        RECT rc{};
        GetClientRect(grid, &rc);
        const int pageHeight = std::max(1L, rc.bottom - rc.top);
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
        const int left = CardGap() + col * (CardWidth() + CardGap());
        const int top = CardGap() + row * (CardHeight() + CardGap()) - scroll;
        return RECT{left, top, left + CardWidth(), top + CardHeight()};
    }

    int HitTest(HWND grid, POINT point, bool widgets) const {
        const int count = static_cast<int>(widgets ? visibleWidgets.size() : visibleWallpapers.size());
        for (int i = 0; i < count; ++i) {
            RECT rect = CardRect(grid, i, widgets);
            if (PtInRect(&rect, point)) return i;
        }
        return -1;
    }

    HBITMAP BitmapFor(const WallpaperLibraryItem& item) {
        fs::path source = item.thumbnail;
        if (source.empty() && item.kind == LibraryWallpaperKind::Image) source = item.source;
        if (source.empty()) return nullptr;
        const std::wstring key = source.wstring() + L"#" + std::to_wstring(CardWidth()) + L"x" + std::to_wstring(CardHeight());
        const auto found = bitmapCache.find(key);
        if (found != bitmapCache.end()) return found->second;
        HBITMAP bitmap = LoadScaledBitmap(source, CardWidth(), CardHeight());
        bitmapCache[key] = bitmap;
        return bitmap;
    }

    void DrawScenePlaceholder(HDC dc, const RECT& rect, const WallpaperLibraryItem& item) const {
        COLORREF base = RGB(24, 32, 48);
        COLORREF accent = RGB(55, 180, 220);
        if (item.id.find(L"aurora") != std::wstring::npos) { base = RGB(8, 18, 38); accent = RGB(42, 210, 170); }
        else if (item.id.find(L"neon") != std::wstring::npos) { base = RGB(10, 8, 28); accent = RGB(214, 55, 225); }
        else if (item.id.find(L"grid") != std::wstring::npos) { base = RGB(26, 31, 38); accent = RGB(60, 125, 150); }
        FillSolid(dc, rect, base);
        HPEN pen = CreatePen(PS_SOLID, 1, accent);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int step = std::max(S(22), 1);
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
        RECT inner = card;
        FillSolid(dc, inner, RGB(255, 255, 255));

        if (HBITMAP bitmap = BitmapFor(item)) {
            HDC memory = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(memory, bitmap);
            BitBlt(dc, card.left, card.top, CardWidth(), CardHeight(), memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
            DeleteDC(memory);
        } else if (item.kind == LibraryWallpaperKind::Scene) {
            DrawScenePlaceholder(dc, card, item);
        } else {
            FillSolid(dc, card, item.kind == LibraryWallpaperKind::Web ? RGB(25, 46, 72) : RGB(45, 45, 50));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(220, 225, 235));
            HGDIOBJ old = SelectObject(dc, cardTitleFont);
            RECT center = card;
            DrawTextW(dc, item.kind == LibraryWallpaperKind::Web ? L"WEB" : L"MEDIA", -1, &center,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
        }

        const int textHeight = S(50);
        RECT textRect{card.left, card.bottom - textHeight, card.right, card.bottom};
        FillSolid(dc, textRect, RGB(24, 24, 28));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        HGDIOBJ old = SelectObject(dc, cardTitleFont);
        RECT titleRect{card.left + S(8), card.bottom - S(46), card.right - S(32), card.bottom - S(25)};
        std::wstring titleText = item.title;
        DrawTextW(dc, titleText.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        SelectObject(dc, cardSmallFont);
        SetTextColor(dc, RGB(190, 195, 205));
        RECT descRect{card.left + S(8), card.bottom - S(25), card.right - S(8), card.bottom - S(5)};
        std::wstring desc = DescriptionFor(item);
        if (SourceMissing(item)) desc += L" · 不可用";
        DrawTextW(dc, desc.c_str(), -1, &descRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        if (item.favorite) {
            SelectObject(dc, cardTitleFont);
            SetTextColor(dc, RGB(255, 215, 64));
            RECT star{card.right - S(28), card.top + S(6), card.right - S(5), card.top + S(30)};
            DrawTextW(dc, L"★", -1, &star, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, old);

        FrameSolid(dc, card, hover ? RGB(130, 130, 140) : RGB(215, 215, 220), 1);
        if (selected) {
            RECT accent{card.left, card.bottom - S(3), card.right, card.bottom};
            FillSolid(dc, accent, GetSysColor(COLOR_HIGHLIGHT));
        }
    }

    void DrawWidgetCard(HDC dc, int index, const RECT& card) {
        if (index < 0 || static_cast<std::size_t>(index) >= visibleWidgets.size()) return;
        const auto& widget = visibleWidgets[static_cast<std::size_t>(index)];
        const bool selected = _wcsicmp(widget.id.c_str(), selectedWidgetId.c_str()) == 0;
        const bool hover = widgetHover == index;
        FillSolid(dc, card, RGB(24, 28, 38));

        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(245, 247, 252));
        RECT preview{card.left + S(10), card.top + S(14), card.right - S(10), card.top + S(78)};
        const bool clock = widget.title.find(L"时钟") != std::wstring::npos;
        DrawTextW(dc, clock ? L"12:34" : L"WEB", -1, &preview, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT textRect{card.left, card.bottom - S(50), card.right, card.bottom};
        FillSolid(dc, textRect, RGB(34, 38, 50));
        SelectObject(dc, cardTitleFont);
        RECT titleRect{card.left + S(8), card.bottom - S(46), card.right - S(8), card.bottom - S(25)};
        DrawTextW(dc, widget.title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        SelectObject(dc, cardSmallFont);
        SetTextColor(dc, widget.enabled ? RGB(105, 220, 150) : RGB(165, 170, 180));
        std::wstring meta = widget.enabled ? L"● 已启用 · " : L"○ 已停用 · ";
        meta += FriendlyMonitor(widget.monitorId);
        RECT metaRect{card.left + S(8), card.bottom - S(25), card.right - S(8), card.bottom - S(5)};
        DrawTextW(dc, meta.c_str(), -1, &metaRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        SelectObject(dc, old);

        FrameSolid(dc, card, hover ? RGB(130, 130, 140) : RGB(75, 80, 92), 1);
        if (selected) {
            RECT accent{card.left, card.bottom - S(3), card.right, card.bottom};
            FillSolid(dc, accent, GetSysColor(COLOR_HIGHLIGHT));
        }
    }

    void PaintGrid(HWND grid, bool widgets) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(grid, &ps);
        RECT client{};
        GetClientRect(grid, &client);
        FillSolid(dc, client, GetSysColor(COLOR_WINDOW));
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
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            HGDIOBJ old = SelectObject(dc, bodyFont);
            std::wstring empty = widgets ? L"还没有小组件。点击下方“新建桌面时钟”开始。" : L"没有找到桌面资源。";
            DrawTextW(dc, empty.c_str(), -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
        }
        EndPaint(grid, &ps);
    }

    void ScrollGrid(HWND grid, bool widgets, int delta) {
        int& offset = GridScrollRef(widgets);
        RECT rc{};
        GetClientRect(grid, &rc);
        const int maxOffset = std::max(0, GridContentHeight(grid, widgets) - (rc.bottom - rc.top));
        offset = std::clamp(offset + delta, 0, maxOffset);
        UpdateGridScroll(grid, widgets);
        InvalidateRect(grid, nullptr, TRUE);
    }

    void UpdateFooter() {
        const bool installed = page == Page::Installed;
        ShowWindow(targetCombo, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(applyButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(favoriteButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(removeButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetCreateButton, installed ? SW_HIDE : SW_SHOW);
        ShowWindow(widgetToggleButton, installed ? SW_HIDE : SW_SHOW);
        ShowWindow(widgetRemoveButton, installed ? SW_HIDE : SW_SHOW);
        ShowWindow(widgetRefreshButton, installed ? SW_HIDE : SW_SHOW);

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
        } else {
            const auto widget = SelectedWidget();
            EnableWindow(widgetToggleButton, widget ? TRUE : FALSE);
            EnableWindow(widgetRemoveButton, widget ? TRUE : FALSE);
            if (!widget) SetStatus(L"小组件独立于壁纸存在。切换壁纸不会删除小组件布局。");
            else {
                std::wostringstream text;
                text << widget->title << L" · " << (widget->enabled ? L"已启用" : L"已停用")
                     << L" · " << FriendlyMonitor(widget->monitorId)
                     << L" · " << widget->x << L", " << widget->y
                     << L" · " << widget->width << L" × " << widget->height;
                SetStatus(text.str());
                SetWindowTextW(widgetToggleButton, widget->enabled ? L"停用" : L"启用");
            }
        }
        Layout();
    }

    void SetPage(Page next) {
        page = next;
        ShowWindow(wallpaperGrid, page == Page::Installed ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetGrid, page == Page::Widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(search, page == Page::Installed ? SW_SHOW : SW_HIDE);
        if (page != Page::Installed) HideWebBar();
        UpdateFooter();
        InvalidateRect(window, nullptr, TRUE);
    }

    void ApplySelected() {
        const auto selected = SelectedWallpaper();
        if (!selected || SourceMissing(*selected)) return;
        if (applyCallback) applyCallback(*selected, SelectedTargetId());
        std::wstring ignored;
        if (library) library->MarkUsed(selected->id, &ignored);
        SetStatus(L"已应用到桌面：" + selected->title);
        RefreshWallpapers();
    }

    void ToggleFavorite() {
        const auto selected = SelectedWallpaper();
        if (!selected || !library) return;
        std::wstring error;
        if (!library->SetFavorite(selected->id, !selected->favorite, &error)) {
            MessageBoxW(window, error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWallpaperId = selected->id;
        RefreshWallpapers();
    }

    void RemoveSelected() {
        const auto selected = SelectedWallpaper();
        if (!selected || !library || selected->kind == LibraryWallpaperKind::Scene) return;
        if (MessageBoxW(window, (L"从桌面库移除“" + selected->title + L"”？").c_str(),
                        L"图灵智能桌面", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        std::wstring error;
        if (!library->Remove(selected->id, selected->managedCopy, &error)) {
            MessageBoxW(window, error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWallpaperId.clear();
        ClearBitmaps();
        RefreshWallpapers();
    }

    void ImportPath(const fs::path& path) {
        if (!library) return;
        std::wstring error;
        auto imported = library->ImportFile(path, {}, &error);
        if (!imported) {
            MessageBoxW(window, error.empty() ? L"导入失败。" : error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWallpaperId = imported->id;
        ClearBitmaps();
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
            MessageBoxW(window, error.empty() ? L"Web URL 导入失败。" : error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
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

    void CreateClockWidget() {
        static constexpr std::string_view html = R"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;display:flex;flex-direction:column;justify-content:center;padding:18px 22px;border-radius:22px;background:rgba(18,24,38,.78);box-shadow:0 10px 30px rgba(0,0,0,.28)}
#time{font-size:44px;font-weight:650;letter-spacing:-1px;line-height:1}#date{margin-top:10px;font-size:16px;opacity:.78}
</style></head><body><div class="card"><div id="time"></div><div id="date"></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});document.getElementById('date').textContent=d.toLocaleDateString([], {weekday:'long',year:'numeric',month:'long',day:'numeric'});}tick();setInterval(tick,1000);
</script></body></html>)HTML";
        std::wstring monitor;
        for (const auto& target : targets) if (target.primary) { monitor = target.monitorId; break; }
        if (monitor.empty() && !targets.empty()) monitor = targets.front().monitorId;
        DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) {
            MessageBoxW(window, error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
            return;
        }
        auto created = store.CreateManagedWeb(L"桌面时钟", html, monitor, 0.72f, 0.05f, 0.23f, 0.16f, &error);
        if (!created) {
            MessageBoxW(window, error.empty() ? L"创建小组件失败。" : error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWidgetId = created->id;
        RefreshWidgets();
        SetStatus(L"已创建桌面时钟。正在等待桌面运行时显示。 ");
    }

    void ToggleWidget() {
        const auto current = SelectedWidget();
        if (!current) return;
        DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) return;
        auto widget = *current;
        widget.enabled = !widget.enabled;
        if (!store.Upsert(widget, &error)) {
            MessageBoxW(window, error.empty() ? L"更新小组件失败。" : error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWidgetId = widget.id;
        RefreshWidgets();
    }

    void RemoveWidget() {
        const auto current = SelectedWidget();
        if (!current) return;
        if (MessageBoxW(window, (L"删除小组件“" + current->title + L"”？").c_str(), L"图灵智能桌面",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error) || !store.Remove(current->id, true, &error)) {
            MessageBoxW(window, error.empty() ? L"删除小组件失败。" : error.c_str(), L"图灵智能桌面", MB_OK | MB_ICONERROR);
            return;
        }
        selectedWidgetId.clear();
        RefreshWidgets();
    }

    void HandleNav(int id) {
        if (id == kNavInstalledId) { SetPage(Page::Installed); return; }
        if (id == kNavWidgetsId) { RefreshWidgets(); SetPage(Page::Widgets); return; }
        if (navigateCallback) navigateCallback(SectionForNav(id));
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
        const int width = std::max(1L, rc.right - rc.left);
        const int height = std::max(1L, rc.bottom - rc.top);
        const int margin = S(12);
        const int headerH = S(48);
        const int navH = S(46);
        const int footerH = S(58);
        const int webH = webBarVisible && page == Page::Installed ? S(46) : 0;

        MoveWindow(title, margin, S(12), S(190), S(26), TRUE);
        const int searchW = std::clamp(width * 36 / 100, S(260), S(420));
        MoveWindow(search, (width - searchW) / 2, S(8), searchW, S(32), TRUE);
        MoveWindow(addButton, width - margin - S(92), S(7), S(92), S(34), TRUE);

        const int navTop = headerH;
        int x = margin;
        const std::array<int, 7> widths{S(76), S(76), S(88), S(66), S(88), S(66), S(82)};
        for (std::size_t i = 0; i < nav.size(); ++i) {
            MoveWindow(nav[i], x, navTop + S(5), widths[i], S(35), TRUE);
            x += widths[i] + S(4);
        }

        if (webBarVisible && page == Page::Installed) {
            const int webTop = headerH + navH;
            MoveWindow(webUrl, margin, webTop + S(6), std::max(S(240), width - margin * 2 - S(210)), S(32), TRUE);
            MoveWindow(webConfirm, width - margin - S(200), webTop + S(6), S(94), S(32), TRUE);
            MoveWindow(webCancel, width - margin - S(98), webTop + S(6), S(98), S(32), TRUE);
        }

        const int contentTop = headerH + navH + webH;
        const int contentBottom = std::max(contentTop, height - footerH);
        const int contentH = std::max(1, contentBottom - contentTop);
        MoveWindow(wallpaperGrid, 0, contentTop, width, contentH, TRUE);
        MoveWindow(widgetGrid, 0, contentTop, width, contentH, TRUE);
        UpdateGridScroll(wallpaperGrid, false);
        UpdateGridScroll(widgetGrid, true);

        const int footerTop = height - footerH;
        MoveWindow(status, margin, footerTop + S(17), std::max(S(220), width - S(620)), S(28), TRUE);
        if (page == Page::Installed) {
            const int targetW = S(180);
            const int actionW = S(106);
            const int smallW = S(90);
            const int right = width - margin;
            MoveWindow(applyButton, right - actionW, footerTop + S(11), actionW, S(36), TRUE);
            MoveWindow(removeButton, right - actionW - S(8) - smallW, footerTop + S(11), smallW, S(36), TRUE);
            MoveWindow(favoriteButton, right - actionW - S(16) - smallW * 2, footerTop + S(11), smallW, S(36), TRUE);
            MoveWindow(targetCombo, right - actionW - S(24) - smallW * 2 - targetW, footerTop + S(11), targetW, S(180), TRUE);
        } else {
            const int right = width - margin;
            MoveWindow(widgetRemoveButton, right - S(84), footerTop + S(11), S(84), S(36), TRUE);
            MoveWindow(widgetToggleButton, right - S(176), footerTop + S(11), S(84), S(36), TRUE);
            MoveWindow(widgetRefreshButton, right - S(268), footerTop + S(11), S(84), S(36), TRUE);
            MoveWindow(widgetCreateButton, right - S(430), footerTop + S(11), S(154), S(36), TRUE);
        }
    }

    LRESULT DrawNavButton(const DRAWITEMSTRUCT* draw) {
        if (!draw) return FALSE;
        const bool active = (page == Page::Installed && draw->CtlID == kNavInstalledId) ||
                            (page == Page::Widgets && draw->CtlID == kNavWidgetsId);
        FillSolid(draw->hDC, draw->rcItem, GetSysColor(COLOR_WINDOW));
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, GetSysColor(COLOR_WINDOWTEXT));
        HGDIOBJ old = SelectObject(draw->hDC, bodyFont);
        wchar_t text[64]{};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
        RECT label = draw->rcItem;
        DrawTextW(draw->hDC, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (active) {
            RECT line{draw->rcItem.left + S(10), draw->rcItem.bottom - S(3), draw->rcItem.right - S(10), draw->rcItem.bottom};
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
        case WM_PAINT: self->PaintGrid(hwnd, widgets); return 0;
        case WM_SIZE: self->UpdateGridScroll(hwnd, widgets); InvalidateRect(hwnd, nullptr, TRUE); return 0;
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
            if (!widgets) {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const int hit = self->HitTest(hwnd, pt, false);
                if (hit >= 0) {
                    self->selectedWallpaperId = self->visibleWallpapers[static_cast<std::size_t>(hit)].id;
                    self->UpdateFooter();
                    POINT screen = pt;
                    ClientToScreen(hwnd, &screen);
                    self->ShowWallpaperContextMenu(screen);
                }
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
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (info) {
                info->ptMinTrackSize.x = self->S(760);
                info->ptMinTrackSize.y = self->S(620);
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
            self->ClearBitmaps();
            self->RebuildFonts();
            self->ApplyFonts();
            self->Layout();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_SIZE: self->Layout(); return 0;
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
            else if (id == kWebConfirmId && notification == BN_CLICKED) self->ImportWeb();
            else if (id == kWebCancelId && notification == BN_CLICKED) self->HideWebBar();
            else if (id == kMenuImportFile) self->ImportFile();
            else if (id == kMenuImportWeb) self->ShowWebBar();
            else if (id == kMenuApply) self->ApplySelected();
            else if (id == kMenuFavorite) self->ToggleFavorite();
            else if (id == kMenuRemove) self->RemoveSelected();
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
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            FillSolid(dc, client, GetSysColor(COLOR_WINDOW));
            const int headerH = self->S(48);
            const int navH = self->S(46);
            const int footerH = self->S(58);
            RECT header{0, 0, client.right, headerH};
            RECT navBand{0, headerH, client.right, headerH + navH};
            RECT footer{0, std::max(0L, client.bottom - footerH), client.right, client.bottom};
            FillSolid(dc, header, GetSysColor(COLOR_WINDOW));
            FillSolid(dc, navBand, GetSysColor(COLOR_WINDOW));
            FillSolid(dc, footer, RGB(248, 248, 250));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(225, 225, 230));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            MoveToEx(dc, 0, headerH + navH - 1, nullptr); LineTo(dc, client.right, headerH + navH - 1);
            MoveToEx(dc, 0, footer.top, nullptr); LineTo(dc, client.right, footer.top);
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

        window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"图灵智能桌面",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, S(1120), S(780),
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

        title = label(L"图灵智能桌面", titleFont);
        search = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kSearchId), instance, nullptr), bodyFont);
        SendMessageW(search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索桌面"));
        addButton = button(L"＋ 添加", kAddId);

        const std::array<const wchar_t*, 7> navLabels{L"桌面库", L"小组件", L"播放列表", L"多屏", L"应用规则", L"性能", L"图灵 AI"};
        const std::array<int, 7> navIds{kNavInstalledId, kNavWidgetsId, kNavPlaylistsId, kNavDisplaysId, kNavRulesId, kNavPerformanceId, kNavAiId};
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

        widgetCreateButton = button(L"＋ 新建桌面时钟", kWidgetCreateId, 0, false);
        widgetToggleButton = button(L"启用 / 停用", kWidgetToggleId, 0, false);
        widgetRemoveButton = button(L"删除", kWidgetRemoveId, 0, false);
        widgetRefreshButton = button(L"刷新", kWidgetRefreshId, 0, false);

        webUrl = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kWebUrlId), instance, nullptr), bodyFont);
        SendMessageW(webUrl, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"https://example.com/wallpaper"));
        webConfirm = button(L"添加 Web", kWebConfirmId, 0, false);
        webCancel = button(L"取消", kWebCancelId, 0, false);

        ApplyFonts();
        RebuildTargets();
        RefreshWallpapers();
        RefreshWidgets();
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
    impl_->RebuildTargets();
    impl_->RefreshWallpapers();
    impl_->RefreshWidgets();
    impl_->SetPage(impl_->page);
    ShowWindow(impl_->window, SW_SHOWNORMAL);
    SetForegroundWindow(impl_->window);
    return true;
}

void WallpaperLibraryWindow::SetTargets(const std::vector<WallpaperLibraryTarget>& targets) {
    impl_->targets = targets;
    impl_->RebuildTargets();
    impl_->RefreshWidgets();
}

void WallpaperLibraryWindow::Close() {
    if (impl_->window) ShowWindow(impl_->window, SW_HIDE);
}

void WallpaperLibraryWindow::Refresh() {
    impl_->RebuildTargets();
    impl_->ClearBitmaps();
    impl_->RefreshWallpapers();
    impl_->RefreshWidgets();
}

bool WallpaperLibraryWindow::Visible() const noexcept {
    return impl_->window && IsWindowVisible(impl_->window);
}

HWND WallpaperLibraryWindow::Window() const noexcept {
    return impl_->window;
}

} // namespace turingdesk::wallpaper
