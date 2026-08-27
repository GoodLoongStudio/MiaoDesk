#include "turingdesk/A2UIParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace turingdesk::a2ui {
namespace {

enum class JsonKind { Null, Boolean, Number, String, Array, Object };

struct JsonValue {
    JsonKind kind{JsonKind::Null};
    bool boolean{};
    double number{};
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool Parse(JsonValue& value, std::wstring& error) {
        SkipWhitespace();
        if (!ParseValue(value)) {
            error = error_.empty() ? L"A2UI JSON 解析失败。" : error_;
            return false;
        }
        SkipWhitespace();
        if (position_ != input_.size()) {
            error = L"A2UI JSON 根对象之后存在额外内容。";
            return false;
        }
        return true;
    }

private:
    bool ParseValue(JsonValue& value) {
        SkipWhitespace();
        if (position_ >= input_.size()) return Fail(L"A2UI JSON 意外结束。");
        const char ch = input_[position_];
        if (ch == '{') return ParseObject(value);
        if (ch == '[') return ParseArray(value);
        if (ch == '"') {
            value.kind = JsonKind::String;
            return ParseString(value.string);
        }
        if (Consume("true")) {
            value.kind = JsonKind::Boolean;
            value.boolean = true;
            return true;
        }
        if (Consume("false")) {
            value.kind = JsonKind::Boolean;
            value.boolean = false;
            return true;
        }
        if (Consume("null")) {
            value.kind = JsonKind::Null;
            return true;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return ParseNumber(value);
        return Fail(L"A2UI JSON 包含非法值。");
    }

    bool ParseObject(JsonValue& value) {
        ++position_;
        value.kind = JsonKind::Object;
        SkipWhitespace();
        if (Take('}')) return true;
        for (;;) {
            std::string key;
            if (!ParseString(key)) return false;
            for (const auto& existing : value.object) {
                if (existing.first == key) return Fail(L"A2UI JSON 不允许重复字段。");
            }
            SkipWhitespace();
            if (!Take(':')) return Fail(L"A2UI JSON 对象字段缺少冒号。");
            JsonValue child;
            if (!ParseValue(child)) return false;
            value.object.emplace_back(std::move(key), std::move(child));
            SkipWhitespace();
            if (Take('}')) return true;
            if (!Take(',')) return Fail(L"A2UI JSON 对象字段之间缺少逗号。");
            SkipWhitespace();
        }
    }

    bool ParseArray(JsonValue& value) {
        ++position_;
        value.kind = JsonKind::Array;
        SkipWhitespace();
        if (Take(']')) return true;
        for (;;) {
            JsonValue child;
            if (!ParseValue(child)) return false;
            value.array.push_back(std::move(child));
            if (value.array.size() > 64) return Fail(L"A2UI JSON 数组元素过多。");
            SkipWhitespace();
            if (Take(']')) return true;
            if (!Take(',')) return Fail(L"A2UI JSON 数组元素之间缺少逗号。");
            SkipWhitespace();
        }
    }

    static void AppendUtf8(std::string& output, unsigned codepoint) {
        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool ParseString(std::string& output) {
        SkipWhitespace();
        if (!Take('"')) return Fail(L"A2UI JSON 字符串缺少引号。");
        while (position_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return Fail(L"A2UI JSON 字符串包含控制字符。");
            if (ch != '\\') {
                output.push_back(static_cast<char>(ch));
                continue;
            }
            if (position_ >= input_.size()) return Fail(L"A2UI JSON 字符串转义不完整。");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > input_.size()) return Fail(L"A2UI JSON Unicode 转义不完整。");
                unsigned codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = input_[position_++];
                    codepoint <<= 4;
                    if (digit >= '0' && digit <= '9') codepoint += static_cast<unsigned>(digit - '0');
                    else if (digit >= 'a' && digit <= 'f') codepoint += static_cast<unsigned>(digit - 'a' + 10);
                    else if (digit >= 'A' && digit <= 'F') codepoint += static_cast<unsigned>(digit - 'A' + 10);
                    else return Fail(L"A2UI JSON Unicode 转义非法。");
                }
                AppendUtf8(output, codepoint);
                break;
            }
            default:
                return Fail(L"A2UI JSON 包含不支持的字符串转义。");
            }
        }
        return Fail(L"A2UI JSON 字符串未闭合。");
    }

    bool ParseNumber(JsonValue& value) {
        const std::size_t start = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return Fail(L"A2UI JSON 数字不完整。");
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) return Fail(L"A2UI JSON 数字非法。");
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_])))
                return Fail(L"A2UI JSON 小数非法。");
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_])))
                return Fail(L"A2UI JSON 指数非法。");
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        try {
            value.kind = JsonKind::Number;
            value.number = std::stod(std::string(input_.substr(start, position_ - start)));
            if (!std::isfinite(value.number)) return Fail(L"A2UI JSON 不允许 NaN/Infinity。");
            return true;
        } catch (...) {
            return Fail(L"A2UI JSON 数字解析失败。");
        }
    }

    bool Consume(std::string_view token) {
        if (input_.substr(position_, token.size()) != token) return false;
        position_ += token.size();
        return true;
    }

    bool Take(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    void SkipWhitespace() {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
    }

    bool Fail(std::wstring message) {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }

    std::string_view input_;
    std::size_t position_{};
    std::wstring error_;
};

