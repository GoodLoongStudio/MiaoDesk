#include "miaodesk/WidgetRuntimeAcceptance.h"
#include "miaodesk/WidgetService.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::wstring_view ReadPhase(int argc, wchar_t** argv) {
    constexpr std::wstring_view prefix = L"--phase=";
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i] ? std::wstring_view(argv[i]) : std::wstring_view{};
        if (arg.starts_with(prefix)) return arg.substr(prefix.size());
    }
    return L"baseline";
}

struct PhaseContextExpectation {
    const wchar_t* windowClass = nullptr;
    const wchar_t* processName = nullptr;
    const wchar_t* description = nullptr;
};

PhaseContextExpectation ExpectedPhaseContext(std::wstring_view phase) {
    if (phase == L"settings") {
        return {L"MiaoDesk.Native.DesktopLibrary", L"MiaoDeskWallpaper.exe", L"MiaoDesk desktop library/settings window"};
    }
    if (phase == L"search") {
        return {L"MiaoDesk.Native.SearchWindow", L"MiaoDesk.exe", L"MiaoDesk search window"};
    }
    return {};
}

bool ProcessImageMatches(DWORD processId, const wchar_t* expectedProcessName) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return false;

    wchar_t imagePath[32768]{};
    DWORD length = static_cast<DWORD>(std::size(imagePath));
    const bool queried = QueryFullProcessImageNameW(process, 0, imagePath, &length) != FALSE;
    CloseHandle(process);
    if (!queried || length == 0 || length >= std::size(imagePath)) return false;

    const std::wstring_view path(imagePath, length);
    const auto slash = path.find_last_of(L"\\/");
    const std::wstring fileName(path.substr(slash == std::wstring_view::npos ? 0 : slash + 1));
    return CompareStringOrdinal(fileName.c_str(), -1, expectedProcessName, -1, TRUE) == CSTR_EQUAL;
}

struct PhaseContextSearch {
    const wchar_t* expectedClass = nullptr;
    const wchar_t* expectedProcessName = nullptr;
    DWORD sessionId = 0;
    bool found = false;
};

BOOL CALLBACK FindVisiblePhaseContextWindow(HWND hwnd, LPARAM parameter) {
    auto* search = reinterpret_cast<PhaseContextSearch*>(parameter);
    if (!search || search->found || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;

    wchar_t className[256]{};
    if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) <= 0) return TRUE;
    if (std::wstring_view(className) != search->expectedClass) return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return TRUE;

    DWORD windowSessionId = 0;
    if (!ProcessIdToSessionId(processId, &windowSessionId) || windowSessionId != search->sessionId) return TRUE;
    if (!ProcessImageMatches(processId, search->expectedProcessName)) return TRUE;

    search->found = true;
    return FALSE;
}

bool PhaseContextReady(std::wstring_view phase, std::wstring* failure) {
    const auto expected = ExpectedPhaseContext(phase);
    if (!expected.windowClass) return true;

    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        if (failure) *failure = L"无法解析 M3 acceptance 进程的 Windows session。";
        return false;
    }

    PhaseContextSearch search{expected.windowClass, expected.processName, sessionId, false};
    EnumWindows(FindVisiblePhaseContextWindow, reinterpret_cast<LPARAM>(&search));
    if (search.found) return true;

    if (failure) {
        *failure = L"M3 " + std::wstring(phase) + L" 阶段缺少同一 Windows session 内可见的 "
            + expected.description + L"（class=" + expected.windowClass + L" process=" + expected.processName
            + L"）。健康取样不能脱离要求的产品窗口上下文。";
    }
    return false;
}

struct ShowcaseSpec {
    const wchar_t* title;
    float width;
    float height;
};

constexpr std::array<ShowcaseSpec, 3> kM3Showcase = {{
    {L"玻璃时钟", 0.30f, 0.20f},
    {L"今日待办", 0.26f, 0.24f},
    {L"玻璃天气", 0.24f, 0.20f},
}};

bool NearlyEqual(float lhs, float rhs) noexcept {
    return std::fabs(lhs - rhs) <= 0.001f;
}

