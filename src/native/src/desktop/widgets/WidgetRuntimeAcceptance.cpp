#include "turingdesk/WidgetRuntimeAcceptance.h"

#include "turingdesk/WidgetService.h"

#include <windows.h>

#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace turingdesk::desktop {
namespace {

std::wstring SafePhase(std::wstring_view phase) {
    std::wstring value = phase.empty() ? L"baseline" : std::wstring(phase);
    for (auto& ch : value) {
        if (!(std::iswalnum(ch) || ch == L'-' || ch == L'_')) ch = L'_';
    }
    return value;
}

fs::path ReportDirectory() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path root = (length > 0 && length < std::size(local))
        ? fs::path(local) / L"TuringDesk"
        : fs::temp_directory_path() / L"TuringDesk";
    return root / L"Diagnostics";
}

bool InteractiveDesktopAvailable() {
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (!desktop) return false;
    CloseDesktop(desktop);
    return true;
}

std::wstring BoolText(bool value) {
    return value ? L"true" : L"false";
}

std::wstring BuildReport(std::wstring_view phase, const WidgetRuntimeHealth& health) {
    std::wostringstream out;
    out << L"TuringDesk M3 Widget Runtime Acceptance\n";
    out << L"phase=" << SafePhase(phase) << L"\n";
    out << L"configuredCount=" << health.configuredCount << L"\n";
    out << L"enabledWebCount=" << health.enabledWebCount << L"\n";
    out << L"runtimeReported=" << BoolText(health.runtimeReported) << L"\n";
    out << L"runtimeHealthy=" << BoolText(health.runtimeHealthy) << L"\n";
    out << L"detail=" << health.detail << L"\n";
    for (const auto& surface : health.surfaces) {
        out << L"\n[widget " << surface.widgetId << L"]\n";
        out << L"pid=" << surface.processId << L"\n";
        out << L"hwnd=0x" << std::hex << surface.hwndValue << std::dec << L"\n";
        out << L"processRunning=" << BoolText(surface.processRunning) << L"\n";
        out << L"hwndReady=" << BoolText(surface.hwndReady) << L"\n";
        out << L"parentValid=" << BoolText(surface.parentValid) << L"\n";
        out << L"childStyleValid=" << BoolText(surface.childStyleValid) << L"\n";
        out << L"visible=" << BoolText(surface.visible) << L"\n";
        out << L"environmentReady=" << BoolText(surface.environmentReady) << L"\n";
        out << L"controllerReady=" << BoolText(surface.controllerReady) << L"\n";
        out << L"navigationReady=" << BoolText(surface.navigationReady) << L"\n";
        out << L"zOrderValid=" << BoolText(surface.zOrderValid) << L"\n";
        out << L"renderingHealthy=" << BoolText(surface.renderingHealthy) << L"\n";
        out << L"issueCode=" << surface.issueCode << L"\n";
        out << L"detail=" << surface.detail << L"\n";
        out << L"recommendedAction=" << surface.recommendedAction << L"\n";
    }
    return out.str();
}

bool WriteUtf16Report(const fs::path& path, const std::wstring& report) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    constexpr unsigned char bom[] = {0xFF, 0xFE};
    stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    stream.write(reinterpret_cast<const char*>(report.data()), static_cast<std::streamsize>(report.size() * sizeof(wchar_t)));
    return stream.good();
}

} // namespace

WidgetRuntimeAcceptanceCode RunWidgetRuntimeAcceptanceProbe(
    std::wstring_view phase,
    std::wstring* reportPath,
    std::wstring* failure) {
    if (!InteractiveDesktopAvailable()) {
        if (failure) *failure = L"当前进程无法访问交互式 Windows input desktop；M3 可视验收不能在非交互会话中冒充通过。";
        return WidgetRuntimeAcceptanceCode::InteractiveDesktopUnavailable;
    }

    WidgetRuntimeHealth health;
    const WidgetService service;
    const auto result = service.GetRuntimeHealth(&health);
    if (!result.success) {
        if (failure) *failure = result.message;
        return WidgetRuntimeAcceptanceCode::RuntimeHealthUnavailable;
    }

    const fs::path path = ReportDirectory() / (L"widget-acceptance-" + SafePhase(phase) + L".txt");
    if (!WriteUtf16Report(path, BuildReport(phase, health))) {
        if (failure) *failure = L"无法写入 Widget acceptance report：" + path.wstring();
        return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
    }
    if (reportPath) *reportPath = path.wstring();

    if (health.enabledWebCount == 0) {
        if (failure) *failure = L"没有启用的 Web Widget；先创建/启用一个 Widget 再执行 M3 可视验收。";
        return WidgetRuntimeAcceptanceCode::NoEnabledWebWidget;
    }
    if (!health.runtimeReported || !health.runtimeHealthy || health.surfaces.size() != health.enabledWebCount) {
        if (failure) *failure = L"Widget runtime aggregate health 未通过；查看 acceptance report 的 issueCode/recommendedAction。";
        return WidgetRuntimeAcceptanceCode::SurfaceUnhealthy;
    }
    for (const auto& surface : health.surfaces) {
        if (!surface.renderingHealthy) {
            if (failure) *failure = L"至少一个 Widget Surface 未通过 rendering health；查看 acceptance report。";
            return WidgetRuntimeAcceptanceCode::SurfaceUnhealthy;
        }
    }
    return WidgetRuntimeAcceptanceCode::Passed;
}

} // namespace turingdesk::desktop
