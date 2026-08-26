#include "turingdesk/DesktopWidgetTools.h"

#include "turingdesk/DesktopControlService.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

int Hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

void AppendCodepoint(std::string& out, unsigned cp) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::optional<std::string> JsonString(std::string_view json, std::string_view key) {
    auto pos = json.find('"' + std::string(key) + '"');
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string_view::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;

    std::string out;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') return out;
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (pos >= json.size()) return std::nullopt;
        const char esc = json[pos++];
        switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            if (pos + 4 > json.size()) return std::nullopt;
            unsigned cp = 0;
            for (int i = 0; i < 4; ++i) {
                const int value = Hex(json[pos + static_cast<std::size_t>(i)]);
                if (value < 0) return std::nullopt;
                cp = (cp << 4) | static_cast<unsigned>(value);
            }
            pos += 4;
            if (cp >= 0xd800 && cp <= 0xdbff && pos + 6 <= json.size() && json[pos] == '\\' && json[pos + 1] == 'u') {
                unsigned low = 0;
                bool valid = true;
                for (int i = 0; i < 4; ++i) {
                    const int value = Hex(json[pos + 2 + static_cast<std::size_t>(i)]);
                    if (value < 0) { valid = false; break; }
                    low = (low << 4) | static_cast<unsigned>(value);
                }
                if (valid && low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    pos += 6;
                }
            }
            AppendCodepoint(out, cp);
            break;
        }
        default: out.push_back(esc); break;
        }
    }
    return std::nullopt;
}

