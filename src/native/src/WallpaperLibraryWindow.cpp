#include "turingdesk/WallpaperLibraryWindow.h"
#include "turingdesk/WallpaperWebRuntimeCoordinator.h"
#include "turingdesk/WebWallpaperHost.h"

#include <commctrl.h>
#include <commdlg.h>

#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kWindowClass[] = L"TuringDesk.Native.DesktopLibrary";
constexpr int kSearchId = 5101;
constexpr int kListId = 5102;
constexpr int kImportId = 5106;
constexpr int kFavoriteId = 5108;
constexpr int kRemoveId = 5109;
constexpr int kApplyId = 5110;
constexpr int kStatusId = 5112;
constexpr int kTargetComboId = 5113;
constexpr int kWebUrlId = 5114;
constexpr int kImportWebUrlId = 5115;
constexpr int kTabsId = 5116;

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

const wchar_t* KindLabel(LibraryWallpaperKind kind) {
    switch (kind) {
    case LibraryWallpaperKind::Image: return L"图片";
    case LibraryWallpaperKind::Video: return L"视频";
    case LibraryWallpaperKind::Web: return L"Web";
    case LibraryWallpaperKind::Scene: return L"Scene";
    case LibraryWallpaperKind::Unknown: break;
    }
    return L"未知";
}

int KindImageIndex(LibraryWallpaperKind kind) {
    switch (kind) {
    case LibraryWallpaperKind::Image: return 1;
    case LibraryWallpaperKind::Video: return 2;
    case LibraryWallpaperKind::Web: return 3;
    case LibraryWallpaperKind::Scene: return 0;
    case LibraryWallpaperKind::Unknown: break;
    }
    return 0;
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

std::wstring DescriptionFor(const WallpaperLibraryItem& item) {
    if (item.kind == LibraryWallpaperKind::Scene) {
        if (_wcsicmp(item.id.c_str(), L"scene-aurora") == 0) return L"柔和流动的极光光带，默认桌面。";
        if (_wcsicmp(item.id.c_str(), L"scene-neon") == 0) return L"赛博霓虹与网格流光。";
        if (_wcsicmp(item.id.c_str(), L"scene-grid") == 0) return L"深色网格与缓慢脉冲。";
        return L"TuringDesk Scene 桌面。";
    }
    if (item.kind == LibraryWallpaperKind::Video) return L"视频壁纸。全屏应用时会遵循性能策略。";
    if (item.kind == LibraryWallpaperKind::Web) return L"Web 壁纸。远程地址仅允许 HTTPS。";
    if (item.kind == LibraryWallpaperKind::Image) return L"图片壁纸。可按当前多屏布局和缩放规则应用。";
    return L"桌面资源。";
}

HBITMAP CreatePreviewBitmap(LibraryWallpaperKind kind) {
    constexpr int width = 196;
    constexpr int height = 92;
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(nullptr, screen);
    if (!dc || !bitmap) {
        if (dc) DeleteDC(dc);
        if (bitmap) DeleteObject(bitmap);
        return nullptr;
    }

    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    COLORREF background = RGB(239, 243, 251);
    COLORREF accent = RGB(186, 201, 232);
    if (kind == LibraryWallpaperKind::Video) {
        background = RGB(240, 239, 249);
        accent = RGB(198, 190, 230);
    } else if (kind == LibraryWallpaperKind::Web) {
        background = RGB(238, 247, 246);
        accent = RGB(180, 217, 211);
    } else if (kind == LibraryWallpaperKind::Image) {
        background = RGB(244, 244, 238);
        accent = RGB(216, 210, 178);
    }

    RECT rect{0, 0, width, height};
    HBRUSH fill = CreateSolidBrush(background);
    FillRect(dc, &rect, fill);
    DeleteObject(fill);

    HPEN pen = CreatePen(PS_SOLID, 1, accent);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 2, 2, width - 2, height - 2, 10, 10);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(91, 111, 151));
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT textRect{14, 52, width - 12, height - 10};
    DrawTextW(dc, KindLabel(kind), -1, &textRect, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);

    SelectObject(dc, oldBitmap);
    DeleteDC(dc);
    return bitmap;
}

