#include "turingdesk/WidgetRuntimeAcceptance.h"

#include "turingdesk/WidgetService.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::desktop {
namespace {

fs::path DiagnosticsDirectory() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path root = (length > 0 && length < std::size(local))
        ? fs::path(local) / L"TuringDesk"
        : fs::temp_directory_path() / L"TuringDesk";
    return root / L"Diagnostics";
}

fs::path BaselineConfigPath() {
    return DiagnosticsDirectory() / L"widget-acceptance-baseline.config";
}

void AppendSized(std::wostringstream& out, std::wstring_view value) {
    out << value.size() << L':' << value;
}

std::wstring CanonicalPlacementConfig(const std::vector<wallpaper::DesktopWidget>& widgets) {
    std::vector<wallpaper::DesktopWidget> enabled;
    for (const auto& widget : widgets) {
        if (widget.enabled &&
            (widget.kind == wallpaper::DesktopWidgetKind::Web || widget.kind == wallpaper::DesktopWidgetKind::Native)) {
            enabled.push_back(widget);
        }
    }
    std::sort(enabled.begin(), enabled.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

    std::wostringstream out;
    out << L"turingdesk.widget-acceptance-config.v2\n";
    for (const auto& widget : enabled) {
        AppendSized(out, widget.id);
        out << L'|';
        AppendSized(out, widget.title);
        out << L'|';
        AppendSized(out, widget.monitorId);
        out << L"|kind=" << static_cast<int>(widget.kind)
            << L"|x=" << std::bit_cast<std::uint32_t>(widget.x)
            << L"|y=" << std::bit_cast<std::uint32_t>(widget.y)
            << L"|w=" << std::bit_cast<std::uint32_t>(widget.width)
            << L"|h=" << std::bit_cast<std::uint32_t>(widget.height)
            << L"|z=" << widget.zIndex
            << L"|enabled=" << (widget.enabled ? 1 : 0) << L'\n';
    }
    return out.str();
}

bool WriteUtf16(const fs::path& path, const std::wstring& value) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    constexpr unsigned char bom[] = {0xFF, 0xFE};
    stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    stream.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size() * sizeof(wchar_t)));
    return stream.good();
}

bool ReadUtf16(const fs::path& path, std::wstring* value) {
    if (!value) return false;
    value->clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) return false;
    const std::size_t wcharBytes = bytes.size() - 2;
    if (wcharBytes % sizeof(wchar_t) != 0) return false;
    value->assign(wcharBytes / sizeof(wchar_t), L'\0');
    if (wcharBytes) std::memcpy(value->data(), bytes.data() + 2, wcharBytes);
    return true;
}

} // namespace

WidgetRuntimeAcceptanceCode CheckWidgetAcceptanceConfigContinuity(
    std::wstring_view phase,
    bool recordBaseline,
    std::wstring* failure) {
    const std::wstring phaseName = phase.empty() ? L"baseline" : std::wstring(phase);
    std::vector<wallpaper::DesktopWidget> widgets;
    const WidgetService service;
    const auto result = service.List(&widgets);
    if (!result.success) {
        if (failure) *failure = L"无法读取 Widget 配置连续性状态（phase=" + phaseName + L"）：" + result.message;
        return WidgetRuntimeAcceptanceCode::RuntimeHealthUnavailable;
    }

    const std::wstring current = CanonicalPlacementConfig(widgets);
    const fs::path path = BaselineConfigPath();
    if (recordBaseline) {
        if (!WriteUtf16(path, current)) {
            if (failure) *failure = L"无法写入 M3 Widget placement config baseline：" + path.wstring();
            return WidgetRuntimeAcceptanceCode::ReportWriteFailed;
        }
        return WidgetRuntimeAcceptanceCode::Passed;
    }

    std::wstring baseline;
    if (!ReadUtf16(path, &baseline)) {
        if (failure) *failure = L"缺少 M3 Widget placement config baseline；请从 baseline phase 重新开始完整验收。";
        return WidgetRuntimeAcceptanceCode::BaselineMissing;
    }
    if (baseline != current) {
        if (failure) {
            *failure = L"M3 Widget showcase 配置在 baseline 后发生变化（phase=" + phaseName +
                L"）；id/title/monitorId/位置/尺寸/zIndex/enabled/kind 必须保持不变。请重新执行 baseline。";
        }
        return WidgetRuntimeAcceptanceCode::BaselineMismatch;
    }
    return WidgetRuntimeAcceptanceCode::Passed;
}

} // namespace turingdesk::desktop