bool SameMonitor(const miaodesk::wallpaper::DesktopWidget& lhs,
                 const miaodesk::wallpaper::DesktopWidget& rhs) {
    return CompareStringOrdinal(lhs.monitorId.c_str(), -1, rhs.monitorId.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool Overlaps(const miaodesk::wallpaper::DesktopWidget& lhs,
              const miaodesk::wallpaper::DesktopWidget& rhs) noexcept {
    const float lhsRight = lhs.x + lhs.width;
    const float lhsBottom = lhs.y + lhs.height;
    const float rhsRight = rhs.x + rhs.width;
    const float rhsBottom = rhs.y + rhs.height;
    return lhs.x < rhsRight && rhs.x < lhsRight && lhs.y < rhsBottom && rhs.y < lhsBottom;
}

bool FixedShowcaseReady(std::wstring* failure) {
    std::vector<miaodesk::wallpaper::DesktopWidget> widgets;
    const miaodesk::desktop::WidgetService service;
    const auto result = service.List(&widgets);
    if (!result.success) {
        if (failure) *failure = result.message.empty() ? L"无法读取 M3 Widget showcase 配置。" : result.message;
        return false;
    }

    std::vector<const miaodesk::wallpaper::DesktopWidget*> enabledShowcase;
    for (const auto& widget : widgets) {
        if (!widget.enabled) continue;
        if (widget.kind != miaodesk::wallpaper::DesktopWidgetKind::Native &&
            widget.kind != miaodesk::wallpaper::DesktopWidgetKind::Web) {
            continue;
        }
        enabledShowcase.push_back(&widget);
    }
    if (enabledShowcase.size() != kM3Showcase.size()) {
        if (failure) {
            *failure = L"M3 real-Windows acceptance 必须同时启用且仅启用三个固定 showcase 小组件；当前 enabledShowcase="
                + std::to_wstring(enabledShowcase.size()) + L"，需要 玻璃时钟/今日待办/玻璃天气 各一个。";
        }
        return false;
    }

    for (const auto& spec : kM3Showcase) {
        const auto it = std::find_if(enabledShowcase.begin(), enabledShowcase.end(), [&](const auto* widget) {
            return widget->title == spec.title;
        });
        if (it == enabledShowcase.end()) {
            if (failure) *failure = L"M3 fixed showcase 缺少启用模板：" + std::wstring(spec.title) + L"。";
            return false;
        }
        if (!NearlyEqual((*it)->width, spec.width) || !NearlyEqual((*it)->height, spec.height)) {
            if (failure) {
                *failure = L"M3 fixed showcase 模板尺寸被修改：" + std::wstring(spec.title)
                    + L"。当前阶段必须使用 preset-owned geometry，不接受自由缩放后的配置。";
            }
            return false;
        }
        if ((*it)->kind != miaodesk::wallpaper::DesktopWidgetKind::Native) {
            if (failure) {
                *failure = L"M3 fixed showcase 必须使用原生 Direct2D preset：" + std::wstring(spec.title)
                    + L"。WebView2 showcase 不再满足 M3 性能与验收要求。";
            }
            return false;
        }
    }

    for (std::size_t i = 0; i < enabledShowcase.size(); ++i) {
        for (std::size_t j = i + 1; j < enabledShowcase.size(); ++j) {
            if (SameMonitor(*enabledShowcase[i], *enabledShowcase[j]) &&
                Overlaps(*enabledShowcase[i], *enabledShowcase[j])) {
                if (failure) {
                    *failure = L"M3 fixed showcase 存在同屏重叠：" + enabledShowcase[i]->title + L" 与 "
                        + enabledShowcase[j]->title + L"。真实可视验收要求三个固定小组件无重叠。";
                }
                return false;
            }
        }
    }
    return true;
}

bool StructuredLifecycleReady(std::wstring* failure) {
    miaodesk::desktop::WidgetRuntimeHealth health;
    const miaodesk::desktop::WidgetService service;
    const auto result = service.GetRuntimeHealth(&health);
    if (!result.success) {
        if (failure) *failure = result.message.empty() ? L"无法读取 M3 Widget lifecycle health。" : result.message;
        return false;
    }

    for (const auto& surface : health.surfaces) {
        if (!surface.environmentReported || !surface.controllerReported || !surface.navigationReported) {
            if (failure) {
                *failure = L"M3 real-Windows acceptance 不接受 legacy/unreported widget lifecycle：widget="
                    + surface.widgetId + L"。必须由 preferred native/Web surface child 明确报告 Environment/Controller/Navigation telemetry。";
            }
            return false;
        }
        if (!surface.environmentReady || !surface.controllerReady || !surface.navigationReady) {
            if (failure) {
                *failure = L"M3 widget lifecycle 尚未 ready：widget=" + surface.widgetId
                    + L" environment=" + (surface.environmentReady ? L"true" : L"false")
                    + L" controller=" + (surface.controllerReady ? L"true" : L"false")
                    + L" navigation=" + (surface.navigationReady ? L"true" : L"false") + L"。";
            }
            return false;
        }
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring_view phase = ReadPhase(argc, argv);
    const bool baseline = phase == L"baseline";
    std::wstring report;
    std::wstring failure;

    // M3 is deliberately a fixed three-widget showcase until real desktop visibility
    // is proven. A single arbitrary Web Widget must never satisfy the acceptance gate.
    if (!FixedShowcaseReady(&failure)) {
        if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
        return static_cast<int>(miaodesk::desktop::WidgetRuntimeAcceptanceCode::SurfaceUnhealthy);
    }

    // Later phases validate persisted placement configuration before the runtime
    // probe can advance the durable sequence cursor. PID/HWND identity is not
    // part of this checkpoint because Explorer recovery may recreate surfaces.
    if (!baseline) {
        const auto configCode = miaodesk::desktop::CheckWidgetAcceptanceConfigContinuity(
            phase, false, &failure);
        if (configCode != miaodesk::desktop::WidgetRuntimeAcceptanceCode::Passed) {
            if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
            return static_cast<int>(configCode);
        }
    }

    // Settings/Search acceptance only means something while the requested product
    // surface is visibly present in the expected product process. Keep this check
    // immediately before the health probe so every extra acceptance failure occurs
    // before RunWidgetRuntimeAcceptanceProbe can advance the durable phase cursor.
    if (!PhaseContextReady(phase, &failure)) {
        if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
        return static_cast<int>(miaodesk::desktop::WidgetRuntimeAcceptanceCode::SurfaceUnhealthy);
    }

    // General runtime health keeps legacy children observable for compatibility,
    // but M3 acceptance is stricter: an unreported lifecycle must never be treated
    // as proof of visible WebView2 readiness. Check before the probe so a failure
    // cannot advance the durable phase sequence cursor.
    failure.clear();
    if (!StructuredLifecycleReady(&failure)) {
        if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
        return static_cast<int>(miaodesk::desktop::WidgetRuntimeAcceptanceCode::SurfaceUnhealthy);
    }

    const auto code = miaodesk::desktop::RunWidgetRuntimeAcceptanceProbe(
        phase, &report, &failure);

    if (!report.empty()) std::wcout << L"report=" << report << L"\n";
    if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
    if (code != miaodesk::desktop::WidgetRuntimeAcceptanceCode::Passed)
        return static_cast<int>(code);

    // Only a successful baseline runtime probe is allowed to establish the
    // placement config checkpoint. If writing it fails, a new baseline is required.
    if (baseline) {
        failure.clear();
        const auto configCode = miaodesk::desktop::CheckWidgetAcceptanceConfigContinuity(
            phase, true, &failure);
        if (configCode != miaodesk::desktop::WidgetRuntimeAcceptanceCode::Passed) {
            if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
            return static_cast<int>(configCode);
        }
    }

    std::wcout << L"M3 Widget runtime acceptance probe passed.\n";
    return static_cast<int>(miaodesk::desktop::WidgetRuntimeAcceptanceCode::Passed);
}
