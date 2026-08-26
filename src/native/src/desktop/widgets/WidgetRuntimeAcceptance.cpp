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

fs::path BaselineSessionPath() {
    return ReportDirectory() / L"widget-acceptance-baseline.session";
}

fs::path SequencePath() {
    return ReportDirectory() / L"widget-acceptance-sequence.phase";
}

bool InteractiveDesktopAvailable() {
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (!desktop) return false;
    CloseDesktop(desktop);
    return true;
}

bool CurrentSessionId(DWORD* sessionId) {
    if (!sessionId) return false;
    return ProcessIdToSessionId(GetCurrentProcessId(), sessionId) != FALSE;
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

std::wstring ExpectedPreviousPhase(std::wstring_view phase) {
    const auto safe = SafePhase(phase);
    if (safe == L"settings") return L"baseline";
    if (safe == L"search") return L"settings";
    if (safe == L"explorer") return L"search";
    if (safe == L"monitor") return L"explorer";
    return {};
}

std::wstring BuildReport(
    std::wstring_view phase,
    const WidgetRuntimeHealth& health,
    std::wstring_view baselineStatus,
    std::wstring_view sessionStatus,
    DWORD sessionId,
    std::wstring_view sequenceStatus) {
    std::wostringstream out;
    out << L"TuringDesk M3 Widget Runtime Acceptance\n";
    out << L"phase=" << SafePhase(phase) << L"\n";
    out << L"baselineStatus=" << baselineStatus << L"\n";
    out << L"sessionStatus=" << sessionStatus << L"\n";
    out << L"sessionId=" << sessionId << L"\n";
    out << L"sequenceStatus=" << sequenceStatus << L"\n";
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
        out << L"monitorId=" << surface.monitorId << L"\n";
        out << L"monitorReported=" << BoolText(surface.monitorReported) << L"\n";
        out << L"monitorValid=" << BoolText(surface.monitorValid) << L"\n";
        out << L"geometryReported=" << BoolText(surface.geometryReported) << L"\n";
        out << L"geometryValid=" << BoolText(surface.geometryValid) << L"\n";
        out << L"expectedRect=" << surface.expectedLeft << L"," << surface.expectedTop << L","
            << surface.expectedRight << L"," << surface.expectedBottom << L"\n";
        out << L"actualRect=" << surface.actualLeft << L"," << surface.actualTop << L","
            << surface.actualRight << L"," << surface.actualBottom << L"\n";
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

bool ReadUtf16Text(const fs::path& path, std::wstring* text) {
    if (!text) return false;
    text->clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) return false;
    const std::size_t wcharBytes = bytes.size() - 2;
    if (wcharBytes % sizeof(wchar_t) != 0) return false;
    text->assign(wcharBytes / sizeof(wchar_t), L'\0');
    if (wcharBytes) std::memcpy(text->data(), bytes.data() + 2, wcharBytes);
    return true;
}

bool ReadBaselineIds(std::vector<std::wstring>* ids) {
    if (!ids) return false;
    ids->clear();
    std::wstring text;
    if (!ReadUtf16Text(BaselinePath(), &text)) return false;
    std::wstringstream parser(text);
    std::wstring id;
    while (std::getline(parser, id, L',')) {
        if (!id.empty()) ids->push_back(id);
    }
    std::sort(ids->begin(), ids->end());
    return true;
}

bool WriteBaselineSession(DWORD sessionId) {
    return WriteUtf16Report(BaselineSessionPath(), std::to_wstring(sessionId));
}

bool ReadBaselineSession(DWORD* sessionId) {
    if (!sessionId) return false;
    std::wstring text;
    if (!ReadUtf16Text(BaselineSessionPath(), &text)) return false;
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L'\0')) text.pop_back();
    if (text.empty()) return false;
    try {
        const unsigned long value = std::stoul(text);
        if (value > MAXDWORD) return false;
        *sessionId = static_cast<DWORD>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool WriteSequencePhase(std::wstring_view phase) {
    return WriteUtf16Report(SequencePath(), SafePhase(phase));
}

bool ReadSequencePhase(std::wstring* phase) {
    if (!ReadUtf16Text(SequencePath(), phase)) return false;
    while (!phase->empty() && (phase->back() == L'\r' || phase->back() == L'\n' || phase->back() == L'\0')) phase->pop_back();
    return !phase->empty();
}

WidgetRuntimeAcceptanceCode CheckPhaseContinuity(
    std::wstring_view phase,
    const WidgetRuntimeHealth& health,
    DWORD currentSessionId,
    std::wstring* baselineStatus,
    std::wstring* sessionStatus,
    std::wstring* sequenceStatus,
    std::wstring* failure) {
    const std::wstring safePhase = SafePhase(phase);
    const auto currentIds = SurfaceIds(health);
    if (safePhase == L"baseline") {
        std::error_code ec;
        fs::remove(SequencePath(), ec);
        if (ec) {
            if (failure) *failure = L"无法重置旧的 M3 acceptance sequence cursor：" + SequencePath().wstring();
            return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
        }
        if (!WriteBaselineIds(currentIds)) {
            if (failure) *failure = L"无法写入 Widget acceptance baseline identity set：" + BaselinePath().wstring();
            return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
        }
        if (!WriteBaselineSession(currentSessionId)) {
            if (failure) *failure = L"无法写入 Widget acceptance baseline Windows session：" + BaselineSessionPath().wstring();
            return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
        }
        if (baselineStatus) *baselineStatus = L"recorded";
        if (sessionStatus) *sessionStatus = L"recorded";
        if (sequenceStatus) *sequenceStatus = L"start";
        return WidgetRuntimeAcceptanceCode::Passed;
    }

    std::vector<std::wstring> baselineIds;
    if (!ReadBaselineIds(&baselineIds)) {
        if (failure) *failure = L"缺少可读取的 baseline identity set；请先执行 phase=baseline，再执行 settings/search/explorer/monitor。";
        if (baselineStatus) *baselineStatus = L"missing";
        if (sessionStatus) *sessionStatus = L"blocked";
        if (sequenceStatus) *sequenceStatus = L"blocked";
        return WidgetRuntimeAcceptanceCode::BaselineMissing;
    }
    if (baselineIds != currentIds) {
        if (failure) {
            *failure = L"当前启用 Widget identity set 与 baseline 不一致；baseline=" + JoinIds(baselineIds)
                + L" current=" + JoinIds(currentIds);
        }
        if (baselineStatus) *baselineStatus = L"mismatch";
        if (sessionStatus) *sessionStatus = L"blocked";
        if (sequenceStatus) *sequenceStatus = L"blocked";
        return WidgetRuntimeAcceptanceCode::BaselineMismatch;
    }
    if (baselineStatus) *baselineStatus = L"matched";

    DWORD baselineSessionId = 0;
    if (!ReadBaselineSession(&baselineSessionId)) {
        if (failure) *failure = L"缺少可读取的 baseline Windows session；请从当前交互会话重新执行 phase=baseline。";
        if (sessionStatus) *sessionStatus = L"missing";
        if (sequenceStatus) *sequenceStatus = L"blocked";
        return WidgetRuntimeAcceptanceCode::BaselineMissing;
    }
    if (baselineSessionId != currentSessionId) {
        if (failure) {
            *failure = L"当前 Windows session 与 baseline 不一致；baselineSession=" + std::to_wstring(baselineSessionId)
                + L" currentSession=" + std::to_wstring(currentSessionId)
                + L"。M3 五阶段必须在同一交互式 Windows session 内完成。";
        }
        if (sessionStatus) *sessionStatus = L"mismatch";
        if (sequenceStatus) *sequenceStatus = L"blocked";
        return WidgetRuntimeAcceptanceCode::BaselineMismatch;
    }
    if (sessionStatus) *sessionStatus = L"matched";

    const std::wstring expected = ExpectedPreviousPhase(safePhase);
    std::wstring previous;
    if (expected.empty() || !ReadSequencePhase(&previous) || previous != expected) {
        if (failure) {
            *failure = L"M3 acceptance phase 顺序错误；当前 phase=" + safePhase + L" 需要上一成功 phase="
                + (expected.empty() ? L"<unknown>" : expected) + L"，实际=" + (previous.empty() ? L"<missing>" : previous);
        }
        if (sequenceStatus) *sequenceStatus = L"out_of_order";
        return WidgetRuntimeAcceptanceCode::SequenceOutOfOrder;
    }
    if (sequenceStatus) *sequenceStatus = L"previous=" + previous;
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

    DWORD sessionId = 0;
    if (!CurrentSessionId(&sessionId)) {
        if (failure) *failure = L"无法解析当前 Windows session；M3 五阶段无法证明来自同一交互式桌面会话。";
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
    std::wstring sessionStatus = L"unchecked";
    std::wstring sequenceStatus = L"unchecked";
    const auto continuity = CheckPhaseContinuity(
        phase, health, sessionId, &baselineStatus, &sessionStatus, &sequenceStatus, failure);
    const fs::path path = ReportDirectory() / (L"widget-acceptance-" + SafePhase(phase) + L".txt");
    if (!WriteUtf16Report(path, BuildReport(phase, health, baselineStatus, sessionStatus, sessionId, sequenceStatus))) {
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

    if (!WriteSequencePhase(phase)) {
        if (failure) *failure = L"无法写入 M3 acceptance sequence cursor：" + SequencePath().wstring();
        return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
    }
    return WidgetRuntimeAcceptanceCode::Passed;
}

} // namespace turingdesk::desktop