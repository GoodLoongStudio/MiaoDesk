#include "miaodesk/MiaoContentPackage.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

std::wstring LowerPathText(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') return static_cast<wchar_t>(ch - L'A' + L'a');
        return ch;
    });
    return value;
}

std::string LowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool IsAsciiStableId(std::string_view value) noexcept {
    if (value.empty() || value.size() > 256) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (std::isalnum(first) == 0) return false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-') continue;
        return false;
    }
    return true;
}

bool IsCapabilityId(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128) return false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-') continue;
        return false;
    }
    return true;
}

bool ReadTextFile(const fs::path& path, std::size_t maxBytes, std::string* output) {
    if (!output) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > maxBytes) return false;
    input.seekg(0, std::ios::beg);
    output->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return static_cast<bool>(input) || input.eof();
}

bool FileWithinBudget(const fs::path& path, std::size_t maxBytes) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return !ec && size <= maxBytes;
}

void AppendUtf8(std::string& out, unsigned codePoint) {
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

std::optional<unsigned> ParseHex4(std::string_view text) {
    if (text.size() < 4) return std::nullopt;
    unsigned value = 0;
    for (char ch : text.substr(0, 4)) {
        value <<= 4;
        if (ch >= '0' && ch <= '9') value |= static_cast<unsigned>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') value |= static_cast<unsigned>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') value |= static_cast<unsigned>(ch - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

std::optional<std::string> ParseJsonStringAt(std::string_view json, std::size_t* position) {
    if (!position || *position >= json.size() || json[*position] != '"') return std::nullopt;
    ++*position;
    std::string result;
    while (*position < json.size()) {
        const char ch = json[(*position)++];
        if (ch == '"') return result;
        if (ch != '\\') {
            if (static_cast<unsigned char>(ch) < 0x20) return std::nullopt;
            result.push_back(ch);
            continue;
        }
        if (*position >= json.size()) return std::nullopt;
        const char escaped = json[(*position)++];
        switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
            if (*position + 4 > json.size()) return std::nullopt;
            const auto first = ParseHex4(json.substr(*position, 4));
            if (!first) return std::nullopt;
            *position += 4;
            unsigned codePoint = *first;
            if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                if (*position + 6 > json.size() || json[*position] != '\\' || json[*position + 1] != 'u')
                    return std::nullopt;
                *position += 2;
                const auto second = ParseHex4(json.substr(*position, 4));
                if (!second || *second < 0xDC00 || *second > 0xDFFF) return std::nullopt;
                *position += 4;
                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*second - 0xDC00);
            } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                return std::nullopt;
            }
            AppendUtf8(result, codePoint);
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> FindJsonValue(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    std::size_t searchFrom = 0;
    while (searchFrom < json.size()) {
        const auto keyPos = json.find(needle, searchFrom);
        if (keyPos == std::string_view::npos) return std::nullopt;
        auto pos = keyPos + needle.size();
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
        if (pos < json.size() && json[pos] == ':') {
            ++pos;
            while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
            return pos;
        }
        searchFrom = keyPos + needle.size();
    }
    return std::nullopt;
}

std::optional<std::string> ExtractJsonString(std::string_view json, std::string_view key) {
    auto pos = FindJsonValue(json, key);
    if (!pos) return std::nullopt;
    return ParseJsonStringAt(json, &*pos);
}

std::optional<int> ExtractJsonInt(std::string_view json, std::string_view key) {
    auto pos = FindJsonValue(json, key);
    if (!pos) return std::nullopt;
    const char* begin = json.data() + *pos;
    const char* end = json.data() + json.size();
    int value = 0;
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr == begin) return std::nullopt;
    return value;
}

std::optional<std::vector<std::string>> ExtractJsonStringArray(std::string_view json, std::string_view key) {
    auto pos = FindJsonValue(json, key);
    if (!pos) return std::nullopt;
    if (*pos >= json.size() || json[*pos] != '[') return std::nullopt;
    ++*pos;
    std::vector<std::string> values;
    while (*pos < json.size()) {
        while (*pos < json.size() && std::isspace(static_cast<unsigned char>(json[*pos])) != 0) ++*pos;
        if (*pos < json.size() && json[*pos] == ']') {
            ++*pos;
            return values;
        }
        auto value = ParseJsonStringAt(json, &*pos);
        if (!value) return std::nullopt;
        values.push_back(std::move(*value));
        while (*pos < json.size() && std::isspace(static_cast<unsigned char>(json[*pos])) != 0) ++*pos;
        if (*pos < json.size() && json[*pos] == ',') {
            ++*pos;
            continue;
        }
        if (*pos < json.size() && json[*pos] == ']') {
            ++*pos;
            return values;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

bool ExtensionMatchesKind(const fs::path& root, ContentKind kind) {
    const auto extension = LowerPathText(root.extension().wstring());
    if (kind == ContentKind::Wallpaper) return extension == L".mdwall";
    return extension == L".mdwidget";
}

bool ValidateOptionalFile(
    const fs::path& root,
    const fs::path& relative,
    std::size_t maxBytes,
    std::wstring_view label,
    std::wstring* error) {
    if (relative.empty()) return true;
    fs::path resolved;
    if (!MiaoContentPackage::ResolvePackagePath(root, relative, &resolved, error)) return false;
    std::error_code ec;
    if (!fs::exists(resolved, ec) || !fs::is_regular_file(resolved, ec))
        return Fail(error, std::wstring(label) + L" file does not exist: " + relative.wstring());
    if (!FileWithinBudget(resolved, maxBytes))
        return Fail(error, std::wstring(label) + L" file exceeds size budget: " + relative.wstring());
    return true;
}

} // namespace

const char* MiaoContentPackage::KindKey(ContentKind kind) noexcept {
    return kind == ContentKind::Widget ? "widget" : "wallpaper";
}

const char* MiaoContentPackage::RuntimeKey(ContentRuntimeKind runtime) noexcept {
    return runtime == ContentRuntimeKind::Web ? "web" : "scene";
}

bool MiaoContentPackage::ParseKind(std::string_view value, ContentKind* kind) noexcept {
    if (!kind) return false;
    const auto lower = LowerAscii(value);
    if (lower == "wallpaper") {
        *kind = ContentKind::Wallpaper;
        return true;
    }
    if (lower == "widget") {
        *kind = ContentKind::Widget;
        return true;
    }
    return false;
}

bool MiaoContentPackage::ParseRuntime(std::string_view value, ContentRuntimeKind* runtime) noexcept {
    if (!runtime) return false;
    const auto lower = LowerAscii(value);
    if (lower == "scene") {
        *runtime = ContentRuntimeKind::Scene;
        return true;
    }
    if (lower == "web") {
        *runtime = ContentRuntimeKind::Web;
        return true;
    }
    return false;
}

bool MiaoContentPackage::IsSafeRelativePath(const fs::path& path) noexcept {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == L"..") return false;
    }
    return true;
}

bool MiaoContentPackage::ResolvePackagePath(
    const fs::path& packageRoot,
    const fs::path& relativePath,
    fs::path* resolved,
    std::wstring* error) {
    if (!resolved) return Fail(error, L"Resolved package path output is null.");
    if (!IsSafeRelativePath(relativePath))
        return Fail(error, L"Package path must be a safe relative path: " + relativePath.wstring());

    std::error_code ec;
    auto root = fs::weakly_canonical(packageRoot, ec);
    if (ec) {
        ec.clear();
        root = fs::absolute(packageRoot, ec).lexically_normal();
        if (ec) return Fail(error, L"Cannot canonicalize package root.");
    }

    ec.clear();
    auto candidate = fs::weakly_canonical(root / relativePath, ec);
    if (ec) {
        ec.clear();
        candidate = fs::absolute(root / relativePath, ec).lexically_normal();
        if (ec) return Fail(error, L"Cannot canonicalize package path: " + relativePath.wstring());
    }

    const auto rootText = LowerPathText(root.wstring());
    const auto candidateText = LowerPathText(candidate.wstring());
    std::wstring prefix = rootText;
    if (!prefix.empty() && prefix.back() != L'\\' && prefix.back() != L'/')
        prefix.push_back(fs::path::preferred_separator);
    if (candidateText != rootText &&
        (candidateText.size() < prefix.size() || candidateText.compare(0, prefix.size(), prefix) != 0)) {
        return Fail(error, L"Package path escapes package root: " + relativePath.wstring());
    }

    *resolved = std::move(candidate);
    if (error) error->clear();
    return true;
}

bool MiaoContentPackage::Validate(
    const fs::path& packageRoot,
    MiaoContentPackageManifest* manifest,
    std::wstring* error) {
    if (error) error->clear();
    std::error_code ec;
    if (!fs::exists(packageRoot, ec) || !fs::is_directory(packageRoot, ec))
        return Fail(error, L"Miao content package directory does not exist: " + packageRoot.wstring());

    const fs::path manifestPath = packageRoot / L"manifest.json";
    if (!fs::exists(manifestPath, ec) || !fs::is_regular_file(manifestPath, ec))
        return Fail(error, L"Miao content package requires manifest.json.");
    if (!FileWithinBudget(manifestPath, kManifestMaxBytes))
        return Fail(error, L"Miao content manifest exceeds size budget.");

    std::string json;
    if (!ReadTextFile(manifestPath, kManifestMaxBytes, &json) || json.empty())
        return Fail(error, L"Cannot read Miao content manifest.json.");

    const auto schema = ExtractJsonInt(json, "schema");
    const auto id = ExtractJsonString(json, "id");
    const auto name = ExtractJsonString(json, "name");
    const auto author = ExtractJsonString(json, "author");
    const auto version = ExtractJsonString(json, "version");
    const auto kindText = ExtractJsonString(json, "kind");
    const auto runtimeText = ExtractJsonString(json, "runtime");
    const auto entryText = ExtractJsonString(json, "entry");
    const auto parametersText = ExtractJsonString(json, "parameters");
    const auto previewText = ExtractJsonString(json, "preview");
    const auto capabilities = ExtractJsonStringArray(json, "capabilities");

    if (!schema || *schema != static_cast<int>(kSchemaVersion))
        return Fail(error, L"Unsupported Miao content package schema version.");
    if (!id || !IsAsciiStableId(*id))
        return Fail(error, L"Miao content package id is missing or invalid.");
    if (!name || name->empty() || name->size() > 256)
        return Fail(error, L"Miao content package name is missing or invalid.");
    if (author && author->size() > 256)
        return Fail(error, L"Miao content package author is too long.");
    if (!version || version->empty() || version->size() > 64)
        return Fail(error, L"Miao content package version is missing or invalid.");
    if (!kindText || !runtimeText || !entryText)
        return Fail(error, L"Miao content package kind/runtime/entry is required.");

    MiaoContentPackageManifest parsed;
    parsed.schema = static_cast<std::uint32_t>(*schema);
    parsed.id = *id;
    parsed.name = *name;
    parsed.author = author.value_or(std::string{});
    parsed.version = *version;
    if (!ParseKind(*kindText, &parsed.kind))
        return Fail(error, L"Miao content package kind must be wallpaper or widget.");
    if (!ParseRuntime(*runtimeText, &parsed.runtime))
        return Fail(error, L"Miao content package runtime must be scene or web.");
    if (!ExtensionMatchesKind(packageRoot, parsed.kind))
        return Fail(error, L"Package extension must match manifest kind (.mdwall/.mdwidget).");

    parsed.entry = fs::u8path(*entryText);
    if (parametersText && !parametersText->empty()) parsed.parameters = fs::u8path(*parametersText);
    if (previewText && !previewText->empty()) parsed.preview = fs::u8path(*previewText);
    if (capabilities) parsed.capabilities = *capabilities;

    if (!IsSafeRelativePath(parsed.entry))
        return Fail(error, L"Package entry must be a safe relative path.");
    if (!parsed.parameters.empty() && !IsSafeRelativePath(parsed.parameters))
        return Fail(error, L"Package parameters path must be a safe relative path.");
    if (!parsed.preview.empty() && !IsSafeRelativePath(parsed.preview))
        return Fail(error, L"Package preview path must be a safe relative path.");

    std::unordered_set<std::string> capabilityIds;
    for (const auto& capability : parsed.capabilities) {
        if (!IsCapabilityId(capability))
            return Fail(error, L"Package capability id contains invalid characters.");
        if (!capabilityIds.emplace(capability).second)
            return Fail(error, L"Package capability list contains duplicates.");
    }

    fs::path entryPath;
    if (!ResolvePackagePath(packageRoot, parsed.entry, &entryPath, error)) return false;
    if (!fs::exists(entryPath, ec) || !fs::is_regular_file(entryPath, ec))
        return Fail(error, L"Package entry does not exist: " + parsed.entry.wstring());

    if (parsed.runtime == ContentRuntimeKind::Scene) {
        if (LowerPathText(entryPath.extension().wstring()) != L".json")
            return Fail(error, L"Scene runtime entry must be a JSON file.");
        if (!FileWithinBudget(entryPath, kSceneEntryMaxBytes))
            return Fail(error, L"Scene runtime entry exceeds size budget.");
    } else {
        const auto ext = LowerPathText(entryPath.extension().wstring());
        if (ext != L".html" && ext != L".htm")
            return Fail(error, L"Web runtime entry must be an HTML file.");
        if (!FileWithinBudget(entryPath, kWebEntryMaxBytes))
            return Fail(error, L"Web runtime entry exceeds size budget.");
    }

    if (!parsed.parameters.empty()) {
        if (LowerPathText(parsed.parameters.extension().wstring()) != L".json")
            return Fail(error, L"Parameter schema must be a JSON file.");
        if (!ValidateOptionalFile(packageRoot, parsed.parameters, kParameterMaxBytes, L"Parameter", error)) return false;
    }
    if (!parsed.preview.empty() &&
        !ValidateOptionalFile(packageRoot, parsed.preview, kPreviewMaxBytes, L"Preview", error)) return false;

    if (manifest) *manifest = std::move(parsed);
    if (error) error->clear();
    return true;
}

bool MiaoContentPackage::Load(
    const fs::path& packageRoot,
    LoadedMiaoContentPackage* package,
    std::wstring* error) {
    if (!package) return Fail(error, L"Loaded package output is null.");

    MiaoContentPackageManifest manifest;
    if (!Validate(packageRoot, &manifest, error)) return false;

    fs::path entryPath;
    if (!ResolvePackagePath(packageRoot, manifest.entry, &entryPath, error)) return false;
    const auto entryLimit = manifest.runtime == ContentRuntimeKind::Scene ? kSceneEntryMaxBytes : kWebEntryMaxBytes;

    LoadedMiaoContentPackage loaded;
    std::error_code ec;
    loaded.root = fs::weakly_canonical(packageRoot, ec);
    if (ec) loaded.root = packageRoot.lexically_normal();
    loaded.manifest = manifest;
    if (!ReadTextFile(entryPath, entryLimit, &loaded.entrySourceUtf8) || loaded.entrySourceUtf8.empty())
        return Fail(error, L"Package entry cannot be read or is empty.");

    if (!manifest.parameters.empty()) {
        fs::path parameterPath;
        if (!ResolvePackagePath(packageRoot, manifest.parameters, &parameterPath, error)) return false;
        if (!ReadTextFile(parameterPath, kParameterMaxBytes, &loaded.parameterSourceUtf8))
            return Fail(error, L"Package parameter schema cannot be read.");
    }

    *package = std::move(loaded);
    if (error) error->clear();
    return true;
}

bool MiaoContentPackage::SelfTest() {
    std::error_code ec;
    const auto root = fs::temp_directory_path() / L"MiaoDesk-ContentPackage-SelfTest.mdwall";
    fs::remove_all(root, ec);
    fs::create_directories(root / L"preview", ec);
    if (ec) return false;

    {
        std::ofstream manifest(root / L"manifest.json", std::ios::binary | std::ios::trunc);
        manifest << R"JSON({
  "schema": 1,
  "id": "com.goodloong.self-test",
  "name": "Miao Content Self Test",
  "author": "MiaoDesk",
  "version": "1.0.0",
  "kind": "wallpaper",
  "runtime": "scene",
  "entry": "scene.json",
  "parameters": "parameters.json",
  "capabilities": ["clock.read", "audio.read"]
})JSON";
    }
    {
        std::ofstream scene(root / L"scene.json", std::ios::binary | std::ios::trunc);
        scene << R"JSON({"schema":1,"id":"scene://self-test"})JSON";
    }
    {
        std::ofstream parameters(root / L"parameters.json", std::ios::binary | std::ios::trunc);
        parameters << R"JSON({"schema":1,"parameters":[]})JSON";
    }

    std::wstring error;
    LoadedMiaoContentPackage loaded;
    const bool valid = Load(root, &loaded, &error) &&
        loaded.manifest.id == "com.goodloong.self-test" &&
        loaded.manifest.kind == ContentKind::Wallpaper &&
        loaded.manifest.runtime == ContentRuntimeKind::Scene &&
        loaded.manifest.capabilities.size() == 2 &&
        !loaded.entrySourceUtf8.empty();

    fs::path resolved;
    const bool traversalRejected = !ResolvePackagePath(root, fs::path(L"..") / L"outside.json", &resolved, &error);

    fs::remove_all(root, ec);
    return valid && traversalRejected;
}

} // namespace miaodesk::content
