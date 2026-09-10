#include "miaodesk/MiaoContentDefinitionLoader.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::optional<std::uint32_t> ParseHex4(std::string_view text) {
    if (text.size() < 4) return std::nullopt;
    std::uint32_t value = 0;
    for (const char ch : text.substr(0, 4)) {
        value <<= 4;
        if (ch >= '0' && ch <= '9') value |= static_cast<std::uint32_t>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') value |= static_cast<std::uint32_t>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') value |= static_cast<std::uint32_t>(ch - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

bool Utf8ToWide(std::string_view value, std::wstring* output) {
    if (!output) return false;
    output->clear();
    if (value.empty()) return true;
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return false;
    output->resize(static_cast<std::size_t>(required));
    return MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output->data(), required) == required;
}

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolean{};
    double number{};
    std::string string;
    std::vector<JsonValue> array;
    std::unordered_map<std::string, JsonValue> object;

    const JsonValue* Find(std::string_view key) const noexcept {
        if (type != Type::Object) return nullptr;
        const auto it = object.find(std::string(key));
        return it == object.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {}

    bool Parse(JsonValue* output, std::wstring* error) {
        if (!output) return Fail(error, L"JSON output is null.");
        SkipWhitespace();
        if (!ParseValue(output)) return Fail(error, Message());
        SkipWhitespace();
        if (position_ != source_.size()) return Fail(error, Message(L"Unexpected trailing JSON content"));
        return true;
    }

private:
    void SkipWhitespace() {
        while (position_ < source_.size()) {
            const char ch = source_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++position_;
        }
    }

    std::wstring Message(std::wstring_view text = L"Invalid JSON") const {
        return std::wstring(text) + L" at byte " + std::to_wstring(position_) + L".";
    }

    bool ParseValue(JsonValue* value) {
        SkipWhitespace();
        if (!value || position_ >= source_.size()) return false;
        const char ch = source_[position_];
        if (ch == '{') return ParseObject(value);
        if (ch == '[') return ParseArray(value);
        if (ch == '"') {
            value->type = JsonValue::Type::String;
            return ParseString(&value->string);
        }
        if (ch == 't' && source_.substr(position_, 4) == "true") {
            position_ += 4;
            value->type = JsonValue::Type::Bool;
            value->boolean = true;
            return true;
        }
        if (ch == 'f' && source_.substr(position_, 5) == "false") {
            position_ += 5;
            value->type = JsonValue::Type::Bool;
            value->boolean = false;
            return true;
        }
        if (ch == 'n' && source_.substr(position_, 4) == "null") {
            position_ += 4;
            value->type = JsonValue::Type::Null;
            return true;
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) return ParseNumber(value);
        return false;
    }

    bool ParseString(std::string* output) {
        if (!output || position_ >= source_.size() || source_[position_] != '"') return false;
        ++position_;
        output->clear();
        while (position_ < source_.size()) {
            const char ch = source_[position_++];
            if (ch == '"') return true;
            if (static_cast<unsigned char>(ch) < 0x20) return false;
            if (ch != '\\') {
                output->push_back(ch);
                continue;
            }
            if (position_ >= source_.size()) return false;
            const char escaped = source_[position_++];
            switch (escaped) {
            case '"': output->push_back('"'); break;
            case '\\': output->push_back('\\'); break;
            case '/': output->push_back('/'); break;
            case 'b': output->push_back('\b'); break;
            case 'f': output->push_back('\f'); break;
            case 'n': output->push_back('\n'); break;
            case 'r': output->push_back('\r'); break;
            case 't': output->push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > source_.size()) return false;
                const auto first = ParseHex4(source_.substr(position_, 4));
                if (!first) return false;
                position_ += 4;
                std::uint32_t cp = *first;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (position_ + 6 > source_.size() || source_[position_] != '\\' || source_[position_ + 1] != 'u')
                        return false;
                    position_ += 2;
                    const auto second = ParseHex4(source_.substr(position_, 4));
                    if (!second || *second < 0xDC00 || *second > 0xDFFF) return false;
                    position_ += 4;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (*second - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                }
                AppendUtf8(*output, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseNumber(JsonValue* value) {
        const std::size_t begin = position_;
        if (source_[position_] == '-') ++position_;
        if (position_ >= source_.size()) return false;
        if (source_[position_] == '0') {
            ++position_;
        } else {
            if (source_[position_] < '1' || source_[position_] > '9') return false;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') return false;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) ++position_;
            if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') return false;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        const auto token = source_.substr(begin, position_ - begin);
        double parsed = 0.0;
        const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), parsed);
        if (ec != std::errc{} || end != token.data() + token.size() || !std::isfinite(parsed)) return false;
        value->type = JsonValue::Type::Number;
        value->number = parsed;
        return true;
    }

    bool ParseArray(JsonValue* value) {
        ++position_;
        value->type = JsonValue::Type::Array;
        value->array.clear();
        SkipWhitespace();
        if (position_ < source_.size() && source_[position_] == ']') {
            ++position_;
            return true;
        }
        while (position_ < source_.size()) {
            JsonValue item;
            if (!ParseValue(&item)) return false;
            value->array.push_back(std::move(item));
            SkipWhitespace();
            if (position_ >= source_.size()) return false;
            if (source_[position_] == ']') {
                ++position_;
                return true;
            }
            if (source_[position_] != ',') return false;
            ++position_;
        }
        return false;
    }

    bool ParseObject(JsonValue* value) {
        ++position_;
        value->type = JsonValue::Type::Object;
        value->object.clear();
        SkipWhitespace();
        if (position_ < source_.size() && source_[position_] == '}') {
            ++position_;
            return true;
        }
        while (position_ < source_.size()) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(&key)) return false;
            SkipWhitespace();
            if (position_ >= source_.size() || source_[position_] != ':') return false;
            ++position_;
            JsonValue item;
            if (!ParseValue(&item)) return false;
            if (!value->object.emplace(std::move(key), std::move(item)).second) return false;
            SkipWhitespace();
            if (position_ >= source_.size()) return false;
            if (source_[position_] == '}') {
                ++position_;
                return true;
            }
            if (source_[position_] != ',') return false;
            ++position_;
        }
        return false;
    }

    std::string_view source_;
    std::size_t position_{};
};

