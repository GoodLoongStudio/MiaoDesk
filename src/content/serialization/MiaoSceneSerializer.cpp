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
#include <utility>
#include <vector>

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

bool DecodeUtf8(std::string_view text, std::size_t* position, std::uint32_t* cp) {
    if (!position || !cp || *position >= text.size()) return false;
    const auto first = static_cast<unsigned char>(text[*position]);
    if (first < 0x80) {
        *cp = first;
        ++*position;
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
    if (*position + static_cast<std::size_t>(count) > text.size()) return false;
    for (int i = 1; i < count; ++i) {
        const auto ch = static_cast<unsigned char>(text[*position + static_cast<std::size_t>(i)]);
        if ((ch & 0xC0) != 0x80) return false;
        value = (value << 6) | (ch & 0x3F);
    }
    if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
        (count == 4 && value < 0x10000) || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) return false;
    *position += static_cast<std::size_t>(count);
    *cp = value;
    return true;
}

bool Utf8ToWide(std::string_view text, std::wstring* output) {
    if (!output) return false;
    output->clear();
    std::size_t position = 0;
    while (position < text.size()) {
        std::uint32_t cp = 0;
        if (!DecodeUtf8(text, &position, &cp)) return false;
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
    explicit JsonParser(std::string_view text) : text_(text) {}

    bool Parse(JsonValue* output, std::wstring* error) {
        if (!output) return Fail(error, L"JSON output is null.");
        Skip();
        if (!ParseValue(output)) return Fail(error, Message());
        Skip();
        if (position_ != text_.size()) return Fail(error, Message(L"Unexpected trailing JSON content"));
        return true;
    }

private:
    void Skip() {
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++position_;
        }
    }

    std::wstring Message(std::wstring_view text = L"Invalid JSON") const {
        return std::wstring(text) + L" at byte " + std::to_wstring(position_) + L".";
    }

    bool ParseValue(JsonValue* value) {
        Skip();
        if (position_ >= text_.size()) return false;
        const char ch = text_[position_];
        if (ch == '{') return ParseObject(value);
        if (ch == '[') return ParseArray(value);
        if (ch == '"') {
            value->type = JsonValue::Type::String;
            return ParseString(&value->string);
        }
        if (ch == 't' && text_.substr(position_, 4) == "true") {
            position_ += 4;
            value->type = JsonValue::Type::Bool;
            value->boolean = true;
            return true;
        }
        if (ch == 'f' && text_.substr(position_, 5) == "false") {
            position_ += 5;
            value->type = JsonValue::Type::Bool;
            value->boolean = false;
            return true;
        }
        if (ch == 'n' && text_.substr(position_, 4) == "null") {
            position_ += 4;
            value->type = JsonValue::Type::Null;
            return true;
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) return ParseNumber(value);
        return false;
    }

    bool ParseString(std::string* output) {
        if (!output || position_ >= text_.size() || text_[position_] != '"') return false;
        ++position_;
        output->clear();
        while (position_ < text_.size()) {
            const char ch = text_[position_++];
            if (ch == '"') return true;
            if (static_cast<unsigned char>(ch) < 0x20) return false;
            if (ch != '\\') {
                output->push_back(ch);
                continue;
            }
            if (position_ >= text_.size()) return false;
            const char escaped = text_[position_++];
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
                if (position_ + 4 > text_.size()) return false;
                const auto first = ParseHex4(text_.substr(position_, 4));
                if (!first) return false;
                position_ += 4;
                std::uint32_t cp = *first;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (position_ + 6 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u') return false;
                    position_ += 2;
                    const auto second = ParseHex4(text_.substr(position_, 4));
                    if (!second || *second < 0xDC00 || *second > 0xDFFF) return false;
                    position_ += 4;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (*second - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                }
                AppendUtf8(*output, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool ParseNumber(JsonValue* value) {
        const std::size_t begin = position_;
        if (text_[position_] == '-') ++position_;
        if (position_ >= text_.size()) return false;
        if (text_[position_] == '0') {
            ++position_;
        } else {
            if (text_[position_] < '1' || text_[position_] > '9') return false;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') return false;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') return false;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        const auto token = text_.substr(begin, position_ - begin);
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
        Skip();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return true;
        }
        while (position_ < text_.size()) {
            JsonValue item;
            if (!ParseValue(&item)) return false;
            value->array.push_back(std::move(item));
            Skip();
            if (position_ >= text_.size()) return false;
            if (text_[position_] == ']') {
                ++position_;
                return true;
            }
            if (text_[position_] != ',') return false;
            ++position_;
        }
        return false;
    }

    bool ParseObject(JsonValue* value) {
        ++position_;
        value->type = JsonValue::Type::Object;
        value->object.clear();
        Skip();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            return true;
        }
        while (position_ < text_.size()) {
            Skip();
            std::string key;
            if (!ParseString(&key)) return false;
            Skip();
            if (position_ >= text_.size() || text_[position_] != ':') return false;
            ++position_;
            JsonValue item;
            if (!ParseValue(&item)) return false;
            if (!value->object.emplace(std::move(key), std::move(item)).second) return false;
            Skip();
            if (position_ >= text_.size()) return false;
            if (text_[position_] == '}') {
                ++position_;
                return true;
            }
            if (text_[position_] != ',') return false;
            ++position_;
        }
        return false;
    }

    std::string_view text_;
    std::size_t position_{};
};

bool RequireObject(const JsonValue& value, std::wstring_view label, std::wstring* error) {
    return value.type == JsonValue::Type::Object || Fail(error, std::wstring(label) + L" must be an object.");
}

bool RequireArray(const JsonValue* value, std::wstring_view label, std::wstring* error) {
    if (!value) return Fail(error, std::wstring(label) + L" is required.");
    return value->type == JsonValue::Type::Array || Fail(error, std::wstring(label) + L" must be an array.");
}

std::wstring FieldName(std::string_view key) {
    return std::wstring(key.begin(), key.end());
}

bool ReadString(const JsonValue& object, std::string_view key, std::wstring* output, bool required, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (required) return Fail(error, L"Missing string field: " + FieldName(key));
        if (output) output->clear();
        return true;
    }
    if (value->type != JsonValue::Type::String) return Fail(error, L"Field must be string: " + FieldName(key));
    if (!Utf8ToWide(value->string, output)) return Fail(error, L"Field contains invalid UTF-8: " + FieldName(key));
    return true;
}

bool ReadString8(const JsonValue& object, std::string_view key, std::string* output, bool required, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (required) return Fail(error, L"Missing string field: " + FieldName(key));
        if (output) output->clear();
        return true;
    }
    if (value->type != JsonValue::Type::String) return Fail(error, L"Field must be string: " + FieldName(key));
    std::wstring check;
    if (!Utf8ToWide(value->string, &check)) return Fail(error, L"Field contains invalid UTF-8: " + FieldName(key));
    if (output) *output = value->string;
    return true;
}

