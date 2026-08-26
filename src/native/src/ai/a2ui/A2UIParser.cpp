#include "turingdesk/A2UIParser.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace turingdesk::a2ui {
namespace {

enum class Kind { Null, Boolean, Number, String, Array, Object };

struct Value {
    Kind kind{Kind::Null};
    bool boolean{};
    double number{};
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool Parse(Value& out, std::wstring& error) {
        SkipSpace();
        if (!ParseValue(out)) {
            error = error_.empty() ? L"A2UI JSON 解析失败。" : error_;
            return false;
        }
        SkipSpace();
        if (pos_ != text_.size()) {
            error = L"A2UI JSON 根对象之后存在额外内容。";
            return false;
        }
        return true;
    }

private:
    bool ParseValue(Value& out) {
        SkipSpace();
        if (pos_ >= text_.size()) return Fail(L"A2UI JSON 意外结束。");
        const char ch = text_[pos_];
        if (ch == '{') return ParseObject(out);
        if (ch == '[') return ParseArray(out);
        if (ch == '"') {
            out.kind = Kind::String;
            return ParseString(out.string);
        }
        if (ch == 't' && Consume("true")) { out.kind = Kind::Boolean; out.boolean = true; return true; }
        if (ch == 'f' && Consume("false")) { out.kind = Kind::Boolean; out.boolean = false; return true; }
        if (ch == 'n' && Consume("null")) { out.kind = Kind::Null; return true; }
        if (ch == '-' || (ch >= '0' && ch <= '9')) return ParseNumber(out);
        return Fail(L"A2UI JSON 包含非法值。");
    }

    bool ParseObject(Value& out) {
        ++pos_;
        out.kind = Kind::Object;
        SkipSpace();
        if (Take('}')) return true;
        while (pos_ < text_.size()) {
            std::string key;
            if (!ParseString(key)) return false;
            for (const auto& entry : out.object) {
                if (entry.first == key) return Fail(L"A2UI JSON 不允许重复字段。" );
            }
            SkipSpace();
            if (!Take(':')) return Fail(L"A2UI JSON 对象字段缺少冒号。" );
            Value value;
            if (!ParseValue(value)) return false;
            out.object.emplace_back(std::move(key), std::move(value));
            SkipSpace();
            if (Take('}')) return true;
            if (!Take(',')) return Fail(L"A2UI JSON 对象字段之间缺少逗号。" );
            SkipSpace();
        }
        return Fail(L"A2UI JSON 对象未闭合。" );
    }

    bool ParseArray(Value& out) {
        ++pos_;
        out.kind = Kind::Array;
        SkipSpace();
        if (Take(']')) return true;
        while (pos_ < text_.size()) {
            Value value;
            if (!ParseValue(value)) return false;
            out.array.push_back(std::move(value));
            SkipSpace();
            if (Take(']')) return true;
            if (!Take(',')) return Fail(L"A2UI JSON 数组元素之间缺少逗号。" );
            SkipSpace();
        }
        return Fail(L"A2UI JSON 数组未闭合。" );
    }

    bool ParseString(std::string& out) {
        SkipSpace();
        if (!Take('"')) return Fail(L"A2UI JSON 字符串缺少引号。" );
        while (pos_ < text_.size()) {
            const unsigned char ch = static_cast<unsigned char>(text_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return Fail(L"A2UI JSON 字符串包含控制字符。" );
            if (ch != '\\') {
                out.push_back(static_cast<char>(ch));
                continue;
            }
            if (pos_ >= text_.size()) return Fail(L"A2UI JSON 字符串转义不完整。" );
            const char escaped = text_[pos_++];
            switch (escaped) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > text_.size()) return Fail(L"A2UI JSON Unicode 转义不完整。" );
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = text_[pos_++];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') code += static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') code += static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') code += static_cast<unsigned>(hex - 'A' + 10);
                    else return Fail(L"A2UI JSON Unicode 转义非法。" );
                }
                AppendUtf8(out, code);
                break;
            }
            default: return Fail(L"A2UI JSON 包含不支持的字符串转义。" );
            }
        }
        return Fail(L"A2UI JSON 字符串未闭合。" );
    }

    bool ParseNumber(Value& out) {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return Fail(L"A2UI JSON 数字不完整。" );
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(text_[pos_]))) return Fail(L"A2UI JSON 数字非法。" );
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) return Fail(L"A2UI JSON 小数非法。" );
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) return Fail(L"A2UI JSON 指数非法。" );
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        try {
            const std::string token(text_.substr(start, pos_ - start));
            const double value = std::stod(token);
            if (!std::isfinite(value)) return Fail(L"A2UI JSON 不允许 NaN/Infinity。" );
            out.kind = Kind::Number;
            out.number = value;
            return true;
        } catch (...) {
            return Fail(L"A2UI JSON 数字解析失败。" );
        }
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }

    bool Consume(std::string_view token) {
        if (text_.substr(pos_, token.size()) != token) return false;
        pos_ += token.size();
        return true;
    }

    bool Take(char ch) {
        if (pos_ >= text_.size() || text_[pos_] != ch) return false;
        ++pos_;
        return true;
    }

    void SkipSpace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    bool Fail(std::wstring message) {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }

    std::string_view text_;
    std::size_t pos_{};
    std::wstring error_;
};