bool RequireObject(const JsonValue& value, std::wstring_view label, std::wstring* error) {
    return value.type == JsonValue::Type::Object || Fail(error, std::wstring(label) + L" must be an object.");
}

bool RequireArray(const JsonValue* value, std::wstring_view label, std::wstring* error) {
    if (!value) return Fail(error, std::wstring(label) + L" is required.");
    return value->type == JsonValue::Type::Array || Fail(error, std::wstring(label) + L" must be an array.");
}

bool ReadWideString(
    const JsonValue& object,
    std::string_view key,
    std::wstring* output,
    bool required,
    std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (required) return Fail(error, L"Missing string field in content schema.");
        if (output) output->clear();
        return true;
    }
    if (value->type != JsonValue::Type::String)
        return Fail(error, L"Content schema field must be a string.");
    if (!Utf8ToWide(value->string, output))
        return Fail(error, L"Content schema string contains invalid UTF-8.");
    return true;
}

bool ReadOptionalNumber(
    const JsonValue& object,
    std::string_view key,
    std::optional<double>* output,
    std::wstring* error) {
    if (!output) return Fail(error, L"Numeric schema output is null.");
    output->reset();
    const auto* value = object.Find(key);
    if (!value) return true;
    if (value->type != JsonValue::Type::Number || !std::isfinite(value->number))
        return Fail(error, L"Content schema numeric field must be finite.");
    *output = value->number;
    return true;
}

bool ReadNumber(
    const JsonValue& object,
    std::string_view key,
    double fallback,
    double* output,
    std::wstring* error) {
    if (!output) return Fail(error, L"Numeric output is null.");
    const auto* value = object.Find(key);
    if (!value) {
        *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Number || !std::isfinite(value->number))
        return Fail(error, L"Content schema numeric field must be finite.");
    *output = value->number;
    return true;
}

bool ReadBool(
    const JsonValue& object,
    std::string_view key,
    bool fallback,
    bool* output,
    std::wstring* error) {
    if (!output) return Fail(error, L"Boolean output is null.");
    const auto* value = object.Find(key);
    if (!value) {
        *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Bool)
        return Fail(error, L"Content schema boolean field must be boolean.");
    *output = value->boolean;
    return true;
}

