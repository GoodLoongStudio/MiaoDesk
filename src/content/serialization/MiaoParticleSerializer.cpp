#include "miaodesk/MiaoParticleSerializer.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
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

void AppendWide(std::wstring& out, std::uint32_t cp) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    } else {
        out.push_back(static_cast<wchar_t>(cp));
    }
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
        AppendWide(*output, cp);
    }
    return true;
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
        if (!output) return Fail(error, L"Particle JSON output is null.");
        Skip();
        if (!ParseValue(output, 0)) return Fail(error, Message());
        Skip();
        if (position_ != text_.size()) return Fail(error, Message(L"Unexpected trailing scene JSON content"));
        return true;
    }

private:
    static constexpr std::size_t kMaxDepth = 128;

    void Skip() noexcept {
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++position_;
        }
    }

    std::wstring Message(std::wstring_view text = L"Invalid scene JSON") const {
        return std::wstring(text) + L" at byte " + std::to_wstring(position_) + L".";
    }

    bool ParseValue(JsonValue* value, std::size_t depth) {
        if (!value || depth > kMaxDepth) return false;
        Skip();
        if (position_ >= text_.size()) return false;
        const char ch = text_[position_];
        if (ch == '{') return ParseObject(value, depth + 1);
        if (ch == '[') return ParseArray(value, depth + 1);
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
                // Store the normalized code point back as UTF-8. Particle field
                // names are ASCII, but string values such as future material ids
                // still need to round-trip through the same UTF-8 contract.
                if (cp <= 0x7F) output->push_back(static_cast<char>(cp));
                else if (cp <= 0x7FF) {
                    output->push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    output->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp <= 0xFFFF) {
                    output->push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    output->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    output->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    output->push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    output->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    output->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    output->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
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

    bool ParseArray(JsonValue* value, std::size_t depth) {
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
            if (!ParseValue(&item, depth)) return false;
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

    bool ParseObject(JsonValue* value, std::size_t depth) {
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
            if (!ParseValue(&item, depth)) return false;
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

bool ReadString(
    const JsonValue& object,
    std::string_view key,
    std::wstring* output,
    bool required,
    std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (required) return Fail(error, L"Missing particle string field: " + std::wstring(key.begin(), key.end()));
        if (output) output->clear();
        return true;
    }
    if (value->type != JsonValue::Type::String)
        return Fail(error, L"Particle field must be string: " + std::wstring(key.begin(), key.end()));
    if (!Utf8ToWide(value->string, output))
        return Fail(error, L"Particle string field contains invalid UTF-8: " + std::wstring(key.begin(), key.end()));
    return true;
}

bool ReadBool(
    const JsonValue& object,
    std::string_view key,
    bool fallback,
    bool* output,
    std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Bool)
        return Fail(error, L"Particle field must be boolean: " + std::wstring(key.begin(), key.end()));
    if (output) *output = value->boolean;
    return true;
}

bool ReadNumber(
    const JsonValue& object,
    std::string_view key,
    double fallback,
    double* output,
    std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Number || !std::isfinite(value->number))
        return Fail(error, L"Particle field must be a finite number: " + std::wstring(key.begin(), key.end()));
    if (output) *output = value->number;
    return true;
}

bool ReadUint32(
    const JsonValue& object,
    std::string_view key,
    std::uint32_t fallback,
    std::uint32_t* output,
    std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Number || !std::isfinite(value->number) ||
        std::floor(value->number) != value->number || value->number < 0.0 ||
        value->number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return Fail(error, L"Particle field must be an exact uint32: " + std::wstring(key.begin(), key.end()));
    if (output) *output = static_cast<std::uint32_t>(value->number);
    return true;
}

bool ReadVector2(
    const JsonValue& object,
    std::string_view key,
    Vec2 fallback,
    Vec2* output,
    std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Array || value->array.size() != 2 ||
        value->array[0].type != JsonValue::Type::Number ||
        value->array[1].type != JsonValue::Type::Number ||
        !std::isfinite(value->array[0].number) || !std::isfinite(value->array[1].number))
        return Fail(error, L"Particle vec2 field must contain two finite numbers: " + std::wstring(key.begin(), key.end()));
    if (output) *output = Vec2{value->array[0].number, value->array[1].number};
    return true;
}

bool ReadColor(
    const JsonValue& object,
    std::string_view key,
    Color4 fallback,
    Color4* output,
    std::wstring* error) {
    const auto* value = object.Find(key);
    if (!value) {
        if (output) *output = fallback;
        return true;
    }
    if (value->type != JsonValue::Type::Array || value->array.size() != 4)
        return Fail(error, L"Particle color field must contain four numbers: " + std::wstring(key.begin(), key.end()));
    double values[4]{};
    for (std::size_t i = 0; i < 4; ++i) {
        if (value->array[i].type != JsonValue::Type::Number || !std::isfinite(value->array[i].number))
            return Fail(error, L"Particle color field must contain finite numbers: " + std::wstring(key.begin(), key.end()));
        values[i] = value->array[i].number;
    }
    if (output) *output = Color4{values[0], values[1], values[2], values[3]};
    return true;
}

