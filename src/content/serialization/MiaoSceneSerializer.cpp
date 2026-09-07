#include "miaodesk/MiaoSceneSerializer.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

void AppendUtf8(std::string& out, std::uint32_t codePoint) {
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

bool DecodeUtf8CodePoint(std::string_view text, std::size_t* pos, std::uint32_t* codePoint) {
    if (!pos || !codePoint || *pos >= text.size()) return false;
    const auto first = static_cast<unsigned char>(text[*pos]);
    if (first < 0x80) {
        *codePoint = first;
        ++*pos;
        return true;
    }

    int count = 0;
    std::uint32_t value = 0;
    if ((first & 0xE0) == 0xC0) {
        count = 2;
        value = first & 0x1F;
        if (value == 0) return false;
    } else if ((first & 0xF0) == 0xE0) {
        count = 3;
        value = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        count = 4;
        value = first & 0x07;
    } else {
        return false;
    }
    if (*pos + static_cast<std::size_t>(count) > text.size()) return false;
    for (int i = 1; i < count; ++i) {
        const auto ch = static_cast<unsigned char>(text[*pos + static_cast<std::size_t>(i)]);
        if ((ch & 0xC0) != 0x80) return false;
        value = (value << 6) | (ch & 0x3F);
    }
    if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
        (count == 4 && value < 0x10000) || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }
    *pos += static_cast<std::size_t>(count);
    *codePoint = value;
    return true;
}

bool Utf8ToWide(std::string_view text, std::wstring* output) {
    if (!output) return false;
    output->clear();
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::uint32_t cp = 0;
        if (!DecodeUtf8CodePoint(text, &pos, &cp)) return false;
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp <= 0xFFFF) {
                output->push_back(static_cast<wchar_t>(cp));
            } else {
                cp -= 0x10000;
                output->push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                output->push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
            }
        } else {
            output->push_back(static_cast<wchar_t>(cp));
        }
    }
    return true;
}

std::string WideToUtf8(std::wstring_view text) {
    std::string output;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(text[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 1 >= text.size()) return {};
                const std::uint32_t second = static_cast<std::uint32_t>(text[++i]);
                if (second < 0xDC00 || second > 0xDFFF) return {};
                cp = 0x10000 + ((cp - 0xD800) << 10) + (second - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return {};
            }
        }
        AppendUtf8(output, cp);
    }
    return output;
}

std::optional<std::uint32_t> ParseHex4(std::string_view text) {
    if (text.size() < 4) return std::nullopt;
    std::uint32_t value = 0;
    for (char ch : text.substr(0, 4)) {
        value <<= 4;
        if (ch >= '0' && ch <= '9') value |= static_cast<std::uint32_t>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') value |= static_cast<std::uint32_t>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') value |= static_cast<std::uint32_t>(ch - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolValue{};
    double numberValue{};
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, JsonValue> objectValue;

    const JsonValue* Find(std::string_view key) const noexcept {
        if (type != Type::Object) return nullptr;
        const auto it = objectValue.find(std::string(key));
        return it == objectValue.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    bool Parse(JsonValue* value, std::wstring* error) {
        if (!value) return Fail(error, L"JSON output is null.");
        SkipWhitespace();
        if (!ParseValue(value)) return Fail(error, ErrorMessage());
        SkipWhitespace();
        if (pos_ != text_.size()) return Fail(error, ErrorMessage(L"Unexpected trailing JSON content"));
        return true;
    }

private:
    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++pos_;
        }
    }

    std::wstring ErrorMessage(std::wstring_view message = L"Invalid JSON") const {
        return std::wstring(message) + L" at byte " + std::to_wstring(pos_) + L".";
    }

    bool ParseValue(JsonValue* value) {
        SkipWhitespace();
        if (pos_ >= text_.size()) return false;
        const char ch = text_[pos_];
        if (ch == '{') return ParseObject(value);
        if (ch == '[') return ParseArray(value);
        if (ch == '"') {
            value->type = JsonValue::Type::String;
            return ParseString(&value->stringValue);
        }
        if (ch == 't' && text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            value->type = JsonValue::Type::Bool;
            value->boolValue = true;
            return true;
        }
        if (ch == 'f' && text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            value->type = JsonValue::Type::Bool;
            value->boolValue = false;
            return true;
        }
        if (ch == 'n' && text_.substr(pos_, 4) == "null") {
            pos_ += 4;
            value->type = JsonValue::Type::Null;
            return true;
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) return ParseNumber(value);
        return false;
    }

    bool ParseString(std::string* output) {
        if (!output || pos_ >= text_.size() || text_[pos_] != '"') return false;
        ++pos_;
        output->clear();
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"') return true;
            if (static_cast<unsigned char>(ch) < 0x20) return false;
            if (ch != '\\') {
                output->push_back(ch);
                continue;
            }
            if (pos_ >= text_.size()) return false;
            const char escaped = text_[pos_++];
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
                if (pos_ + 4 > text_.size()) return false;
                const auto first = ParseHex4(text_.substr(pos_, 4));
                if (!first) return false;
                pos_ += 4;
                std::uint32_t cp = *first;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pos_ + 6 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') return false;
                    pos_ += 2;
                    const auto second = ParseHex4(text_.substr(pos_, 4));
                    if (!second || *second < 0xDC00 || *second > 0xDFFF) return false;
                    pos_ += 4;
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
        const std::size_t begin = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return false;
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            if (text_[pos_] < '1' || text_[pos_] > '9') return false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') return false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') return false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        double number = 0.0;
        const auto token = text_.substr(begin, pos_ - begin);
        const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), number);
        if (ec != std::errc{} || ptr != token.data() + token.size() || !std::isfinite(number)) return false;
        value->type = JsonValue::Type::Number;
        value->numberValue = number;
        return true;
    }

    bool ParseArray(JsonValue* value) {
        if (text_[pos_] != '[') return false;
        ++pos_;
        value->type = JsonValue::Type::Array;
        value->arrayValue.clear();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < text_.size()) {
            JsonValue item;
            if (!ParseValue(&item)) return false;
            value->arrayValue.push_back(std::move(item));
            SkipWhitespace();
            if (pos_ >= text_.size()) return false;
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (text_[pos_] != ',') return false;
            ++pos_;
            SkipWhitespace();
        }
        return false;
    }

    bool ParseObject(JsonValue* value) {
        if (text_[pos_] != '{') return false;
        ++pos_;
        value->type = JsonValue::Type::Object;
        value->objectValue.clear();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < text_.size()) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(&key)) return false;
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return false;
            ++pos_;
            JsonValue item;
            if (!ParseValue(&item)) return false;
            if (!value->objectValue.emplace(std::move(key), std::move(item)).second) return false;
            SkipWhitespace();
            if (pos_ >= text_.size()) return false;
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (text_[pos_] != ',') return false;
            ++pos_;
        }
        return false;
    }

    std::string_view text_;
    std::size_t pos_{};
};