const JsonValue* Member(const JsonValue& object, std::string_view key) {
    if (object.kind != JsonKind::Object) return nullptr;
    for (const auto& [name, value] : object.object) if (name == key) return &value;
    return nullptr;
}

bool HasOnly(const JsonValue& object, std::initializer_list<std::string_view> allowed, std::wstring& error) {
    if (object.kind != JsonKind::Object) {
        error = L"A2UI 节点必须是对象。";
        return false;
    }
    for (const auto& [name, value] : object.object) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            error = L"A2UI 包含未允许字段：";
            error.append(name.begin(), name.end());
            return false;
        }
    }
    return true;
}

bool StringIs(const JsonValue* value, std::initializer_list<std::string_view> choices) {
    if (!value || value->kind != JsonKind::String) return false;
    return std::find(choices.begin(), choices.end(), value->string) != choices.end();
}

bool ValidateString(const JsonValue* value, std::size_t minLength, std::size_t maxLength, std::wstring& error) {
    if (!value || value->kind != JsonKind::String) {
        error = L"A2UI 字段必须是字符串。";
        return false;
    }
    if (value->string.size() < minLength || value->string.size() > maxLength) {
        error = L"A2UI 字符串长度超出限制。";
        return false;
    }
    return true;
}

bool ValidateNumber(const JsonValue* value, double minimum, double maximum, std::wstring& error) {
    if (!value || value->kind != JsonKind::Number || value->number < minimum || value->number > maximum) {
        error = L"A2UI 数值缺失或超出允许范围。";
        return false;
    }
    return true;
}

bool ValidateColor(const JsonValue* value, std::wstring& error) {
    if (!value) return true;
    if (value->kind != JsonKind::String) {
        error = L"A2UI 颜色必须是字符串。";
        return false;
    }
    const auto& color = value->string;
    if ((color.size() != 7 && color.size() != 9) || color.front() != '#') {
        error = L"A2UI 颜色必须使用 #RRGGBB 或 #RRGGBBAA。";
        return false;
    }
    for (std::size_t i = 1; i < color.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(color[i]))) {
            error = L"A2UI 颜色包含非法字符。";
            return false;
        }
    }
    return true;
}

bool ValidateLayout(const JsonValue* layout, std::wstring& error) {
    if (!layout || !HasOnly(*layout, {"x", "y", "width", "height", "horizontal", "vertical"}, error)) return false;
    if (!ValidateNumber(Member(*layout, "x"), 0.0, 1.0, error) ||
        !ValidateNumber(Member(*layout, "y"), 0.0, 1.0, error) ||
        !ValidateNumber(Member(*layout, "width"), 0.05, 1.0, error) ||
        !ValidateNumber(Member(*layout, "height"), 0.05, 1.0, error)) return false;
    if (const auto* horizontal = Member(*layout, "horizontal"); horizontal &&
        !StringIs(horizontal, {"start", "center", "end", "stretch"})) {
        error = L"A2UI horizontal 非法。";
        return false;
    }
    if (const auto* vertical = Member(*layout, "vertical"); vertical &&
        !StringIs(vertical, {"start", "center", "end", "stretch"})) {
        error = L"A2UI vertical 非法。";
        return false;
    }
    return true;
}

bool ValidateCommonStyle(const JsonValue& props, std::wstring& error) {
    if (!ValidateColor(Member(props, "background"), error) || !ValidateColor(Member(props, "foreground"), error)) return false;
    if (const auto* value = Member(props, "opacity"); value && !ValidateNumber(value, 0.0, 1.0, error)) return false;
    if (const auto* value = Member(props, "cornerRadius"); value && !ValidateNumber(value, 0.0, 32.0, error)) return false;
    if (const auto* value = Member(props, "padding"); value && !ValidateNumber(value, 0.0, 32.0, error)) return false;
    if (const auto* value = Member(props, "fontSize"); value && !ValidateNumber(value, 10.0, 48.0, error)) return false;
    if (const auto* value = Member(props, "fontWeight"); value &&
        !StringIs(value, {"normal", "medium", "semibold", "bold"})) {
        error = L"A2UI fontWeight 非法。";
        return false;
    }
    return true;
}