std::optional<double> JsonNumber(std::string_view json, std::string_view key) {
    auto pos = json.find('"' + std::string(key) + '"');
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string_view::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    const std::size_t start = pos;
    while (pos < json.size()) {
        const char ch = json[pos];
        if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')) break;
        ++pos;
    }
    if (pos == start) return std::nullopt;
    try {
        const double value = std::stod(std::string(json.substr(start, pos - start)));
        return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> JsonBool(std::string_view json, std::string_view key) {
    auto pos = json.find('"' + std::string(key) + '"');
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string_view::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return std::nullopt;
}

NativeToolResult ToNative(desktop::DesktopControlResult result) {
    return {result.success, std::move(result.message)};
}

const desktop::WidgetSurfaceHealth* FindSurfaceHealth(
    const desktop::WidgetRuntimeHealth& health,
    std::wstring_view widgetId) {
    for (const auto& surface : health.surfaces) {
        if (surface.widgetId == widgetId) return &surface;
    }
    return nullptr;
}

void AppendSurfaceHealth(std::wostringstream& text, const desktop::WidgetSurfaceHealth& surface) {
    text << L"\r\n  surface id=" << surface.widgetId
         << L"; rendering=" << (surface.renderingHealthy ? L"healthy" : L"attention")
         << L"; pid=" << surface.processId
         << L"; hwnd=" << surface.hwndValue
         << L"; parent=" << (surface.parentValid ? L"ok" : L"bad")
         << L"; child_style=" << (surface.childStyleValid ? L"ok" : L"bad")
         << L"; visible=" << (surface.visible ? L"true" : L"false")
         << L"; monitor=" << (surface.monitorId.empty() ? L"unresolved" : surface.monitorId)
         << L"; monitor_state=" << (surface.monitorReported ? (surface.monitorValid ? L"ok" : L"bad") : L"unreported")
         << L"; geometry=" << (surface.geometryReported ? (surface.geometryValid ? L"ok" : L"bad") : L"unreported")
         << L"; expected_rect=" << surface.expectedLeft << L"," << surface.expectedTop << L"," << surface.expectedRight << L"," << surface.expectedBottom
         << L"; actual_rect=" << surface.actualLeft << L"," << surface.actualTop << L"," << surface.actualRight << L"," << surface.actualBottom
         << L"; environment=" << (surface.environmentReported ? (surface.environmentReady ? L"ready" : L"pending") : L"unreported")
         << L"; controller=" << (surface.controllerReported ? (surface.controllerReady ? L"ready" : L"pending") : L"unreported")
         << L"; navigation=" << (surface.navigationReported ? (surface.navigationReady ? L"ready" : L"pending") : L"unreported")
         << L"; zorder=" << (surface.zOrderReported ? (surface.zOrderValid ? L"ok" : L"bad") : L"unreported");
    if (!surface.issueCode.empty()) text << L"; issue=" << surface.issueCode;
    if (!surface.detail.empty()) text << L"; detail=" << surface.detail;
    if (!surface.recommendedAction.empty()) text << L"; action=" << surface.recommendedAction;
}

NativeToolResult WallpaperStateGet() {
    desktop::DesktopControlService service;
    desktop::DesktopSnapshot snapshot;
    const auto result = service.GetSnapshot(&snapshot);
    if (!result.success) return ToNative(result);
    const auto& state = snapshot.desktop;

    std::wostringstream text;
    text << L"当前桌面状态：scene=" << state.scene
         << L"; layout=" << state.layout
         << L"; scale=" << state.scale
         << L"; fps=" << state.fpsCap
         << L"; widgets=" << state.widgetCount
         << L"; widget_runtime=" << (snapshot.widgetRuntime.Healthy() ? L"healthy" : L"attention")
         << L"; widget_enabled_web=" << snapshot.widgetRuntime.enabledWebCount
         << L"; widget_surfaces=" << snapshot.widgetRuntime.surfaces.size();
    if (!state.imageOrWebSource.empty()) text << L"; image/web=" << state.imageOrWebSource;
    if (!state.videoSource.empty()) text << L"; video=" << state.videoSource;
    if (!snapshot.widgetRuntime.detail.empty()) text << L"; widget_detail=" << snapshot.widgetRuntime.detail;
    for (const auto& surface : snapshot.widgetRuntime.surfaces) AppendSurfaceHealth(text, surface);
    return {true, text.str()};
}

NativeToolResult WallpaperApplyWebPackage(std::string_view arguments) {
    const auto rawPath = JsonString(arguments, "path");
    if (!rawPath || rawPath->empty()) return {false, L"缺少 .tdwall package path。"};
    desktop::DesktopControlService service;
    return ToNative(service.ApplyWebPackage(fs::path(Utf8ToWide(*rawPath))));
}

NativeToolResult WidgetCreate(std::string_view arguments) {
    const auto html = JsonString(arguments, "html");
    if (!html || html->empty()) return {false, L"缺少 desktop widget HTML。"};

    desktop::WebWidgetCreateRequest request;
    if (const auto title = JsonString(arguments, "title")) request.title = Utf8ToWide(*title);
    if (const auto monitor = JsonString(arguments, "monitor_id")) request.monitorId = Utf8ToWide(*monitor);
    request.htmlUtf8 = *html;
    request.x = static_cast<float>(JsonNumber(arguments, "x").value_or(0.68));
    request.y = static_cast<float>(JsonNumber(arguments, "y").value_or(0.05));
    request.width = static_cast<float>(JsonNumber(arguments, "width").value_or(0.28));
    request.height = static_cast<float>(JsonNumber(arguments, "height").value_or(0.18));

    desktop::DesktopControlService service;
    wallpaper::DesktopWidget created;
    const auto result = service.CreateWebWidget(request, &created);
    if (!result.success) return ToNative(result);

    std::wostringstream text;
    text << L"桌面小组件已创建：id=" << created.id << L"; title=" << created.title
         << L"; rect=" << created.x << L"," << created.y << L"," << created.width << L"," << created.height;
    return {true, text.str()};
}

NativeToolResult WidgetUpdate(std::string_view arguments) {
    const auto id = JsonString(arguments, "id");
    if (!id || id->empty()) return {false, L"缺少 desktop widget id。"};

    desktop::WidgetUpdateRequest request;
    request.id = Utf8ToWide(*id);
    if (const auto value = JsonString(arguments, "title")) request.title = Utf8ToWide(*value);
    if (const auto value = JsonString(arguments, "html")) request.htmlUtf8 = *value;
    if (const auto value = JsonString(arguments, "monitor_id")) request.monitorId = Utf8ToWide(*value);
    if (const auto value = JsonNumber(arguments, "x")) request.x = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "y")) request.y = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "width")) request.width = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "height")) request.height = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "z_index")) request.zIndex = static_cast<int>(*value);
    if (const auto value = JsonBool(arguments, "enabled")) request.enabled = *value;

    desktop::DesktopControlService service;
    return ToNative(service.UpdateWidget(request));
}