bool ParseEmitter(const JsonValue& value, ParticleEmitterDefinition* emitter, std::wstring* error) {
    if (!emitter || value.type != JsonValue::Type::Object)
        return Fail(error, L"Each particleEmitters item must be an object.");

    ParticleEmitterDefinition parsed;
    if (!ReadString(value, "id", &parsed.id, true, error) ||
        !ReadBool(value, "enabled", parsed.enabled, &parsed.enabled, error) ||
        !ReadUint32(value, "maxParticles", parsed.maxParticles, &parsed.maxParticles, error) ||
        !ReadNumber(value, "spawnRate", parsed.spawnRate, &parsed.spawnRate, error) ||
        !ReadNumber(value, "lifetimeMin", parsed.lifetimeMinSeconds, &parsed.lifetimeMinSeconds, error) ||
        !ReadNumber(value, "lifetimeMax", parsed.lifetimeMaxSeconds, &parsed.lifetimeMaxSeconds, error) ||
        !ReadVector2(value, "position", parsed.position, &parsed.position, error) ||
        !ReadVector2(value, "positionSpread", parsed.positionSpread, &parsed.positionSpread, error) ||
        !ReadVector2(value, "velocity", parsed.velocity, &parsed.velocity, error) ||
        !ReadVector2(value, "velocitySpread", parsed.velocitySpread, &parsed.velocitySpread, error) ||
        !ReadVector2(value, "acceleration", parsed.acceleration, &parsed.acceleration, error) ||
        !ReadNumber(value, "sizeStart", parsed.sizeStart, &parsed.sizeStart, error) ||
        !ReadNumber(value, "sizeEnd", parsed.sizeEnd, &parsed.sizeEnd, error) ||
        !ReadColor(value, "colorStart", parsed.colorStart, &parsed.colorStart, error) ||
        !ReadColor(value, "colorEnd", parsed.colorEnd, &parsed.colorEnd, error) ||
        !ReadString(value, "materialId", &parsed.materialId, false, error) ||
        !ReadUint32(value, "seed", parsed.seed, &parsed.seed, error))
        return false;

    *emitter = std::move(parsed);
    return true;
}

} // namespace

bool MiaoParticleSerializer::DeserializeEmitters(
    std::string_view sceneJsonUtf8,
    SceneRuntimeDefinition* runtime,
    std::wstring* error) {
    if (!runtime) return Fail(error, L"Particle scene runtime output is null.");
    if (sceneJsonUtf8.empty()) return Fail(error, L"Particle scene JSON source is empty.");

    JsonValue root;
    JsonParser parser(sceneJsonUtf8);
    if (!parser.Parse(&root, error)) return false;
    if (root.type != JsonValue::Type::Object) return Fail(error, L"scene.json root must be an object.");

    runtime->particleEmitters.clear();
    const auto* emitters = root.Find("particleEmitters");
    if (!emitters) {
        if (error) error->clear();
        return true;
    }
    if (emitters->type != JsonValue::Type::Array)
        return Fail(error, L"particleEmitters must be an array.");

    runtime->particleEmitters.reserve(emitters->array.size());
    for (const auto& value : emitters->array) {
        ParticleEmitterDefinition emitter;
        if (!ParseEmitter(value, &emitter, error)) return false;
        runtime->particleEmitters.push_back(std::move(emitter));
    }

    std::wstring validationError;
    if (!MiaoSceneRuntimeModel::Validate(*runtime, &validationError))
        return Fail(error, L"Particle emitters make the scene runtime invalid: " + validationError);
    if (error) error->clear();
    return true;
}

bool MiaoParticleSerializer::SelfTest() {
    SceneRuntimeDefinition runtime;
    runtime.scene.id = L"scene://particle-serializer-self-test";
    runtime.scene.kind = ContentKind::Wallpaper;
    runtime.scene.rootNodeId = L"node://root";
    runtime.profile = RuntimeProfile::Wallpaper;
    SceneNodeDefinition root;
    root.id = L"node://root";
    runtime.scene.nodes.push_back(std::move(root));

    constexpr std::string_view sceneJson = R"json({
      "schema":1,
      "id":"scene://particle-serializer-self-test",
      "particleEmitters":[
        {
          "id":"particle://sparks",
          "enabled":true,
          "maxParticles":512,
          "spawnRate":48.0,
          "lifetimeMin":1.25,
          "lifetimeMax":2.5,
          "position":[640.0,500.0],
          "positionSpread":[220.0,40.0],
          "velocity":[0.0,-70.0],
          "velocitySpread":[30.0,15.0],
          "acceleration":[0.0,16.0],
          "sizeStart":9.0,
          "sizeEnd":1.5,
          "colorStart":[0.6,0.9,1.0,0.85],
          "colorEnd":[0.2,0.4,1.0,0.0],
          "materialId":"",
          "seed":1337
        }
      ]
    })json";

    std::wstring error;
    if (!DeserializeEmitters(sceneJson, &runtime, &error)) return false;
    if (runtime.particleEmitters.size() != 1) return false;
    const auto& emitter = runtime.particleEmitters.front();
    if (emitter.id != L"particle://sparks" || emitter.maxParticles != 512 || emitter.seed != 1337) return false;
    if (std::abs(emitter.spawnRate - 48.0) > 0.000001 ||
        std::abs(emitter.position.x - 640.0) > 0.000001 ||
        std::abs(emitter.velocity.y + 70.0) > 0.000001 ||
        std::abs(emitter.colorStart.a - 0.85) > 0.000001) return false;

    if (!DeserializeEmitters(R"json({"schema":1})json", &runtime, &error)) return false;
    if (!runtime.particleEmitters.empty()) return false;

    if (DeserializeEmitters(
            R"json({"particleEmitters":[{"id":"particle://bad","maxParticles":1.5}]})json",
            &runtime, &error)) return false;
    if (DeserializeEmitters(
            R"json({"particleEmitters":[{"id":"bad-id","maxParticles":8}]})json",
            &runtime, &error)) return false;
    return true;
}

} // namespace miaodesk::content