const Value* Member(const Value& object, std::string_view name) {
    if (object.kind != Kind::Object) return nullptr;
    for (const auto& entry : object.object) if (entry.first == name) return &entry.second;
    return nullptr;
}

bool HasOnly(const Value& object, std::initializer_list<std::string_view> names, std::wstring& error) {
    if (object.kind != Kind::Object) { error = L"A2UI 节点必须是对象。"; return false; }
    for (const auto& entry : object.object) {
        bool allowed = false;
        for (const auto name : names) if (entry.first == name) { allowed = true; break; }
        if (!allowed) {
            error = L"A2UI 包含未允许字段：";
            error.append(entry.first.begin(), entry.first.end());
            return false;
        }
    }
    return true;
}

bool StringIn(const Value* value, std::initializer_list<std::string_view> choices) {
    if (!value || value->kind != Kind::String) return false;
    for (auto choice : choices) if (value->string == choice) return true;
    return false;
}

bool BoundedString(const Value* value, std::size_t minLength, std::size_t maxLength, std::wstring& error) {
    if (!value || value->kind != Kind::String) { error = L"A2UI 字段必须是字符串。"; return false; }
    if (value->string.size() < minLength || value->string.size() > maxLength) { error = L"A2UI 字符串长度超出限制。"; return false; }
    return true;
}

bool BoundedNumber(const Value* value, double low, double high, std::wstring& error) {
    if (!value || value->kind != Kind::Number) { error = L"A2UI 字段必须是数字。"; return false; }
    if (value->number < low || value->number > high) { error = L"A2UI 数值超出允许范围。"; return false; }
    return true;
}

bool IsColor(const Value* value, std::wstring& error) {
    if (!value) return true;
    if (value->kind != Kind::String) { error = L"A2UI 颜色必须是字符串。"; return false; }
    const auto& text = value->string;
    if (!(text.size() == 7 || text.size() == 9) || text.front() != '#') { error = L"A2UI 颜色必须使用 #RRGGBB 或 #RRGGBBAA。"; return false; }
    for (std::size_t i = 1; i < text.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(text[i]))) { error = L"A2UI 颜色包含非法字符。"; return false; }
    return true;
}

bool ValidateLayout(const Value* layout, std::wstring& error) {
    if (!layout || !HasOnly(*layout, {"x","y","width","height","horizontal","vertical"}, error)) return false;
    if (!BoundedNumber(Member(*layout, "x"), 0, 1, error) ||
        !BoundedNumber(Member(*layout, "y"), 0, 1, error) ||
        !BoundedNumber(Member(*layout, "width"), 0.05, 1, error) ||
        !BoundedNumber(Member(*layout, "height"), 0.05, 1, error)) return false;
    if (const auto* h = Member(*layout, "horizontal"); h && !StringIn(h, {"start","center","end","stretch"})) { error = L"A2UI horizontal 非法。"; return false; }
    if (const auto* v = Member(*layout, "vertical"); v && !StringIn(v, {"start","center","end","stretch"})) { error = L"A2UI vertical 非法。"; return false; }
    return true;
}

bool ValidateCommonStyle(const Value& props, std::wstring& error) {
    if (!IsColor(Member(props, "background"), error) || !IsColor(Member(props, "foreground"), error)) return false;
    if (const auto* v = Member(props, "opacity"); v && !BoundedNumber(v, 0, 1, error)) return false;
    if (const auto* v = Member(props, "cornerRadius"); v && !BoundedNumber(v, 0, 32, error)) return false;
    if (const auto* v = Member(props, "padding"); v && !BoundedNumber(v, 0, 32, error)) return false;
    if (const auto* v = Member(props, "fontSize"); v && !BoundedNumber(v, 10, 48, error)) return false;
    if (const auto* v = Member(props, "fontWeight"); v && !StringIn(v, {"normal","medium","semibold","bold"})) { error = L"A2UI fontWeight 非法。"; return false; }
    return true;
}