WallpaperSettingsSection SectionForTab(int index) {
    switch (index) {
    case 1: return WallpaperSettingsSection::Playlists;
    case 2: return WallpaperSettingsSection::Displays;
    case 3: return WallpaperSettingsSection::Rules;
    case 4: return WallpaperSettingsSection::Performance;
    case 5: return WallpaperSettingsSection::AI;
    default: return WallpaperSettingsSection::Installed;
    }
}

} // namespace

struct WallpaperLibraryWindow::Impl {
    HINSTANCE instance{};
    HWND window{};
    HWND tabs{};
    HWND headerTitle{};
    HWND headerSubtitle{};
    HWND importButton{};
    HWND pageTitle{};
    HWND pageSubtitle{};
    HWND search{};
    HWND list{};
    HWND detailFrame{};
    HWND selectedTitle{};
    HWND selectedMeta{};
    HWND selectedDescription{};
    HWND targetLabel{};
    HWND targetCombo{};
    HWND applyButton{};
    HWND favoriteButton{};
    HWND removeButton{};
    HWND webLabel{};
    HWND webUrl{};
    HWND importWebButton{};
    HWND status{};
    HIMAGELIST previewImages{};
    WallpaperLibrary* library{};
    ApplyCallback applyCallback;
    NavigateCallback navigateCallback;
    std::vector<std::wstring> visibleIds;
    std::vector<WallpaperLibraryTarget> targets;
    std::vector<std::wstring> targetIds;
    HFONT titleFont{};
    HFONT pageTitleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};

    ~Impl() {
        if (window && IsWindow(window)) DestroyWindow(window);
        DestroyResources();
    }

    void DestroyResources() {
        if (previewImages) { ImageList_Destroy(previewImages); previewImages = nullptr; }
        if (titleFont) { DeleteObject(titleFont); titleFont = nullptr; }
        if (pageTitleFont) { DeleteObject(pageTitleFont); pageTitleFont = nullptr; }
        if (bodyFont) { DeleteObject(bodyFont); bodyFont = nullptr; }
        if (smallFont) { DeleteObject(smallFont); smallFont = nullptr; }
    }

    void SetStatus(const std::wstring& text) const {
        if (status) SetWindowTextW(status, text.c_str());
    }

    std::optional<WallpaperLibraryItem> Selected() const {
        if (!library || !list) return std::nullopt;
        const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        if (selected < 0 || static_cast<std::size_t>(selected) >= visibleIds.size()) return std::nullopt;
        return library->Find(visibleIds[static_cast<std::size_t>(selected)]);
    }

    std::wstring SelectedTargetId() const {
        if (!targetCombo) return {};
        const LRESULT selected = SendMessageW(targetCombo, CB_GETCURSEL, 0, 0);
        if (selected == CB_ERR || selected < 0 || static_cast<std::size_t>(selected) >= targetIds.size()) return {};
        return targetIds[static_cast<std::size_t>(selected)];
    }

    std::wstring TargetDisplayName(std::wstring_view id) const {
        if (id.empty()) return L"全局壁纸";
        for (const auto& target : targets) {
            if (_wcsicmp(target.monitorId.c_str(), std::wstring(id).c_str()) == 0)
                return target.displayName.empty() ? L"显示器" : target.displayName;
        }
        return L"显示器";
    }

    bool SourceMissing(const WallpaperLibraryItem& item) const {
        if (item.kind == LibraryWallpaperKind::Scene) return false;
        if (item.kind == LibraryWallpaperKind::Web)
            return !WebWallpaperProcessSet::IsSupportedSource(item.source.wstring());
        if (item.source.empty()) return true;
        std::error_code ec;
        return !fs::exists(item.source, ec) || !fs::is_regular_file(item.source, ec);
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

    void RebuildList() {
        if (!library || !list) return;
        const auto selectedBefore = Selected();
        const std::wstring previous = selectedBefore ? selectedBefore->id : L"";
        ListView_DeleteAllItems(list);
        visibleIds.clear();

        const auto items = library->Search(WindowText(search));
        int restore = -1;
        int index = 0;
        for (const auto& item : items) {
            std::wstring title = item.title;
            if (item.favorite) title = L"★ " + title;
            if (SourceMissing(item)) title += L" · 不可用";

            LVITEMW lv{};
            lv.mask = LVIF_TEXT | LVIF_IMAGE;
            lv.iItem = index;
            lv.pszText = title.data();
            lv.iImage = KindImageIndex(item.kind);
            ListView_InsertItem(list, &lv);
            visibleIds.push_back(item.id);
            if (!previous.empty() && _wcsicmp(previous.c_str(), item.id.c_str()) == 0) restore = index;
            ++index;
        }

        if (restore < 0 && !visibleIds.empty()) restore = 0;
        if (restore >= 0) {
            ListView_SetItemState(list, restore, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, restore, FALSE);
        }
        UpdateSelectionActions();
        SetStatus(L"已安装 " + std::to_wstring(library->Items().size()) + L" 个桌面资源");
    }

    void UpdateSelectionActions() {
        const auto selected = Selected();
        if (!selected) {
            if (selectedTitle) SetWindowTextW(selectedTitle, L"选择一个桌面");
            if (selectedMeta) SetWindowTextW(selectedMeta, L"");
            if (selectedDescription) SetWindowTextW(selectedDescription, L"从左边选择一个桌面，然后直接应用。");
            if (applyButton) EnableWindow(applyButton, FALSE);
            if (favoriteButton) EnableWindow(favoriteButton, FALSE);
            if (removeButton) EnableWindow(removeButton, FALSE);
            return;
        }

        if (selectedTitle) SetWindowTextW(selectedTitle, selected->title.c_str());
        std::wstring meta = KindLabel(selected->kind);
        meta += L" · TuringDesk";
        if (selectedMeta) SetWindowTextW(selectedMeta, meta.c_str());
        std::wstring description = DescriptionFor(*selected);
        if (!selected->source.empty() && selected->kind != LibraryWallpaperKind::Scene)
            description += L"\r\n\r\n" + selected->source.wstring();
        if (selectedDescription) SetWindowTextW(selectedDescription, description.c_str());
        if (favoriteButton) {
            SetWindowTextW(favoriteButton, selected->favorite ? L"取消收藏" : L"收藏");
            EnableWindow(favoriteButton, TRUE);
        }
        if (applyButton) EnableWindow(applyButton, SourceMissing(*selected) ? FALSE : TRUE);
        if (removeButton) EnableWindow(removeButton, selected->kind == LibraryWallpaperKind::Scene ? FALSE : TRUE);
    }

    void FinishImport(const WallpaperLibraryItem& imported, const std::wstring& message) {
        if (search) SetWindowTextW(search, L"");
        RebuildList();
        for (std::size_t i = 0; i < visibleIds.size(); ++i) {
            if (_wcsicmp(visibleIds[i].c_str(), imported.id.c_str()) == 0) {
                ListView_SetItemState(list, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(list, static_cast<int>(i), FALSE);
                break;
            }
        }
        UpdateSelectionActions();
        SetStatus(message);
    }

    void ImportFile() {
        if (!library) return;
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFile = path;
        dialog.nMaxFile = static_cast<DWORD>(std::size(path));
        dialog.lpstrFilter =
            L"壁纸文件\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.tif;*.tiff;*.mp4;*.mov;*.wmv;*.m4v;*.avi;*.mkv;*.webm;*.html;*.htm\0"
            L"图片\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.tif;*.tiff\0"
            L"视频\0*.mp4;*.mov;*.wmv;*.m4v;*.avi;*.mkv;*.webm\0"
            L"本地 Web\0*.html;*.htm\0所有文件\0*.*\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;

        WallpaperImportOptions options;
        options.managedCopy = true;
        std::wstring error;
        const auto imported = library->ImportFile(path, options, &error);
        if (!imported) {
            SetStatus(error.empty() ? L"导入失败。" : error);
            return;
        }
        FinishImport(*imported, L"已导入：" + imported->title);
    }

    void ImportWebUrl() {
        if (!library || !webUrl) return;
        const std::wstring url = WindowText(webUrl);
        if (url.empty()) {
            SetStatus(L"请粘贴 HTTPS Web 壁纸地址。");
            return;
        }
        std::wstring error;
        const auto imported = library->ImportWebUrl(url, {}, &error);
        if (!imported) {
            SetStatus(error.empty() ? L"Web URL 导入失败。" : error);
            return;
        }
        SetWindowTextW(webUrl, L"");
        FinishImport(*imported, L"已导入 HTTPS Web 壁纸：" + imported->title);
    }

    void ToggleFavorite() {
        if (!library) return;
        const auto selected = Selected();
        if (!selected) return;
        std::wstring error;
        if (!library->SetFavorite(selected->id, !selected->favorite, &error)) {
            SetStatus(error.empty() ? L"收藏状态保存失败。" : error);
            return;
        }
        RebuildList();
    }

    void RemoveSelected() {
        if (!library) return;
        const auto selected = Selected();
        if (!selected || selected->kind == LibraryWallpaperKind::Scene) return;
        std::wstring error;
        if (!library->Remove(selected->id, false, &error)) {
            SetStatus(error.empty() ? L"删除库记录失败。" : error);
            return;
        }
        RebuildList();
        SetStatus(L"已从壁纸库移除记录；原文件未删除。");
    }

    void ApplySelected() {
        if (!library) return;
        const auto selected = Selected();
        if (!selected) {
            SetStatus(L"请先选择一个壁纸。");
            return;
        }
        if (selected->kind == LibraryWallpaperKind::Unknown || SourceMissing(*selected)) {
            SetStatus(L"当前资源不可应用。");
            return;
        }

        const std::wstring targetId = SelectedTargetId();
        std::wstring error;
        if (selected->kind == LibraryWallpaperKind::Web) {
            if (!ActivateWebWallpaperItem(*selected, targetId, &error)) {
                SetStatus(error.empty() ? L"Web 壁纸应用失败。" : error);
                return;
            }
        } else {
            if (!applyCallback) {
                SetStatus(L"壁纸运行时没有提供应用回调。");
                return;
            }
            applyCallback(*selected, targetId);
        }

        std::wstring markError;
        library->MarkUsed(selected->id, &markError);
        RebuildList();
        SetStatus(markError.empty()
            ? L"已应用到 " + TargetDisplayName(targetId) + L"：" + selected->title
            : L"壁纸已应用，但最近使用记录保存失败：" + markError);
    }

    void NavigateToTab(int index) {
        if (index <= 0) return;
        if (navigateCallback) navigateCallback(SectionForTab(index));
        if (tabs) TabCtrl_SetCurSel(tabs, 0);
    }

    void Layout() {
        if (!window) return;
        RECT rc{};
        GetClientRect(window, &rc);
        const int width = std::max(900L, rc.right - rc.left);
        const int height = std::max(620L, rc.bottom - rc.top);
        constexpr int margin = 18;
        constexpr int headerHeight = 94;
        constexpr int tabsHeight = 42;
        const int contentTop = headerHeight + tabsHeight + 18;
        constexpr int detailWidth = 292;
        constexpr int gap = 18;
        const int detailX = width - margin - detailWidth;
        const int libraryWidth = std::max(420, detailX - gap - margin);

        MoveWindow(headerTitle, margin, 16, 420, 32, TRUE);
        MoveWindow(headerSubtitle, margin, 50, 560, 24, TRUE);
        MoveWindow(importButton, width - margin - 126, 22, 126, 42, TRUE);
        MoveWindow(tabs, 0, headerHeight, width, tabsHeight, TRUE);

        MoveWindow(pageTitle, margin, contentTop, 260, 34, TRUE);
        MoveWindow(pageSubtitle, margin, contentTop + 36, 520, 24, TRUE);
        MoveWindow(search, margin + libraryWidth - 210, contentTop + 6, 210, 34, TRUE);
        MoveWindow(list, margin, contentTop + 72, libraryWidth, height - (contentTop + 72) - 44, TRUE);

        MoveWindow(detailFrame, detailX, contentTop, detailWidth, height - contentTop - 44, TRUE);
        MoveWindow(selectedTitle, detailX + 18, contentTop + 22, detailWidth - 36, 32, TRUE);
        MoveWindow(selectedMeta, detailX + 18, contentTop + 56, detailWidth - 36, 24, TRUE);
        MoveWindow(selectedDescription, detailX + 18, contentTop + 98, detailWidth - 36, 102, TRUE);
        MoveWindow(targetLabel, detailX + 18, contentTop + 214, detailWidth - 36, 22, TRUE);
        MoveWindow(targetCombo, detailX + 18, contentTop + 238, detailWidth - 36, 180, TRUE);
        MoveWindow(applyButton, detailX + 18, contentTop + 282, detailWidth - 36, 40, TRUE);
        MoveWindow(favoriteButton, detailX + 18, contentTop + 332, 118, 34, TRUE);
        MoveWindow(removeButton, detailX + 146, contentTop + 332, detailWidth - 164, 34, TRUE);
        MoveWindow(webLabel, detailX + 18, contentTop + 388, detailWidth - 36, 22, TRUE);
        MoveWindow(webUrl, detailX + 18, contentTop + 414, detailWidth - 36, 30, TRUE);
        MoveWindow(importWebButton, detailX + 18, contentTop + 452, detailWidth - 36, 34, TRUE);
        MoveWindow(status, margin, height - 30, width - margin * 2, 24, TRUE);
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
        case WM_SIZE:
            self->Layout();
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            if (id == kSearchId && notification == EN_CHANGE) self->RebuildList();
            else if (id == kImportId && notification == BN_CLICKED) self->ImportFile();
            else if (id == kImportWebUrlId && notification == BN_CLICKED) self->ImportWebUrl();
            else if (id == kFavoriteId && notification == BN_CLICKED) self->ToggleFavorite();
            else if (id == kRemoveId && notification == BN_CLICKED) self->RemoveSelected();
            else if (id == kApplyId && notification == BN_CLICKED) self->ApplySelected();
            return 0;
        }
        case WM_NOTIFY: {
            const auto* note = reinterpret_cast<NMHDR*>(lParam);
            if (!note) break;
            if (note->idFrom == kTabsId && note->code == TCN_SELCHANGE) {
                self->NavigateToTab(TabCtrl_GetCurSel(self->tabs));
                return 0;
            }
            if (note->idFrom == kListId && note->code == LVN_ITEMCHANGED) {
                self->UpdateSelectionActions();
                return 0;
            }
            if (note->idFrom == kListId && note->code == NM_DBLCLK) {
                self->ApplySelected();
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            self->window = nullptr;
            self->tabs = nullptr;
            self->search = nullptr;
            self->list = nullptr;
            self->targetCombo = nullptr;
            self->webUrl = nullptr;
            self->favoriteButton = nullptr;
            self->removeButton = nullptr;
            self->applyButton = nullptr;
            self->status = nullptr;
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool CreateWindowUi() {
        INITCOMMONCONTROLSEX common{};
        common.dwSize = sizeof(common);
        common.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
        InitCommonControlsEx(&common);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &Impl::WndProc;
        wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"TuringDesk 设置",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 1240, 800,
                                 nullptr, nullptr, instance, this);
        if (!window) return false;

        titleFont = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        pageTitleFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        bodyFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        smallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        auto font = [&](HWND control, HFONT use) {
            if (control && use) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(use), TRUE);
            return control;
        };
        auto label = [&](const wchar_t* text, DWORD style, HFONT use) {
            return font(CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style,
                                        0, 0, 10, 10, window, nullptr, instance, nullptr), use);
        };
        auto button = [&](const wchar_t* text, int id, DWORD extra = 0) {
            return font(CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra,
                                        0, 0, 10, 10, window, ControlId(id), instance, nullptr), bodyFont);
        };

        headerTitle = label(L"桌面设置", 0, titleFont);
        headerSubtitle = label(L"场景、播放列表、多屏、应用规则、性能和 AI", 0, smallFont);
        importButton = button(L"导入壁纸", kImportId, BS_DEFPUSHBUTTON);

        tabs = font(CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    0, 0, 10, 10, window, ControlId(kTabsId), instance, nullptr), bodyFont);
        for (const wchar_t* tabText : {L"已安装", L"播放列表", L"多屏配置", L"应用规则", L"性能", L"AI"}) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(tabText);
            TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &item);
        }
        TabCtrl_SetCurSel(tabs, 0);

        pageTitle = label(L"你的桌面", 0, pageTitleFont);
        pageSubtitle = label(L"把图片、视频、HTML 或 .tdwall 拖进窗口即可导入。", 0, smallFont);
        search = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kSearchId), instance, nullptr), bodyFont);
        SendMessageW(search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索桌面"));

        list = font(CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_AUTOARRANGE,
                                   0, 0, 10, 10, window, ControlId(kListId), instance, nullptr), bodyFont);
        ListView_SetExtendedListViewStyle(list, LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_BORDERSELECT);
        ListView_SetIconSpacing(list, 226, 154);

        previewImages = ImageList_Create(196, 92, ILC_COLOR32, 4, 1);
        for (LibraryWallpaperKind kind : {LibraryWallpaperKind::Scene, LibraryWallpaperKind::Image,
                                          LibraryWallpaperKind::Video, LibraryWallpaperKind::Web}) {
            HBITMAP bitmap = CreatePreviewBitmap(kind);
            if (bitmap) {
                ImageList_Add(previewImages, bitmap, nullptr);
                DeleteObject(bitmap);
            }
        }
        if (previewImages) ListView_SetImageList(list, previewImages, LVSIL_NORMAL);

        detailFrame = label(L"", SS_WHITEFRAME, bodyFont);
        selectedTitle = label(L"选择一个桌面", 0, pageTitleFont);
        selectedMeta = label(L"", 0, smallFont);
        selectedDescription = label(L"从左边选择一个桌面，然后直接应用。", SS_LEFT, bodyFont);
        targetLabel = label(L"应用目标", 0, smallFont);
        targetCombo = font(CreateWindowExW(0, L"COMBOBOX", L"",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                           0, 0, 10, 10, window, ControlId(kTargetComboId), instance, nullptr), bodyFont);
        applyButton = button(L"应用到桌面", kApplyId, BS_DEFPUSHBUTTON);
        favoriteButton = button(L"收藏", kFavoriteId);
        removeButton = button(L"移出库", kRemoveId);
        webLabel = label(L"HTTPS Web 壁纸", 0, smallFont);
        webUrl = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kWebUrlId), instance, nullptr), bodyFont);
        SendMessageW(webUrl, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"https://example.com/wallpaper"));
        importWebButton = button(L"导入 Web URL", kImportWebUrlId);
        status = label(L"", 0, smallFont);

        RebuildTargets();
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
    if (!impl_ || !library) return false;
    impl_->instance = instance;
    impl_->library = library;
    impl_->targets = targets;
    impl_->applyCallback = std::move(applyCallback);
    impl_->navigateCallback = std::move(navigateCallback);
    if (!impl_->window || !IsWindow(impl_->window)) {
        if (!impl_->CreateWindowUi()) return false;
    } else {
        impl_->RebuildTargets();
    }
    impl_->RebuildList();
    ShowWindow(impl_->window, SW_SHOWNORMAL);
    SetForegroundWindow(impl_->window);
    return true;
}

void WallpaperLibraryWindow::SetTargets(const std::vector<WallpaperLibraryTarget>& targets) {
    if (!impl_) return;
    impl_->targets = targets;
    if (impl_->window && IsWindow(impl_->window)) impl_->RebuildTargets();
}

void WallpaperLibraryWindow::Close() {
    if (impl_ && impl_->window && IsWindow(impl_->window)) ShowWindow(impl_->window, SW_HIDE);
}

void WallpaperLibraryWindow::Refresh() {
    if (impl_ && impl_->window && IsWindow(impl_->window)) impl_->RebuildList();
}

bool WallpaperLibraryWindow::Visible() const noexcept {
    return impl_ && impl_->window && IsWindowVisible(impl_->window) != FALSE;
}

HWND WallpaperLibraryWindow::Window() const noexcept {
    return impl_ ? impl_->window : nullptr;
}

} // namespace turingdesk::wallpaper
