#include "turingdesk/WallpaperLibraryWindow.h"
#include "turingdesk/L3Agent.h"
#include "turingdesk/WallpaperWebRuntimeCoordinator.h"
#include "turingdesk/WebWallpaperHost.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kWindowClass[] = L"TuringDesk.Native.DesktopLibrary";
constexpr wchar_t kSavedKeyMask[] = L"********";

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

constexpr int kAiUrlId = 5201;
constexpr int kAiKeyId = 5202;
constexpr int kAiModelId = 5203;
constexpr int kAiProbeId = 5204;
constexpr int kAiSaveId = 5205;
constexpr int kAiHarnessId = 5206;
constexpr int kAiClearKeyId = 5207;

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

fs::path ModuleDirectory() {
    wchar_t modulePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (length == 0 || length >= std::size(modulePath)) return {};
    return fs::path(std::wstring(modulePath, length)).parent_path();
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

std::wstring DescriptionFor(const WallpaperLibraryItem& item) {
    if (item.kind == LibraryWallpaperKind::Scene) {
        if (_wcsicmp(item.id.c_str(), L"scene-aurora") == 0) return L"柔和流动的极光光带，默认桌面。";
        if (_wcsicmp(item.id.c_str(), L"scene-neon") == 0) return L"赛博霓虹与网格流光。";
        if (_wcsicmp(item.id.c_str(), L"scene-grid") == 0) return L"深色网格与缓慢脉冲。";
        return L"TuringDesk Scene 桌面。";
    }
    if (item.kind == LibraryWallpaperKind::Video) return L"视频壁纸。全屏时遵循性能策略。";
    if (item.kind == LibraryWallpaperKind::Web) return L"Web 壁纸。远程地址仅允许 HTTPS。";
    if (item.kind == LibraryWallpaperKind::Image) return L"图片壁纸。支持当前多屏布局与缩放规则。";
    return L"桌面资源。";
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

    HWND aiUrlLabel{};
    HWND aiUrl{};
    HWND aiKeyLabel{};
    HWND aiKey{};
    HWND aiModelLabel{};
    HWND aiModel{};
    HWND aiProviderLabel{};
    HWND aiProviderValue{};
    HWND aiProbeButton{};
    HWND aiSaveButton{};
    HWND aiClearKeyButton{};
    HWND aiStatus{};
    HWND aiRuntimeTitle{};
    HWND aiRuntimeStatus{};
    HWND aiHarnessButton{};

    WallpaperLibrary* library{};
    ApplyCallback applyCallback;
    NavigateCallback navigateCallback;
    std::vector<std::wstring> visibleIds;
    std::vector<WallpaperLibraryTarget> targets;
    std::vector<std::wstring> targetIds;

    std::unique_ptr<turingdesk::L3Agent> aiAgent;
    turingdesk::ModelProbeResult aiProbe;
    std::wstring aiProbedUrl;
    bool aiHasProbe{};
    bool aiHadStoredKey{};
    bool aiPage{};

    HFONT titleFont{};
    HFONT pageTitleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HFONT cardTitleFont{};
    HFONT sectionFont{};

    ~Impl() {
        if (window && IsWindow(window)) DestroyWindow(window);
        DestroyResources();
    }

    void DestroyResources() {
        for (HFONT* font : {&titleFont, &pageTitleFont, &bodyFont, &smallFont, &cardTitleFont, &sectionFont}) {
            if (*font) { DeleteObject(*font); *font = nullptr; }
        }
    }

    void SetStatus(const std::wstring& text) const {
        if (status) SetWindowTextW(status, text.c_str());
    }

    void SetAiStatus(const std::wstring& text) const {
        if (aiStatus) SetWindowTextW(aiStatus, text.c_str());
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

    bool SourceMissing(const WallpaperLibraryItem& item) const {
        if (item.kind == LibraryWallpaperKind::Scene) return false;
        if (item.kind == LibraryWallpaperKind::Web) return !WallpaperLibrary::IsTrustedWebUrl(item.source.wstring());
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
            std::wstring title = item.favorite ? L"★ " + item.title : item.title;
            if (SourceMissing(item)) title += L" · 不可用";
            LVITEMW lv{};
            lv.mask = LVIF_TEXT;
            lv.iItem = index;
            lv.pszText = title.data();
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
            SetWindowTextW(selectedTitle, L"选择一个桌面");
            SetWindowTextW(selectedMeta, L"");
            SetWindowTextW(selectedDescription, L"从左边选择一个桌面，然后应用到当前布局或指定显示器。");
            EnableWindow(applyButton, FALSE);
            EnableWindow(favoriteButton, FALSE);
            EnableWindow(removeButton, FALSE);
            return;
        }

        SetWindowTextW(selectedTitle, selected->title.c_str());
        std::wstring meta = KindLabel(selected->kind);
        meta += L" · TuringDesk";
        SetWindowTextW(selectedMeta, meta.c_str());
        std::wstring description = DescriptionFor(*selected);
        if (!selected->source.empty() && selected->kind != LibraryWallpaperKind::Scene)
            description += L"\r\n\r\n" + selected->source.wstring();
        SetWindowTextW(selectedDescription, description.c_str());
        SetWindowTextW(favoriteButton, selected->favorite ? L"取消收藏" : L"收藏");
        EnableWindow(favoriteButton, TRUE);
        EnableWindow(applyButton, SourceMissing(*selected) ? FALSE : TRUE);
        EnableWindow(removeButton, selected->kind == LibraryWallpaperKind::Scene ? FALSE : TRUE);
    }

    void FinishImport(const WallpaperLibraryItem& imported, const std::wstring& message) {
        SetWindowTextW(search, L"");
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
            L"壁纸文件\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.tif;*.tiff;*.mp4;*.mov;*.wmv;*.m4v;*.avi;*.mkv;*.webm;*.html;*.htm;*.tdwall\0"
            L"所有文件\0*.*\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        std::wstring error;
        auto imported = library->ImportFile(path, {}, &error);
        if (!imported) {
            MessageBoxW(window, error.empty() ? L"导入失败。" : error.c_str(), L"TuringDesk 设置", MB_OK | MB_ICONERROR);
            return;
        }
        FinishImport(*imported, L"已导入：" + imported->title);
    }

    void ImportWebUrl() {
        if (!library) return;
        const auto url = Trim(WindowText(webUrl));
        if (url.empty()) return;
        std::wstring error;
        auto imported = library->ImportWebUrl(url, {}, &error);
        if (!imported) {
            MessageBoxW(window, error.empty() ? L"Web URL 导入失败。" : error.c_str(), L"TuringDesk 设置", MB_OK | MB_ICONERROR);
            return;
        }
        SetWindowTextW(webUrl, L"");
        FinishImport(*imported, L"已导入 Web 壁纸：" + imported->title);
    }

    void ToggleFavorite() {
        const auto selected = Selected();
        if (!selected || !library) return;
        std::wstring error;
        if (!library->SetFavorite(selected->id, !selected->favorite, &error)) {
            MessageBoxW(window, error.c_str(), L"TuringDesk 设置", MB_OK | MB_ICONERROR);
            return;
        }
        RebuildList();
    }

    void RemoveSelected() {
        const auto selected = Selected();
        if (!selected || !library || selected->kind == LibraryWallpaperKind::Scene) return;
        if (MessageBoxW(window, (L"从桌面库移除“" + selected->title + L"”？").c_str(), L"TuringDesk 设置",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        std::wstring error;
        if (!library->Remove(selected->id, selected->managedCopy, &error)) {
            MessageBoxW(window, error.c_str(), L"TuringDesk 设置", MB_OK | MB_ICONERROR);
            return;
        }
        RebuildList();
    }

    void ApplySelected() {
        const auto selected = Selected();
        if (!selected || !library || SourceMissing(*selected)) return;
        if (applyCallback) applyCallback(*selected, SelectedTargetId());
        std::wstring ignored;
        library->MarkUsed(selected->id, &ignored);
        SetStatus(L"已应用到桌面：" + selected->title);
    }

    void RefreshAiFields() {
        if (!aiAgent) return;
        const auto& config = aiAgent->Config();
        SetWindowTextW(aiUrl, aiAgent->CurrentApiUrl().c_str());
        SetWindowTextW(aiModel, config.model.c_str());
        SetWindowTextW(aiProviderValue, config.providerId.empty() ? L"未配置" : config.providerId.c_str());
        aiHadStoredKey = aiAgent->HasStoredApiKey();
        SetWindowTextW(aiKey, aiHadStoredKey ? kSavedKeyMask : L"");
        aiHasProbe = false;
        aiProbedUrl.clear();
        RefreshAiRuntimeStatus();
    }

    bool AiUsesStoredKey() const {
        const auto text = Trim(WindowText(aiKey));
        return aiHadStoredKey && (text.empty() || text == kSavedKeyMask);
    }

    void PopulateAiModels(const turingdesk::ModelProbeResult& probe) {
        const auto previous = Trim(WindowText(aiModel));
        SendMessageW(aiModel, CB_RESETCONTENT, 0, 0);
        if (probe.models.empty()) {
            SetWindowTextW(aiModel, previous.c_str());
            return;
        }
        for (const auto& model : probe.models)
            SendMessageW(aiModel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(model.c_str()));
        std::wstring selected = probe.recommendedModel;
        if (!previous.empty() && std::find(probe.models.begin(), probe.models.end(), previous) != probe.models.end()) selected = previous;
        if (selected.empty()) selected = probe.models.front();
        const auto index = SendMessageW(aiModel, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(selected.c_str()));
        if (index != CB_ERR) SendMessageW(aiModel, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        else SetWindowTextW(aiModel, selected.c_str());
    }

    void ProbeAi() {
        if (!aiAgent) return;
        const auto url = Trim(WindowText(aiUrl));
        const auto keyText = Trim(WindowText(aiKey));
        const bool useStored = AiUsesStoredKey();
        SetAiStatus(L"正在识别 API 协议并读取模型列表…");
        EnableWindow(aiProbeButton, FALSE);
        aiProbe = aiAgent->ProbeModels(url, useStored ? L"" : keyText, useStored);
        EnableWindow(aiProbeButton, TRUE);
        aiHasProbe = !aiProbe.baseUrl.empty() && !aiProbe.endpoint.empty();
        aiProbedUrl = url;
        PopulateAiModels(aiProbe);
        SetWindowTextW(aiProviderValue, aiProbe.providerId.empty() ? L"未识别" : aiProbe.providerId.c_str());
        if (aiProbe.ok) {
            SetAiStatus(L"✓ " + aiProbe.protocolLabel + L" · 找到 " + std::to_wstring(aiProbe.models.size()) + L" 个模型");
        } else if (!aiProbe.protocolLabel.empty()) {
            SetAiStatus(L"⚠ " + aiProbe.protocolLabel + L" · " + aiProbe.message);
        } else {
            SetAiStatus(L"无法连接 · " + aiProbe.message);
        }
    }

    void SaveAi() {
        if (!aiAgent) return;
        const auto url = Trim(WindowText(aiUrl));
        if (url.empty()) {
            MessageBoxW(window, L"请填写 API 地址。", L"TuringDesk AI", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!aiHasProbe || aiProbedUrl != url) ProbeAi();
        if (!aiHasProbe) {
            MessageBoxW(window, L"无法识别这个 API 地址，请检查后重试。", L"TuringDesk AI", MB_OK | MB_ICONERROR);
            return;
        }
        const auto model = Trim(WindowText(aiModel));
        if (model.empty()) {
            MessageBoxW(window, L"请填写或选择模型。", L"TuringDesk AI", MB_OK | MB_ICONWARNING);
            return;
        }
        const auto keyText = Trim(WindowText(aiKey));
        const bool preserve = AiUsesStoredKey();
        std::wstring reply;
        if (!aiAgent->ApplyModelConfig(aiProbe, model, preserve ? L"" : keyText, preserve, reply)) {
            MessageBoxW(window, reply.empty() ? L"保存失败。" : reply.c_str(), L"TuringDesk AI", MB_OK | MB_ICONERROR);
            return;
        }
        aiHadStoredKey = aiAgent->HasStoredApiKey();
        SetWindowTextW(aiKey, aiHadStoredKey ? kSavedKeyMask : L"");
        SetWindowTextW(aiProviderValue, aiAgent->Config().providerId.c_str());
        SetAiStatus(reply + L" · Codex CLI 下次请求立即使用此配置");
        RefreshAiRuntimeStatus();
    }

    void ClearAiKey() {
        if (!aiAgent) return;
        std::wstring reply;
        bool consumedSecret = false;
        aiAgent->TryHandleLocal(L"/clear-key", reply, consumedSecret);
        aiHadStoredKey = false;
        SetWindowTextW(aiKey, L"");
        aiHasProbe = false;
        SetAiStatus(reply);
    }

    void RefreshAiRuntimeStatus() {
        const fs::path root = ModuleDirectory();
        std::error_code ec;
        const bool codex = fs::is_regular_file(root / L"Codex" / L"codex.exe", ec);
        ec.clear();
        const bool relay = fs::is_regular_file(root / L"CodexRelay" / L"codex-relay.exe", ec);
        std::wstring text = L"Codex CLI：";
        text += codex ? L"已安装" : L"未安装";
        text += L"\r\nCodex Relay：";
        text += relay ? L"已安装（Chat Completions 按需使用）" : L"未安装";
        text += L"\r\n默认路由：Codex CLI → 轻量问答 fallback";
        SetWindowTextW(aiRuntimeStatus, text.c_str());
    }

    void OpenHarness() {
        const fs::path root = ModuleDirectory();
        const fs::path executable = root / L"TuringDeskHarness.exe";
        std::error_code ec;
        if (!fs::is_regular_file(executable, ec)) {
            MessageBoxW(window, L"当前安装包缺少 TuringDeskHarness.exe。", L"DeepSeek Harness", MB_OK | MB_ICONERROR);
            return;
        }
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", executable.c_str(), L"--ui", root.c_str(), SW_SHOWNORMAL));
        if (result <= 32) {
            MessageBoxW(window, L"DeepSeek Harness 工作台启动失败。", L"DeepSeek Harness", MB_OK | MB_ICONERROR);
            return;
        }
        SetAiStatus(L"DeepSeek Harness 工作台已打开；后台服务与 UI 保持分离。");
    }

    void ShowControl(HWND control, bool show) const {
        if (control) ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }

    void ShowInstalledPage(bool show) {
        for (HWND control : {search, list, detailFrame, selectedTitle, selectedMeta, selectedDescription, targetLabel,
                             targetCombo, applyButton, favoriteButton, removeButton, webLabel, webUrl, importWebButton})
            ShowControl(control, show);
        ShowControl(importButton, show);
    }

    void ShowAiPage(bool show) {
        for (HWND control : {aiUrlLabel, aiUrl, aiKeyLabel, aiKey, aiModelLabel, aiModel, aiProviderLabel, aiProviderValue,
                             aiProbeButton, aiSaveButton, aiClearKeyButton, aiStatus, aiRuntimeTitle, aiRuntimeStatus, aiHarnessButton})
            ShowControl(control, show);
    }

    void NavigateToTab(int index) {
        if (index == 5) {
            aiPage = true;
            SetWindowTextW(pageTitle, L"AI 与 Agent");
            SetWindowTextW(pageSubtitle, L"API、模型、Codex CLI 和 DeepSeek Harness 都在这里统一管理。");
            ShowInstalledPage(false);
            ShowAiPage(true);
            RefreshAiFields();
            Layout();
            return;
        }
        if (index == 0) {
            aiPage = false;
            SetWindowTextW(pageTitle, L"你的桌面");
            SetWindowTextW(pageSubtitle, L"把图片、视频、HTML 或 .tdwall 拖进窗口即可导入。");
            ShowAiPage(false);
            ShowInstalledPage(true);
            Layout();
            return;
        }
        if (navigateCallback) navigateCallback(SectionForTab(index));
        TabCtrl_SetCurSel(tabs, 0);
        NavigateToTab(0);
    }

    LRESULT DrawListCustom(NMLVCUSTOMDRAW* draw) {
        if (!draw || !list) return CDRF_DODEFAULT;
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
        const int index = static_cast<int>(draw->nmcd.dwItemSpec);
        if (index < 0 || static_cast<std::size_t>(index) >= visibleIds.size()) return CDRF_DODEFAULT;
        const auto item = library ? library->Find(visibleIds[static_cast<std::size_t>(index)]) : std::nullopt;
        if (!item) return CDRF_DODEFAULT;

        RECT rc{};
        if (!ListView_GetItemRect(list, index, &rc, LVIR_BOUNDS)) return CDRF_DODEFAULT;
        rc.left += 6; rc.top += 6; rc.right -= 10; rc.bottom -= 10;
        HDC dc = draw->nmcd.hdc;
        const bool selected = (ListView_GetItemState(list, index, LVIS_SELECTED) & LVIS_SELECTED) != 0;

        HBRUSH cardBrush = CreateSolidBrush(RGB(255, 255, 255));
        HPEN borderPen = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? RGB(37, 99, 235) : RGB(216, 222, 232));
        HGDIOBJ oldBrush = SelectObject(dc, cardBrush);
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 16, 16);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
        DeleteObject(cardBrush); DeleteObject(borderPen);

        RECT preview{rc.left + 12, rc.top + 12, rc.right - 12, rc.top + 104};
        HBRUSH previewBrush = CreateSolidBrush(RGB(242, 245, 252));
        HPEN previewPen = CreatePen(PS_SOLID, 1, RGB(218, 227, 249));
        oldBrush = SelectObject(dc, previewBrush); oldPen = SelectObject(dc, previewPen);
        RoundRect(dc, preview.left, preview.top, preview.right, preview.bottom, 12, 12);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
        DeleteObject(previewBrush); DeleteObject(previewPen);

        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(dc, smallFont);
        SetTextColor(dc, RGB(92, 112, 153));
        RECT kindRect{preview.left + 12, preview.bottom - 30, preview.right - 8, preview.bottom - 8};
        DrawTextW(dc, KindLabel(item->kind), -1, &kindRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, cardTitleFont);
        SetTextColor(dc, RGB(26, 30, 38));
        std::wstring title = item->favorite ? L"★ " + item->title : item->title;
        RECT titleRect{rc.left + 12, preview.bottom + 9, rc.right - 12, preview.bottom + 37};
        DrawTextW(dc, title.c_str(), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(dc, smallFont);
        SetTextColor(dc, RGB(95, 105, 126));
        const std::wstring description = SourceMissing(*item) ? L"资源不可用" : DescriptionFor(*item);
        RECT descRect{rc.left + 12, preview.bottom + 39, rc.right - 12, rc.bottom - 8};
        DrawTextW(dc, description.c_str(), -1, &descRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
        return CDRF_SKIPDEFAULT;
    }

    void Layout() {
        if (!window) return;
        RECT rc{}; GetClientRect(window, &rc);
        const int width = std::max(980L, rc.right - rc.left);
        const int height = std::max(680L, rc.bottom - rc.top);
        constexpr int margin = 22;
        constexpr int headerHeight = 108;
        constexpr int tabsHeight = 50;
        const int contentTop = headerHeight + tabsHeight + 20;

        MoveWindow(headerTitle, margin, 18, 460, 38, TRUE);
        MoveWindow(headerSubtitle, margin, 58, 720, 28, TRUE);
        MoveWindow(importButton, width - margin - 146, 24, 146, 46, TRUE);
        MoveWindow(tabs, 0, headerHeight, width, tabsHeight, TRUE);
        MoveWindow(pageTitle, margin, contentTop, 360, 38, TRUE);
        MoveWindow(pageSubtitle, margin, contentTop + 42, 760, 28, TRUE);

        if (!aiPage) {
            constexpr int detailWidth = 340;
            constexpr int gap = 22;
            const int detailX = width - margin - detailWidth;
            const int libraryWidth = std::max(500, detailX - gap - margin);
            MoveWindow(search, margin + libraryWidth - 250, contentTop + 3, 250, 40, TRUE);
            MoveWindow(list, margin, contentTop + 82, libraryWidth, height - (contentTop + 82) - 48, TRUE);
            MoveWindow(detailFrame, detailX, contentTop, detailWidth, height - contentTop - 48, TRUE);
            MoveWindow(selectedTitle, detailX + 20, contentTop + 24, detailWidth - 40, 34, TRUE);
            MoveWindow(selectedMeta, detailX + 20, contentTop + 62, detailWidth - 40, 26, TRUE);
            MoveWindow(selectedDescription, detailX + 20, contentTop + 104, detailWidth - 40, 112, TRUE);
            MoveWindow(targetLabel, detailX + 20, contentTop + 228, detailWidth - 40, 26, TRUE);
            MoveWindow(targetCombo, detailX + 20, contentTop + 258, detailWidth - 40, 200, TRUE);
            MoveWindow(applyButton, detailX + 20, contentTop + 306, detailWidth - 40, 44, TRUE);
            MoveWindow(favoriteButton, detailX + 20, contentTop + 362, 140, 38, TRUE);
            MoveWindow(removeButton, detailX + 170, contentTop + 362, detailWidth - 190, 38, TRUE);
            MoveWindow(webLabel, detailX + 20, contentTop + 424, detailWidth - 40, 26, TRUE);
            MoveWindow(webUrl, detailX + 20, contentTop + 454, detailWidth - 40, 36, TRUE);
            MoveWindow(importWebButton, detailX + 20, contentTop + 500, detailWidth - 40, 40, TRUE);
        } else {
            const int leftWidth = std::min(760, width - 430);
            const int rightX = margin + leftWidth + 42;
            const int rightWidth = std::max(300, width - rightX - margin);
            int y = contentTop + 88;
            MoveWindow(aiUrlLabel, margin, y, 220, 28, TRUE); y += 32;
            MoveWindow(aiUrl, margin, y, leftWidth, 40, TRUE); y += 56;
            MoveWindow(aiKeyLabel, margin, y, 220, 28, TRUE); y += 32;
            MoveWindow(aiKey, margin, y, leftWidth - 150, 40, TRUE);
            MoveWindow(aiClearKeyButton, margin + leftWidth - 138, y, 138, 40, TRUE); y += 56;
            MoveWindow(aiModelLabel, margin, y, 220, 28, TRUE); y += 32;
            MoveWindow(aiModel, margin, y, leftWidth - 150, 180, TRUE);
            MoveWindow(aiProbeButton, margin + leftWidth - 138, y, 138, 40, TRUE); y += 58;
            MoveWindow(aiProviderLabel, margin, y, 110, 28, TRUE);
            MoveWindow(aiProviderValue, margin + 116, y, leftWidth - 116, 28, TRUE); y += 46;
            MoveWindow(aiSaveButton, margin, y, 180, 44, TRUE); y += 58;
            MoveWindow(aiStatus, margin, y, leftWidth, 70, TRUE);

            MoveWindow(aiRuntimeTitle, rightX, contentTop + 88, rightWidth, 34, TRUE);
            MoveWindow(aiRuntimeStatus, rightX, contentTop + 130, rightWidth, 112, TRUE);
            MoveWindow(aiHarnessButton, rightX, contentTop + 260, rightWidth, 44, TRUE);
        }
        MoveWindow(status, margin, height - 32, width - margin * 2, 26, TRUE);
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
            else if (id == kAiProbeId && notification == BN_CLICKED) self->ProbeAi();
            else if (id == kAiSaveId && notification == BN_CLICKED) self->SaveAi();
            else if (id == kAiClearKeyId && notification == BN_CLICKED) self->ClearAiKey();
            else if (id == kAiHarnessId && notification == BN_CLICKED) self->OpenHarness();
            return 0;
        }
        case WM_NOTIFY: {
            const auto* note = reinterpret_cast<NMHDR*>(lParam);
            if (!note) break;
            if (note->idFrom == kTabsId && note->code == TCN_SELCHANGE) {
                self->NavigateToTab(TabCtrl_GetCurSel(self->tabs));
                return 0;
            }
            if (note->idFrom == kListId && note->code == NM_CUSTOMDRAW)
                return self->DrawListCustom(reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
            if (note->idFrom == kListId && note->code == LVN_ITEMCHANGED) {
                self->UpdateSelectionActions();
                InvalidateRect(self->list, nullptr, FALSE);
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
                                 CW_USEDEFAULT, CW_USEDEFAULT, 1280, 840,
                                 nullptr, nullptr, instance, this);
        if (!window) return false;

        titleFont = CreateFontW(-32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        pageTitleFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        sectionFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        bodyFont = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        smallFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        cardTitleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        auto font = [&](HWND control, HFONT use) {
            if (control && use) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(use), TRUE);
            return control;
        };
        auto label = [&](const wchar_t* text, DWORD style, HFONT use, bool visible = true) {
            DWORD windowStyle = WS_CHILD | style | (visible ? WS_VISIBLE : 0);
            return font(CreateWindowExW(0, L"STATIC", text, windowStyle, 0, 0, 10, 10, window, nullptr, instance, nullptr), use);
        };
        auto button = [&](const wchar_t* text, int id, bool visible = true) {
            DWORD style = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | (visible ? WS_VISIBLE : 0);
            return font(CreateWindowExW(0, L"BUTTON", text, style, 0, 0, 10, 10, window, ControlId(id), instance, nullptr), bodyFont);
        };

        headerTitle = label(L"桌面设置", 0, titleFont);
        headerSubtitle = label(L"场景、播放列表、多屏、应用规则、性能和 AI", 0, smallFont);
        importButton = button(L"导入壁纸", kImportId);

        tabs = font(CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    0, 0, 10, 10, window, ControlId(kTabsId), instance, nullptr), bodyFont);
        for (const wchar_t* tabText : {L"已安装", L"播放列表", L"多屏配置", L"应用规则", L"性能", L"AI"}) {
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(tabText);
            TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &item);
        }
        TabCtrl_SetCurSel(tabs, 0);

        pageTitle = label(L"你的桌面", 0, pageTitleFont);
        pageSubtitle = label(L"把图片、视频、HTML 或 .tdwall 拖进窗口即可导入。", 0, smallFont);
        search = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kSearchId), instance, nullptr), bodyFont);
        SendMessageW(search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索桌面"));

        list = font(CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                   LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_AUTOARRANGE,
                                   0, 0, 10, 10, window, ControlId(kListId), instance, nullptr), bodyFont);
        ListView_SetExtendedListViewStyle(list, LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_BORDERSELECT);
        ListView_SetIconSpacing(list, 254, 178);

        detailFrame = label(L"", SS_ETCHEDFRAME, bodyFont);
        selectedTitle = label(L"选择一个桌面", 0, pageTitleFont);
        selectedMeta = label(L"", 0, smallFont);
        selectedDescription = label(L"从左边选择一个桌面，然后应用。", SS_LEFT, bodyFont);
        targetLabel = label(L"应用目标", 0, smallFont);
        targetCombo = font(CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                           0, 0, 10, 10, window, ControlId(kTargetComboId), instance, nullptr), bodyFont);
        applyButton = button(L"应用到桌面", kApplyId);
        favoriteButton = button(L"收藏", kFavoriteId);
        removeButton = button(L"移出库", kRemoveId);
        webLabel = label(L"HTTPS Web 壁纸", 0, smallFont);
        webUrl = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window, ControlId(kWebUrlId), instance, nullptr), bodyFont);
        importWebButton = button(L"导入 Web URL", kImportWebUrlId);
        status = label(L"", 0, smallFont);

        aiUrlLabel = label(L"API 地址", 0, sectionFont, false);
        aiUrl = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                    0, 0, 10, 10, window, ControlId(kAiUrlId), instance, nullptr), bodyFont);
        aiKeyLabel = label(L"API Key", 0, sectionFont, false);
        aiKey = font(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                                    0, 0, 10, 10, window, ControlId(kAiKeyId), instance, nullptr), bodyFont);
        aiModelLabel = label(L"模型", 0, sectionFont, false);
        aiModel = font(CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                      0, 0, 10, 10, window, ControlId(kAiModelId), instance, nullptr), bodyFont);
        aiProviderLabel = label(L"Provider", 0, bodyFont, false);
        aiProviderValue = label(L"未配置", 0, bodyFont, false);
        aiProbeButton = button(L"检测模型", kAiProbeId, false);
        aiSaveButton = button(L"保存 AI 设置", kAiSaveId, false);
        aiClearKeyButton = button(L"清除 Key", kAiClearKeyId, false);
        aiStatus = label(L"", SS_LEFT, bodyFont, false);
        aiRuntimeTitle = label(L"AI Runtime", 0, pageTitleFont, false);
        aiRuntimeStatus = label(L"", SS_LEFT, bodyFont, false);
        aiHarnessButton = button(L"打开 DeepSeek Harness 工作台", kAiHarnessId, false);

        aiAgent = std::make_unique<turingdesk::L3Agent>();
        ShowAiPage(false);
        RebuildTargets();
        RebuildList();
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
    impl_->RebuildList();
    impl_->NavigateToTab(TabCtrl_GetCurSel(impl_->tabs));
    ShowWindow(impl_->window, SW_SHOWNORMAL);
    SetForegroundWindow(impl_->window);
    return true;
}

void WallpaperLibraryWindow::SetTargets(const std::vector<WallpaperLibraryTarget>& targets) {
    impl_->targets = targets;
    impl_->RebuildTargets();
}

void WallpaperLibraryWindow::Close() {
    if (impl_->window) ShowWindow(impl_->window, SW_HIDE);
}

void WallpaperLibraryWindow::Refresh() {
    impl_->RebuildTargets();
    impl_->RebuildList();
    if (impl_->aiPage) impl_->RefreshAiFields();
}

bool WallpaperLibraryWindow::Visible() const noexcept {
    return impl_->window && IsWindowVisible(impl_->window);
}

HWND WallpaperLibraryWindow::Window() const noexcept {
    return impl_->window;
}

} // namespace turingdesk::wallpaper
