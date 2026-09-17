#include "miaodesk/WallpaperPackage.h"
#include "miaodesk/MiaoContentPackage.h"

#include <windows.h>
#include <objbase.h>
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
    const std::wstring normalized = Lower(std::wstring(value));
    if (normalized == L"image") return WallpaperPackageType::Image;
    if (normalized == L"video") return WallpaperPackageType::Video;
    if (normalized == L"web") return WallpaperPackageType::Web;
    if (normalized == L"scene") return WallpaperPackageType::Scene;
    return WallpaperPackageType::Unknown;
}

WallpaperPackageValidation WallpaperPackage::LoadAndValidate(const fs::path& packageRoot,
                                                              std::uintmax_t maxBytes) {
    WallpaperPackageValidation result{};
    result.packageRoot = packageRoot;
    std::error_code ec;
    if (!fs::exists(packageRoot, ec) || !fs::is_directory(packageRoot, ec)) {
        result.message = L"壁纸包目录不存在";
        return result;
    }

    const fs::path manifestPath = packageRoot / L"manifest.json";
    const std::string json = ReadTextFile(manifestPath, 512 * 1024);
    if (json.empty()) {
        result.message = L"manifest.json 不存在或为空";
        return result;
    }

    result.manifest.schema = std::max(1, ExtractJsonInt(json, "schema", 1));
    result.manifest.title = Utf8ToWide(ExtractJsonString(json, "title"));
    if (result.manifest.title.empty()) result.manifest.title = Utf8ToWide(ExtractJsonString(json, "name"));
    result.manifest.author = Utf8ToWide(ExtractJsonString(json, "author"));
    const std::string kind = ExtractJsonString(json, "kind");
    const std::string runtime = ExtractJsonString(json, "runtime");
    const std::string legacyEntry = ExtractJsonString(json, "legacy_entry");
    const std::string entry = !legacyEntry.empty() ? legacyEntry : ExtractJsonString(json, "entry");
    result.manifest.entry = fs::u8path(entry);
    result.manifest.type = ParseType(Utf8ToWide(!runtime.empty() ? runtime : ExtractJsonString(json, "type")));
    result.manifest.provenance = Utf8ToWide(ExtractJsonString(json, "provenance"));
    result.manifest.fpsCap = std::clamp(ExtractJsonInt(json, "fps_cap", 30), 1, 240);
    result.manifest.audio = ExtractJsonBool(json, "audio", false);

    if (!kind.empty() && kind != "wallpaper") {
        result.message = L"manifest kind 必须为 wallpaper";
        return result;
    }
    if (result.manifest.title.empty() || result.manifest.entry.empty()) {
        result.message = L"manifest 缺少 title/name 或 entry";
        return result;
    }
    if (result.manifest.type == WallpaperPackageType::Unknown) {
        result.message = L"manifest runtime/type 不受支持";
        return result;
    }
    if (!IsSafeRelativeEntry(result.manifest.entry)) {
        result.message = L"manifest entry 必须是安全的相对路径";
        return result;
    }

    result.resolvedEntry = packageRoot / result.manifest.entry;
    if (!fs::exists(result.resolvedEntry, ec) || !fs::is_regular_file(result.resolvedEntry, ec)) {
        result.message = L"壁纸包入口文件不存在";
        return result;
    }
    if (!IsInside(result.resolvedEntry, packageRoot)) {
        result.message = L"壁纸包入口越界";
        return result;
    }

    std::uintmax_t total = 0;
    for (fs::recursive_directory_iterator it(packageRoot, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        total += it->file_size(ec);
        if (ec || total > maxBytes) {
            result.message = L"壁纸包体积超过限制";
            return result;
        }
    }
    if (ec) {
        result.message = L"无法遍历壁纸包";
        return result;
    }

    result.totalBytes = total;
    result.ok = true;
    return result;
}

bool WallpaperPackage::WriteCanonicalManifest(const fs::path& packageRoot,
                                              const WallpaperPackageManifest& manifest,
                                              std::string_view stableId) {
    if (packageRoot.empty() || stableId.empty()) return false;
    return WriteManifest(packageRoot / L"manifest.json", manifest, stableId);
}

bool WallpaperPackage::SelfTest() {
    std::error_code ec;
    const fs::path tempRoot = fs::temp_directory_path(ec);
    if (ec || tempRoot.empty()) return false;
    const fs::path root = tempRoot / (L"MiaoDesk-WallpaperPackage-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root, ec);
    fs::create_directories(root / L"nested", ec);
    if (ec) return false;

    {
        WallpaperPackageManifest manifest{};
        manifest.title = L"网页壁纸";
        manifest.author = L"测试作者";
        manifest.type = WallpaperPackageType::Web;
        manifest.entry = L"index.html";
        manifest.provenance = L"self-test";
        manifest.fpsCap = 42;
        manifest.audio = true;
        if (!WriteCanonicalManifest(root, manifest, "com.goodloong.miaodesk.theme.test-web")) return false;
    }
    {
        std::ofstream output(root / L"index.html", std::ios::binary | std::ios::trunc);
        output << "<!doctype html><title>test</title>";
    }
    const auto canonicalWeb = LoadAndValidate(root, 1024 * 1024);
    bool ok = canonicalWeb.ok && canonicalWeb.manifest.title == L"网页壁纸" &&
              canonicalWeb.manifest.author == L"测试作者" &&
              canonicalWeb.manifest.type == WallpaperPackageType::Web &&
              canonicalWeb.manifest.entry == fs::path(L"index.html");

    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::wallpaper
