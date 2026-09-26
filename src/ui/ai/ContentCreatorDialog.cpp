#include "miaodesk/ContentCreatorDialog.h"

#include "miaodesk/ConversationPanel.h"
#include "miaodesk/DesktopControlService.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoContentPackageManager.h"
#include "miaodesk/NativeTools.h"
#include "miaodesk/NativeUiScale.h"
#include "miaodesk/PiRuntime.h"
#include "miaodesk/WallpaperLibrary.h"
#include "miaodesk/WallpaperRuntimeControl.h"

#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::creator {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
namespace {

constexpr wchar_t kWindowClass[] = L"MiaoDesk.Native.ContentCreatorDialog";
constexpr int kTranscriptId = 7801;
constexpr int kPromptId = 7802;
constexpr int kSendId = 7803;
constexpr int kClearId = 7804;
constexpr int kSkillListId = 7805;
constexpr int kSkillTextId = 7806;
constexpr int kPreviewPaneId = 7807;
constexpr int kPreset1Id = 7810;
constexpr int kPreset2Id = 7811;
constexpr int kPreset3Id = 7812;
constexpr int kPreset4Id = 7813;
constexpr int kPreset5Id = 7814;
constexpr int kPreviewId = 7820;
constexpr int kLibraryId = 7821;
constexpr int kApplyId = 7822;
constexpr UINT kAppendDelta = WM_APP + 0x311;
constexpr UINT kRequestDone = WM_APP + 0x312;
constexpr UINT kActivityEvent = WM_APP + 0x313;

struct CreatorWindowPlacement {
    int x{};
    int y{};
    int width{};
    int height{};
};

MONITORINFO MonitorInfoForWindow(HWND reference) noexcept {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    HMONITOR monitor = nullptr;
    if (reference && IsWindow(reference))
        monitor = MonitorFromWindow(reference, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (monitor && GetMonitorInfoW(monitor, &info)) return info;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    info.rcMonitor = work;
    info.rcWork = work;
    return info;
}

CreatorWindowPlacement ResolveCreatorWindowPlacement(HWND owner) noexcept {
    const MONITORINFO monitor = MonitorInfoForWindow(owner);
    const int workW = std::max(1, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int workH = std::max(1, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    const int monitorW = std::max(1, static_cast<int>(monitor.rcMonitor.right - monitor.rcMonitor.left));
    const int monitorH = std::max(1, static_cast<int>(monitor.rcMonitor.bottom - monitor.rcMonitor.top));

    UINT nativeDpi = owner && IsWindow(owner) ? GetDpiForWindow(owner) : GetDpiForSystem();
    if (!nativeDpi) nativeDpi = USER_DEFAULT_SCREEN_DPI;
    const UINT effectiveDpi = std::max(nativeDpi, ui::ResolutionFontDpiForSize(monitorW, monitorH));

    // Size the editor from the current monitor instead of opening a fixed 1320x820 window.
    // 82% leaves enough desktop context on compact displays, while the effective-DPI
    // preferred size keeps the editor comfortably readable on 2K/4K monitors.
    const int preferredW = MulDiv(1120, static_cast<int>(effectiveDpi), USER_DEFAULT_SCREEN_DPI);
    const int preferredH = MulDiv(720, static_cast<int>(effectiveDpi), USER_DEFAULT_SCREEN_DPI);
    const int maxW = std::max(1, workW * 82 / 100);
    const int maxH = std::max(1, workH * 82 / 100);
    const int width = std::clamp(std::min(preferredW, maxW), std::min(workW, 760), workW);
    const int height = std::clamp(std::min(preferredH, maxH), std::min(workH, 520), workH);

    CreatorWindowPlacement placement{};
    placement.width = width;
    placement.height = height;
    placement.x = monitor.rcWork.left + (workW - width) / 2;
    placement.y = monitor.rcWork.top + (workH - height) / 2;
    return placement;
}

void ApplySuggestedDpiRect(HWND hwnd, const RECT& suggested) noexcept {
    HMONITOR monitorHandle = MonitorFromRect(&suggested, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!monitorHandle || !GetMonitorInfoW(monitorHandle, &monitor)) {
        SetWindowPos(hwnd, nullptr, suggested.left, suggested.top,
                     suggested.right - suggested.left, suggested.bottom - suggested.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    const int workW = std::max(1, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int workH = std::max(1, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    const int width = std::min(workW, std::max(1, static_cast<int>(suggested.right - suggested.left)));
    const int height = std::min(workH, std::max(1, static_cast<int>(suggested.bottom - suggested.top)));
    const int x = std::clamp(static_cast<int>(suggested.left), static_cast<int>(monitor.rcWork.left),
                             static_cast<int>(monitor.rcWork.right) - width);
    const int y = std::clamp(static_cast<int>(suggested.top), static_cast<int>(monitor.rcWork.top),
                             static_cast<int>(monitor.rcWork.bottom) - height);
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

bool Valid(ContentCreatorKind kind) noexcept {
    return kind == ContentCreatorKind::Wallpaper || kind == ContentCreatorKind::Widget;
}

std::wstring ReadText(HWND control) {
    const int length = control ? GetWindowTextLengthW(control) : 0;
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void AppendText(HWND edit, std::wstring_view text) {
    if (!edit || text.empty()) return;
    const int length = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, length, length);
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(std::wstring(text).c_str()));
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

std::wstring Trim(std::wstring value) {
    const auto notSpace = [](wchar_t ch) { return !std::iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring SkillField(std::wstring_view markdown, std::wstring_view key) {
    const std::wstring prefix = std::wstring(key) + L":";
    std::size_t start = 0;
    while (start < markdown.size()) {
        std::size_t end = markdown.find_first_of(L"\r\n", start);
        if (end == std::wstring_view::npos) end = markdown.size();
        std::wstring line = Trim(std::wstring(markdown.substr(start, end - start)));
        if (line.rfind(prefix, 0) == 0) return Trim(line.substr(prefix.size()));
        start = markdown.find_first_not_of(L"\r\n", end);
        if (start == std::wstring_view::npos) break;
    }
    return {};
}

std::wstring FormatSkillDetails(std::wstring_view markdown) {
    const std::wstring name = SkillField(markdown, L"name");
    const std::wstring description = SkillField(markdown, L"description");
    std::wstring out;
    if (!name.empty()) out += L"名称\r\n" + name + L"\r\n\r\n";
    if (!description.empty()) out += L"能力\r\n" + description + L"\r\n\r\n";
    out += L"完整规范\r\n";
    out.append(markdown);
    return out;
}

HBITMAP LoadPreviewBitmap(const fs::path& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) return nullptr;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf())))) return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
            decoder.GetAddressOf()))) return nullptr;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return nullptr;

    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return nullptr;

    constexpr UINT kMaxPreviewWidth = 1000;
    constexpr UINT kMaxPreviewHeight = 800;
    const double scale = std::min(
        1.0,
        std::min(static_cast<double>(kMaxPreviewWidth) / width,
                 static_cast<double>(kMaxPreviewHeight) / height));
    const UINT targetW = std::max<UINT>(1, static_cast<UINT>(std::lround(width * scale)));
    const UINT targetH = std::max<UINT>(1, static_cast<UINT>(std::lround(height * scale)));

    IWICBitmapSource* source = frame.Get();
    ComPtr<IWICBitmapScaler> scaler;
    if (targetW != width || targetH != height) {
        if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(frame.Get(), targetW, targetH, WICBitmapInterpolationModeFant)))
            return nullptr;
        source = scaler.Get();
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(
            source, GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(targetW);
    info.bmiHeader.biHeight = -static_cast<LONG>(targetH);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return nullptr;
    }

    const UINT stride = targetW * 4;
    const UINT bytes = stride * targetH;
    if (FAILED(converter->CopyPixels(nullptr, stride, bytes, static_cast<BYTE*>(bits)))) {
        DeleteObject(bitmap);
        return nullptr;
    }
    return bitmap;
}

std::optional<fs::path> FindGeneratedPackagePath(
    std::wstring_view text, ContentCreatorKind kind) {
    const std::wstring extension =
        kind == ContentCreatorKind::Widget ? L".mdwidget" : L".mdwall";
    const std::wstring raw(text);
    const std::wstring lower = Lower(raw);
    std::size_t search = 0;

    while (true) {
        const std::size_t ext = lower.find(extension, search);
        if (ext == std::wstring::npos) break;
        const std::size_t end = ext + extension.size();

        std::size_t lineStart = raw.find_last_of(L"\r\n", ext);
        lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;

        std::size_t start = lineStart;
        if (ext >= lineStart + 2) {
            for (std::size_t i = ext; i >= lineStart + 2; --i) {
                const std::size_t drive = i - 2;
                if (std::iswalpha(raw[drive]) && raw[drive + 1] == L':' &&
                    drive + 2 < raw.size() &&
                    (raw[drive + 2] == L'\\' || raw[drive + 2] == L'/')) {
                    start = drive;
                    break;
                }
                if (i == lineStart + 2) break;
            }
        }

        if (start == lineStart) {
            const auto quote = raw.find_last_of(L"\"'", ext);
            if (quote != std::wstring::npos && quote >= lineStart) start = quote + 1;
        }

        std::wstring candidate = Trim(raw.substr(start, end - start));
        while (!candidate.empty() &&
               (candidate.front() == L'-' || candidate.front() == L'*' ||
                candidate.front() == L':' || candidate.front() == L'：')) {
            candidate.erase(candidate.begin());
            candidate = Trim(std::move(candidate));
        }

        fs::path path(candidate);
        std::error_code ec;
        if (!candidate.empty() && fs::is_directory(path, ec) && !ec &&
            _wcsicmp(path.extension().c_str(), extension.c_str()) == 0) {
            return path;
        }

        search = end;
    }
    return std::nullopt;
}

const wchar_t* PackageExtension(ContentCreatorKind kind) noexcept {
    return kind == ContentCreatorKind::Widget ? L".mdwidget" : L".mdwall";
}

struct SkillItem {
    const wchar_t* label;
    const char* name;
};

constexpr std::array<SkillItem, 3> kWallpaperSkills{{
    {L"1. 内容包基础 · content-package-basics", "content-package-basics"},
    {L"2. 壁纸规则 · wallpaper-content", "wallpaper-content"},
    {L"3. 交付检查 · content-review", "content-review"},
}};
constexpr std::array<SkillItem, 3> kWidgetSkills{{
    {L"1. 内容包基础 · content-package-basics", "content-package-basics"},
    {L"2. 组件规则 · widget-content", "widget-content"},
    {L"3. 交付检查 · content-review", "content-review"},
}};

struct CreatorPreset {
    const wchar_t* label;
    const wchar_t* prompt;
};

constexpr std::array<CreatorPreset, 5> kWallpaperPresets{{
    {L"治愈猫咪", L"制作一张治愈猫咪主题壁纸，云层柔和，轻微动态，不遮挡桌面图标。"},
    {L"星空夜景", L"制作宁静星空夜景壁纸，层次清晰，动态克制，适合长期使用。"},
    {L"赛博城市", L"制作夜间赛博霓虹城市壁纸，光效有层次但避免高频闪烁。"},
    {L"自然风景", L"制作自然风景壁纸，山湖或海岸构图，色彩舒适，桌面可读性优先。"},
    {L"动漫风格", L"制作清爽动漫风格壁纸，主体构图明确，动态柔和，保留桌面图标可读区域。"},
}};
constexpr std::array<CreatorPreset, 5> kWidgetPresets{{
    {L"天气组件", L"制作一个简洁的天气组件，显示当前城市、温度、天气状况和未来几小时预报。"},
    {L"时钟组件", L"制作一个极简时钟组件，显示时间和日期，适合桌面常驻。"},
    {L"待办清单", L"制作一个待办事项组件，支持完成状态，信息层级清楚。"},
    {L"桌面宠物", L"制作一个轻量桌面宠物信息卡组件，风格可爱但不遮挡主要桌面内容。"},
    {L"系统监控", L"制作一个系统监控组件，显示 CPU、内存和 GPU 等关键状态，信息清晰。"},
}};

struct DialogState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    L3Agent* agent{};
    PiRuntime* pi{};
    ContentCreatorKind kind{ContentCreatorKind::None};
    HWND heading{};
    HWND note{};
    HWND transcript{};
    HWND prompt{};
    HWND send{};
    HWND clear{};
    HWND previewHeading{};
    HWND previewPane{};
    HWND skillHeading{};
    HWND skillList{};
    HWND skillDetailHeading{};
    HWND skillText{};
    HWND resultNote{};
    HWND preview{};
    HWND library{};
    HWND apply{};
    std::array<HWND, 5> presets{};
    HFONT bodyFont{};
    HFONT titleFont{};
    HFONT smallFont{};
    UINT fontScaleDpi{};
    HBITMAP previewBitmap{};
    bool primed{};
    bool busy{};
    fs::path generatedPackage;
    std::wstring lastUserPrompt;

    ~DialogState() {
        if (previewBitmap) DeleteObject(previewBitmap);
        if (bodyFont) DeleteObject(bodyFont);
        if (titleFont) DeleteObject(titleFont);
        if (smallFont) DeleteObject(smallFont);
    }

    bool IsWidget() const noexcept { return kind == ContentCreatorKind::Widget; }
    int S(int value) const noexcept {
        const UINT dpi = window ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window))
                                : USER_DEFAULT_SCREEN_DPI;
        return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    }

    const std::array<SkillItem, 3>& Skills() const noexcept {
        return IsWidget() ? kWidgetSkills : kWallpaperSkills;
    }
    const std::array<CreatorPreset, 5>& PresetText() const noexcept {
        return IsWidget() ? kWidgetPresets : kWallpaperPresets;
    }

    void RebuildFonts() {
        fontScaleDpi = ui::EffectiveFontDpi(window);
        if (bodyFont) DeleteObject(bodyFont);
        if (titleFont) DeleteObject(titleFont);
        if (smallFont) DeleteObject(smallFont);
        bodyFont = ui::CreateUiFont(window, 14, FW_NORMAL);
        titleFont = ui::CreateUiFont(window, 18, FW_SEMIBOLD, L"Segoe UI Variable Display");
        smallFont = ui::CreateUiFont(window, 12, FW_NORMAL);
    }

    void ApplyFonts() const {
        if (heading && titleFont) SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        for (HWND child : {note, transcript, prompt, send, clear, previewHeading,
                           skillHeading, skillList, skillDetailHeading, skillText,
                           resultNote, preview, library, apply}) {
            if (child && bodyFont) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
        }
        for (HWND child : presets)
            if (child && smallFont) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    }

    void RefreshFontScaleIfNeeded() {
        if (!window || !bodyFont || ui::EffectiveFontDpi(window) == fontScaleDpi) return;
        RebuildFonts();
        ApplyFonts();
        Layout();
    }

    void Layout() const {
        if (!window) return;
        RECT rc{};
        GetClientRect(window, &rc);
        const int width = std::max(1, static_cast<int>(rc.right - rc.left));
        const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));

        const bool compactVertical = height < S(700);
        const int margin = compactVertical ? S(10) : S(16);
        const int gap = compactVertical ? S(8) : S(12);
        const int headerH = compactVertical ? S(48) : S(62);
        const int footerH = compactVertical ? S(86) : S(104);
        const int bodyTop = margin + headerH + gap;
        const int footerTop = std::max(bodyTop + S(180), height - margin - footerH);
        const int bodyH = std::max(S(180), footerTop - gap - bodyTop);

        // Always derive the three columns from the actual client width. The previous
        // S(900)/430/290/260 floors could make the content wider than the window on
        // 125%-150% scaling and effectively force the editor to behave fullscreen.
        const int usableW = std::max(1, width - margin * 2 - gap * 2);
        const bool compactHorizontal = usableW < S(980);
        int leftW = compactHorizontal ? usableW * 42 / 100
                                      : std::clamp(usableW * 43 / 100, S(360), S(600));
        int previewW = compactHorizontal ? usableW * 28 / 100
                                         : std::clamp(usableW * 29 / 100, S(240), S(410));
        leftW = std::max(1, std::min(leftW, usableW));
        previewW = std::max(1, std::min(previewW, usableW - leftW));
        const int skillW = std::max(1, usableW - leftW - previewW);
        const int previewX = margin + leftW + gap;
        const int skillX = previewX + previewW + gap;

        auto place = [](HWND child, int x, int y, int w, int h) {
            if (child) SetWindowPos(child, nullptr, x, y, std::max(1, w), std::max(1, h),
                                    SWP_NOZORDER | SWP_NOACTIVATE);
        };

        place(heading, margin, margin, width - margin * 2, compactVertical ? S(26) : S(30));
        place(note, margin, margin + (compactVertical ? S(28) : S(34)),
              width - margin * 2, compactVertical ? S(18) : S(22));

        // Left: focused conversation workspace.
        place(transcript, margin, bodyTop, leftW, bodyH);

        // Middle: result preview. Clicking the pane opens the full preview flow.
        const int sectionHeadingH = compactVertical ? S(24) : S(28);
        const int previewButtonH = compactVertical ? S(34) : S(38);
        place(previewHeading, previewX, bodyTop, previewW, sectionHeadingH);
        place(previewPane, previewX, bodyTop + sectionHeadingH + S(6), previewW,
              bodyH - sectionHeadingH - previewButtonH - S(14));
        place(preview, previewX, bodyTop + bodyH - previewButtonH, previewW, previewButtonH);

        // Right: Skill chain and readable Skill detail.
        const int skillListH = compactVertical ? S(84) : S(112);
        const int skillListTop = bodyTop + sectionHeadingH + S(6);
        const int detailHeadingTop = skillListTop + skillListH + S(8);
        const int detailHeadingH = compactVertical ? S(22) : S(26);
        const int detailTop = detailHeadingTop + detailHeadingH + S(4);
        place(skillHeading, skillX, bodyTop, skillW, sectionHeadingH);
        place(skillList, skillX, skillListTop, skillW, skillListH);
        place(skillDetailHeading, skillX, detailHeadingTop, skillW, detailHeadingH);
        place(skillText, skillX, detailTop, skillW, bodyTop + bodyH - detailTop);

        // Bottom composer spans conversation + preview columns.
        const int composerW = leftW + gap + previewW;
        const int actionW = compactHorizontal ? S(78) : S(92);
        const int clearW = compactHorizontal ? S(78) : S(92);
        const int promptH = compactVertical ? S(42) : S(48);
        const int promptTop = footerTop;
        place(prompt, margin, promptTop,
              std::max(1, composerW - actionW - clearW - gap * 2), promptH);
        place(send, margin + composerW - actionW - clearW - gap, promptTop, actionW, promptH);
        place(clear, margin + composerW - clearW, promptTop, clearW, promptH);

        const int presetTop = promptTop + promptH + (compactVertical ? S(5) : S(8));
        const int presetGap = compactHorizontal ? S(5) : S(8);
        const int presetW = std::max(1, (composerW - presetGap * 4) / 5);
        const int presetH = compactVertical ? S(28) : S(32);
        for (int i = 0; i < 5; ++i)
            place(presets[static_cast<std::size_t>(i)],
                  margin + i * (presetW + presetGap), presetTop, presetW, presetH);

        // Bottom-right: result state + final actions.
        const int resultH = compactVertical ? S(24) : S(28);
        const int resultButtonH = compactVertical ? S(42) : S(48);
        place(resultNote, skillX, footerTop, skillW, resultH);
        const int resultButtonTop = footerTop + resultH + (compactVertical ? S(4) : S(8));
        const int libraryW = std::max(1, (skillW - gap) / 2);
        place(library, skillX, resultButtonTop, libraryW, resultButtonH);
        place(apply, skillX + libraryW + gap, resultButtonTop,
              std::max(1, skillW - libraryW - gap), resultButtonH);
    }

    void DrawPrimaryAction(const DRAWITEMSTRUCT* draw) const {
        if (!draw) return;
        const bool enabled = IsWindowEnabled(draw->hwndItem) != FALSE;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        const COLORREF fillColor = !enabled
            ? RGB(184, 201, 224)
            : pressed ? RGB(25, 93, 205) : RGB(37, 116, 236);
        HBRUSH fill = CreateSolidBrush(fillColor);
        HPEN pen = CreatePen(PS_SOLID, 1, enabled ? RGB(31, 101, 214) : RGB(170, 188, 214));
        HGDIOBJ oldBrush = SelectObject(draw->hDC, fill);
        HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
        const int radius = S(12);
        RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top,
                  draw->rcItem.right, draw->rcItem.bottom, radius, radius);
        SelectObject(draw->hDC, oldPen);
        SelectObject(draw->hDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(fill);

        wchar_t text[96]{};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, RGB(255, 255, 255));
        HGDIOBJ oldFont = SelectObject(draw->hDC, bodyFont);
        RECT label = draw->rcItem;
        DrawTextW(draw->hDC, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(draw->hDC, oldFont);
        if (draw->itemState & ODS_FOCUS) {
            RECT focus = draw->rcItem;
            InflateRect(&focus, -S(4), -S(4));
            DrawFocusRect(draw->hDC, &focus);
        }
    }

    void DrawPresetChip(const DRAWITEMSTRUCT* draw) const {
        if (!draw) return;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        HBRUSH fill = CreateSolidBrush(pressed ? RGB(222, 236, 255) : RGB(239, 246, 255));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 232, 248));
        HGDIOBJ oldBrush = SelectObject(draw->hDC, fill);
        HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
        const int radius = S(12);
        RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top,
                  draw->rcItem.right, draw->rcItem.bottom, radius, radius);
        SelectObject(draw->hDC, oldPen);
        SelectObject(draw->hDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(fill);

        wchar_t text[96]{};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, RGB(35, 105, 225));
        HGDIOBJ oldFont = SelectObject(draw->hDC, smallFont);
        RECT label = draw->rcItem;
        DrawTextW(draw->hDC, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(draw->hDC, oldFont);
        if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
    }

    void DrawPreviewPane(const DRAWITEMSTRUCT* draw) const {
        if (!draw) return;
        RECT bounds = draw->rcItem;
        HDC dc = draw->hDC;
        HBRUSH background = CreateSolidBrush(RGB(248, 250, 253));
        FillRect(dc, &bounds, background);
        DeleteObject(background);

        RECT inner = bounds;
        InflateRect(&inner, -S(10), -S(10));
        if (previewBitmap) {
            BITMAP bitmap{};
            GetObjectW(previewBitmap, sizeof(bitmap), &bitmap);
            const int sourceW = std::max<LONG>(1, bitmap.bmWidth);
            const int sourceH = std::max<LONG>(1, bitmap.bmHeight);
            const int availableW = std::max<int>(1, static_cast<int>(inner.right - inner.left));
            const int availableH = std::max<int>(1, static_cast<int>(inner.bottom - inner.top));
            const double scale = std::min(
                static_cast<double>(availableW) / sourceW,
                static_cast<double>(availableH) / sourceH);
            const int drawW = std::max(1, static_cast<int>(std::lround(sourceW * scale)));
            const int drawH = std::max(1, static_cast<int>(std::lround(sourceH * scale)));
            const int x = inner.left + (availableW - drawW) / 2;
            const int y = inner.top + (availableH - drawH) / 2;

            HDC memory = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(memory, previewBitmap);
            const int oldMode = SetStretchBltMode(dc, HALFTONE);
            StretchBlt(dc, x, y, drawW, drawH, memory, 0, 0, sourceW, sourceH, SRCCOPY);
            SetStretchBltMode(dc, oldMode);
            SelectObject(memory, old);
            DeleteDC(memory);
        } else {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(115, 124, 140));
            HGDIOBJ old = SelectObject(dc, bodyFont);
            const wchar_t* text = busy
                ? L"AI 正在生成内容包…\n完成后将在这里显示预览"
                : L"预览效果\n生成内容后将在这里显示";
            DrawTextW(dc, text, -1, &inner, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
            SelectObject(dc, old);
        }

        HBRUSH border = CreateSolidBrush(RGB(222, 230, 240));
        FrameRect(dc, &bounds, border);
        DeleteObject(border);
    }

    void LoadSkill() const {
        if (!skillList || !skillText) return;
        LRESULT selected = SendMessageW(skillList, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR) selected = 0;
        if (selected < 0 || static_cast<std::size_t>(selected) >= Skills().size()) return;
        const auto& skill = Skills()[static_cast<std::size_t>(selected)];
        const std::string args = std::string("{\"name\":\"") + skill.name + "\"}";
        const auto result = ExecuteNativeToolRaw("content_skill_get", args);
        const std::wstring text = result.success
            ? FormatSkillDetails(result.message)
            : L"无法读取 Skill：\r\n" + result.message;
        SetWindowTextW(skillText, text.c_str());
        SendMessageW(skillText, EM_SETSEL, 0, 0);
    }

    void SetBusy(bool value) {
        busy = value;
        EnableWindow(send, !value);
        SetWindowTextW(send, value ? L"生成中…" : L"生成");
        if (previewPane) InvalidateRect(previewPane, nullptr, TRUE);
    }

    content::ContentKind ExpectedKind() const noexcept {
        return IsWidget() ? content::ContentKind::Widget : content::ContentKind::Wallpaper;
    }

    void SetGeneratedPackage(const fs::path& path) {
        desktop::DesktopControlService control;
        content::ManagedContentPackageInfo info;
        const auto inspected = control.InspectContentPackage(path, &info);
        if (!inspected.success || info.kind != ExpectedKind()) return;

        generatedPackage = path;

        if (previewBitmap) {
            DeleteObject(previewBitmap);
            previewBitmap = nullptr;
        }
        content::LoadedMiaoContentPackage package;
        std::wstring previewError;
        if (content::MiaoContentPackage::Load(path, &package, &previewError) &&
            !package.manifest.preview.empty()) {
            fs::path previewPath;
            if (content::MiaoContentPackage::ResolvePackagePath(
                    package.root, package.manifest.preview, &previewPath, &previewError)) {
                previewBitmap = LoadPreviewBitmap(previewPath);
            }
        }
        if (previewPane) InvalidateRect(previewPane, nullptr, TRUE);

        const std::wstring status =
            std::wstring(L"已生成并校验：") + path.filename().wstring() + L" · 可预览 / 入库 / 应用";
        SetWindowTextW(resultNote, status.c_str());
        EnableWindow(preview, TRUE);
        EnableWindow(library, TRUE);
        EnableWindow(apply, TRUE);
    }

    void InspectForGeneratedPackage(std::wstring_view text) {
        if (auto path = FindGeneratedPackagePath(text, kind)) SetGeneratedPackage(*path);
    }

    bool OpenPreview() {
        if (generatedPackage.empty()) return false;

        content::LoadedMiaoContentPackage package;
        std::wstring error;
        if (!content::MiaoContentPackage::Load(generatedPackage, &package, &error)) {
            MessageBoxW(window, error.empty() ? L"内容包预览校验失败。" : error.c_str(),
                        L"妙喵 AI", MB_OK | MB_ICONERROR);
            return false;
        }

        fs::path previewPath;
        if (!package.manifest.preview.empty() &&
            content::MiaoContentPackage::ResolvePackagePath(
                package.root, package.manifest.preview, &previewPath, &error)) {
            const HINSTANCE opened = ShellExecuteW(
                window, L"open", previewPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(opened) > 32) return true;
        }

        ShellExecuteW(window, L"open", generatedPackage.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        MessageBoxW(window,
                    L"该内容包没有可直接打开的 manifest.preview，已打开内容包目录。"
                    L"\n生成内容仍需在应用前由你确认。",
                    L"妙喵 AI · Preview-first", MB_OK | MB_ICONINFORMATION);
        return true;
    }

    bool InstallGeneratedPackage(bool activate) {
        if (generatedPackage.empty()) return false;

        desktop::DesktopControlService control;
        content::ManagedContentPackageInfo inspected;
        auto result = control.InspectContentPackage(generatedPackage, &inspected);
        if (!result.success || inspected.kind != ExpectedKind()) {
            MessageBoxW(window,
                        result.message.empty() ? L"生成内容包校验失败。" : result.message.c_str(),
                        L"妙喵 AI", MB_OK | MB_ICONERROR);
            return false;
        }

        content::ContentPackageInstallResult installed;
        content::ManagedContentPackageInfo existing;
        const auto resolved = control.ResolveContentPackage(ExpectedKind(), inspected.source, &existing);
        std::error_code equivalentError;
        const bool alreadyManaged =
            resolved.success &&
            fs::exists(existing.packageRoot, equivalentError) && !equivalentError &&
            fs::exists(generatedPackage, equivalentError) && !equivalentError &&
            fs::equivalent(existing.packageRoot, generatedPackage, equivalentError) && !equivalentError;

        if (alreadyManaged) {
            installed.package = existing;
            installed.replacedExisting = true;
        } else {
            content::ContentPackageInstallOptions options;
            options.replaceExisting = true;
            result = control.InstallContentPackage(generatedPackage, &installed, options);
            if (!result.success) {
                MessageBoxW(window,
                            result.message.empty() ? L"加入内容库失败。" : result.message.c_str(),
                            L"妙喵 AI", MB_OK | MB_ICONERROR);
                return false;
            }
        }

        generatedPackage = installed.package.packageRoot;
        if (!activate) {
            SetWindowTextW(resultNote,
                IsWidget() ? L"已加入组件库 · 可继续添加到桌面"
                           : L"已加入壁纸库 · 可继续应用到桌面");
            return true;
        }

        if (IsWidget()) {
            desktop::ContentWidgetCreateRequest request;
            request.definitionId = std::wstring(
                installed.package.id.begin(), installed.package.id.end());
            request.title = installed.package.name;
            wallpaper::DesktopWidget created;
            result = control.CreateContentWidget(request, &created);
            if (!result.success) {
                MessageBoxW(window,
                            result.message.empty() ? L"组件已入库，但添加到桌面失败。" : result.message.c_str(),
                            L"妙喵 AI", MB_OK | MB_ICONERROR);
                return false;
            }
            control.EnsureRuntime();
            SetWindowTextW(resultNote, L"组件已加入组件库并添加到桌面");
            return true;
        }

        wallpaper::WallpaperLibraryItem item;
        item.id = installed.package.source;
        item.kind = installed.package.runtime == content::ContentRuntimeKind::Web
            ? wallpaper::LibraryWallpaperKind::Web
            : wallpaper::LibraryWallpaperKind::Scene;
        item.title = installed.package.name;
        item.source = installed.package.packageRoot;
        item.managedCopy = true;
        result = control.ApplyLibraryItem(item);
        if (!result.success) {
            MessageBoxW(window,
                        result.message.empty() ? L"壁纸已入库，但应用到桌面失败。" : result.message.c_str(),
                        L"妙喵 AI", MB_OK | MB_ICONERROR);
            return false;
        }
        control.EnsureRuntime();
        wallpaper::NotifyWallpaperRuntimeReload();

        wallpaper::WallpaperLibrary index;
        std::wstring ignored;
        index.Load(&ignored);
        SetWindowTextW(resultNote, L"壁纸已加入壁纸库并应用到桌面");
        return true;
    }

    std::wstring BuildPrompt(std::wstring_view userText) {
        std::wstring request;
        if (!primed) {
            request = InitialPrompt(kind);
            request += L"\n\n当前窗口是独立的轻量创作界面，但模型执行必须走现有 Pi 工具链。"
                       L"完成后必须先执行 content-review，并在最终回复中单独给出生成内容包的绝对路径（";
            request += PackageExtension(kind);
            request += L"）。不要未经用户点击按钮直接安装或应用。\n\n用户需求：";
            primed = true;
        } else {
            request = L"继续当前";
            request += IsWidget() ? L"组件" : L"壁纸";
            request += L"创作任务。继续使用既定 Skills，保持 preview-first；最终再次给出内容包绝对路径。用户补充：";
        }
        request.append(userText);
        return request;
    }

    void SendPrompt() {
        if (!agent || !pi || busy) return;
        std::wstring text = Trim(ReadText(prompt));
        if (text.empty()) return;
        lastUserPrompt = text;
        AppendText(transcript, L"\r\n你：" + text + L"\r\n\r\n妙喵：");
        SetWindowTextW(prompt, L"");
        SetBusy(true);
        agent->ReloadConfig();
        const HWND target = window;
        pi->AskAsync(
            *agent,
            BuildPrompt(text),
            [target](std::wstring delta) {
                auto* heap = new std::wstring(std::move(delta));
                if (!PostMessageW(target, kAppendDelta, 0, reinterpret_cast<LPARAM>(heap))) delete heap;
            },
            [target](std::wstring done) {
                auto* heap = new std::wstring(std::move(done));
                if (!PostMessageW(target, kRequestDone, 0, reinterpret_cast<LPARAM>(heap))) delete heap;
            },
            [target](PiActivityEvent event) {
                auto* heap = new PiActivityEvent(std::move(event));
                if (!PostMessageW(target, kActivityEvent, 0, reinterpret_cast<LPARAM>(heap))) delete heap;
            });
    }

    void Regenerate() {
        if (busy || lastUserPrompt.empty()) return;
        SetWindowTextW(prompt, lastUserPrompt.c_str());
        SendPrompt();
    }

    void ResetSession() {
        if (pi && pi->Busy()) pi->Stop();
        if (agent && agent->Busy()) agent->Stop();
        if (pi) pi->ResetSession();
        generatedPackage.clear();
        lastUserPrompt.clear();
        if (previewBitmap) {
            DeleteObject(previewBitmap);
            previewBitmap = nullptr;
        }
        if (previewPane) InvalidateRect(previewPane, nullptr, TRUE);
        primed = false;
        SetBusy(false);
        SetWindowTextW(transcript,
            IsWidget()
                ? L"妙喵：你好！我是妙喵组件助手。\r\n"
                  L"你可以描述天气、时钟、待办、桌面宠物等组件需求。\r\n"
                  L"我会按右侧 Skills 生成 .mdwidget，并先让你预览确认。\r\n"
                : L"妙喵：你好！我是妙喵壁纸助手。\r\n"
                  L"你可以描述风格、主题、动态效果和希望保留的桌面可读性。\r\n"
                  L"我会按右侧 Skills 生成 .mdwall，并先让你预览确认。\r\n");
        SetWindowTextW(resultNote, L"尚未生成内容包 · 生成后先预览，再加入库或应用");
        EnableWindow(preview, FALSE);
        EnableWindow(library, FALSE);
        EnableWindow(apply, FALSE);
    }

    bool CreateControls() {
        RebuildFonts();
        auto label = [&](const wchar_t* text) {
            return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   0, 0, 10, 10, window, nullptr, instance, nullptr);
        };
        auto button = [&](const wchar_t* text, int id, DWORD extra = 0) {
            return CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra,
                0, 0, 10, 10, window, ControlId(id), instance, nullptr);
        };

        heading = label(IsWidget() ? L"AI 制作组件" : L"AI 制作壁纸");
        note = label(IsWidget()
            ? L"描述你想要的组件，AI 会根据 Skills 生成并打包成 .mdwidget"
            : L"描述你想要的壁纸，AI 会根据 Skills 生成并打包成 .mdwall");
        transcript = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 10, 10, window, ControlId(kTranscriptId), instance, nullptr);
        prompt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 10, 10, window, ControlId(kPromptId), instance, nullptr);
        SendMessageW(prompt, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(
            IsWidget() ? L"例如：做一个玻璃天气组件…" : L"例如：做一个治愈系猫咪动态壁纸…"));
        send = button(L"生成", kSendId, BS_OWNERDRAW);
        clear = button(L"新对话", kClearId);
        for (int i = 0; i < 5; ++i)
            presets[static_cast<std::size_t>(i)] =
                button(PresetText()[static_cast<std::size_t>(i)].label, kPreset1Id + i, BS_OWNERDRAW);
        previewHeading = label(L"预览效果");
        previewPane = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_OWNERDRAW | SS_NOTIFY,
            0, 0, 10, 10, window, ControlId(kPreviewPaneId), instance, nullptr);
        skillHeading = label(IsWidget()
            ? L"当前 Skills · 输出 .mdwidget"
            : L"当前 Skills · 输出 .mdwall");
        skillList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 10, 10, window, ControlId(kSkillListId), instance, nullptr);
        skillDetailHeading = label(L"Skill 详情");
        skillText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 10, 10, window, ControlId(kSkillTextId), instance, nullptr);
        resultNote = label(L"尚未生成内容包 · 生成后先预览，再加入库或应用");
        preview = button(L"重新生成", kPreviewId);
        library = button(IsWidget() ? L"加入组件库" : L"加入壁纸库", kLibraryId);
        apply = button(
            IsWidget() ? L"添加到桌面" : L"应用到桌面",
            kApplyId, BS_OWNERDRAW);
        EnableWindow(preview, FALSE);
        EnableWindow(library, FALSE);
        EnableWindow(apply, FALSE);

        for (const auto& item : Skills())
            SendMessageW(skillList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.label));
        SendMessageW(skillList, LB_SETCURSEL, 0, 0);
        ApplyFonts();
        ResetSession();
        LoadSkill();
        Layout();
        return heading && note && transcript && prompt && send && clear &&
               previewHeading && previewPane && skillList && skillText;
    }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        if (!state) return FALSE;
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        return state->CreateControls() ? 0 : -1;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        if (info) {
            UINT dpi = GetDpiForWindow(hwnd);
            if (!dpi) dpi = USER_DEFAULT_SCREEN_DPI;
            const MONITORINFO monitor = MonitorInfoForWindow(hwnd);
            const int workW = std::max(1, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
            const int workH = std::max(1, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
            const int edge = MulDiv(16, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
            const int availableW = std::max(640, workW - edge * 2);
            const int availableH = std::max(460, workH - edge * 2);
            info->ptMinTrackSize.x = std::min(
                MulDiv(860, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI), availableW);
            info->ptMinTrackSize.y = std::min(
                MulDiv(560, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI), availableH);
        }
        return 0;
    }
    case WM_SIZE:
        state->Layout();
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) ApplySuggestedDpiRect(hwnd, *suggested);
        state->RefreshFontScaleIfNeeded();
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (draw && draw->CtlID == kPreviewPaneId) {
            state->DrawPreviewPane(draw);
            return TRUE;
        }
        if (draw && draw->CtlType == ODT_BUTTON &&
            (draw->CtlID == kSendId || draw->CtlID == kApplyId)) {
            state->DrawPrimaryAction(draw);
            return TRUE;
        }
        if (draw && draw->CtlType == ODT_BUTTON &&
            draw->CtlID >= kPreset1Id && draw->CtlID <= kPreset5Id) {
            state->DrawPresetChip(draw);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kSendId && HIWORD(wParam) == BN_CLICKED) { state->SendPrompt(); return 0; }
        if (id == kClearId && HIWORD(wParam) == BN_CLICKED) { state->ResetSession(); return 0; }
        if (id == kSkillListId && HIWORD(wParam) == LBN_SELCHANGE) { state->LoadSkill(); return 0; }
        if (id >= kPreset1Id && id <= kPreset5Id && HIWORD(wParam) == BN_CLICKED) {
            SetWindowTextW(
                state->prompt,
                state->PresetText()[static_cast<std::size_t>(id - kPreset1Id)].prompt);
            SetFocus(state->prompt);
            return 0;
        }
        if (id == kPreviewPaneId && HIWORD(wParam) == STN_CLICKED) {
            if (!state->generatedPackage.empty()) state->OpenPreview();
            return 0;
        }
        if (id == kPreviewId && HIWORD(wParam) == BN_CLICKED) { state->Regenerate(); return 0; }
        if (id == kLibraryId && HIWORD(wParam) == BN_CLICKED) { state->InstallGeneratedPackage(false); return 0; }
        if (id == kApplyId && HIWORD(wParam) == BN_CLICKED) { state->InstallGeneratedPackage(true); return 0; }
        break;
    }
    case kAppendDelta: {
        std::unique_ptr<std::wstring> delta(reinterpret_cast<std::wstring*>(lParam));
        if (delta) AppendText(state->transcript, *delta);
        return 0;
    }
    case kActivityEvent: {
        std::unique_ptr<PiActivityEvent> event(reinterpret_cast<PiActivityEvent*>(lParam));
        if (event && !event->resultText.empty()) state->InspectForGeneratedPackage(event->resultText);
        return 0;
    }
    case kRequestDone: {
        std::unique_ptr<std::wstring> done(reinterpret_cast<std::wstring*>(lParam));
        state->SetBusy(false);
        if (done) state->InspectForGeneratedPackage(*done);
        AppendText(state->transcript, L"\r\n");
        if (state->generatedPackage.empty()) {
            SetWindowTextW(state->resultNote,
                done && !done->empty()
                    ? L"本轮生成已完成 · 尚未检测到有效内容包路径"
                    : L"本轮请求结束 · 未检测到可操作的内容包");
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state->pi && state->pi->Busy()) state->pi->Stop();
        if (state->agent && state->agent->Busy()) state->agent->Stop();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

ContentCreatorLayout ResolveContentCreatorLayout(int clientWidth, int clientHeight) noexcept {
    clientWidth = std::max(760, clientWidth);
    clientHeight = std::max(560, clientHeight);
    ContentCreatorLayout layout{};
    layout.margin = 18;
    layout.gap = 14;
    layout.headerHeight = 64;
    layout.footerHeight = 42;
    const int usable = clientWidth - layout.margin * 2 - layout.gap;
    layout.leftWidth = std::clamp(usable * 58 / 100, 430, 720);
    layout.rightWidth = std::max(250, usable - layout.leftWidth);
    layout.bodyHeight = std::max(220, clientHeight - layout.margin * 2 - layout.headerHeight - layout.footerHeight - layout.gap * 2);
    return layout;
}

bool ShowContentCreatorDialog(HINSTANCE instance, HWND owner, L3Agent& agent, ContentCreatorKind kind) {
    if (!instance || !Valid(kind)) return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    auto* state = new DialogState{};
    state->instance = instance;
    state->owner = owner;
    state->agent = &agent;
    state->pi = &SharedConversationPiRuntime();
    state->kind = kind;

    const wchar_t* title = kind == ContentCreatorKind::Widget ? L"妙喵 · AI 制作组件" : L"妙喵 · AI 制作壁纸";
    const CreatorWindowPlacement placement = ResolveCreatorWindowPlacement(owner);
    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW, kWindowClass, title,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        placement.x, placement.y, placement.width, placement.height,
        owner, nullptr, instance, state);
    if (!window) {
        delete state;
        return false;
    }
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    return true;
}

} // namespace miaodesk::creator