bool ValidateNode(const JsonValue& node, int depth, int& count, std::wstring& error) {
    if (depth > 4) {
        error = L"A2UI 嵌套层级超过 4。";
        return false;
    }
    if (++count > 32) {
        error = L"A2UI 组件数量超过 32。";
        return false;
    }
    if (!HasOnly(node, {"type", "props", "layout"}, error)) return false;

    const auto* type = Member(node, "type");
    const auto* props = Member(node, "props");
    if (!type || type->kind != JsonKind::String || !props || props->kind != JsonKind::Object ||
        !ValidateLayout(Member(node, "layout"), error) || !ValidateCommonStyle(*props, error)) {
        if (error.empty()) error = L"A2UI 节点缺少合法的 type / props / layout。";
        return false;
    }

    if (type->string == "Card") {
        if (!HasOnly(*props, {"title", "subtitle", "children", "background", "foreground", "opacity", "cornerRadius", "padding", "fontSize", "fontWeight"}, error)) return false;
        if (const auto* title = Member(*props, "title"); title && !ValidateString(title, 0, 120, error)) return false;
        if (const auto* subtitle = Member(*props, "subtitle"); subtitle && !ValidateString(subtitle, 0, 240, error)) return false;
        if (const auto* children = Member(*props, "children")) {
            if (children->kind != JsonKind::Array || children->array.size() > 16) {
                error = L"A2UI Card.children 必须是不超过 16 项的数组。";
                return false;
            }
            for (const auto& child : children->array) if (!ValidateNode(child, depth + 1, count, error)) return false;
        }
        return true;
    }
    if (type->string == "Text") {
        if (!HasOnly(*props, {"text", "background", "foreground", "opacity", "cornerRadius", "padding", "fontSize", "fontWeight"}, error)) return false;
        return ValidateString(Member(*props, "text"), 1, 1000, error);
    }
    if (type->string == "Button") {
        if (!HasOnly(*props, {"text", "action", "background", "foreground", "opacity", "cornerRadius", "padding", "fontSize", "fontWeight"}, error) ||
            !ValidateString(Member(*props, "text"), 1, 80, error)) return false;
        if (const auto* action = Member(*props, "action"); action && !StringIs(action, {"none"})) {
            error = L"AI 生成的 Button 不允许执行宿主动作。";
            return false;
        }
        return true;
    }
    if (type->string == "Weather") {
        if (!HasOnly(*props, {"location", "unit", "showForecast", "background", "foreground", "opacity", "cornerRadius", "padding", "fontSize", "fontWeight"}, error) ||
            !ValidateString(Member(*props, "location"), 1, 120, error) ||
            !StringIs(Member(*props, "unit"), {"celsius", "fahrenheit"})) {
            if (error.empty()) error = L"A2UI Weather 字段非法。";
            return false;
        }
        if (const auto* forecast = Member(*props, "showForecast"); forecast && forecast->kind != JsonKind::Boolean) {
            error = L"A2UI showForecast 必须是布尔值。";
            return false;
        }
        return true;
    }
    if (type->string == "List") {
        if (!HasOnly(*props, {"items", "ordered", "background", "foreground", "opacity", "cornerRadius", "padding", "fontSize", "fontWeight"}, error)) return false;
        const auto* items = Member(*props, "items");
        if (!items || items->kind != JsonKind::Array || items->array.empty() || items->array.size() > 20) {
            error = L"A2UI List.items 必须包含 1-20 项。";
            return false;
        }
        for (const auto& item : items->array) if (!ValidateString(&item, 1, 240, error)) return false;
        if (const auto* ordered = Member(*props, "ordered"); ordered && ordered->kind != JsonKind::Boolean) {
            error = L"A2UI ordered 必须是布尔值。";
            return false;
        }
        return true;
    }

    error = L"A2UI type 只允许 Card / Text / Button / Weather / List。";
    return false;
}

void EscapeJson(std::ostringstream& output, std::string_view text) {
    output << '"';
    for (const unsigned char ch : text) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
            else output << static_cast<char>(ch);
        }
    }
    output << '"';
}

void WriteJson(std::ostringstream& output, const JsonValue& value) {
    switch (value.kind) {
    case JsonKind::Null: output << "null"; break;
    case JsonKind::Boolean: output << (value.boolean ? "true" : "false"); break;
    case JsonKind::Number: output << std::setprecision(15) << value.number; break;
    case JsonKind::String: EscapeJson(output, value.string); break;
    case JsonKind::Array:
        output << '[';
        for (std::size_t i = 0; i < value.array.size(); ++i) {
            if (i) output << ',';
            WriteJson(output, value.array[i]);
        }
        output << ']';
        break;
    case JsonKind::Object:
        output << '{';
        for (std::size_t i = 0; i < value.object.size(); ++i) {
            if (i) output << ',';
            EscapeJson(output, value.object[i].first);
            output << ':';
            WriteJson(output, value.object[i].second);
        }
        output << '}';
        break;
    }
}

