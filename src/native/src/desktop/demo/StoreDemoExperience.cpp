#include "turingdesk/StoreDemoExperience.h"

#include "turingdesk/DesktopWidgetController.h"
#include "turingdesk/GeneratedDesktopPreview.h"
#include "turingdesk/NativeWidgetPreset.h"
#include "turingdesk/WallpaperLibrary.h"
#include "turingdesk/WidgetIntentComposer.h"
#include "turingdesk/WidgetService.h"

#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::demo {
namespace {

constexpr wchar_t kIniRelative[] = L"TuringDesk\\store-demo.ini";
constexpr wchar_t kSection[] = L"StoreDemo";
constexpr wchar_t kFirstRunKey[] = L"FirstRunCompleted";

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

fs::path ConfigPath() {
    wchar_t localAppData[32768]{};
    const DWORD count = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (count == 0 || count >= std::size(localAppData)) {
        return fs::temp_directory_path() / L"TuringDesk" / L"store-demo.ini";
    }
    return fs::path(std::wstring(localAppData, count)) / kIniRelative;
}

bool ReadFlag(const wchar_t* key) {
    wchar_t buffer[16]{};
    GetPrivateProfileStringW(kSection, key, L"0", buffer, static_cast<DWORD>(std::size(buffer)),
                             ConfigPath().c_str());
    return buffer[0] == L'1';
}

void WriteFlag(const wchar_t* key, bool value) {
    std::error_code ec;
    fs::create_directories(ConfigPath().parent_path(), ec);
    WritePrivateProfileStringW(kSection, key, value ? L"1" : L"0", ConfigPath().c_str());
}

bool ContainsAny(const std::wstring& haystack, std::initializer_list<const wchar_t*> needles) {
    for (const wchar_t* needle : needles) {
        if (haystack.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

wallpaper::WallpaperLibraryItem SceneItem(std::wstring id, std::wstring title) {
    wallpaper::WallpaperLibraryItem item;
    item.id = std::move(id);
    item.kind = wallpaper::LibraryWallpaperKind::Scene;
    item.title = std::move(title);
    return item;
}

wallpaper::WallpaperLibraryItem ResolveScene(std::wstring_view sceneId) {
    if (sceneId == L"scene-neon" || sceneId == L"neon" || sceneId == L"neon_flow")
        return SceneItem(L"scene-neon", L"Neon Flow");
    if (sceneId == L"scene-grid" || sceneId == L"grid" || sceneId == L"ocean" || sceneId == L"ocean_flow")
        return SceneItem(L"scene-grid", L"Ocean Flow");
    // Default showcase: Aurora (ocean-like cool tones on Snapdragon demos).
    return SceneItem(L"scene-aurora", L"Aurora Flow");
}

bool NearlyEqual(float a, float b) noexcept {
    return std::fabs(a - b) <= 0.0005f;
}

bool MatchesLegacyShowcaseGeometry(
    const wallpaper::DesktopWidget& widget,
    wallpaper::NativeWidgetPreset preset) noexcept {
    switch (preset) {
    case wallpaper::NativeWidgetPreset::GlassClock:
        return NearlyEqual(widget.width, 0.30f) && NearlyEqual(widget.height, 0.20f);
    case wallpaper::NativeWidgetPreset::TodayTasks:
        return NearlyEqual(widget.width, 0.26f) && NearlyEqual(widget.height, 0.24f);
    case wallpaper::NativeWidgetPreset::WeatherGlass:
        return NearlyEqual(widget.width, 0.24f) && NearlyEqual(widget.height, 0.20f);
    }
    return false;
}

std::vector<wallpaper::DesktopWidget>::iterator FindWidgetTitle(
    std::vector<wallpaper::DesktopWidget>& widgets,
    const wchar_t* title) {
    return std::find_if(widgets.begin(), widgets.end(), [&](const auto& widget) {
        return _wcsicmp(widget.title.c_str(), title) == 0;
    });
}

bool MigrateLegacyShowcaseGeometry(
    wallpaper::DesktopWidget* widget,
    wallpaper::NativeWidgetPreset preset,
    std::wstring* error) {
    if (!widget || !MatchesLegacyShowcaseGeometry(*widget, preset)) return true;
    const auto* definition = wallpaper::NativePresetDefinition(preset);
    if (!definition) return true;

    desktop::WidgetUpdateRequest request;
    request.id = widget->id;
    request.width = definition->defaultWidth;
    request.height = definition->defaultHeight;
    request.x = std::clamp(widget->x, 0.0f, std::max(0.0f, 1.0f - definition->defaultWidth));
    request.y = std::clamp(widget->y, 0.0f, std::max(0.0f, 1.0f - definition->defaultHeight));

    const desktop::WidgetService service;
    const auto updated = service.Update(request);
    if (!updated.success) {
        if (error) *error = updated.message;
        return false;
    }
    widget->x = *request.x;
    widget->y = *request.y;
    widget->width = definition->defaultWidth;
    widget->height = definition->defaultHeight;
    return true;
}

} // namespace

bool IsStoreDemoScopeEnabled() noexcept {
    return true;
}

bool HideAdvancedWorkbench() noexcept {
    return IsStoreDemoScopeEnabled();
}

bool NeedsFirstRun() {
    return IsStoreDemoScopeEnabled() && !ReadFlag(kFirstRunKey);
}

void MarkFirstRunCompleted() {
    WriteFlag(kFirstRunKey, true);
}

desktop::DesktopControlResult ApplyShowcaseWallpaper(std::wstring_view sceneId) {
    desktop::DesktopControlService service;
    const auto item = ResolveScene(sceneId);
    auto applied = service.ApplyLibraryItem(item);
    if (!applied.success) return applied;
    const auto runtime = service.EnsureRuntime();
    if (!runtime.success) {
        return {false, L"壁纸已选择，但桌面运行时未就绪：" + runtime.message};
    }
    return {true, L"已应用动态壁纸：" + item.title};
}

desktop::DesktopControlResult EnsureShowcaseWidgets() {
    desktop::DesktopWidgetController controller;
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = controller.Refresh(&existing);
    if (!listed.success) return listed;

    struct Needed {
        desktop::WidgetFixedPreset preset;
        wallpaper::NativeWidgetPreset nativePreset;
        const wchar_t* title;
    };
    constexpr std::array<Needed, 3> needed{{
        {desktop::WidgetFixedPreset::GlassClock, wallpaper::NativeWidgetPreset::GlassClock, L"玻璃时钟"},
        {desktop::WidgetFixedPreset::TodayTasks, wallpaper::NativeWidgetPreset::TodayTasks, L"今日待办"},
        {desktop::WidgetFixedPreset::WeatherGlass, wallpaper::NativeWidgetPreset::WeatherGlass, L"玻璃天气"},
    }};

    std::wstring createdTitles;
    for (const auto& item : needed) {
        auto found = FindWidgetTitle(existing, item.title);
        if (found != existing.end()) {
            std::wstring migrationError;
            if (!MigrateLegacyShowcaseGeometry(&*found, item.nativePreset, &migrationError)) {
                return {false, L"更新小组件布局失败：" + migrationError};
            }
            continue;
        }
        wallpaper::DesktopWidget created;
        const auto result = controller.CreatePreset(item.preset, {}, &created);
        if (!result.success) return result;
        existing.push_back(created);
        if (!createdTitles.empty()) createdTitles += L"、";
        createdTitles += item.title;
    }

    desktop::DesktopControlService service;
    const auto runtime = service.EnsureRuntime();
    if (!runtime.success) {
        return {false, L"小组件已创建，但桌面运行时未就绪：" + runtime.message};
    }
    if (createdTitles.empty()) return {true, L"三款桌面小组件已就绪。"};
    return {true, L"已创建桌面小组件：" + createdTitles};
}

desktop::DesktopControlResult RunGoldenPath() {
    auto wallpaper = ApplyShowcaseWallpaper(L"scene-aurora");
    if (!wallpaper.success) return wallpaper;
    auto widgets = EnsureShowcaseWidgets();
    if (!widgets.success) return widgets;
    return {true, L"演示桌面已就绪：Aurora 动态壁纸 + 玻璃时钟、今日待办、玻璃天气。按 Alt+Space 可继续和妙喵聊天。"};
}

bool TryHandleDemoPrompt(std::wstring_view prompt, std::wstring* reply) {
    if (!reply || !IsStoreDemoScopeEnabled()) return false;
    const auto lower = Lower(std::wstring(prompt));
    if (lower.empty()) return false;

    const bool wantsWallpaper = ContainsAny(lower, {
        L"壁纸", L"桌面背景", L"动态壁纸", L"aurora", L"极光", L"neon", L"霓虹",
        L"ocean", L"海洋", L"海边", L"换个壁纸", L"换壁纸",
    });
    const bool wantsWidget = ContainsAny(lower, {
        L"时钟", L"小组件", L"组件", L"widget", L"钟", L"待办", L"天气",
    });
    const bool wantsDemo = ContainsAny(lower, {
        L"演示", L"体验", L"demo", L"showcase", L"试试", L"好看", L"装扮桌面",
    });
    const bool greeting = ContainsAny(lower, {
        L"你好", L"在吗", L"hello", L"hi", L"嗨",
    }) && lower.size() <= 12;

    if (greeting) {
        *reply = L"在。我是妙喵。还没配置 API Key 时也能先体验桌面："
                 L"可以说「给我一个动态壁纸」「右上角加个待办小组件」，也可以说「一键体验」。";
        return true;
    }

    if (wantsDemo && !wantsWallpaper && !wantsWidget) {
        const auto result = RunGoldenPath();
        *reply = result.success
            ? (result.message + L"\r\n（演示模式：无需 API Key。配置模型后可解锁完整 Agent。）")
            : (L"演示未能完成：" + result.message);
        return true;
    }

    if (wantsWallpaper) {
        std::wstring scene = L"scene-aurora";
        if (ContainsAny(lower, {L"neon", L"霓虹"})) scene = L"scene-neon";
        else if (ContainsAny(lower, {L"ocean", L"海洋", L"海边", L"蓝", L"深海", L"海浪"})) scene = L"scene-grid";
        else if (ContainsAny(lower, {L"grid", L"网格"})) scene = L"scene-neon";
        const auto result = ApplyShowcaseWallpaper(scene);
        *reply = result.success
            ? (result.message + L"\r\n已直接应用到桌面（演示模式）。配置 API Key 后，AI 会先出预览再让你点 Apply。")
            : (L"换壁纸失败：" + result.message);
        return true;
    }

    if (wantsWidget) {
        if (ContainsAny(lower, {L"三", L"全部", L"套装"})) {
            const auto result = EnsureShowcaseWidgets();
            *reply = result.success ? result.message : (L"创建小组件失败：" + result.message);
            return true;
        }
        if (widget_intent::LooksLikeOneSentenceWidgetRequest(prompt)) {
            HWND owner = FindWindowW(L"TuringDesk.Native.SearchWindow", nullptr);
            const auto preview = preview::ShowWidgetPreviewForPrompt(owner, prompt);
            *reply = preview.message;
            return true;
        }
        desktop::DesktopWidgetController controller;
        desktop::WidgetFixedPreset preset = desktop::WidgetFixedPreset::GlassClock;
        if (ContainsAny(lower, {L"待办", L"任务", L"todo"})) preset = desktop::WidgetFixedPreset::TodayTasks;
        else if (ContainsAny(lower, {L"天气", L"weather"})) preset = desktop::WidgetFixedPreset::WeatherGlass;
        wallpaper::DesktopWidget created;
        const auto result = controller.CreatePreset(preset, {}, &created);
        if (!result.success) {
            *reply = L"创建小组件失败：" + result.message;
            return true;
        }
        desktop::DesktopControlService service;
        service.EnsureRuntime();
        *reply = L"已在桌面添加「" + created.title + L"」。可以继续说「加个今日待办」或「加个玻璃天气」。";
        return true;
    }

    if (ContainsAny(lower, {L"api", L"key", L"密钥", L"模型", L"配置"})) {
        *reply = L"请打开设置 →「妙喵 AI」，填写 API 地址和 Key。未配置前，壁纸与小组件演示仍可用。";
        return true;
    }

    // Soft catch-all in demo mode: steer users back to the golden path instead of a dead end.
    if (!ContainsAny(lower, {L"/"})) {
        *reply = L"当前是演示模式（未配置 API Key），我可以直接帮你换动态壁纸、用一句话生成小组件预览。"
                 L"试试：「给我一个极光壁纸」「右上角加个天气小组件」「一键体验」。"
                 L"完整 Agent 能力请先在设置里保存模型配置。";
        return true;
    }
    return false;
}

void OfferGoldenPath(HWND owner, bool quietStatus) {
    const int choice = MessageBoxW(
        owner,
        L"立即布置演示桌面？\r\n\r\n"
        L"• 应用 Aurora 动态壁纸\r\n"
        L"• 添加三款小组件（玻璃时钟 / 今日待办 / 玻璃天气）\r\n\r\n"
        L"无需 API Key。之后可用 Alt+Space 继续体验。",
        L"妙喵 · 一键体验",
        MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON1);
    if (choice != IDOK) return;

    HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const auto result = RunGoldenPath();
    SetCursor(previous);
    if (quietStatus && result.success) return;
    MessageBoxW(owner,
                result.success ? result.message.c_str() : (L"未能完成演示：\r\n" + result.message).c_str(),
                L"妙喵",
                result.success ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING);
}

void MaybeShowFirstRun(HWND owner) {
    if (!NeedsFirstRun()) return;

    const int choice = MessageBoxW(
        owner,
        L"欢迎使用妙喵 — 会说话的动态桌面。\r\n\r\n"
        L"快捷键 Alt+Space 打开搜索与 AI。\r\n"
        L"现在可以一键体验动态壁纸和桌面小组件（无需 API Key）。\r\n\r\n"
        L"选择「确定」立即体验，「取消」稍后再说。",
        L"妙喵",
        MB_OKCANCEL | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);

    MarkFirstRunCompleted();
    if (choice == IDOK) OfferGoldenPath(owner, false);
}

} // namespace turingdesk::demo