bool ReadUtf8File(const fs::path& path, std::size_t limit, std::string* output) {
    if (!output) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > limit) return false;
    input.seekg(0, std::ios::beg);
    output->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return static_cast<bool>(input) || input.eof();
}

std::wstring ParameterKeyFromRuntimeId(std::wstring_view runtimeId) {
    constexpr std::wstring_view prefix = L"param://";
    std::wstring key = runtimeId.starts_with(prefix)
        ? std::wstring(runtimeId.substr(prefix.size()))
        : std::wstring(runtimeId);
    for (auto& ch : key) {
        if (ch == L'/') ch = L'.';
    }
    return key;
}

bool ParseParameterType(std::string_view text, ContentParameterType* type) {
    if (!type) return false;
    if (text == "bool") *type = ContentParameterType::Bool;
    else if (text == "int") *type = ContentParameterType::Int;
    else if (text == "float") *type = ContentParameterType::Float;
    else if (text == "string") *type = ContentParameterType::String;
    else if (text == "color") *type = ContentParameterType::Color;
    else if (text == "enum") *type = ContentParameterType::Enum;
    else if (text == "asset" || text == "assetReference" || text == "image") *type = ContentParameterType::Asset;
    else return false;
    return true;
}

bool ParseHexNibble(wchar_t ch, unsigned* value) {
    if (!value) return false;
    if (ch >= L'0' && ch <= L'9') *value = static_cast<unsigned>(ch - L'0');
    else if (ch >= L'a' && ch <= L'f') *value = static_cast<unsigned>(ch - L'a' + 10);
    else if (ch >= L'A' && ch <= L'F') *value = static_cast<unsigned>(ch - L'A' + 10);
    else return false;
    return true;
}

bool ParseHexByte(std::wstring_view text, std::size_t offset, double* output) {
    if (!output || offset + 2 > text.size()) return false;
    unsigned high = 0;
    unsigned low = 0;
    if (!ParseHexNibble(text[offset], &high) || !ParseHexNibble(text[offset + 1], &low)) return false;
    *output = static_cast<double>((high << 4) | low) / 255.0;
    return true;
}

bool ParseColor(const JsonValue& value, Color4* color, std::wstring* error) {
    if (!color) return Fail(error, L"Color output is null.");
    if (value.type == JsonValue::Type::Array) {
        if (value.array.size() != 4) return Fail(error, L"Color parameter default must contain four channels.");
        double channels[4]{};
        for (std::size_t i = 0; i < 4; ++i) {
            if (value.array[i].type != JsonValue::Type::Number || !std::isfinite(value.array[i].number))
                return Fail(error, L"Color parameter channels must be finite numbers.");
            channels[i] = value.array[i].number;
        }
        *color = Color4{channels[0], channels[1], channels[2], channels[3]};
        return true;
    }
    if (value.type == JsonValue::Type::String) {
        std::wstring text;
        if (!Utf8ToWide(value.string, &text)) return Fail(error, L"Color parameter contains invalid UTF-8.");
        if (text.size() != 7 && text.size() != 9) return Fail(error, L"Hex color must be #RRGGBB or #RRGGBBAA.");
        if (text[0] != L'#') return Fail(error, L"Hex color must start with #.");
        double r = 0.0, g = 0.0, b = 0.0, a = 1.0;
        if (!ParseHexByte(text, 1, &r) || !ParseHexByte(text, 3, &g) || !ParseHexByte(text, 5, &b) ||
            (text.size() == 9 && !ParseHexByte(text, 7, &a)))
            return Fail(error, L"Hex color contains invalid digits.");
        *color = Color4{r, g, b, a};
        return true;
    }
    return Fail(error, L"Color parameter default must be an RGBA array or hex string.");
}