bool ReadBool(const JsonValue& object, std::string_view key, bool fallback, bool* output, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Bool) return Fail(error, L"Field must be boolean: " + FieldName(key));
    if (output) *output = value->boolean;
    return true;
}

bool ReadNumber(const JsonValue& object, std::string_view key, double fallback, double* output, bool required, std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (required) return Fail(error, L"Missing numeric field: " + FieldName(key));
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Number || !std::isfinite(value->number))
        return Fail(error, L"Field must be finite number: " + FieldName(key));
    if (output) *output = value->number;
    return true;
}

bool ReadSchema(const JsonValue& object, std::wstring_view label, std::wstring* error) {
    double schema = 0.0;
    if (!ReadNumber(object, "schema", 0.0, &schema, true, error)) return false;
    return schema == 1.0 || Fail(error, std::wstring(label) + L" schema version must be 1.");
}

bool ParseContentKind(std::string_view value, ContentKind* kind) noexcept {
    if (!kind) return false;
    if (value == "wallpaper") *kind = ContentKind::Wallpaper;
    else if (value == "widget") *kind = ContentKind::Widget;
    else return false;
    return true;
}

bool ParseProfile(std::string_view value, RuntimeProfile* profile) noexcept {
    if (!profile) return false;
    if (value == "wallpaper") *profile = RuntimeProfile::Wallpaper;
    else if (value == "widget") *profile = RuntimeProfile::Widget;
    else return false;
    return true;
}

bool ParseComponentKind(std::string_view value, ComponentKind* kind) noexcept {
    if (!kind) return false;
    static constexpr std::pair<std::string_view, ComponentKind> values[] = {
        {"transform", ComponentKind::Transform}, {"spriteRenderer", ComponentKind::SpriteRenderer},
        {"textRenderer", ComponentKind::TextRenderer}, {"videoRenderer", ComponentKind::VideoRenderer},
        {"material", ComponentKind::Material}, {"particleSystem", ComponentKind::ParticleSystem},
        {"animator", ComponentKind::Animator}, {"script", ComponentKind::Script},
        {"inputBinding", ComponentKind::InputBinding}, {"custom", ComponentKind::Custom},
    };
    for (const auto& [key, item] : values) if (value == key) { *kind = item; return true; }
    return false;
}

bool ParseAssetType(std::string_view value, AssetType* type) noexcept {
    if (!type) return false;
    static constexpr std::pair<std::string_view, AssetType> values[] = {
        {"image", AssetType::Image}, {"video", AssetType::Video}, {"audio", AssetType::Audio},
        {"font", AssetType::Font}, {"shader", AssetType::Shader}, {"script", AssetType::Script},
        {"mesh", AssetType::Mesh}, {"binary", AssetType::Binary},
    };
    for (const auto& [key, item] : values) if (value == key) { *type = item; return true; }
    return false;
}

bool ParseShaderStage(std::string_view value, ShaderStage* stage) noexcept {
    if (!stage) return false;
    if (value == "vertex") *stage = ShaderStage::Vertex;
    else if (value == "pixel") *stage = ShaderStage::Pixel;
    else if (value == "compute") *stage = ShaderStage::Compute;
    else return false;
    return true;
}

bool ParsePropertyType(std::string_view value, PropertyType* type) noexcept {
    if (!type) return false;
    static constexpr std::pair<std::string_view, PropertyType> values[] = {
        {"bool", PropertyType::Bool}, {"int", PropertyType::Int}, {"float", PropertyType::Float},
        {"string", PropertyType::String}, {"vec2", PropertyType::Vec2}, {"vec3", PropertyType::Vec3},
        {"vec4", PropertyType::Vec4}, {"color", PropertyType::Color}, {"assetReference", PropertyType::AssetReference},
    };
    for (const auto& [key, item] : values) if (value == key) { *type = item; return true; }
    return false;
}

bool ParseMaterialModel(std::string_view value, MaterialModel* model) noexcept {
    if (!model) return false;
    if (value == "builtin") *model = MaterialModel::Builtin;
    else if (value == "programmable") *model = MaterialModel::Programmable;
    else return false;
    return true;
}

