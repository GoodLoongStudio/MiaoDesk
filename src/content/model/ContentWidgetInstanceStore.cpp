#include "miaodesk/ContentWidgetInstanceStore.h"

#include "miaodesk/AppPaths.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool SafeInstanceId(std::wstring_view value) {
    if (value.empty() || value.size() > 100) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_';
    });
}

bool CreateUnicodeIni(const fs::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    constexpr unsigned char bom[2]{0xff, 0xfe};
    stream.write(reinterpret_cast<const char*>(bom), 2);
    return static_cast<bool>(stream);
}

std::wstring ReadText(
    const fs::path& path,
    std::wstring_view section,
    std::wstring_view key,
    std::wstring_view fallback = {}) {
    std::vector<wchar_t> buffer(32768);
    const std::wstring sectionText(section);
    const std::wstring keyText(key);
    const std::wstring fallbackText(fallback);
    GetPrivateProfileStringW(sectionText.c_str(), keyText.c_str(), fallbackText.c_str(),
                             buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

bool WriteText(
    const fs::path& path,
    std::wstring_view section,
    std::wstring_view key,
    std::wstring_view value) {
    const std::wstring sectionText(section);
    const std::wstring keyText(key);
    const std::wstring valueText(value);
    return WritePrivateProfileStringW(sectionText.c_str(), keyText.c_str(), valueText.c_str(), path.c_str()) != FALSE;
}

std::wstring HexEncode(std::wstring_view value) {
    static constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result = L"u16:";
    result.reserve(4 + value.size() * 4);
    for (wchar_t ch : value) {
        const auto code = static_cast<std::uint16_t>(ch);
        result.push_back(digits[(code >> 12) & 0x0f]);
        result.push_back(digits[(code >> 8) & 0x0f]);
        result.push_back(digits[(code >> 4) & 0x0f]);
        result.push_back(digits[code & 0x0f]);
    }
    return result;
}

int HexNibble(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
    if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
    return -1;
}

bool HexDecode(std::wstring_view encoded, std::wstring* value) {
    if (!value || encoded.size() < 4 || encoded.substr(0, 4) != L"u16:") return false;
    encoded.remove_prefix(4);
    if (encoded.size() % 4 != 0 || encoded.size() > 65536) return false;
    std::wstring result;
    result.reserve(encoded.size() / 4);
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        int n0 = HexNibble(encoded[i]);
        int n1 = HexNibble(encoded[i + 1]);
        int n2 = HexNibble(encoded[i + 2]);
        int n3 = HexNibble(encoded[i + 3]);
        if (n0 < 0 || n1 < 0 || n2 < 0 || n3 < 0) return false;
        const auto code = static_cast<std::uint16_t>((n0 << 12) | (n1 << 8) | (n2 << 4) | n3);
        result.push_back(static_cast<wchar_t>(code));
    }
    *value = std::move(result);
    return true;
}

std::wstring DoubleText(double value) {
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

bool ParseDouble(std::wstring_view text, double* value) {
    if (!value || text.empty()) return false;
    std::wstring copy(text);
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(copy.c_str(), &end);
    if (!end || end != copy.c_str() + copy.size() || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

bool ParseInt64(std::wstring_view text, std::int64_t* value) {
    if (!value || text.empty()) return false;
    std::wstring copy(text);
    wchar_t* end = nullptr;
    errno = 0;
    const long long parsed = std::wcstoll(copy.c_str(), &end, 10);
    if (errno == ERANGE || !end || end != copy.c_str() + copy.size()) return false;
    *value = static_cast<std::int64_t>(parsed);
    return true;
}

std::wstring EncodeValue(const ContentParameterDefinition& parameter, const ContentParameterValue& value) {
    switch (parameter.type) {
    case ContentParameterType::Bool:
        if (const auto* item = std::get_if<bool>(&value)) return *item ? L"1" : L"0";
        break;
    case ContentParameterType::Int:
        if (const auto* item = std::get_if<std::int64_t>(&value)) return std::to_wstring(*item);
        break;
    case ContentParameterType::Float:
        if (const auto* item = std::get_if<double>(&value)) return DoubleText(*item);
        break;
    case ContentParameterType::String:
    case ContentParameterType::Enum:
        if (const auto* item = std::get_if<std::wstring>(&value)) return HexEncode(*item);
        break;
    case ContentParameterType::Color:
        if (const auto* item = std::get_if<Color4>(&value)) {
            return DoubleText(item->r) + L"," + DoubleText(item->g) + L"," +
                   DoubleText(item->b) + L"," + DoubleText(item->a);
        }
        break;
    case ContentParameterType::Asset:
        if (const auto* item = std::get_if<AssetReference>(&value)) return HexEncode(item->id);
        break;
    }
    return {};
}

bool DecodeColor(std::wstring_view text, Color4* color) {
    if (!color) return false;
    double channels[4]{};
    std::size_t start = 0;
    for (int index = 0; index < 4; ++index) {
        const std::size_t comma = text.find(L',', start);
        const bool last = index == 3;
        if ((last && comma != std::wstring_view::npos) || (!last && comma == std::wstring_view::npos)) return false;
        const std::size_t end = last ? text.size() : comma;
        if (!ParseDouble(text.substr(start, end - start), &channels[index])) return false;
        start = end + 1;
    }
    *color = Color4{channels[0], channels[1], channels[2], channels[3]};
    return true;
}

bool DecodeValue(
    const ContentParameterDefinition& parameter,
    std::wstring_view raw,
    ContentParameterValue* value) {
    if (!value) return false;
    switch (parameter.type) {
    case ContentParameterType::Bool:
        if (raw == L"1" || raw == L"true") { *value = true; return true; }
        if (raw == L"0" || raw == L"false") { *value = false; return true; }
        return false;
    case ContentParameterType::Int: {
        std::int64_t parsed{};
        if (!ParseInt64(raw, &parsed)) return false;
        *value = parsed;
        return true;
    }
    case ContentParameterType::Float: {
        double parsed{};
        if (!ParseDouble(raw, &parsed)) return false;
        *value = parsed;
        return true;
    }
    case ContentParameterType::String:
    case ContentParameterType::Enum: {
        std::wstring parsed;
        if (!HexDecode(raw, &parsed)) return false;
        *value = std::move(parsed);
        return true;
    }
    case ContentParameterType::Color: {
        Color4 parsed;
        if (!DecodeColor(raw, &parsed)) return false;
        *value = parsed;
        return true;
    }
    case ContentParameterType::Asset: {
        std::wstring parsed;
        if (!HexDecode(raw, &parsed)) return false;
        *value = AssetReference{std::move(parsed)};
        return true;
    }
    }
    return false;
}

std::vector<std::wstring> SplitKeys(std::wstring_view value) {
    std::vector<std::wstring> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find(L';', start);
        const std::size_t end = separator == std::wstring_view::npos ? value.size() : separator;
        if (end > start) result.emplace_back(value.substr(start, end - start));
        if (separator == std::wstring_view::npos) break;
        start = separator + 1;
    }
    return result;
}

bool SaveOverridesToPath(
    const fs::path& path,
    const ContentDefinition& definition,
    const ContentParameterValues& overrides,
    std::wstring* error) {
    ContentParameterValues resolved;
    if (!MiaoContentModel::ResolveParameterValues(definition, overrides, &resolved, error)) return false;

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return Fail(error, L"Unable to create Content widget instance directory.");

    fs::path temporary = path;
    temporary += L".tmp";
    fs::remove(temporary, ec);
    ec.clear();
    if (!CreateUnicodeIni(temporary)) return Fail(error, L"Unable to create Content widget instance file.");

    std::wstring keys;
    for (const auto& [key, value] : overrides) {
        const auto* parameter = MiaoContentModel::FindParameter(definition, key);
        if (!parameter) continue;
        const std::wstring encoded = EncodeValue(*parameter, value);
        if (encoded.empty() && parameter->type != ContentParameterType::String &&
            parameter->type != ContentParameterType::Enum) {
            fs::remove(temporary, ec);
            return Fail(error, L"Unable to encode Content parameter: " + key);
        }
        if (!keys.empty()) keys.push_back(L';');
        keys += key;
        if (!WriteText(temporary, L"Parameters", key, encoded)) {
            fs::remove(temporary, ec);
            return Fail(error, L"Unable to persist Content parameter: " + key);
        }
    }
    if (!WriteText(temporary, L"Parameters", L"Keys", keys)) {
        fs::remove(temporary, ec);
        return Fail(error, L"Unable to persist Content parameter index.");
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());

    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED)) {
        const DWORD code = GetLastError();
        fs::remove(temporary, ec);
        return Fail(error, L"Unable to atomically replace Content widget instance file. Win32=" +
                           std::to_wstring(code));
    }
    if (error) error->clear();
    return true;
}

} // namespace

