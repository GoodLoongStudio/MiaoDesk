#include "turingdesk/WidgetRuntimeAcceptance.h"

#include "turingdesk/WidgetService.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

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

// Cross-phase identity semantics are documented in
// docs/WIDGET_ACCEPTANCE_SEQUENCE_M3.md. PID/HWND continuity is intentionally
// excluded because Explorer recovery may recreate runtime surfaces.
fs::path BaselinePath() {
    return ReportDirectory() / L"widget-acceptance-baseline.ids";
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

std::vector<std::wstring> SurfaceIds(const WidgetRuntimeHealth& health) {
    std::vector<std::wstring> ids;
    ids.reserve(health.surfaces.size());
    for (const auto& surface : health.surfaces) ids.push_back(surface.widgetId);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::wstring JoinIds(const std::vector<std::wstring>& ids) {
    std::wostringstream out;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) out << L',';
        out << ids[i];
    }
    return out.str();
}

std::wstring BuildReport(
    std::wstring_view phase,
    const WidgetRuntimeHealth& health,
    std::wstring_view baselineStatus) {
    std::wostringstream out;
    out << L"TuringDesk M3 Widget Runtime Acceptance\n";
    out << L"phase=" << SafePhase(phase) << L"\n";
    out << L"baselineStatus=" << baselineStatus << L"\n";
    out << L"surfaceIds=" << JoinIds(SurfaceIds(health)) << L"\n";
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

bool WriteBaselineIds(const std::vector<std::wstring>& ids) {
    return WriteUtf16Report(BaselinePath(), JoinIds(ids));
}

bool ReadBaselineIds(std::vector<std::wstring>* ids) {
    if (!ids) return false;
    ids->clear();
    std::ifstream stream(BaselinePath(), std::ios::binary);
    if (!stream) return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) return false;
    const std::size_t wcharBytes = bytes.size() - 2;
    if (wcharBytes % sizeof(wchar_t) != 0) return false;
    std::wstring text(wcharBytes / sizeof(wchar_t), L'\0');
    if (wcharBytes) std::memcpy(text.data(), bytes.data() + 2, wcharBytes);
    std::wstringstream parser(text);
    std::wstring id;
    while (std::getline(parser, id, L',')) {
        if (!id.empty()) ids->push_back(id);
    }
    std::sort(ids->begin(), ids->end());
    return true;
}

WidgetRuntimeAcceptanceCode CheckPhaseContinuity(
    std::wstring_view phase,
    const WidgetRuntimeHealth& health,
    std::wstring* baselineStatus,
    std::wstring* failure) {
    const std::wstring safePhase = SafePhase(phase);
    const auto currentIds = SurfaceIds(health);
    if (safePhase == L"baseline") {
        if (!WriteBaselineIds(currentIds)) {
            if (failure) *failure = L"无法写入 Widget acceptance baseline identity set：" + BaselinePath().wstring();
            return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
        }
        if (baselineStatus) *baselineStatus = L"recorded";
        return WidgetRuntimeAcceptanceCode::Passed;
    }

    std::vector<std::wstring> baselineIds;
    if (!ReadBaselineIds(&baselineIds)) {
        if (failure) *failure = L"缺少可读取的 baseline identity set；请先执行 phase=baseline，再执行 settings/search/explorer/monitor。";
        if (baselineStatus) *baselineStatus = L"missing";
        return WidgetRuntimeAcceptanceCode::BaselineMissing;
    }
    if (baselineIds != currentIds) {
        if (failure) {
            *failure = L"当前启用 Widget identity set 与 baseline 不一致；baseline=" + JoinIds(baselineIds)
                + L" current=" + JoinIds(currentIds);
        }
        if (baselineStatus) *baselineStatus = L"mismatch";
        return WidgetRuntimeAcceptanceCode::BaselineMismatch;
    }
    if (baselineStatus) *baselineStatus = L"matched";
    return WidgetRuntimeAcceptanceCode::Passed;
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

    if (health.enabledWebCount == 0) {
        if (failure) *failure = L"没有启用的 Web Widget；先创建/启用一个 Widget 再执行 M3 可视验收。";
        return WidgetRuntimeAcceptanceCode::NoEnabledWebWidget;
    }

    std::wstring baselineStatus = L"unchecked";
    const auto continuity = CheckPhaseContinuity(phase, health, &baselineStatus, failure);
    const fs::path path = ReportDirectory() / (L"widget-acceptance-" + SafePhase(phase) + L".txt");
    if (!WriteUtf16Report(path, BuildReport(phase, health, baselineStatus))) {
        if (failure) *failure = L"无法写入 Widget acceptance report：" + path.wstring();
        return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
    }
    if (reportPath) *reportPath = path.wstring();
    if (continuity != WidgetRuntimeAcceptanceCode::Passed) return continuity;

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