bool ParseBindingSource(std::string_view value, BindingSourceKind* kind) noexcept {
    if (!kind) return false;
    if (value == "parameter") *kind = BindingSourceKind::Parameter;
    else if (value == "input") *kind = BindingSourceKind::Input;
    else return false;
    return true;
}

bool ParsePostEffect(std::string_view value, PostProcessEffectKind* effect) noexcept {
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

bool ParseAnimationLoop(std::string_view value, AnimationLoopMode* mode) noexcept {
    if (!mode) return false;
    if (value == "once") *mode = AnimationLoopMode::Once;
    else if (value == "loop") *mode = AnimationLoopMode::Loop;
    else if (value == "pingPong") *mode = AnimationLoopMode::PingPong;
    else return false;
    return true;
}

bool ParseAnimationEasing(std::string_view value, AnimationEasing* easing) noexcept {
    if (!easing) return false;
    if (value == "linear") *easing = AnimationEasing::Linear;
    else if (value == "easeIn") *easing = AnimationEasing::EaseIn;
    else if (value == "easeOut") *easing = AnimationEasing::EaseOut;
    else if (value == "easeInOut") *easing = AnimationEasing::EaseInOut;
    else return false;
    return true;
}

bool ParseAnimationTrigger(std::string_view value, AnimationTriggerMode* mode) noexcept {
    if (!mode) return false;
    if (value == "timeline") *mode = AnimationTriggerMode::Timeline;
    else if (value == "inputChange") *mode = AnimationTriggerMode::InputChange;
    else if (value == "inputRisingEdge") *mode = AnimationTriggerMode::InputRisingEdge;
    else return false;
    return true;
}

const char* ContentKindKey(ContentKind kind) noexcept { return kind == ContentKind::Widget ? "widget" : "wallpaper"; }
const char* ProfileKey(RuntimeProfile profile) noexcept { return profile == RuntimeProfile::Widget ? "widget" : "wallpaper"; }

const char* ComponentKindKey(ComponentKind kind) noexcept {
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

const char* AssetTypeKey(AssetType type) noexcept {
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

const char* ShaderStageKey(ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::Vertex: return "vertex";
    case ShaderStage::Pixel: return "pixel";
    case ShaderStage::Compute: return "compute";
    }
    return "pixel";
}

const char* PropertyTypeKey(PropertyType type) noexcept {
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

const char* PostEffectKey(PostProcessEffectKind effect) noexcept {
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

const char* AnimationLoopKey(AnimationLoopMode mode) noexcept {
    switch (mode) {
    case AnimationLoopMode::Once: return "once";
    case AnimationLoopMode::Loop: return "loop";
    case AnimationLoopMode::PingPong: return "pingPong";
    }
    return "loop";
}

const char* AnimationEasingKey(AnimationEasing easing) noexcept {
    switch (easing) {
    case AnimationEasing::Linear: return "linear";
    case AnimationEasing::EaseIn: return "easeIn";
    case AnimationEasing::EaseOut: return "easeOut";
    case AnimationEasing::EaseInOut: return "easeInOut";
    }
    return "linear";
}

const char* AnimationTriggerKey(AnimationTriggerMode mode) noexcept {
    switch (mode) {
    case AnimationTriggerMode::Timeline: return "timeline";
    case AnimationTriggerMode::InputChange: return "inputChange";
    case AnimationTriggerMode::InputRisingEdge: return "inputRisingEdge";
    }
    return "timeline";
}

bool ReadVector(const JsonValue& value, std::size_t count, double* output, std::wstring* error) {
    if (value.type != JsonValue::Type::Array || value.array.size() != count)
        return Fail(error, L"Vector/color value has wrong element count.");
    for (std::size_t i = 0; i < count; ++i) {
        if (value.array[i].type != JsonValue::Type::Number || !std::isfinite(value.array[i].number))
            return Fail(error, L"Vector/color value must contain finite numbers.");
        output[i] = value.array[i].number;
    }
    return true;
}

bool ParsePropertyValue(PropertyType type, const JsonValue& value, PropertyValue* output, std::wstring* error) {
    if (!output) return Fail(error, L"Property value output is null.");
    switch (type) {
    case PropertyType::Bool:
        if (value.type != JsonValue::Type::Bool) return Fail(error, L"Boolean property value is invalid.");
        *output = value.boolean;
        return true;
    case PropertyType::Int:
        if (value.type != JsonValue::Type::Number || std::floor(value.number) != value.number ||
            value.number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            value.number > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return Fail(error, L"Integer property value is invalid.");
        *output = static_cast<std::int64_t>(value.number);
        return true;
    case PropertyType::Float:
        if (value.type != JsonValue::Type::Number || !std::isfinite(value.number)) return Fail(error, L"Float property value is invalid.");
        *output = value.number;
        return true;
    case PropertyType::String: {
        if (value.type != JsonValue::Type::String) return Fail(error, L"String property value is invalid.");
        std::wstring text;
        if (!Utf8ToWide(value.string, &text)) return Fail(error, L"String property value contains invalid UTF-8.");
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
        if (value.type != JsonValue::Type::String) return Fail(error, L"AssetReference value must be a stable asset id string.");
        std::wstring id;
        if (!Utf8ToWide(value.string, &id)) return Fail(error, L"AssetReference contains invalid UTF-8.");
        *output = AssetReference{std::move(id)};
        return true;
    }
    }
    return Fail(error, L"Unsupported property type.");
}

bool ParseProperty(const JsonValue& object, PropertyDefinition* property, std::wstring* error) {
    if (!property || !RequireObject(object, L"Property", error)) return false;
    if (!ReadString(object, "name", &property->name, true, error)) return false;
    std::string type;
    if (!ReadString8(object, "type", &type, true, error) || !ParsePropertyType(type, &property->type))
        return Fail(error, L"Property type is invalid: " + property->name);
    const auto* value = object.Find("default");
    if (!value) return Fail(error, L"Property default is required: " + property->name);
    return ParsePropertyValue(property->type, *value, &property->defaultValue, error);
}

bool ParseProperties(const JsonValue& object, std::vector<PropertyDefinition>* properties, std::wstring* error) {
    if (!properties) return Fail(error, L"Property list output is null.");
    const auto* array = object.Find("properties");
    if (!array) {
        properties->clear();
        return true;
    }
    if (!RequireArray(array, L"properties", error)) return false;
    properties->clear();
    for (const auto& value : array->array) {
        PropertyDefinition property;
        if (!ParseProperty(value, &property, error)) return false;
        properties->push_back(std::move(property));
    }
    return true;
}

bool ParseSceneRoot(const JsonValue& root, SceneRuntimeDefinition* runtime, std::wstring* error) {
    if (!runtime || !RequireObject(root, L"scene.json root", error) || !ReadSchema(root, L"scene.json", error)) return false;

    if (!ReadString(root, "id", &runtime->scene.id, true, error)) return false;
    std::string kind;
    if (!ReadString8(root, "kind", &kind, true, error) || !ParseContentKind(kind, &runtime->scene.kind))
        return Fail(error, L"scene.json kind must be wallpaper or widget.");
    std::string profile;
    if (!ReadString8(root, "profile", &profile, true, error) || !ParseProfile(profile, &runtime->profile))
        return Fail(error, L"scene.json profile must be wallpaper or widget.");
    if (!ReadString(root, "rootNodeId", &runtime->scene.rootNodeId, true, error)) return false;
    runtime->scene.schemaVersion = 1;

    const auto* nodes = root.Find("nodes");
    if (!RequireArray(nodes, L"nodes", error)) return false;
    runtime->scene.nodes.clear();
    for (const auto& nodeValue : nodes->array) {
        if (!RequireObject(nodeValue, L"Node", error)) return false;
        SceneNodeDefinition node;
        if (!ReadString(nodeValue, "id", &node.id, true, error) ||
            !ReadString(nodeValue, "name", &node.name, false, error) ||
            !ReadString(nodeValue, "parentId", &node.parentId, false, error) ||
            !ReadBool(nodeValue, "enabled", true, &node.enabled, error)) return false;
        const auto* components = nodeValue.Find("components");
        if (!RequireArray(components, L"components", error)) return false;
        for (const auto& componentValue : components->array) {
            if (!RequireObject(componentValue, L"Component", error)) return false;
            SceneComponentDefinition component;
            if (!ReadString(componentValue, "id", &component.id, true, error)) return false;
            std::string componentKind;
            if (!ReadString8(componentValue, "kind", &componentKind, true, error) ||
                !ParseComponentKind(componentKind, &component.kind))
                return Fail(error, L"Component kind is invalid: " + component.id);
            if (!ParseProperties(componentValue, &component.properties, error)) return false;
            node.components.push_back(std::move(component));
        }
        runtime->scene.nodes.push_back(std::move(node));
    }

    const auto* assets = root.Find("assets");
    if (!RequireArray(assets, L"assets", error)) return false;
    runtime->scene.assets.clear();
    for (const auto& assetValue : assets->array) {
        if (!RequireObject(assetValue, L"Asset", error)) return false;
        AssetDefinition asset;
        if (!ReadString(assetValue, "id", &asset.id, true, error)) return false;
        std::string type;
        if (!ReadString8(assetValue, "type", &type, true, error) || !ParseAssetType(type, &asset.type))
            return Fail(error, L"Asset type is invalid: " + asset.id);
        if (!ReadString(assetValue, "source", &asset.source, true, error)) return false;
        runtime->scene.assets.push_back(std::move(asset));
    }

    const auto* shaders = root.Find("shaders");
    if (!RequireArray(shaders, L"shaders", error)) return false;
    runtime->scene.shaders.clear();
    for (const auto& shaderValue : shaders->array) {
        if (!RequireObject(shaderValue, L"Shader", error)) return false;
        ShaderDefinition shader;
        if (!ReadString(shaderValue, "id", &shader.id, true, error)) return false;
        std::string stage;
        if (!ReadString8(shaderValue, "stage", &stage, true, error) || !ParseShaderStage(stage, &shader.stage))
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
    for (const auto& materialValue : materials->array) {
        if (!RequireObject(materialValue, L"Material", error)) return false;
        MaterialDefinition material;
        if (!ReadString(materialValue, "id", &material.id, true, error)) return false;
        std::string model;
        if (!ReadString8(materialValue, "model", &model, true, error) || !ParseMaterialModel(model, &material.model))
            return Fail(error, L"Material model is invalid: " + material.id);
        if (!ReadString(materialValue, "builtinName", &material.builtinName, false, error) ||
            !ReadString(materialValue, "vertexShaderId", &material.vertexShaderId, false, error) ||
            !ReadString(materialValue, "pixelShaderId", &material.pixelShaderId, false, error) ||
            !ParseProperties(materialValue, &material.properties, error)) return false;
        const auto* textures = materialValue.Find("textures");
        if (!RequireArray(textures, L"material textures", error)) return false;
        for (const auto& textureValue : textures->array) {
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
    for (const auto& inputValue : inputs->array) {
        if (!RequireObject(inputValue, L"Input", error)) return false;
        InputChannelDefinition input;
        if (!ReadString(inputValue, "id", &input.id, true, error)) return false;
        std::string type;
        if (!ReadString8(inputValue, "type", &type, true, error) || !ParsePropertyType(type, &input.type))
            return Fail(error, L"Input type is invalid: " + input.id);
        const auto* defaultValue = inputValue.Find("default");
        if (!defaultValue || !ParsePropertyValue(input.type, *defaultValue, &input.defaultValue, error)) return false;
        runtime->inputs.push_back(std::move(input));
    }

    const auto* bindings = root.Find("bindings");
    if (!RequireArray(bindings, L"bindings", error)) return false;
    runtime->bindings.clear();
    for (const auto& bindingValue : bindings->array) {
        if (!RequireObject(bindingValue, L"Binding", error)) return false;
        PropertyBindingDefinition binding;
        if (!ReadString(bindingValue, "id", &binding.id, true, error)) return false;
        std::string sourceKind;
        if (!ReadString8(bindingValue, "sourceKind", &sourceKind, true, error) ||
            !ParseBindingSource(sourceKind, &binding.sourceKind))
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

    runtime->animations.clear();
    if (const auto* animations = root.Find("animations")) {
        if (!RequireArray(animations, L"animations", error)) return false;
        for (const auto& animationValue : animations->array) {
            if (!RequireObject(animationValue, L"Animation", error)) return false;
            AnimationTrackDefinition animation;
            if (!ReadString(animationValue, "id", &animation.id, true, error) ||
                !ReadBool(animationValue, "enabled", true, &animation.enabled, error) ||
                !ReadNumber(animationValue, "duration", 1.0, &animation.durationSeconds, true, error)) return false;
            std::string loop;
            if (!ReadString8(animationValue, "loop", &loop, false, error)) return false;
            if (loop.empty()) loop = "loop";
            if (!ParseAnimationLoop(loop, &animation.loopMode))
                return Fail(error, L"Animation loop mode is invalid: " + animation.id);

            if (const auto* trigger = animationValue.Find("trigger")) {
                if (!RequireObject(*trigger, L"Animation trigger", error)) return false;
                std::string triggerMode;
                if (!ReadString8(*trigger, "mode", &triggerMode, true, error) ||
                    !ParseAnimationTrigger(triggerMode, &animation.triggerMode))
                    return Fail(error, L"Animation trigger mode is invalid: " + animation.id);
                if (!ReadString(*trigger, "inputId", &animation.triggerInputId, false, error)) return false;
            }

            const auto* target = animationValue.Find("target");
            if (!target || !RequireObject(*target, L"Animation target", error) ||
                !ReadString(*target, "componentId", &animation.target.componentId, true, error) ||
                !ReadString(*target, "propertyName", &animation.target.propertyName, true, error)) return false;
            const auto* targetProperty = MiaoSceneRuntimeModel::FindProperty(runtime->scene, animation.target);
            if (!targetProperty) return Fail(error, L"Animation target does not resolve: " + animation.id);

            const auto* keyframes = animationValue.Find("keyframes");
            if (!RequireArray(keyframes, L"animation keyframes", error)) return false;
            for (const auto& keyframeValue : keyframes->array) {
                if (!RequireObject(keyframeValue, L"Animation keyframe", error)) return false;
                AnimationKeyframeDefinition keyframe;
                if (!ReadNumber(keyframeValue, "time", 0.0, &keyframe.timeSeconds, true, error)) return false;
                const auto* value = keyframeValue.Find("value");
                if (!value || !ParsePropertyValue(targetProperty->type, *value, &keyframe.value, error)) return false;
                std::string easing;
                if (!ReadString8(keyframeValue, "easing", &easing, false, error)) return false;
                if (easing.empty()) easing = "linear";
                if (!ParseAnimationEasing(easing, &keyframe.easing))
                    return Fail(error, L"Animation easing is invalid: " + animation.id);
                animation.keyframes.push_back(std::move(keyframe));
            }
            runtime->animations.push_back(std::move(animation));
        }
    }

    runtime->postProcesses.clear();
    if (const auto* postProcesses = root.Find("postProcesses")) {
        if (!RequireArray(postProcesses, L"postProcesses", error)) return false;
        for (const auto& effectValue : postProcesses->array) {
            if (!RequireObject(effectValue, L"Post-process", error)) return false;
            PostProcessDefinition effect;
            if (!ReadString(effectValue, "id", &effect.id, true, error)) return false;
            std::string effectName;
            if (!ReadString8(effectValue, "effect", &effectName, true, error) || !ParsePostEffect(effectName, &effect.effect))
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

bool ParseParameters(std::string_view source, SceneRuntimeDefinition* runtime, std::wstring* error) {
    if (!runtime) return Fail(error, L"Parameter runtime output is null.");
    runtime->parameters.clear();
    if (source.empty()) return true;
    JsonValue root;
    JsonParser parser(source);
    if (!parser.Parse(&root, error) || !RequireObject(root, L"parameters.json root", error) ||
        !ReadSchema(root, L"parameters.json", error)) return false;
    const auto* parameters = root.Find("parameters");
    if (!RequireArray(parameters, L"parameters", error)) return false;
    for (const auto& parameterValue : parameters->array) {
        if (!RequireObject(parameterValue, L"Parameter", error)) return false;
        ParameterDefinition parameter;
        if (!ReadString(parameterValue, "id", &parameter.id, true, error)) return false;
        std::string type;
        if (!ReadString8(parameterValue, "type", &type, true, error) || !ParsePropertyType(type, &parameter.type))
            return Fail(error, L"Parameter type is invalid: " + parameter.id);
        const auto* defaultValue = parameterValue.Find("default");
        if (!defaultValue || !ParsePropertyValue(parameter.type, *defaultValue, &parameter.defaultValue, error)) return false;
        runtime->parameters.push_back(std::move(parameter));
    }
    return true;
}

std::string EscapeJson(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 8);
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20) {
                output += "\\u00";
                output.push_back(hex[(ch >> 4) & 0xF]);
                output.push_back(hex[ch & 0xF]);
            } else {
                output.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return output;
}

std::string Quote(std::wstring_view value) { return "\"" + EscapeJson(WideToUtf8(value)) + "\""; }
std::string Quote8(std::string_view value) { return "\"" + EscapeJson(value) + "\""; }

std::string Number(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string SerializeValue(const PropertyValue& value) {
    if (const auto* item = std::get_if<bool>(&value)) return *item ? "true" : "false";
    if (const auto* item = std::get_if<std::int64_t>(&value)) return std::to_string(*item);
    if (const auto* item = std::get_if<double>(&value)) return Number(*item);
    if (const auto* item = std::get_if<std::wstring>(&value)) return Quote(*item);
    if (const auto* item = std::get_if<Vec2>(&value)) return "[" + Number(item->x) + "," + Number(item->y) + "]";
    if (const auto* item = std::get_if<Vec3>(&value)) return "[" + Number(item->x) + "," + Number(item->y) + "," + Number(item->z) + "]";
    if (const auto* item = std::get_if<Vec4>(&value))
        return "[" + Number(item->x) + "," + Number(item->y) + "," + Number(item->z) + "," + Number(item->w) + "]";
    if (const auto* item = std::get_if<Color4>(&value))
        return "[" + Number(item->r) + "," + Number(item->g) + "," + Number(item->b) + "," + Number(item->a) + "]";
    if (const auto* item = std::get_if<AssetReference>(&value)) return Quote(item->id);
    return "null";
}

std::string SerializeProperties(const std::vector<PropertyDefinition>& properties) {
    std::string out = "[";
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (i) out += ",";
        const auto& property = properties[i];
        out += "{\"name\":" + Quote(property.name) + ",\"type\":" + Quote8(PropertyTypeKey(property.type)) +
               ",\"default\":" + SerializeValue(property.defaultValue) + "}";
    }
    out += "]";
    return out;
}

bool EquivalentRuntime(const SceneRuntimeDefinition& a, const SceneRuntimeDefinition& b) noexcept {
    return a.scene.id == b.scene.id && a.scene.kind == b.scene.kind && a.profile == b.profile &&
           a.scene.rootNodeId == b.scene.rootNodeId && a.scene.nodes.size() == b.scene.nodes.size() &&
           a.scene.assets.size() == b.scene.assets.size() && a.scene.shaders.size() == b.scene.shaders.size() &&
           a.materials.size() == b.materials.size() && a.parameters.size() == b.parameters.size() &&
           a.inputs.size() == b.inputs.size() && a.bindings.size() == b.bindings.size() &&
           a.animations.size() == b.animations.size() && a.postProcesses.size() == b.postProcesses.size();
}

} // namespace

bool MiaoSceneSerializer::Deserialize(
    std::string_view sceneJsonUtf8,
    std::string_view parameterJsonUtf8,
    SceneRuntimeDefinition* runtime,
    std::wstring* error) {
    if (!runtime) return Fail(error, L"Scene runtime output is null.");
    if (sceneJsonUtf8.empty()) return Fail(error, L"scene.json source is empty.");

    JsonValue root;
    JsonParser parser(sceneJsonUtf8);
    SceneRuntimeDefinition parsed;
    if (!parser.Parse(&root, error) || !ParseSceneRoot(root, &parsed, error)) return false;
    if (!ParseParameters(parameterJsonUtf8, &parsed, error)) return false;

    std::wstring validationError;
    if (!MiaoSceneRuntimeModel::Validate(parsed, &validationError))
        return Fail(error, L"Deserialized scene runtime is invalid: " + validationError);
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
    std::wstring validationError;
    if (!MiaoSceneRuntimeModel::Validate(runtime, &validationError))
        return Fail(error, L"Cannot serialize invalid runtime: " + validationError);

    std::string out;
    out += "{\n  \"schema\":1,\n  \"id\":" + Quote(runtime.scene.id) +
           ",\n  \"kind\":" + Quote8(ContentKindKey(runtime.scene.kind)) +
           ",\n  \"profile\":" + Quote8(ProfileKey(runtime.profile)) +
           ",\n  \"rootNodeId\":" + Quote(runtime.scene.rootNodeId) + ",\n";

    out += "  \"nodes\":[";
    for (std::size_t i = 0; i < runtime.scene.nodes.size(); ++i) {
        if (i) out += ",";
        const auto& node = runtime.scene.nodes[i];
        out += "{\"id\":" + Quote(node.id) + ",\"name\":" + Quote(node.name) +
               ",\"parentId\":" + Quote(node.parentId) +
               ",\"enabled\":" + std::string(node.enabled ? "true" : "false") + ",\"components\":[";
        for (std::size_t c = 0; c < node.components.size(); ++c) {
            if (c) out += ",";
            const auto& component = node.components[c];
            out += "{\"id\":" + Quote(component.id) + ",\"kind\":" + Quote8(ComponentKindKey(component.kind)) +
                   ",\"properties\":" + SerializeProperties(component.properties) + "}";
        }
        out += "]}";
    }
    out += "],\n";

    out += "  \"assets\":[";
    for (std::size_t i = 0; i < runtime.scene.assets.size(); ++i) {
        if (i) out += ",";
        const auto& asset = runtime.scene.assets[i];
        out += "{\"id\":" + Quote(asset.id) + ",\"type\":" + Quote8(AssetTypeKey(asset.type)) +
               ",\"source\":" + Quote(asset.source) + "}";
    }
    out += "],\n";

    out += "  \"shaders\":[";
    for (std::size_t i = 0; i < runtime.scene.shaders.size(); ++i) {
        if (i) out += ",";
        const auto& shader = runtime.scene.shaders[i];
        out += "{\"id\":" + Quote(shader.id) + ",\"stage\":" + Quote8(ShaderStageKey(shader.stage)) +
               ",\"assetId\":" + Quote(shader.assetId) + ",\"entryPoint\":" + Quote8(shader.entryPoint) +
               ",\"userAuthored\":" + std::string(shader.userAuthored ? "true" : "false") + "}";
    }
    out += "],\n";

    out += "  \"materials\":[";
    for (std::size_t i = 0; i < runtime.materials.size(); ++i) {
        if (i) out += ",";
        const auto& material = runtime.materials[i];
        out += "{\"id\":" + Quote(material.id) + ",\"model\":" +
               Quote8(material.model == MaterialModel::Builtin ? "builtin" : "programmable");
        if (!material.builtinName.empty()) out += ",\"builtinName\":" + Quote(material.builtinName);
        if (!material.vertexShaderId.empty()) out += ",\"vertexShaderId\":" + Quote(material.vertexShaderId);
        if (!material.pixelShaderId.empty()) out += ",\"pixelShaderId\":" + Quote(material.pixelShaderId);
        out += ",\"properties\":" + SerializeProperties(material.properties) + ",\"textures\":[";
        for (std::size_t t = 0; t < material.textures.size(); ++t) {
            if (t) out += ",";
            out += "{\"slot\":" + Quote(material.textures[t].slot) +
                   ",\"assetId\":" + Quote(material.textures[t].asset.id) + "}";
        }
        out += "]}";
    }
    out += "],\n";

    out += "  \"inputs\":[";
    for (std::size_t i = 0; i < runtime.inputs.size(); ++i) {
        if (i) out += ",";
        const auto& input = runtime.inputs[i];
        out += "{\"id\":" + Quote(input.id) + ",\"type\":" + Quote8(PropertyTypeKey(input.type)) +
               ",\"default\":" + SerializeValue(input.defaultValue) + "}";
    }
    out += "],\n";

    out += "  \"bindings\":[";
    for (std::size_t i = 0; i < runtime.bindings.size(); ++i) {
        if (i) out += ",";
        const auto& binding = runtime.bindings[i];
        out += "{\"id\":" + Quote(binding.id) + ",\"sourceKind\":" +
               Quote8(binding.sourceKind == BindingSourceKind::Parameter ? "parameter" : "input") +
               ",\"sourceId\":" + Quote(binding.sourceId) +
               ",\"target\":{\"componentId\":" + Quote(binding.target.componentId) +
               ",\"propertyName\":" + Quote(binding.target.propertyName) +
               "},\"scale\":" + Number(binding.scale) + ",\"offset\":" + Number(binding.offset) + "}";
    }
    out += "],\n";

    out += "  \"animations\":[";
    for (std::size_t i = 0; i < runtime.animations.size(); ++i) {
        if (i) out += ",";
        const auto& animation = runtime.animations[i];
        out += "{\"id\":" + Quote(animation.id) +
               ",\"target\":{\"componentId\":" + Quote(animation.target.componentId) +
               ",\"propertyName\":" + Quote(animation.target.propertyName) +
               "},\"enabled\":" + std::string(animation.enabled ? "true" : "false") +
               ",\"loop\":" + Quote8(AnimationLoopKey(animation.loopMode)) +
               ",\"duration\":" + Number(animation.durationSeconds);
        if (animation.triggerMode != AnimationTriggerMode::Timeline) {
            out += ",\"trigger\":{\"mode\":" + Quote8(AnimationTriggerKey(animation.triggerMode)) +
                   ",\"inputId\":" + Quote(animation.triggerInputId) + "}";
        }
        out += ",\"keyframes\":[";
        for (std::size_t k = 0; k < animation.keyframes.size(); ++k) {
            if (k) out += ",";
            const auto& keyframe = animation.keyframes[k];
            out += "{\"time\":" + Number(keyframe.timeSeconds) +
                   ",\"value\":" + SerializeValue(keyframe.value) +
                   ",\"easing\":" + Quote8(AnimationEasingKey(keyframe.easing)) + "}";
        }
        out += "]}";
    }
    out += "],\n";

    out += "  \"postProcesses\":[";
    for (std::size_t i = 0; i < runtime.postProcesses.size(); ++i) {
        if (i) out += ",";
        const auto& effect = runtime.postProcesses[i];
        out += "{\"id\":" + Quote(effect.id) + ",\"effect\":" + Quote8(PostEffectKey(effect.effect)) +
               ",\"enabled\":" + std::string(effect.enabled ? "true" : "false") +
               ",\"amount\":" + Number(effect.amount) + ",\"radius\":" + Number(effect.radius) +
               ",\"softness\":" + Number(effect.softness) + "}";
    }
    out += "]\n}\n";

    *sceneJsonUtf8 = std::move(out);
    if (error) error->clear();
    return true;
}

bool MiaoSceneSerializer::SerializeParameters(
    const SceneRuntimeDefinition& runtime,
    std::string* parameterJsonUtf8,
    std::wstring* error) {
    if (!parameterJsonUtf8) return Fail(error, L"Serialized parameter output is null.");
    std::wstring validationError;
    if (!MiaoSceneRuntimeModel::Validate(runtime, &validationError))
        return Fail(error, L"Cannot serialize invalid runtime: " + validationError);
    std::string out = "{\n  \"schema\":1,\n  \"parameters\":[";
    for (std::size_t i = 0; i < runtime.parameters.size(); ++i) {
        if (i) out += ",";
        const auto& parameter = runtime.parameters[i];
        out += "{\"id\":" + Quote(parameter.id) + ",\"type\":" + Quote8(PropertyTypeKey(parameter.type)) +
               ",\"default\":" + SerializeValue(parameter.defaultValue) + "}";
    }
    out += "]\n}\n";
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
      "inputs":[
        {"id":"input://frame/time","type":"float","default":0.0},
        {"id":"input://event/pulse","type":"bool","default":false}
      ],
      "bindings":[{"id":"binding://opacity","sourceKind":"parameter","sourceId":"param://opacity",
        "target":{"componentId":"component://root/transform","propertyName":"opacity"},"scale":1.0,"offset":0.0}],
      "animations":[
        {
          "id":"animation://opacity-pulse",
          "target":{"componentId":"component://root/transform","propertyName":"opacity"},
          "enabled":true,"loop":"pingPong","duration":2.0,
          "keyframes":[
            {"time":0.0,"value":0.35,"easing":"easeInOut"},
            {"time":1.0,"value":1.0,"easing":"easeOut"},
            {"time":2.0,"value":0.35,"easing":"linear"}
          ]
        },
        {
          "id":"animation://event-pulse",
          "target":{"componentId":"component://root/transform","propertyName":"opacity"},
          "enabled":true,"loop":"once","duration":0.5,
          "trigger":{"mode":"inputRisingEdge","inputId":"input://event/pulse"},
          "keyframes":[
            {"time":0.0,"value":0.2,"easing":"easeOut"},
            {"time":0.5,"value":1.0,"easing":"linear"}
          ]
        }
      ],
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
    if (first.scene.id != L"scene://serializer-self-test" || first.scene.nodes.size() != 1 ||
        first.materials.size() != 1 || first.parameters.size() != 1 || first.inputs.size() != 2 ||
        first.animations.size() != 2 || first.postProcesses.size() != 2) return false;
    if (first.animations[0].loopMode != AnimationLoopMode::PingPong || first.animations[0].keyframes.size() != 3 ||
        first.animations[0].keyframes[0].easing != AnimationEasing::EaseInOut ||
        first.animations[0].keyframes[1].easing != AnimationEasing::EaseOut) return false;
    if (first.animations[0].triggerMode != AnimationTriggerMode::Timeline || !first.animations[0].triggerInputId.empty()) return false;
    if (first.animations[1].triggerMode != AnimationTriggerMode::InputRisingEdge ||
        first.animations[1].triggerInputId != L"input://event/pulse" ||
        first.animations[1].loopMode != AnimationLoopMode::Once) return false;
    if (!std::holds_alternative<double>(first.animations[0].keyframes[1].value) ||
        std::get<double>(first.animations[0].keyframes[1].value) != 1.0) return false;

    std::string serializedScene;
    std::string serializedParameters;
    if (!SerializeScene(first, &serializedScene, &error) ||
        !SerializeParameters(first, &serializedParameters, &error)) return false;
    if (serializedScene.find("\"trigger\":{\"mode\":\"inputRisingEdge\",\"inputId\":\"input://event/pulse\"}") == std::string::npos)
        return false;

    SceneRuntimeDefinition second;
    if (!Deserialize(serializedScene, serializedParameters, &second, &error)) return false;
    if (!EquivalentRuntime(first, second) || second.animations[0].id != L"animation://opacity-pulse" ||
        second.animations[0].durationSeconds != 2.0 || second.animations[0].keyframes.size() != 3 ||
        second.animations[0].keyframes[0].easing != AnimationEasing::EaseInOut) return false;
    if (second.animations[1].triggerMode != AnimationTriggerMode::InputRisingEdge ||
        second.animations[1].triggerInputId != L"input://event/pulse") return false;
    if (second.postProcesses[0].id != L"postfx://vignette" ||
        second.postProcesses[0].amount != first.postProcesses[0].amount) return false;

    SceneRuntimeDefinition invalidTrigger = first;
    invalidTrigger.animations[1].triggerInputId = L"input://missing";
    if (SerializeScene(invalidTrigger, &serializedScene, &error)) return false;

    SceneRuntimeDefinition invalid;
    if (Deserialize("{\"schema\":1}", "", &invalid, &error)) return false;
    return true;
}

} // namespace miaodesk::content