bool ValidateNode(const Value& node, int depth, int& nodeCount, std::wstring& error) {
    if (depth > 4) { error = L"A2UI 嵌套层级超过 4。"; return false; }
    if (++nodeCount > 32) { error = L"A2UI 组件数量超过 32。"; return false; }
    if (!HasOnly(node, {"type","props","layout"}, error)) return false;
    const auto* type = Member(node, "type");
    const auto* props = Member(node, "props");
    const auto* layout = Member(node, "layout");
    if (!type || type->kind != Kind::String || !props || props->kind != Kind::Object || !ValidateLayout(layout, error)) {
        if (error.empty()) error = L"A2UI 节点缺少 type/props/layout。";
        return false;
    }
    if (!ValidateCommonStyle(*props, error)) return false;

    constexpr std::initializer_list<std::string_view> cardFields = {
        "title","subtitle","children","background","foreground","opacity","cornerRadius","padding","fontSize","fontWeight"};
    constexpr std::initializer_list<std::string_view> textFields = {
        "text","background","foreground","opacity","cornerRadius","padding","fontSize","fontWeight"};
    constexpr std::initializer_list<std::string_view> buttonFields = {
        "text","action","background","foreground","opacity","cornerRadius","padding","fontSize","fontWeight"};
    constexpr std::initializer_list<std::string_view> weatherFields = {
        "location","unit","showForecast","background","foreground","opacity","cornerRadius","padding","fontSize","fontWeight"};
    constexpr std::initializer_list<std::string_view> listFields = {
        "items","ordered","background","foreground","opacity","cornerRadius","padding","fontSize","fontWeight"};

    if (type->string == "Card") {
        if (!HasOnly(*props, cardFields, error)) return false;
        if (const auto* title = Member(*props, "title"); title && !BoundedString(title, 0, 120, error)) return false;
        if (const auto* subtitle = Member(*props, "subtitle"); subtitle && !BoundedString(subtitle, 0, 240, error)) return false;
        if (const auto* children = Member(*props, "children")) {
            if (children->kind != Kind::Array || children->array.size() > 16) { error = L"A2UI Card.children 必须是不超过 16 项的数组。"; return false; }
            for (const auto& child : children->array) if (!ValidateNode(child, depth + 1, nodeCount, error)) return false;
        }
        return true;
    }
    if (type->string == "Text") {
        if (!HasOnly(*props, textFields, error)) return false;
        return BoundedString(Member(*props, "text"), 1, 1000, error);
    }
    if (type->string == "Button") {
        if (!HasOnly(*props, buttonFields, error) || !BoundedString(Member(*props, "text"), 1, 80, error)) return false;
        if (const auto* action = Member(*props, "action"); action && !StringIn(action, {"none"})) { error = L"AI 生成的 Button 不允许执行宿主动作。"; return false; }
        return true;
    }
    if (type->string == "Weather") {
        if (!HasOnly(*props, weatherFields, error) || !BoundedString(Member(*props, "location"), 1, 120, error)) return false;
        if (!StringIn(Member(*props, "unit"), {"celsius","fahrenheit"})) { error = L"A2UI Weather.unit 非法。"; return false; }
        if (const auto* forecast = Member(*props, "showForecast"); forecast && forecast->kind != Kind::Boolean) { error = L"A2UI showForecast 必须是布尔值。"; return false; }
        return true;
    }
    if (type->string == "List") {
        if (!HasOnly(*props, listFields, error)) return false;
        const auto* items = Member(*props, "items");
        if (!items || items->kind != Kind::Array || items->array.empty() || items->array.size() > 20) { error = L"A2UI List.items 必须包含 1-20 项。"; return false; }
        for (const auto& item : items->array) if (!BoundedString(&item, 1, 240, error)) return false;
        if (const auto* ordered = Member(*props, "ordered"); ordered && ordered->kind != Kind::Boolean) { error = L"A2UI ordered 必须是布尔值。"; return false; }
        return true;
    }

    error = L"A2UI type 只允许 Card/Text/Button/Weather/List。";
    return false;
}

void EscapeJson(std::ostringstream& out, std::string_view text) {
    out << '"';
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
            else out << static_cast<char>(ch);
        }
    }
    out << '"';
}

void WriteJson(std::ostringstream& out, const Value& value) {
    switch (value.kind) {
    case Kind::Null: out << "null"; break;
    case Kind::Boolean: out << (value.boolean ? "true" : "false"); break;
    case Kind::Number: out << std::setprecision(15) << value.number; break;
    case Kind::String: EscapeJson(out, value.string); break;
    case Kind::Array:
        out << '[';
        for (std::size_t i = 0; i < value.array.size(); ++i) { if (i) out << ','; WriteJson(out, value.array[i]); }
        out << ']';
        break;
    case Kind::Object:
        out << '{';
        for (std::size_t i = 0; i < value.object.size(); ++i) {
            if (i) out << ',';
            EscapeJson(out, value.object[i].first);
            out << ':';
            WriteJson(out, value.object[i].second);
        }
        out << '}';
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

    Value root;
    Parser parser(json);
    if (!parser.Parse(root, result.message)) return result;
    if (!HasOnly(root, {"version","type","props","layout"}, result.message)) return result;
    const auto* version = Member(root, "version");
    if (!version || version->kind != Kind::Number || std::fabs(version->number - 1.0) > 1e-9) {
        result.message = L"A2UI version 必须为 1。";
        return result;
    }
    const auto* type = Member(root, "type");
    if (!type || type->kind != Kind::String || type->string != "Card") {
        result.message = L"A2UI 根组件必须是 Card。";
        return result;
    }

    Value node;
    node.kind = Kind::Object;
    if (const auto* t = Member(root, "type")) node.object.emplace_back("type", *t);
    if (const auto* p = Member(root, "props")) node.object.emplace_back("props", *p);
    if (const auto* l = Member(root, "layout")) node.object.emplace_back("layout", *l);
    int nodeCount = 0;
    if (!ValidateNode(node, 0, nodeCount, result.message)) return result;

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