fs::path ContentWidgetInstanceStore::InstanceDirectory() {
    const fs::path root = paths::DesktopWidgetsRoot();
    return root.empty() ? fs::path{} : root / L"Instances";
}

fs::path ContentWidgetInstanceStore::InstancePath(std::wstring_view widgetId) {
    if (!SafeInstanceId(widgetId)) return {};
    const fs::path directory = InstanceDirectory();
    return directory.empty() ? fs::path{} : directory / (std::wstring(widgetId) + L".ini");
}

bool ContentWidgetInstanceStore::LoadOverrides(
    std::wstring_view widgetId,
    const ContentDefinition& definition,
    ContentParameterValues* overrides,
    std::wstring* error) {
    if (!overrides) return Fail(error, L"Content parameter overrides output is null.");
    overrides->clear();
    const fs::path path = InstancePath(widgetId);
    if (path.empty()) return Fail(error, L"Content widget instance id is invalid.");

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        if (error) error->clear();
        return !ec;
    }
    if (ec || !fs::is_regular_file(path, ec))
        return Fail(error, L"Content widget instance parameter file is unavailable.");

    const std::wstring keys = ReadText(path, L"Parameters", L"Keys", L"");
    for (const auto& key : SplitKeys(keys)) {
        const auto* parameter = MiaoContentModel::FindParameter(definition, key);
        // Package updates may remove a parameter. Ignore stale overrides instead
        // of making an otherwise valid widget permanently un-runnable.
        if (!parameter) continue;
        const std::wstring raw = ReadText(path, L"Parameters", key, L"");
        ContentParameterValue decoded;
        if (!DecodeValue(*parameter, raw, &decoded))
            return Fail(error, L"Content parameter override is malformed: " + key);
        overrides->emplace(key, std::move(decoded));
    }

    ContentParameterValues resolved;
    if (!MiaoContentModel::ResolveParameterValues(definition, *overrides, &resolved, error)) return false;
    if (error) error->clear();
    return true;
}

