#include "miaodesk/WallpaperPackage.h"
#include "miaodesk/MiaoContentPackage.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace miaodesk::wallpaper {
namespace {

void SetError(std::wstring* error, std::wstring value) {
    if (error) *error = std::move(value);
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

std::string EscapeJson(std::wstring_view value) {
    const auto utf8 = WideToUtf8(value);
    std::string out;
    out.reserve(utf8.size() + 16);
    for (unsigned char ch : utf8) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char text[7]{};
                sprintf_s(text, "\\u%04x", static_cast<unsigned>(ch));
                out += text;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    return out;
}

std::string ReadTextFile(const fs::path& path, std::size_t maxBytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > maxBytes) return {};
    input.seekg(0, std::ios::beg);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string ExtractJsonString(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return {};
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;

    std::string out;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') return out;
        if (ch != '\\') { out.push_back(ch); continue; }
        if (pos >= json.size()) break;
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
        default: out.push_back(esc); break;
        }
    }
    return {};
}

int ExtractJsonInt(std::string_view json, std::string_view key, int fallback) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return fallback;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') { negative = true; ++pos; }
    int value = 0;
    bool any = false;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        any = true;
        value = value * 10 + (json[pos++] - '0');
    }
    return any ? (negative ? -value : value) : fallback;
}

bool ExtractJsonBool(std::string_view json, std::string_view key, bool fallback) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return fallback;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return fallback;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool IsSafeRelativeEntry(const fs::path& entry) {
    if (entry.empty() || entry.is_absolute() || entry.has_root_name() || entry.has_root_directory()) return false;
    for (const auto& part : entry) {
        if (part == L"..") return false;
    }
    return true;
}

bool IsInside(const fs::path& candidate, const fs::path& root) {
    std::error_code ec;
    auto base = fs::weakly_canonical(root, ec);
    if (ec) base = fs::absolute(root, ec).lexically_normal();
    ec.clear();
    auto child = fs::weakly_canonical(candidate, ec);
    if (ec) child = fs::absolute(candidate, ec).lexically_normal();
    const auto baseText = Lower(base.wstring());
    const auto childText = Lower(child.wstring());
    if (childText == baseText) return true;
    std::wstring prefix = baseText;
    if (!prefix.empty() && prefix.back() != L'\\' && prefix.back() != L'/') prefix.push_back(fs::path::preferred_separator);
    return childText.size() >= prefix.size() && childText.compare(0, prefix.size(), prefix) == 0;
}

std::string MakeGeneratedThemeId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return "com.goodloong.miaodesk.theme.web.fallback-" +
               std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    }
    char suffix[64]{};
    sprintf_s(suffix, "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
              static_cast<unsigned long>(guid.Data1),
              static_cast<unsigned>(guid.Data2),
              static_cast<unsigned>(guid.Data3),
              static_cast<unsigned>(guid.Data4[0]), static_cast<unsigned>(guid.Data4[1]),
              static_cast<unsigned>(guid.Data4[2]), static_cast<unsigned>(guid.Data4[3]),
              static_cast<unsigned>(guid.Data4[4]), static_cast<unsigned>(guid.Data4[5]),
              static_cast<unsigned>(guid.Data4[6]), static_cast<unsigned>(guid.Data4[7]));
    return std::string("com.goodloong.miaodesk.theme.web.") + suffix;
}

bool WriteManifest(const fs::path& path, const WallpaperPackageManifest& manifest,
                   std::string_view stableId) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    const std::string runtime = WideToUtf8(WallpaperPackage::TypeKey(manifest.type));
    output << "{\n"
           << "  \"schema\": " << manifest.schema << ",\n"
           << "  \"id\": \"" << stableId << "\",\n"
           << "  \"name\": \"" << EscapeJson(manifest.title) << "\",\n"
           << "  \"title\": \"" << EscapeJson(manifest.title) << "\",\n"
           << "  \"author\": \"" << EscapeJson(manifest.author) << "\",\n"
           << "  \"version\": \"1.0.0\",\n"
           << "  \"kind\": \"wallpaper\",\n"
           << "  \"runtime\": \"" << runtime << "\",\n"
           << "  \"type\": \"" << runtime << "\",\n"
           << "  \"entry\": \"" << EscapeJson(manifest.entry.generic_wstring()) << "\",\n"
           << "  \"provenance\": \"" << EscapeJson(manifest.provenance) << "\",\n"
           << "  \"fps_cap\": " << manifest.fpsCap << ",\n"
           << "  \"audio\": " << (manifest.audio ? "true" : "false") << ",\n"
           << "  \"capabilities\": [\"theme.wallpaper\"]\n"
           << "}\n";
    return static_cast<bool>(output);
}

} // namespace