bool ParseParameterValue(
    ContentParameterType type,
    const JsonValue& value,
    ContentParameterValue* output,
    std::wstring* error) {
    if (!output) return Fail(error, L"Parameter value output is null.");
    switch (type) {
    case ContentParameterType::Bool:
        if (value.type != JsonValue::Type::Bool) return Fail(error, L"Boolean parameter default is invalid.");
        *output = value.boolean;
        return true;
    case ContentParameterType::Int:
        if (value.type != JsonValue::Type::Number || !std::isfinite(value.number) ||
            std::floor(value.number) != value.number ||
            value.number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            value.number > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return Fail(error, L"Integer parameter default is invalid.");
        *output = static_cast<std::int64_t>(value.number);
        return true;
    case ContentParameterType::Float:
        if (value.type != JsonValue::Type::Number || !std::isfinite(value.number))
            return Fail(error, L"Float parameter default is invalid.");
        *output = value.number;
        return true;
    case ContentParameterType::String:
    case ContentParameterType::Enum: {
        if (value.type != JsonValue::Type::String) return Fail(error, L"String/enum parameter default is invalid.");
        std::wstring text;
        if (!Utf8ToWide(value.string, &text)) return Fail(error, L"Parameter default contains invalid UTF-8.");
        *output = std::move(text);
        return true;
    }
    case ContentParameterType::Color: {
        Color4 color;
        if (!ParseColor(value, &color, error)) return false;
        *output = color;
        return true;
    }
    case ContentParameterType::Asset: {
        if (value.type != JsonValue::Type::String) return Fail(error, L"Asset parameter default must be an asset id string.");
        std::wstring id;
        if (!Utf8ToWide(value.string, &id)) return Fail(error, L"Asset parameter default contains invalid UTF-8.");
        *output = AssetReference{std::move(id)};
        return true;
    }
    }
    return Fail(error, L"Unsupported content parameter type.");
}

bool ReadChoices(const JsonValue& object, std::vector<std::wstring>* choices, std::wstring* error) {
    if (!choices) return Fail(error, L"Choice output is null.");
    choices->clear();
    const auto* value = object.Find("choices");
    if (!value) return true;
    if (value->type != JsonValue::Type::Array) return Fail(error, L"Parameter choices must be an array.");
    for (const auto& item : value->array) {
        if (item.type != JsonValue::Type::String) return Fail(error, L"Parameter choice must be a string.");
        std::wstring text;
        if (!Utf8ToWide(item.string, &text)) return Fail(error, L"Parameter choice contains invalid UTF-8.");
        choices->push_back(std::move(text));
    }
    return true;
}

bool ParseGeometry(
    const LoadedMiaoContentPackage& package,
    ContentGeometryPolicy* geometry,
    std::wstring* error) {
    if (!geometry) return Fail(error, L"Geometry output is null.");

    if (package.manifest.kind == ContentKind::Widget) {
        geometry->defaultWidth = 0.30f;
        geometry->defaultHeight = 0.30f;
        geometry->resizeAllowed = false;
        geometry->minWidth = 0.30f;
        geometry->minHeight = 0.30f;
        geometry->maxWidth = 0.30f;
        geometry->maxHeight = 0.30f;
        geometry->aspectRatio.reset();
    } else {
        geometry->defaultWidth = 1.0f;
        geometry->defaultHeight = 1.0f;
        geometry->resizeAllowed = false;
        geometry->minWidth = 1.0f;
        geometry->minHeight = 1.0f;
        geometry->maxWidth = 1.0f;
        geometry->maxHeight = 1.0f;
        geometry->aspectRatio.reset();
    }

    std::string manifestSource;
    if (!ReadUtf8File(package.root / L"manifest.json", MiaoContentPackage::kManifestMaxBytes, &manifestSource))
        return Fail(error, L"Cannot read content manifest for geometry policy.");
    JsonValue root;
    JsonParser parser(manifestSource);
    if (!parser.Parse(&root, error) || !RequireObject(root, L"manifest.json root", error)) return false;
    const auto* geometryValue = root.Find("geometry");
    if (!geometryValue) return true;
    if (!RequireObject(*geometryValue, L"geometry", error)) return false;

    double defaultWidth = geometry->defaultWidth;
    double defaultHeight = geometry->defaultHeight;
    bool resize = geometry->resizeAllowed;
    if (!ReadNumber(*geometryValue, "defaultWidth", defaultWidth, &defaultWidth, error) ||
        !ReadNumber(*geometryValue, "defaultHeight", defaultHeight, &defaultHeight, error) ||
        !ReadBool(*geometryValue, "resize", resize, &resize, error)) return false;

    geometry->defaultWidth = static_cast<float>(defaultWidth);
    geometry->defaultHeight = static_cast<float>(defaultHeight);
    geometry->resizeAllowed = resize;

    double minWidth = resize ? 0.05 : defaultWidth;
    double minHeight = resize ? 0.05 : defaultHeight;
    double maxWidth = resize ? 1.0 : defaultWidth;
    double maxHeight = resize ? 1.0 : defaultHeight;
    if (!ReadNumber(*geometryValue, "minWidth", minWidth, &minWidth, error) ||
        !ReadNumber(*geometryValue, "minHeight", minHeight, &minHeight, error) ||
        !ReadNumber(*geometryValue, "maxWidth", maxWidth, &maxWidth, error) ||
        !ReadNumber(*geometryValue, "maxHeight", maxHeight, &maxHeight, error)) return false;
    geometry->minWidth = static_cast<float>(minWidth);
    geometry->minHeight = static_cast<float>(minHeight);
    geometry->maxWidth = static_cast<float>(maxWidth);
    geometry->maxHeight = static_cast<float>(maxHeight);

    const auto* aspect = geometryValue->Find("aspectRatio");
    if (!aspect) return true;
    if (aspect->type == JsonValue::Type::Number && std::isfinite(aspect->number) && aspect->number > 0.0) {
        geometry->aspectRatio = static_cast<float>(aspect->number);
        return true;
    }
    if (aspect->type == JsonValue::Type::String && aspect->string == "free") {
        geometry->aspectRatio.reset();
        return true;
    }
    return Fail(error, L"geometry.aspectRatio must be a positive number or \"free\".");
}

bool WriteTextFile(const fs::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}

} // namespace