bool ContentWidgetInstanceStore::SaveOverrides(
    std::wstring_view widgetId,
    const ContentDefinition& definition,
    const ContentParameterValues& overrides,
    std::wstring* error) {
    const fs::path path = InstancePath(widgetId);
    if (path.empty()) return Fail(error, L"Content widget instance id is invalid.");
    return SaveOverridesToPath(path, definition, overrides, error);
}

bool ContentWidgetInstanceStore::SetParameter(
    std::wstring_view widgetId,
    const ContentDefinition& definition,
    std::wstring_view key,
    ContentParameterValue value,
    std::wstring* error) {
    const auto* parameter = MiaoContentModel::FindParameter(definition, key);
    if (!parameter) return Fail(error, L"Unknown Content widget parameter: " + std::wstring(key));

    ContentParameterValues overrides;
    if (!LoadOverrides(widgetId, definition, &overrides, error)) return false;
    overrides[std::wstring(key)] = std::move(value);
    return SaveOverrides(widgetId, definition, overrides, error);
}

bool ContentWidgetInstanceStore::Remove(std::wstring_view widgetId, std::wstring* error) {
    const fs::path path = InstancePath(widgetId);
    if (path.empty()) return Fail(error, L"Content widget instance id is invalid.");
    std::error_code ec;
    const bool existed = fs::exists(path, ec);
    if (ec) return Fail(error, L"Unable to inspect Content widget instance file.");
    if (!existed) {
        if (error) error->clear();
        return true;
    }
    if (!fs::remove(path, ec) || ec)
        return Fail(error, L"Unable to remove Content widget instance parameter file.");
    if (error) error->clear();
    return true;
}

bool ContentWidgetInstanceStore::SelfTest() {
    ContentDefinition definition;
    definition.id = L"instance-store-test";
    definition.name = L"Instance Store Test";
    definition.version = L"1";
    definition.kind = ContentKind::Widget;
    definition.runtime = ContentRuntimeKind::Scene;
    definition.entry = L"scene.json";
    definition.geometry.defaultWidth = 0.2f;
    definition.geometry.defaultHeight = 0.2f;
    definition.geometry.minWidth = 0.2f;
    definition.geometry.minHeight = 0.2f;
    definition.geometry.maxWidth = 0.2f;
    definition.geometry.maxHeight = 0.2f;

    ContentParameterDefinition opacity;
    opacity.runtimeId = L"param://opacity";
    opacity.key = L"opacity";
    opacity.type = ContentParameterType::Float;
    opacity.defaultValue = 1.0;
    opacity.minimum = 0.0;
    opacity.maximum = 1.0;
    definition.parameters.push_back(opacity);

    ContentParameterDefinition title;
    title.runtimeId = L"param://title";
    title.key = L"title";
    title.type = ContentParameterType::String;
    title.defaultValue = std::wstring{};
    definition.parameters.push_back(title);

    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-ContentWidgetInstanceStore-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return false;

    // Exercise the codec/atomic writer without mutating the user's real app state.
    const fs::path file = root / L"test.ini";
    ContentParameterValues original;
    original.emplace(L"opacity", 0.42);
    original.emplace(L"title", std::wstring(L"妙喵 Content"));
    std::wstring error;
    const bool saved = SaveOverridesToPath(file, definition, original, &error);

    ContentParameterValues decoded;
    if (saved) {
        const std::wstring keys = ReadText(file, L"Parameters", L"Keys", L"");
        for (const auto& key : SplitKeys(keys)) {
            const auto* parameter = MiaoContentModel::FindParameter(definition, key);
            if (!parameter) continue;
            ContentParameterValue value;
            if (!DecodeValue(*parameter, ReadText(file, L"Parameters", key, L""), &value)) {
                fs::remove_all(root, ec);
                return false;
            }
            decoded.emplace(key, std::move(value));
        }
    }
    fs::remove_all(root, ec);
    if (!saved || decoded.size() != 2) return false;
    return std::get<double>(decoded.at(L"opacity")) == 0.42 &&
           std::get<std::wstring>(decoded.at(L"title")) == L"妙喵 Content";
}

} // namespace miaodesk::content