const wchar_t* WallpaperPackage::TypeKey(WallpaperPackageType type) noexcept {
    switch (type) {
    case WallpaperPackageType::Image: return L"image";
    case WallpaperPackageType::Video: return L"video";
    case WallpaperPackageType::Web: return L"web";
    case WallpaperPackageType::Scene: return L"scene";
    case WallpaperPackageType::Unknown: break;
    }
    return L"unknown";
}

WallpaperPackageType WallpaperPackage::ParseType(std::wstring_view value) noexcept {
    const auto lower = Lower(std::wstring(value));
    if (lower == L"image") return WallpaperPackageType::Image;
    if (lower == L"video") return WallpaperPackageType::Video;
    if (lower == L"web") return WallpaperPackageType::Web;
    if (lower == L"scene") return WallpaperPackageType::Scene;
    return WallpaperPackageType::Unknown;
}

bool WallpaperPackage::CreateWeb(const fs::path& packageDirectory, std::wstring title,
                                 std::string_view htmlUtf8, std::wstring provenance,
                                 std::wstring author, std::wstring* error) {
    SetError(error, L"");
    if (packageDirectory.empty()) {
        SetError(error, L"壁纸包目录不能为空");
        return false;
    }
    if (htmlUtf8.empty() || htmlUtf8.size() > 2 * 1024 * 1024) {
        SetError(error, L"Web 壁纸 HTML 不能为空且不能超过 2 MB");
        return false;
    }
    if (title.empty()) title = L"MiaoDesk Wallpaper";
    if (title.size() > 256) title.resize(256);
    if (author.empty()) author = L"MiaoDesk";
    if (provenance.empty()) provenance = L"user-authored";

    std::error_code ec;
    fs::create_directories(packageDirectory, ec);
    if (ec) {
        SetError(error, L"无法创建 .mdwall 目录：" + packageDirectory.wstring());
        return false;
    }

    const fs::path entry = packageDirectory / L"index.html";
    std::ofstream html(entry, std::ios::binary | std::ios::trunc);
    if (!html) {
        SetError(error, L"无法写入 Web 壁纸入口文件");
        return false;
    }
    html.write(htmlUtf8.data(), static_cast<std::streamsize>(htmlUtf8.size()));
    if (!html) {
        SetError(error, L"Web 壁纸入口文件写入失败");
        return false;
    }
    html.close();

    WallpaperPackageManifest manifest;
    manifest.schema = 1;
    manifest.type = WallpaperPackageType::Web;
    manifest.title = std::move(title);
    manifest.author = std::move(author);
    manifest.entry = L"index.html";
    manifest.provenance = std::move(provenance);
    manifest.fpsCap = 30;
    manifest.audio = false;
    if (!WriteManifest(packageDirectory / L"manifest.json", manifest, MakeGeneratedThemeId())) {
        SetError(error, L"无法写入壁纸主题包 manifest.json");
        return false;
    }
    return Validate(packageDirectory, nullptr, error);
}