constexpr std::string_view kTodayTasks = R"JSON({"version":1,"type":"Card","props":{"title":"今日待办","background":"#E6FFFFFF","foreground":"#1F2937","cornerRadius":18,"padding":16,"children":[{"type":"List","props":{"items":["整理需求","完成预览","提交版本"],"foreground":"#1F2937","fontSize":15},"layout":{"x":0.06,"y":0.24,"width":0.88,"height":0.68}}]},"layout":{"x":0.68,"y":0.05,"width":0.28,"height":0.20}})JSON";
constexpr std::string_view kFocusClock = R"JSON({"version":1,"type":"Card","props":{"title":"Focus","subtitle":"25:00","background":"#CC101827","foreground":"#FFFFFF","cornerRadius":22,"padding":18,"children":[{"type":"Text","props":{"text":"专注时间","foreground":"#BFD7FF","fontSize":14},"layout":{"x":0.08,"y":0.62,"width":0.84,"height":0.20}}]},"layout":{"x":0.06,"y":0.08,"width":0.24,"height":0.20}})JSON";
constexpr std::string_view kWeatherGlass = R"JSON({"version":1,"type":"Card","props":{"title":"天气","background":"#BFFFFFFF","foreground":"#24324A","cornerRadius":20,"padding":16,"children":[{"type":"Weather","props":{"location":"Berlin","unit":"celsius","showForecast":true,"foreground":"#24324A"},"layout":{"x":0.06,"y":0.24,"width":0.88,"height":0.68}}]},"layout":{"x":0.70,"y":0.30,"width":0.26,"height":0.22}})JSON";
constexpr std::string_view kSystemPulse = R"JSON({"version":1,"type":"Card","props":{"title":"System Pulse","background":"#D91B2230","foreground":"#F8FAFC","cornerRadius":18,"padding":16,"children":[{"type":"List","props":{"items":["CPU --%","Memory --%","Network --"],"foreground":"#DCE7F5","fontSize":14},"layout":{"x":0.06,"y":0.26,"width":0.88,"height":0.64}}]},"layout":{"x":0.04,"y":0.70,"width":0.28,"height":0.20}})JSON";

} // namespace

ValidationResult ValidateWidgetDocument(std::string_view json) {
    ValidationResult result;
    if (json.empty() || json.size() > 64 * 1024) {
        result.message = L"A2UI JSON 为空或超过 64 KiB 限制。";
        return result;
    }

    JsonValue root;
    JsonParser parser(json);
    if (!parser.Parse(root, result.message)) return result;
    if (!HasOnly(root, {"version", "type", "props", "layout"}, result.message)) return result;

    const auto* version = Member(root, "version");
    if (!version || version->kind != JsonKind::Number || std::fabs(version->number - 1.0) > 1e-9) {
        result.message = L"A2UI version 必须为 1。";
        return result;
    }
    const auto* type = Member(root, "type");
    if (!type || type->kind != JsonKind::String || type->string != "Card") {
        result.message = L"A2UI 根组件必须是 Card。";
        return result;
    }

    JsonValue node;
    node.kind = JsonKind::Object;
    node.object.emplace_back("type", *type);
    node.object.emplace_back("props", *Member(root, "props"));
    node.object.emplace_back("layout", *Member(root, "layout"));
    int count = 0;
    if (!ValidateNode(node, 0, count, result.message)) return result;

    std::ostringstream normalized;
    WriteJson(normalized, root);
    result.success = true;
    result.normalizedJson = normalized.str();
    result.message = L"A2UI widget validation passed.";
    return result;
}

std::string BuiltInWidgetExample(std::string_view key) {
    if (key == "today_tasks") return std::string(kTodayTasks);
    if (key == "focus_clock") return std::string(kFocusClock);
    if (key == "weather_glass") return std::string(kWeatherGlass);
    if (key == "system_pulse") return std::string(kSystemPulse);
    return {};
}

std::string BuiltInWidgetExampleCatalogJson() {
    return R"JSON({"examples":[{"key":"today_tasks","title":"Today Tasks"},{"key":"focus_clock","title":"Focus Clock"},{"key":"weather_glass","title":"Weather Glass"},{"key":"system_pulse","title":"System Pulse"}]})JSON";
}

} // namespace turingdesk::a2ui