bool MiaoContentDefinitionLoader::ParseParameterSchema(
    std::string_view parameterJsonUtf8,
    ContentParameterSchema* schema,
    std::wstring* error) {
    if (!schema) return Fail(error, L"Parameter schema output is null.");
    schema->clear();
    if (parameterJsonUtf8.empty()) {
        if (error) error->clear();
        return true;
    }

    JsonValue root;
    JsonParser parser(parameterJsonUtf8);
    if (!parser.Parse(&root, error) || !RequireObject(root, L"parameters.json root", error)) return false;
    const auto* schemaValue = root.Find("schema");
    if (!schemaValue || schemaValue->type != JsonValue::Type::Number || schemaValue->number != 1.0)
        return Fail(error, L"parameters.json schema version must be 1.");
    const auto* parameters = root.Find("parameters");
    if (!RequireArray(parameters, L"parameters", error)) return false;

    ContentParameterSchema parsed;
    parsed.reserve(parameters->array.size());
    for (const auto& item : parameters->array) {
        if (!RequireObject(item, L"parameter", error)) return false;
        ContentParameterDefinition parameter;
        if (!ReadWideString(item, "id", &parameter.runtimeId, true, error) ||
            !ReadWideString(item, "key", &parameter.key, false, error)) return false;
        if (parameter.key.empty()) parameter.key = ParameterKeyFromRuntimeId(parameter.runtimeId);

        const auto* typeValue = item.Find("type");
        if (!typeValue || typeValue->type != JsonValue::Type::String ||
            !ParseParameterType(typeValue->string, &parameter.type))
            return Fail(error, L"Content parameter type is missing or unsupported: " + parameter.runtimeId);

        const auto* defaultValue = item.Find("default");
        if (!defaultValue || !ParseParameterValue(parameter.type, *defaultValue, &parameter.defaultValue, error))
            return false;
        if (!ReadOptionalNumber(item, "min", &parameter.minimum, error) ||
            !ReadOptionalNumber(item, "max", &parameter.maximum, error) ||
            !ReadOptionalNumber(item, "step", &parameter.step, error) ||
            !ReadChoices(item, &parameter.choices, error)) return false;
        parsed.push_back(std::move(parameter));
    }

    *schema = std::move(parsed);
    if (error) error->clear();
    return true;
}