bool WallpaperPackage::Validate(const fs::path& packageDirectory, WallpaperPackageManifest* manifest,
                                std::wstring* error) {
    SetError(error, L"");
    std::error_code ec;
    if (!fs::exists(packageDirectory, ec) || !fs::is_directory(packageDirectory, ec)) {
        SetError(error, L"壁纸包目录不存在：" + packageDirectory.wstring());
        return false;
    }

    const fs::path manifestPath = packageDirectory / L"manifest.json";
    const std::string json = ReadTextFile(manifestPath, 1024 * 1024);
    if (json.empty()) {
        SetError(error, L"壁纸包缺少有效的 manifest.json");
        return false;
    }

    WallpaperPackageManifest parsed;
    parsed.schema = ExtractJsonInt(json, "schema", 0);
    parsed.type = ParseType(Utf8ToWide(ExtractJsonString(json, "type")));
    parsed.title = Utf8ToWide(ExtractJsonString(json, "title"));
    parsed.author = Utf8ToWide(ExtractJsonString(json, "author"));
    const std::wstring canonicalEntry = Utf8ToWide(ExtractJsonString(json, "entry"));
    const std::wstring legacyEntry = Utf8ToWide(ExtractJsonString(json, "legacy_entry"));
    parsed.entry = legacyEntry.empty() ? fs::path(canonicalEntry) : fs::path(legacyEntry);
    parsed.provenance = Utf8ToWide(ExtractJsonString(json, "provenance"));
    parsed.fpsCap = ExtractJsonInt(json, "fps_cap", 30);
    parsed.audio = ExtractJsonBool(json, "audio", false);

    if (parsed.schema != 1) {
        SetError(error, L"不支持的 .mdwall schema 版本");
        return false;
    }
    if (parsed.type == WallpaperPackageType::Unknown) {
        SetError(error, L"壁纸包 type 无效");
        return false;
    }
    if (parsed.title.empty() || parsed.title.size() > 256) {
        SetError(error, L"壁纸包 title 无效");
        return false;
    }
    if (!IsSafeRelativeEntry(parsed.entry)) {
        SetError(error, L"壁纸包 entry 必须是包内安全相对路径");
        return false;
    }
    parsed.fpsCap = std::clamp(parsed.fpsCap, 1, 240);

    const fs::path resolvedEntry = packageDirectory / parsed.entry;
    if (!IsInside(resolvedEntry, packageDirectory) || !fs::exists(resolvedEntry, ec) || !fs::is_regular_file(resolvedEntry, ec)) {
        SetError(error, L"壁纸包 entry 不存在或越过包目录");
        return false;
    }
    if (parsed.type == WallpaperPackageType::Web) {
        const auto extension = Lower(resolvedEntry.extension().wstring());
        if (extension != L".html" && extension != L".htm") {
            SetError(error, L"Web .mdwall 的 entry 必须是 HTML 文件");
            return false;
        }
    }

    if (manifest) *manifest = std::move(parsed);
    return true;
}

bool WallpaperPackage::SelfTest() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-tdwall-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".mdwall");
    fs::remove_all(root, ec);
    std::wstring error;
    const bool created = CreateWeb(root, L"中文 Web 主题", "<!doctype html><html><body>MiaoDesk</body></html>",
                                   L"self-test", L"妙喵", &error);
    WallpaperPackageManifest manifest;
    bool valid = created && Validate(root, &manifest, &error) && manifest.type == WallpaperPackageType::Web &&
                 manifest.entry == fs::path(L"index.html") && manifest.title == L"中文 Web 主题" &&
                 manifest.author == L"妙喵";

    content::LoadedMiaoContentPackage canonicalWeb;
    valid = valid && content::MiaoContentPackage::Load(root, &canonicalWeb, &error) &&
            canonicalWeb.manifest.kind == content::ContentKind::Wallpaper &&
            canonicalWeb.manifest.runtime == content::ContentRuntimeKind::Web &&
            canonicalWeb.manifest.name == u8"中文 Web 主题" &&
            canonicalWeb.manifest.author == u8"妙喵";

    // Canonical Content theme manifests use a JSON entry. The legacy layered
    // renderer can coexist during migration through legacy_entry, so validate
    // that it still resolves the established scene.ini without changing the
    // canonical package contract consumed by MiaoContentPackage.
    const fs::path dual = fs::temp_directory_path() /
        (L"MiaoDesk-dual-theme-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L".mdwall");
    fs::remove_all(dual, ec);
    fs::create_directories(dual, ec);
    if (!ec) {
        std::ofstream json(dual / L"manifest.json", std::ios::binary | std::ios::trunc);
        json << R"JSON({
  "schema": 1,
  "id": "com.goodloong.selftest.theme",
  "name": "中文主题",
  "title": "中文主题",
  "author": "MiaoDesk",
  "version": "1.0.0",
  "kind": "wallpaper",
  "runtime": "scene",
  "type": "scene",
  "entry": "scene.json",
  "legacy_entry": "scene.ini",
  "capabilities": ["theme.wallpaper"]
})JSON";
        json.close();
        std::ofstream sceneJson(dual / L"scene.json", std::ios::binary | std::ios::trunc);
        sceneJson << R"JSON({"schema":1,"id":"scene://dual-theme"})JSON";
        sceneJson.close();
        std::ofstream sceneIni(dual / L"scene.ini", std::ios::binary | std::ios::trunc);
        sceneIni << "[Scene]\nlayer_count=0\n";
        sceneIni.close();
        WallpaperPackageManifest dualManifest;
        valid = valid && Validate(dual, &dualManifest, &error) &&
                dualManifest.type == WallpaperPackageType::Scene &&
                dualManifest.title == L"中文主题" &&
                dualManifest.entry == fs::path(L"scene.ini");
    } else {
        valid = false;
    }

    fs::remove_all(root, ec);
    fs::remove_all(dual, ec);
    return valid;
}

} // namespace miaodesk::wallpaper