bool RequireObject(const JsonValue& value, std::wstring_view label, std::wstring* error) {
    return value.type == JsonValue::Type::Object || Fail(error, std::wstring(label) + L" must be an object.");
}

bool RequireArray(const JsonValue* value, std::wstring_view label, std::wstring* error) {
    if (!value) return Fail(error, std::wstring(label) + L" is required.");
    return value->type == JsonValue::Type::Array || Fail(error, std::wstring(label) + L" must be an array.");
}

bool ReadString(const JsonValue& object, std::string_view key, std::wstring* output, bool required, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (!required) {
            if (output) output->clear();
            return true;
        }
        return Fail(error, L"Missing string field: " + std::wstring(key.begin(), key.end()));
    }
    if (value->type != JsonValue::Type::String) return Fail(error, L"Field must be string: " + std::wstring(key.begin(), key.end()));
    if (!Utf8ToWide(value->stringValue, output)) return Fail(error, L"Field contains invalid UTF-8: " + std::wstring(key.begin(), key.end()));
    return true;
}

bool ReadString8(const JsonValue& object, std::string_view key, std::string* output, bool required, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (!required) {
            if (output) output->clear();
            return true;
        }
        return Fail(error, L"Missing string field: " + std::wstring(key.begin(), key.end()));
    }
    if (value->type != JsonValue::Type::String) return Fail(error, L"Field must be string: " + std::wstring(key.begin(), key.end()));
    if (output) *output = value->stringValue;
    std::wstring decoded;
    if (!Utf8ToWide(value->stringValue, &decoded)) return Fail(error, L"Field contains invalid UTF-8: " + std::wstring(key.begin(), key.end()));
    return true;
}

bool ReadBool(const JsonValue& object, std::string_view key, bool fallback, bool* output, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Bool) return Fail(error, L"Field must be boolean: " + std::wstring(key.begin(), key.end()));
    if (output) *output = value->boolValue;
    return true;
}

bool ReadNumber(const JsonValue& object, std::string_view key, double fallback, double* output, bool required, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (required) return Fail(error, L"Missing numeric field: " + std::wstring(key.begin(), key.end()));
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Number || !std::isfinite(value->numberValue))
        return Fail(error, L"Field must be finite number: " + std::wstring(key.begin(), key.end()));
    if (output) *output = value->numberValue;
    return true;
}

bool ReadSchema(const JsonValue& object, std::wstring_view label, std::wstring* error) {
    double schema = 0.0;
    if (!ReadNumber(object, "schema", 0.0, &schema, true, error)) return false;
    if (schema != 1.0) return Fail(error, std::wstring(label) + L" schema version must be 1.");
    return true;
}

bool ParseContentKind(std::string_view value, ContentKind* kind) {
    if (value == "wallpaper") { *kind = ContentKind::Wallpaper; return true; }
    if (value == "widget") { *kind = ContentKind::Widget; return true; }
    return false;
}

bool ParseProfile(std::string_view value, RuntimeProfile* profile) {
    if (value == "wallpaper") { *profile = RuntimeProfile::Wallpaper; return true; }
    if (value == "widget") { *profile = RuntimeProfile::Widget; return true; }
    return false;
}

bool ParseComponentKind(std::string_view value, ComponentKind* kind) {
    static const std::pair<std::string_view, ComponentKind> values[] = {
        {"transform", ComponentKind::Transform},
        {"spriteRenderer", ComponentKind::SpriteRenderer},
        {"textRenderer", ComponentKind::TextRenderer},
        {"videoRenderer", ComponentKind::VideoRenderer},
        {"material", ComponentKind::Material},
        {"particleSystem", ComponentKind::ParticleSystem},
        {"animator", ComponentKind::Animator},
        {"script", ComponentKind::Script},
        {"inputBinding", ComponentKind::InputBinding},
        {"custom", ComponentKind::Custom},
    };
    for (const auto& [name, item] : values) if (value == name) { *kind = item; return true; }
    return false;
}

bool ParseAssetType(std::string_view value, AssetType* type) {
    static const std::pair<std::string_view, AssetType> values[] = {
        {"image", AssetType::Image}, {"video", AssetType::Video}, {"audio", AssetType::Audio},
        {"font", AssetType::Font}, {"shader", AssetType::Shader}, {"script", AssetType::Script},
        {"mesh", AssetType::Mesh}, {"binary", AssetType::Binary},
    };
    for (const auto& [name, item] : values) if (value == name) { *type = item; return true; }
    return false;
}