NativeToolResult WidgetRemove(std::string_view arguments) {
    const auto id = JsonString(arguments, "id");
    if (!id || id->empty()) return {false, L"缺少 desktop widget id。"};
    desktop::DesktopControlService service;
    return ToNative(service.RemoveWidget(Utf8ToWide(*id)));
}

NativeToolResult WidgetList() {
    desktop::DesktopControlService service;
    desktop::DesktopSnapshot snapshot;
    const auto result = service.GetSnapshot(&snapshot);
    if (!result.success) return ToNative(result);

    std::wostringstream text;
    text << L"桌面小组件：" << snapshot.widgets.size();
    for (const auto& widget : snapshot.widgets) {
        text << L"\r\n- id=" << widget.id << L"; title=" << widget.title
             << L"; enabled=" << (widget.enabled ? L"true" : L"false")
             << L"; monitor=" << (widget.monitorId.empty() ? L"primary" : widget.monitorId)
             << L"; rect=" << widget.x << L"," << widget.y << L"," << widget.width << L"," << widget.height;
        if (const auto* surface = FindSurfaceHealth(snapshot.widgetRuntime, widget.id)) {
            text << L"; runtime=" << (surface->renderingHealthy ? L"healthy" : L"attention")
                 << L"; resolved_monitor=" << (surface->monitorId.empty() ? L"unresolved" : surface->monitorId)
                 << L"; monitor_state=" << (surface->monitorReported ? (surface->monitorValid ? L"ok" : L"bad") : L"unreported")
                 << L"; geometry=" << (surface->geometryReported ? (surface->geometryValid ? L"ok" : L"bad") : L"unreported")
                 << L"; expected_rect=" << surface->expectedLeft << L"," << surface->expectedTop << L"," << surface->expectedRight << L"," << surface->expectedBottom
                 << L"; actual_rect=" << surface->actualLeft << L"," << surface->actualTop << L"," << surface->actualRight << L"," << surface->actualBottom;
            if (!surface->issueCode.empty()) text << L"; issue=" << surface->issueCode;
            if (!surface->recommendedAction.empty()) text << L"; action=" << surface->recommendedAction;
        }
    }
    return {true, text.str()};
}

} // namespace

bool IsDesktopControlTool(std::string_view toolName) noexcept {
    return toolName == "wallpaper_state_get" ||
           toolName == "wallpaper_apply_web_package" ||
           toolName == "desktop_widget_create_web" ||
           toolName == "desktop_widget_update" ||
           toolName == "desktop_widget_remove" ||
           toolName == "desktop_widget_list";
}

NativeToolResult ExecuteDesktopControlTool(std::string_view toolName, std::string_view argumentsJson) {
    if (toolName == "wallpaper_state_get") return WallpaperStateGet();
    if (toolName == "wallpaper_apply_web_package") return WallpaperApplyWebPackage(argumentsJson);
    if (toolName == "desktop_widget_create_web") return WidgetCreate(argumentsJson);
    if (toolName == "desktop_widget_update") return WidgetUpdate(argumentsJson);
    if (toolName == "desktop_widget_remove") return WidgetRemove(argumentsJson);
    if (toolName == "desktop_widget_list") return WidgetList();
    return {false, L"未知桌面控制工具。"};
}

} // namespace turingdesk