bool MiaoContentDefinitionLoader::FromPackage(
    const LoadedMiaoContentPackage& package,
    ContentDefinition* definition,
    std::wstring* error) {
    if (!definition) return Fail(error, L"Content definition output is null.");

    ContentDefinition parsed;
    if (!Utf8ToWide(package.manifest.id, &parsed.id) ||
        !Utf8ToWide(package.manifest.name, &parsed.name) ||
        !Utf8ToWide(package.manifest.version, &parsed.version))
        return Fail(error, L"Content manifest metadata contains invalid UTF-8.");
    parsed.kind = package.manifest.kind;
    parsed.runtime = package.manifest.runtime;
    parsed.entry = package.manifest.entry;

    parsed.capabilities.reserve(package.manifest.capabilities.size());
    for (const auto& capability : package.manifest.capabilities) {
        std::wstring text;
        if (!Utf8ToWide(capability, &text)) return Fail(error, L"Content capability contains invalid UTF-8.");
        parsed.capabilities.push_back(std::move(text));
    }

    if (!ParseParameterSchema(package.parameterSourceUtf8, &parsed.parameters, error)) return false;
    if (!ParseGeometry(package, &parsed.geometry, error)) return false;
    if (!MiaoContentModel::ValidateDefinition(parsed, error)) return false;

    *definition = std::move(parsed);
    if (error) error->clear();
    return true;
}

bool MiaoContentDefinitionLoader::Load(
    const fs::path& packageRoot,
    ContentDefinition* definition,
    std::wstring* error) {
    LoadedMiaoContentPackage package;
    if (!MiaoContentPackage::Load(packageRoot, &package, error)) return false;
    return FromPackage(package, definition, error);
}

bool MiaoContentDefinitionLoader::SelfTest() {
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-ContentDefinition-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L".mdwidget");
    std::error_code ec;
    fs::remove_all(root, ec);
    ec.clear();
    fs::create_directories(root, ec);
    if (ec) return false;

    const bool filesWritten =
        WriteTextFile(root / L"manifest.json", R"json({
          "schema":1,
          "id":"com.goodloong.glass-clock-selftest",
          "name":"Glass Clock Self Test",
          "author":"MiaoDesk",
          "version":"1.0.0",
          "kind":"widget",
          "runtime":"scene",
          "entry":"scene.json",
          "parameters":"parameters.json",
          "capabilities":["clock.read"],
          "geometry":{
            "defaultWidth":0.30,
            "defaultHeight":0.30,
            "resize":false,
            "aspectRatio":1.0
          }
        })json") &&
        WriteTextFile(root / L"scene.json", R"json({"schema":1})json") &&
        WriteTextFile(root / L"parameters.json", R"json({
          "schema":1,
          "parameters":[
            {"id":"param://opacity","type":"float","default":0.55,"min":0.0,"max":1.0,"step":0.01},
            {"id":"param://accent-color","key":"accentColor","type":"color","default":"#72A7FF"},
            {"id":"param://time-format","key":"timeFormat","type":"enum","default":"24h","choices":["24h","12h"]}
          ]
        })json");

    ContentDefinition definition;
    std::wstring error;
    bool ok = filesWritten && Load(root, &definition, &error);
    ok = ok && definition.id == L"com.goodloong.glass-clock-selftest" &&
        definition.kind == ContentKind::Widget && definition.runtime == ContentRuntimeKind::Scene &&
        definition.parameters.size() == 3 && definition.capabilities.size() == 1 &&
        std::fabs(definition.geometry.defaultWidth - 0.30f) < 0.0001f &&
        !definition.geometry.resizeAllowed && definition.geometry.aspectRatio.has_value();
    if (ok) {
        const auto* opacity = MiaoContentModel::FindParameter(definition, L"opacity");
        const auto* accent = MiaoContentModel::FindParameterByRuntimeId(definition, L"param://accent-color");
        ok = opacity && opacity->minimum == 0.0 && opacity->maximum == 1.0 && opacity->step == 0.01 &&
             accent && accent->type == ContentParameterType::Color &&
             std::holds_alternative<Color4>(accent->defaultValue);
    }

    if (ok) {
        ContentInstance instance;
        instance.instanceId = L"glass-clock-selftest-1";
        instance.definitionId = definition.id;
        instance.x = 0.60f;
        instance.y = 0.05f;
        instance.width = definition.geometry.defaultWidth;
        instance.height = definition.geometry.defaultHeight;
        instance.parameterValues.emplace(L"opacity", 0.40);
        ok = MiaoContentModel::ValidateInstance(definition, instance, &error);
        instance.width = 0.35f;
        ok = ok && !MiaoContentModel::ValidateInstance(definition, instance, &error);
    }

    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::content
