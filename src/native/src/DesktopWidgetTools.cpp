#include "turingdesk/DesktopWidgetTools.h"

#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/WallpaperPackage.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr wchar_t kWallpaperControlClass[] = L"TuringDesk.Native.WallpaperControl";

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

bool HasJsonKey(std::string_view json, std::string_view key) {
    return json.find('"' + std::string(key) + '"') != std::string_view::npos;
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

fs::path LocalTuringDeskDirectory() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path base = (length > 0 && length < std::size(local)) ? fs::path(local) : fs::temp_directory_path();
    fs::path directory = base / L"TuringDesk";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory;
}

fs::path ModuleDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return fs::path(path).parent_path();
}

void EnsureWallpaperRuntime() {
    if (FindWindowW(kWallpaperControlClass, nullptr)) return;
    const fs::path executable = ModuleDirectory() / L"TuringDeskWallpaper.exe";
    std::error_code ec;
    if (fs::exists(executable, ec) && fs::is_regular_file(executable, ec))
        ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, executable.parent_path().c_str(), SW_SHOWNOACTIVATE);
}

std::wstring ReadProfile(const fs::path& path, const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(L"Wallpaper", key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

NativeToolResult WallpaperStateGet() {
    const fs::path config = LocalTuringDeskDirectory() / L"wallpaper.ini";
    wallpaper::DesktopWidgetStore widgets;
    std::wstring error;
    if (!widgets.Load(&error)) return {false, error};

    std::wostringstream text;
    text << L"当前桌面状态：scene=" << ReadProfile(config, L"Scene", L"aurora")
         << L"; layout=" << ReadProfile(config, L"Layout", L"span")
         << L"; scale=" << ReadProfile(config, L"Scale", L"cover")
         << L"; fps=" << GetPrivateProfileIntW(L"Wallpaper", L"FpsCap", 30, config.c_str())
         << L"; widgets=" << widgets.Items().size();
    const std::wstring image = ReadProfile(config, L"Image", L"");
    const std::wstring video = ReadProfile(config, L"Video", L"");
    if (!image.empty()) text << L"; image/web=" << image;
    if (!video.empty()) text << L"; video=" << video;
    return {true, text.str()};
}

NativeToolResult WallpaperApplyWebPackage(std::string_view arguments) {
    const auto rawPath = JsonString(arguments, "path");
    if (!rawPath || rawPath->empty()) return {false, L"缺少 .tdwall package path。"};
    const fs::path package(Utf8ToWide(*rawPath));
    wallpaper::WallpaperPackageManifest manifest;
    std::wstring error;
    if (!wallpaper::WallpaperPackage::Validate(package, &manifest, &error))
        return {false, error.empty() ? L".tdwall 校验失败。" : error};
    if (manifest.type != wallpaper::WallpaperPackageType::Web)
        return {false, L"当前 AI 直接应用接口只接受 Web 类型 .tdwall；其他类型走桌面库。"};

    std::error_code ec;
    const fs::path source = fs::absolute(package / manifest.entry, ec).lexically_normal();
    if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec))
        return {false, L".tdwall Web entry 不存在。"};

    const fs::path config = LocalTuringDeskDirectory() / L"wallpaper.ini";
    bool ok = true;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Enabled", L"1", config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Scene", L"web", config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Image", source.c_str(), config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Video", L"", config.c_str()) != FALSE && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    if (!ok) return {false, L"无法保存当前 Web 桌面状态。"};
    EnsureWallpaperRuntime();
    return {true, L"已应用 Web 桌面：" + manifest.title + L" · " + source.wstring()};
}

NativeToolResult WidgetCreate(std::string_view arguments) {
    const auto html = JsonString(arguments, "html");
    if (!html || html->empty()) return {false, L"缺少 desktop widget HTML。"};
    const auto titleRaw = JsonString(arguments, "title");
    const auto monitorRaw = JsonString(arguments, "monitor_id");
    const float x = static_cast<float>(JsonNumber(arguments, "x").value_or(0.68));
    const float y = static_cast<float>(JsonNumber(arguments, "y").value_or(0.05));
    const float width = static_cast<float>(JsonNumber(arguments, "width").value_or(0.28));
    const float height = static_cast<float>(JsonNumber(arguments, "height").value_or(0.18));

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return {false, error};
    auto created = store.CreateManagedWeb(
        titleRaw ? Utf8ToWide(*titleRaw) : L"AI Desktop Widget", *html,
        monitorRaw ? Utf8ToWide(*monitorRaw) : L"", x, y, width, height, &error);
    if (!created) return {false, error.empty() ? L"创建桌面小组件失败。" : error};
    EnsureWallpaperRuntime();
    std::wostringstream text;
    text << L"桌面小组件已创建：id=" << created->id << L"; title=" << created->title
         << L"; rect=" << created->x << L"," << created->y << L"," << created->width << L"," << created->height;
    return {true, text.str()};
}

NativeToolResult WidgetUpdate(std::string_view arguments) {
    const auto idRaw = JsonString(arguments, "id");
    if (!idRaw || idRaw->empty()) return {false, L"缺少 desktop widget id。"};
    const std::wstring id = Utf8ToWide(*idRaw);

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return {false, error};
    auto existing = store.Find(id);
    if (!existing) return {false, L"没有找到桌面小组件：" + id};
    auto widget = *existing;

    if (const auto title = JsonString(arguments, "title")) widget.title = Utf8ToWide(*title);
    if (const auto monitor = JsonString(arguments, "monitor_id")) widget.monitorId = Utf8ToWide(*monitor);
    if (const auto value = JsonNumber(arguments, "x")) widget.x = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "y")) widget.y = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "width")) widget.width = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "height")) widget.height = static_cast<float>(*value);
    if (const auto value = JsonNumber(arguments, "z_index")) widget.zIndex = static_cast<int>(*value);
    if (const auto value = JsonBool(arguments, "enabled")) widget.enabled = *value;

    auto saved = store.Upsert(widget, &error);
    if (!saved) return {false, error.empty() ? L"更新桌面小组件失败。" : error};
    if (HasJsonKey(arguments, "html")) {
        const auto html = JsonString(arguments, "html");
        if (!html || html->empty()) return {false, L"desktop widget html 不能为空。"};
        if (!store.UpdateManagedHtml(id, *html, &error)) return {false, error};
    }
    EnsureWallpaperRuntime();
    return {true, L"桌面小组件已更新：" + id};
}

NativeToolResult WidgetRemove(std::string_view arguments) {
    const auto idRaw = JsonString(arguments, "id");
    if (!idRaw || idRaw->empty()) return {false, L"缺少 desktop widget id。"};
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return {false, error};
    const std::wstring id = Utf8ToWide(*idRaw);
    if (!store.Remove(id, true, &error)) return {false, error};
    return {true, L"桌面小组件已删除：" + id};
}

NativeToolResult WidgetList() {
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return {false, error};
    std::wostringstream text;
    text << L"桌面小组件：" << store.Items().size();
    for (const auto& widget : store.Items()) {
        text << L"\r\n- id=" << widget.id << L"; title=" << widget.title
             << L"; enabled=" << (widget.enabled ? L"true" : L"false")
             << L"; monitor=" << (widget.monitorId.empty() ? L"primary" : widget.monitorId)
             << L"; rect=" << widget.x << L"," << widget.y << L"," << widget.width << L"," << widget.height;
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