bool ParseShaderStage(std::string_view value, ShaderStage* stage) {
    if (value == "vertex") { *stage = ShaderStage::Vertex; return true; }
    if (value == "pixel") { *stage = ShaderStage::Pixel; return true; }
    if (value == "compute") { *stage = ShaderStage::Compute; return true; }
    return false;
}

bool ParsePropertyType(std::string_view value, PropertyType* type) {
    static const std::pair<std::string_view, PropertyType> values[] = {
        {"bool", PropertyType::Bool}, {"int", PropertyType::Int}, {"float", PropertyType::Float},
        {"string", PropertyType::String}, {"vec2", PropertyType::Vec2}, {"vec3", PropertyType::Vec3},
        {"vec4", PropertyType::Vec4}, {"color", PropertyType::Color}, {"assetReference", PropertyType::AssetReference},
    };
    for (const auto& [name, item] : values) if (value == name) { *type = item; return true; }
    return false;
}

bool ParseMaterialModel(std::string_view value, MaterialModel* model) {
    if (value == "builtin") { *model = MaterialModel::Builtin; return true; }
    if (value == "programmable") { *model = MaterialModel::Programmable; return true; }
    return false;
}

bool ParseBindingSource(std::string_view value, BindingSourceKind* kind) {
    if (value == "parameter") { *kind = BindingSourceKind::Parameter; return true; }
    if (value == "input") { *kind = BindingSourceKind::Input; return true; }
    return false;
}

bool ParsePostProcessEffect(std::string_view value, PostProcessEffectKind* effect) {
    if (!effect) return false;
    if (value == "copy") *effect = PostProcessEffectKind::Copy;
    else if (value == "vignette") *effect = PostProcessEffectKind::Vignette;
    else if (value == "noise") *effect = PostProcessEffectKind::Noise;
    else if (value == "colorMatrix") *effect = PostProcessEffectKind::ColorMatrix;
    else if (value == "blurHorizontal") *effect = PostProcessEffectKind::BlurHorizontal;
    else if (value == "blurVertical") *effect = PostProcessEffectKind::BlurVertical;
    else if (value == "bloomThreshold") *effect = PostProcessEffectKind::BloomThreshold;
    else if (value == "bloomCombine") *effect = PostProcessEffectKind::BloomCombine;
    else return false;
    return true;
}

const char* PostProcessEffectKey(PostProcessEffectKind effect) {
    switch (effect) {
    case PostProcessEffectKind::Copy: return "copy";
    case PostProcessEffectKind::Vignette: return "vignette";
    case PostProcessEffectKind::Noise: return "noise";
    case PostProcessEffectKind::ColorMatrix: return "colorMatrix";
    case PostProcessEffectKind::BlurHorizontal: return "blurHorizontal";
    case PostProcessEffectKind::BlurVertical: return "blurVertical";
    case PostProcessEffectKind::BloomThreshold: return "bloomThreshold";
    case PostProcessEffectKind::BloomCombine: return "bloomCombine";
    }
    return "copy";
}

const char* ContentKindKey(ContentKind kind) { return kind == ContentKind::Widget ? "widget" : "wallpaper"; }
const char* ProfileKey(RuntimeProfile profile) { return profile == RuntimeProfile::Widget ? "widget" : "wallpaper"; }

const char* ComponentKindKey(ComponentKind kind) {
    switch (kind) {
    case ComponentKind::Transform: return "transform";
    case ComponentKind::SpriteRenderer: return "spriteRenderer";
    case ComponentKind::TextRenderer: return "textRenderer";
    case ComponentKind::VideoRenderer: return "videoRenderer";
    case ComponentKind::Material: return "material";
    case ComponentKind::ParticleSystem: return "particleSystem";
    case ComponentKind::Animator: return "animator";
    case ComponentKind::Script: return "script";
    case ComponentKind::InputBinding: return "inputBinding";
    case ComponentKind::Custom: return "custom";
    }
    return "custom";
}

const char* AssetTypeKey(AssetType type) {
    switch (type) {
    case AssetType::Image: return "image";
    case AssetType::Video: return "video";
    case AssetType::Audio: return "audio";
    case AssetType::Font: return "font";
    case AssetType::Shader: return "shader";
    case AssetType::Script: return "script";
    case AssetType::Mesh: return "mesh";
    case AssetType::Binary: return "binary";
    }
    return "binary";
}

const char* ShaderStageKey(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return "vertex";
    case ShaderStage::Pixel: return "pixel";
    case ShaderStage::Compute: return "compute";
    }
    return "pixel";
}

const char* PropertyTypeKey(PropertyType type) {
    switch (type) {
    case PropertyType::Bool: return "bool";
    case PropertyType::Int: return "int";
    case PropertyType::Float: return "float";
    case PropertyType::String: return "string";
    case PropertyType::Vec2: return "vec2";
    case PropertyType::Vec3: return "vec3";
    case PropertyType::Vec4: return "vec4";
    case PropertyType::Color: return "color";
    case PropertyType::AssetReference: return "assetReference";
    }
    return "float";
}

bool ReadVector(const JsonValue& value, std::size_t count, double* output, std::wstring* error) {
    if (value.type != JsonValue::Type::Array || value.arrayValue.size() != count)
        return Fail(error, L"Vector/color default value has wrong element count.");
    for (std::size_t i = 0; i < count; ++i) {
        if (value.arrayValue[i].type != JsonValue::Type::Number || !std::isfinite(value.arrayValue[i].numberValue))
            return Fail(error, L"Vector/color default value must contain finite numbers.");
        output[i] = value.arrayValue[i].numberValue;
    }
    return true;
}

bool ParsePropertyValue(PropertyType type, const JsonValue& value, PropertyValue* output, std::wstring* error) {
    if (!output) return Fail(error, L"Property output is null.");
    switch (type) {
    case PropertyType::Bool:
        if (value.type != JsonValue::Type::Bool) return Fail(error, L"Property default must be boolean.");
        *output = value.boolValue;
        return true;
    case PropertyType::Int: {
        if (value.type != JsonValue::Type::Number || !std::isfinite(value.numberValue) || std::floor(value.numberValue) != value.numberValue ||
            value.numberValue < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            value.numberValue > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return Fail(error, L"Integer property default is invalid.");
        *output = static_cast<std::int64_t>(value.numberValue);
        return true;
    }
    case PropertyType::Float:
        if (value.type != JsonValue::Type::Number || !std::isfinite(value.numberValue)) return Fail(error, L"Float property default is invalid.");
        *output = value.numberValue;
        return true;
    case PropertyType::String: {
        if (value.type != JsonValue::Type::String) return Fail(error, L"String property default is invalid.");
        std::wstring text;
        if (!Utf8ToWide(value.stringValue, &text)) return Fail(error, L"String property default contains invalid UTF-8.");
        *output = std::move(text);
        return true;
    }
    case PropertyType::Vec2: {
        double data[2]{};
        if (!ReadVector(value, 2, data, error)) return false;
        *output = Vec2{data[0], data[1]};
        return true;
    }
    case PropertyType::Vec3: {
        double data[3]{};
        if (!ReadVector(value, 3, data, error)) return false;
        *output = Vec3{data[0], data[1], data[2]};
        return true;
    }
    case PropertyType::Vec4: {
        double data[4]{};
        if (!ReadVector(value, 4, data, error)) return false;
        *output = Vec4{data[0], data[1], data[2], data[3]};
        return true;
    }
    case PropertyType::Color: {
        double data[4]{};
        if (!ReadVector(value, 4, data, error)) return false;
        *output = Color4{data[0], data[1], data[2], data[3]};
        return true;
    }
    case PropertyType::AssetReference: {
        if (value.type != JsonValue::Type::String) return Fail(error, L"AssetReference property default must be a stable asset id string.");
        std::wstring id;
        if (!Utf8ToWide(value.stringValue, &id)) return Fail(error, L"AssetReference property default contains invalid UTF-8.");
        *output = AssetReference{std::move(id)};
        return true;
    }
    }
    return Fail(error, L"Unsupported property type.");
}

bool ParsePropertyDefinition(const JsonValue& object, PropertyDefinition* property, std::wstring* error) {
    if (!RequireObject(object, L"Property", error)) return false;
    if (!ReadString(object, "name", &property->name, true, error)) return false;
    std::string typeText;
    if (!ReadString8(object, "type", &typeText, true, error) || !ParsePropertyType(typeText, &property->type))
        return Fail(error, L"Property type is invalid: " + property->name);
    const auto* value = object.Find("default");
    if (!value) return Fail(error, L"Property default is required: " + property->name);
    return ParsePropertyValue(property->type, *value, &property->defaultValue, error);
}

bool ParseProperties(const JsonValue& object, std::vector<PropertyDefinition>* properties, std::wstring* error) {
    const auto* values = object.Find("properties");
    if (!values) {
        properties->clear();
        return true;
    }
    if (!RequireArray(values, L"properties", error)) return false;
    properties->clear();
    for (const auto& value : values->arrayValue) {
        PropertyDefinition property;
        if (!ParsePropertyDefinition(value, &property, error)) return false;
        properties->push_back(std::move(property));
    }
    return true;
}

bool ParseSceneRoot(const JsonValue& root, SceneRuntimeDefinition* runtime, std::wstring* error) {
    if (!RequireObject(root, L"scene.json root", error) || !ReadSchema(root, L"scene.json", error)) return false;

    if (!ReadString(root, "id", &runtime->scene.id, true, error)) return false;
    std::string kindText;
    if (!ReadString8(root, "kind", &kindText, true, error) || !ParseContentKind(kindText, &runtime->scene.kind))
        return Fail(error, L"scene.json kind must be wallpaper or widget.");
    std::string profileText;
    if (!ReadString8(root, "profile", &profileText, true, error) || !ParseProfile(profileText, &runtime->profile))
        return Fail(error, L"scene.json profile must be wallpaper or widget.");
    if (!ReadString(root, "rootNodeId", &runtime->scene.rootNodeId, true, error)) return false;
    runtime->scene.schemaVersion = 1;

    const auto* nodes = root.Find("nodes");
    if (!RequireArray(nodes, L"nodes", error)) return false;
    runtime->scene.nodes.clear();
    for (const auto& nodeValue : nodes->arrayValue) {
        if (!RequireObject(nodeValue, L"Node", error)) return false;
        SceneNodeDefinition node;
        if (!ReadString(nodeValue, "id", &node.id, true, error) ||
            !ReadString(nodeValue, "name", &node.name, false, error) ||
            !ReadString(nodeValue, "parentId", &node.parentId, false, error) ||
            !ReadBool(nodeValue, "enabled", true, &node.enabled, error)) return false;
        const auto* components = nodeValue.Find("components");
        if (!RequireArray(components, L"components", error)) return false;
        for (const auto& componentValue : components->arrayValue) {
            if (!RequireObject(componentValue, L"Component", error)) return false;
            SceneComponentDefinition component;
            if (!ReadString(componentValue, "id", &component.id, true, error)) return false;
            std::string componentKind;
            if (!ReadString8(componentValue, "kind", &componentKind, true, error) || !ParseComponentKind(componentKind, &component.kind))
                return Fail(error, L"Component kind is invalid: " + component.id);
            if (!ParseProperties(componentValue, &component.properties, error)) return false;
            node.components.push_back(std::move(component));
        }
        runtime->scene.nodes.push_back(std::move(node));
    }

    const auto* assets = root.Find("assets");
    if (!RequireArray(assets, L"assets", error)) return false;
    runtime->scene.assets.clear();
    for (const auto& assetValue : assets->arrayValue) {
        if (!RequireObject(assetValue, L"Asset", error)) return false;
        AssetDefinition asset;
        if (!ReadString(assetValue, "id", &asset.id, true, error)) return false;
        std::string typeText;
        if (!ReadString8(assetValue, "type", &typeText, true, error) || !ParseAssetType(typeText, &asset.type))
            return Fail(error, L"Asset type is invalid: " + asset.id);
        if (!ReadString(assetValue, "source", &asset.source, true, error)) return false;
        runtime->scene.assets.push_back(std::move(asset));
    }

    const auto* shaders = root.Find("shaders");
    if (!RequireArray(shaders, L"shaders", error)) return false;
    runtime->scene.shaders.clear();
    for (const auto& shaderValue : shaders->arrayValue) {
        if (!RequireObject(shaderValue, L"Shader", error)) return false;
        ShaderDefinition shader;
        if (!ReadString(shaderValue, "id", &shader.id, true, error)) return false;
        std::string stageText;
        if (!ReadString8(shaderValue, "stage", &stageText, true, error) || !ParseShaderStage(stageText, &shader.stage))
            return Fail(error, L"Shader stage is invalid: " + shader.id);
        if (!ReadString(shaderValue, "assetId", &shader.assetId, true, error) ||
            !ReadString8(shaderValue, "entryPoint", &shader.entryPoint, false, error) ||
            !ReadBool(shaderValue, "userAuthored", true, &shader.userAuthored, error)) return false;
        if (shader.entryPoint.empty()) shader.entryPoint = "main";
        runtime->scene.shaders.push_back(std::move(shader));
    }

    const auto* materials = root.Find("materials");
    if (!RequireArray(materials, L"materials", error)) return false;
    runtime->materials.clear();
    for (const auto& materialValue : materials->arrayValue) {
        if (!RequireObject(materialValue, L"Material", error)) return false;
        MaterialDefinition material;
        if (!ReadString(materialValue, "id", &material.id, true, error)) return false;
        std::string modelText;
        if (!ReadString8(materialValue, "model", &modelText, true, error) || !ParseMaterialModel(modelText, &material.model))
            return Fail(error, L"Material model is invalid: " + material.id);
        if (!ReadString(materialValue, "builtinName", &material.builtinName, false, error) ||
            !ReadString(materialValue, "vertexShaderId", &material.vertexShaderId, false, error) ||
            !ReadString(materialValue, "pixelShaderId", &material.pixelShaderId, false, error) ||
            !ParseProperties(materialValue, &material.properties, error)) return false;

        const auto* textures = materialValue.Find("textures");
        if (!RequireArray(textures, L"material textures", error)) return false;
        for (const auto& textureValue : textures->arrayValue) {
            if (!RequireObject(textureValue, L"Material texture", error)) return false;
            MaterialTextureBinding texture;
            if (!ReadString(textureValue, "slot", &texture.slot, true, error) ||
                !ReadString(textureValue, "assetId", &texture.asset.id, true, error)) return false;
            material.textures.push_back(std::move(texture));
        }
        runtime->materials.push_back(std::move(material));
    }

    const auto* inputs = root.Find("inputs");
    if (!RequireArray(inputs, L"inputs", error)) return false;
    runtime->inputs.clear();
    for (const auto& inputValue : inputs->arrayValue) {
        if (!RequireObject(inputValue, L"Input", error)) return false;
        InputChannelDefinition input;
        if (!ReadString(inputValue, "id", &input.id, true, error)) return false;
        std::string typeText;
        if (!ReadString8(inputValue, "type", &typeText, true, error) || !ParsePropertyType(typeText, &input.type))
            return Fail(error, L"Input type is invalid: " + input.id);
        const auto* defaultValue = inputValue.Find("default");
        if (!defaultValue || !ParsePropertyValue(input.type, *defaultValue, &input.defaultValue, error)) return false;
        runtime->inputs.push_back(std::move(input));
    }

    const auto* bindings = root.Find("bindings");
    if (!RequireArray(bindings, L"bindings", error)) return false;
    runtime->bindings.clear();
    for (const auto& bindingValue : bindings->arrayValue) {
        if (!RequireObject(bindingValue, L"Binding", error)) return false;
        PropertyBindingDefinition binding;
        if (!ReadString(bindingValue, "id", &binding.id, true, error)) return false;
        std::string sourceKind;
        if (!ReadString8(bindingValue, "sourceKind", &sourceKind, true, error) || !ParseBindingSource(sourceKind, &binding.sourceKind))
            return Fail(error, L"Binding sourceKind is invalid: " + binding.id);
        if (!ReadString(bindingValue, "sourceId", &binding.sourceId, true, error)) return false;
        const auto* target = bindingValue.Find("target");
        if (!target || !RequireObject(*target, L"Binding target", error) ||
            !ReadString(*target, "componentId", &binding.target.componentId, true, error) ||
            !ReadString(*target, "propertyName", &binding.target.propertyName, true, error) ||
            !ReadNumber(bindingValue, "scale", 1.0, &binding.scale, false, error) ||
            !ReadNumber(bindingValue, "offset", 0.0, &binding.offset, false, error)) return false;
        runtime->bindings.push_back(std::move(binding));
    }

    runtime->postProcesses.clear();
    if (const auto* postProcesses = root.Find("postProcesses")) {
        if (!RequireArray(postProcesses, L"postProcesses", error)) return false;
        for (const auto& effectValue : postProcesses->arrayValue) {
            if (!RequireObject(effectValue, L"Post-process", error)) return false;
            PostProcessDefinition effect;
            if (!ReadString(effectValue, "id", &effect.id, true, error)) return false;
            std::string effectText;
            if (!ReadString8(effectValue, "effect", &effectText, true, error) ||
                !ParsePostProcessEffect(effectText, &effect.effect))
                return Fail(error, L"Post-process effect is invalid: " + effect.id);
            if (!ReadBool(effectValue, "enabled", true, &effect.enabled, error) ||
                !ReadNumber(effectValue, "amount", 1.0, &effect.amount, false, error) ||
                !ReadNumber(effectValue, "radius", 0.75, &effect.radius, false, error) ||
                !ReadNumber(effectValue, "softness", 0.25, &effect.softness, false, error)) return false;
            runtime->postProcesses.push_back(std::move(effect));
        }
    }
    return true;
}

bool ParseParameters(std::string_view jsonText, SceneRuntimeDefinition* runtime, std::wstring* error) {
    runtime->parameters.clear();
    if (jsonText.empty()) return true;
    JsonValue root;
    JsonParser parser(jsonText);
    if (!parser.Parse(&root, error) || !RequireObject(root, L"parameters.json root", error) || !ReadSchema(root, L"parameters.json", error)) return false;
    const auto* parameters = root.Find("parameters");
    if (!RequireArray(parameters, L"parameters", error)) return false;
    for (const auto& parameterValue : parameters->arrayValue) {
        if (!RequireObject(parameterValue, L"Parameter", error)) return false;
        ParameterDefinition parameter;
        if (!ReadString(parameterValue, "id", &parameter.id, true, error)) return false;
        std::string typeText;
        if (!ReadString8(parameterValue, "type", &typeText, true, error) || !ParsePropertyType(typeText, &parameter.type))
            return Fail(error, L"Parameter type is invalid: " + parameter.id);
        const auto* defaultValue = parameterValue.Find("default");
        if (!defaultValue || !ParsePropertyValue(parameter.type, *defaultValue, &parameter.defaultValue, error)) return false;
        runtime->parameters.push_back(std::move(parameter));
    }
    return true;
}

std::string EscapeJson(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[(ch >> 4) & 0xF]);
                out.push_back(hex[ch & 0xF]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    return out;
}

std::string Quote(std::wstring_view value) { return "\"" + EscapeJson(WideToUtf8(value)) + "\""; }
std::string Quote8(std::string_view value) { return "\"" + EscapeJson(value) + "\""; }

std::string Number(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string SerializePropertyValue(const PropertyValue& value) {
    if (const auto* item = std::get_if<bool>(&value)) return *item ? "true" : "false";
    if (const auto* item = std::get_if<std::int64_t>(&value)) return std::to_string(*item);
    if (const auto* item = std::get_if<double>(&value)) return Number(*item);
    if (const auto* item = std::get_if<std::wstring>(&value)) return Quote(*item);
    if (const auto* item = std::get_if<Vec2>(&value)) return "[" + Number(item->x) + "," + Number(item->y) + "]";
    if (const auto* item = std::get_if<Vec3>(&value)) return "[" + Number(item->x) + "," + Number(item->y) + "," + Number(item->z) + "]";
    if (const auto* item = std::get_if<Vec4>(&value)) return "[" + Number(item->x) + "," + Number(item->y) + "," + Number(item->z) + "," + Number(item->w) + "]";
    if (const auto* item = std::get_if<Color4>(&value)) return "[" + Number(item->r) + "," + Number(item->g) + "," + Number(item->b) + "," + Number(item->a) + "]";
    if (const auto* item = std::get_if<AssetReference>(&value)) return Quote(item->id);
    return "null";
}

void AppendProperty(std::string& out, const PropertyDefinition& property, std::string_view indent) {
    out += std::string(indent) + "{\"name\":" + Quote(property.name) + ",\"type\":" + Quote8(PropertyTypeKey(property.type)) +
           ",\"default\":" + SerializePropertyValue(property.defaultValue) + "}";
}

void AppendProperties(std::string& out, const std::vector<PropertyDefinition>& properties, std::string_view indent) {
    out += "[";
    if (!properties.empty()) out += "\n";
    for (std::size_t i = 0; i < properties.size(); ++i) {
        AppendProperty(out, properties[i], indent);
        out += i + 1 == properties.size() ? "\n" : ",\n";
    }
    if (!properties.empty() && indent.size() >= 2) out += std::string(indent.substr(0, indent.size() - 2));
    out += "]";
}

bool EquivalentRuntime(const SceneRuntimeDefinition& a, const SceneRuntimeDefinition& b) {
    return a.scene.id == b.scene.id && a.scene.kind == b.scene.kind && a.profile == b.profile &&
           a.scene.rootNodeId == b.scene.rootNodeId && a.scene.nodes.size() == b.scene.nodes.size() &&
           a.scene.assets.size() == b.scene.assets.size() && a.scene.shaders.size() == b.scene.shaders.size() &&
           a.materials.size() == b.materials.size() && a.parameters.size() == b.parameters.size() &&
           a.inputs.size() == b.inputs.size() && a.bindings.size() == b.bindings.size() &&
           a.postProcesses.size() == b.postProcesses.size();
}

} // namespace

bool MiaoSceneSerializer::Deserialize(
    std::string_view sceneJsonUtf8,
    std::string_view parameterJsonUtf8,
    SceneRuntimeDefinition* runtime,
    std::wstring* error) {
    if (!runtime) return Fail(error, L"Scene runtime output is null.");
    if (sceneJsonUtf8.empty()) return Fail(error, L"scene.json source is empty.");

    JsonValue sceneRoot;
    JsonParser sceneParser(sceneJsonUtf8);
    SceneRuntimeDefinition parsed;
    if (!sceneParser.Parse(&sceneRoot, error) || !ParseSceneRoot(sceneRoot, &parsed, error)) return false;
    if (!ParseParameters(parameterJsonUtf8, &parsed, error)) return false;

    std::wstring validateError;
    if (!MiaoSceneRuntimeModel::Validate(parsed, &validateError))
        return Fail(error, L"Deserialized scene runtime is invalid: " + validateError);

    *runtime = std::move(parsed);
    if (error) error->clear();
    return true;
}

bool MiaoSceneSerializer::DeserializePackage(
    const LoadedMiaoContentPackage& package,
    SceneRuntimeDefinition* runtime,
    std::wstring* error) {
    if (package.manifest.runtime != ContentRuntimeKind::Scene)
        return Fail(error, L"MiaoSceneSerializer only accepts runtime=scene packages.");
    SceneRuntimeDefinition parsed;
    if (!Deserialize(package.entrySourceUtf8, package.parameterSourceUtf8, &parsed, error)) return false;
    if (parsed.scene.kind != package.manifest.kind)
        return Fail(error, L"scene.json kind does not match package manifest kind.");
    *runtime = std::move(parsed);
    if (error) error->clear();
    return true;
}

bool MiaoSceneSerializer::SerializeScene(
    const SceneRuntimeDefinition& runtime,
    std::string* sceneJsonUtf8,
    std::wstring* error) {
    if (!sceneJsonUtf8) return Fail(error, L"Serialized scene output is null.");
    std::wstring validateError;
    if (!MiaoSceneRuntimeModel::Validate(runtime, &validateError))
        return Fail(error, L"Cannot serialize invalid runtime: " + validateError);

    std::string out;
    out += "{\n  \"schema\":1,\n  \"id\":" + Quote(runtime.scene.id) + ",\n  \"kind\":" + Quote8(ContentKindKey(runtime.scene.kind)) +
           ",\n  \"profile\":" + Quote8(ProfileKey(runtime.profile)) + ",\n  \"rootNodeId\":" + Quote(runtime.scene.rootNodeId) + ",\n";

    out += "  \"nodes\":[";
    if (!runtime.scene.nodes.empty()) out += "\n";
    for (std::size_t i = 0; i < runtime.scene.nodes.size(); ++i) {
        const auto& node = runtime.scene.nodes[i];
        out += "    {\"id\":" + Quote(node.id) + ",\"name\":" + Quote(node.name) + ",\"parentId\":" + Quote(node.parentId) +
               ",\"enabled\":" + std::string(node.enabled ? "true" : "false") + ",\"components\":[";
        if (!node.components.empty()) out += "\n";
        for (std::size_t c = 0; c < node.components.size(); ++c) {
            const auto& component = node.components[c];
            out += "      {\"id\":" + Quote(component.id) + ",\"kind\":" + Quote8(ComponentKindKey(component.kind)) + ",\"properties\":";
            AppendProperties(out, component.properties, "        ");
            out += c + 1 == node.components.size() ? "}\n" : "},\n";
        }
        out += "    ]}";
        out += i + 1 == runtime.scene.nodes.size() ? "\n" : ",\n";
    }
    out += "  ],\n";

    out += "  \"assets\":[";
    for (std::size_t i = 0; i < runtime.scene.assets.size(); ++i) {
        const auto& asset = runtime.scene.assets[i];
        if (i) out += ",";
        out += "{\"id\":" + Quote(asset.id) + ",\"type\":" + Quote8(AssetTypeKey(asset.type)) + ",\"source\":" + Quote(asset.source) + "}";
    }
    out += "],\n";

    out += "  \"shaders\":[";
    for (std::size_t i = 0; i < runtime.scene.shaders.size(); ++i) {
        const auto& shader = runtime.scene.shaders[i];
        if (i) out += ",";
        out += "{\"id\":" + Quote(shader.id) + ",\"stage\":" + Quote8(ShaderStageKey(shader.stage)) + ",\"assetId\":" + Quote(shader.assetId) +
               ",\"entryPoint\":" + Quote8(shader.entryPoint) + ",\"userAuthored\":" + std::string(shader.userAuthored ? "true" : "false") + "}";
    }
    out += "],\n";

    out += "  \"materials\":[";
    if (!runtime.materials.empty()) out += "\n";
    for (std::size_t i = 0; i < runtime.materials.size(); ++i) {
        const auto& material = runtime.materials[i];
        out += "    {\"id\":" + Quote(material.id) + ",\"model\":" + Quote8(material.model == MaterialModel::Builtin ? "builtin" : "programmable");
        if (!material.builtinName.empty()) out += ",\"builtinName\":" + Quote(material.builtinName);
        if (!material.vertexShaderId.empty()) out += ",\"vertexShaderId\":" + Quote(material.vertexShaderId);
        if (!material.pixelShaderId.empty()) out += ",\"pixelShaderId\":" + Quote(material.pixelShaderId);
        out += ",\"properties\":";
        AppendProperties(out, material.properties, "      ");
        out += ",\"textures\":[";
        for (std::size_t t = 0; t < material.textures.size(); ++t) {
            if (t) out += ",";
            out += "{\"slot\":" + Quote(material.textures[t].slot) + ",\"assetId\":" + Quote(material.textures[t].asset.id) + "}";
        }
        out += "]}";
        out += i + 1 == runtime.materials.size() ? "\n" : ",\n";
    }
    out += "  ],\n";

    out += "  \"inputs\":[";
    for (std::size_t i = 0; i < runtime.inputs.size(); ++i) {
        const auto& input = runtime.inputs[i];
        if (i) out += ",";
        out += "{\"id\":" + Quote(input.id) + ",\"type\":" + Quote8(PropertyTypeKey(input.type)) + ",\"default\":" + SerializePropertyValue(input.defaultValue) + "}";
    }
    out += "],\n";

    out += "  \"bindings\":[";
    if (!runtime.bindings.empty()) out += "\n";
    for (std::size_t i = 0; i < runtime.bindings.size(); ++i) {
        const auto& binding = runtime.bindings[i];
        out += "    {\"id\":" + Quote(binding.id) + ",\"sourceKind\":" + Quote8(binding.sourceKind == BindingSourceKind::Parameter ? "parameter" : "input") +
               ",\"sourceId\":" + Quote(binding.sourceId) + ",\"target\":{\"componentId\":" + Quote(binding.target.componentId) +
               ",\"propertyName\":" + Quote(binding.target.propertyName) + "},\"scale\":" + Number(binding.scale) + ",\"offset\":" + Number(binding.offset) + "}";
        out += i + 1 == runtime.bindings.size() ? "\n" : ",\n";
    }
    out += "  ],\n";

    out += "  \"postProcesses\":[";
    if (!runtime.postProcesses.empty()) out += "\n";
    for (std::size_t i = 0; i < runtime.postProcesses.size(); ++i) {
        const auto& effect = runtime.postProcesses[i];
        out += "    {\"id\":" + Quote(effect.id) + ",\"effect\":" + Quote8(PostProcessEffectKey(effect.effect)) +
               ",\"enabled\":" + std::string(effect.enabled ? "true" : "false") +
               ",\"amount\":" + Number(effect.amount) + ",\"radius\":" + Number(effect.radius) +
               ",\"softness\":" + Number(effect.softness) + "}";
        out += i + 1 == runtime.postProcesses.size() ? "\n" : ",\n";
    }
    out += "  ]\n}\n";

    *sceneJsonUtf8 = std::move(out);
    if (error) error->clear();
    return true;
}

bool MiaoSceneSerializer::SerializeParameters(
    const SceneRuntimeDefinition& runtime,
    std::string* parameterJsonUtf8,
    std::wstring* error) {
    if (!parameterJsonUtf8) return Fail(error, L"Serialized parameter output is null.");
    std::wstring validateError;
    if (!MiaoSceneRuntimeModel::Validate(runtime, &validateError))
        return Fail(error, L"Cannot serialize invalid runtime: " + validateError);
    std::string out = "{\n  \"schema\":1,\n  \"parameters\":[";
    if (!runtime.parameters.empty()) out += "\n";
    for (std::size_t i = 0; i < runtime.parameters.size(); ++i) {
        const auto& parameter = runtime.parameters[i];
        out += "    {\"id\":" + Quote(parameter.id) + ",\"type\":" + Quote8(PropertyTypeKey(parameter.type)) +
               ",\"default\":" + SerializePropertyValue(parameter.defaultValue) + "}";
        out += i + 1 == runtime.parameters.size() ? "\n" : ",\n";
    }
    out += "  ]\n}\n";
    *parameterJsonUtf8 = std::move(out);
    if (error) error->clear();
    return true;
}

bool MiaoSceneSerializer::SelfTest() {
    constexpr std::string_view sceneJson = R"json({
      "schema":1,
      "id":"scene://serializer-self-test",
      "kind":"wallpaper",
      "profile":"wallpaper",
      "rootNodeId":"node://root",
      "nodes":[
        {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[
          {"id":"component://root/transform","kind":"transform","properties":[
            {"name":"opacity","type":"float","default":1.0}
          ]}
        ]}
      ],
      "assets":[],
      "shaders":[],
      "materials":[
        {"id":"material://background","model":"builtin","builtinName":"solidColor","properties":[
          {"name":"color","type":"color","default":[0.1,0.2,0.3,1.0]}
        ],"textures":[]}
      ],
      "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
      "bindings":[{"id":"binding://opacity","sourceKind":"parameter","sourceId":"param://opacity",
        "target":{"componentId":"component://root/transform","propertyName":"opacity"},"scale":1.0,"offset":0.0}],
      "postProcesses":[
        {"id":"postfx://vignette","effect":"vignette","enabled":true,"amount":0.8,"radius":0.72,"softness":0.22},
        {"id":"postfx://noise-disabled","effect":"noise","enabled":false,"amount":0.15}
      ]
    })json";
    constexpr std::string_view parametersJson = R"json({
      "schema":1,
      "parameters":[{"id":"param://opacity","type":"float","default":0.85,"min":0.0,"max":1.0,"step":0.01}]
    })json";

    SceneRuntimeDefinition first;
    std::wstring error;
    if (!Deserialize(sceneJson, parametersJson, &first, &error)) return false;
    if (first.scene.id != L"scene://serializer-self-test" || first.scene.nodes.size() != 1 || first.materials.size() != 1 ||
        first.parameters.size() != 1 || first.postProcesses.size() != 2)
        return false;
    if (first.postProcesses[0].effect != PostProcessEffectKind::Vignette ||
        first.postProcesses[1].effect != PostProcessEffectKind::Noise || first.postProcesses[1].enabled)
        return false;

    std::string serializedScene;
    std::string serializedParameters;
    if (!SerializeScene(first, &serializedScene, &error) || !SerializeParameters(first, &serializedParameters, &error)) return false;
    SceneRuntimeDefinition second;
    if (!Deserialize(serializedScene, serializedParameters, &second, &error)) return false;
    if (!EquivalentRuntime(first, second)) return false;
    if (second.postProcesses[0].id != L"postfx://vignette" || second.postProcesses[0].amount != first.postProcesses[0].amount)
        return false;

    SceneRuntimeDefinition invalid;
    if (Deserialize("{\"schema\":1}", "", &invalid, &error)) return false;
    return true;
}

} // namespace miaodesk::content
