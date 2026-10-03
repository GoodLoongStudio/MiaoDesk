#include "miaodesk/JsonStringField.h"

#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <optional>

namespace miaodesk {
namespace {

// 找一个**键位置**上的 key,而不是任何出现的地方。
//
// 为什么必须查这一条:"kind":"id" 里含有子串 "id"(带引号),所以
// find('"id"') 会命中那个**值**。于是"id 字段缺失"被判成"id 在",
// 而同一个文件在两处判据下得出两个不同结论 —— 我的校验器第一版就是这个 bug。
//
// 键位置的定义:前面只可能有空白,再往前必须是 { 或 , 。
bool IsKeyPosition(std::string_view json, std::size_t at) {
    std::size_t before = at;
    while (before > 0 && std::isspace(static_cast<unsigned char>(json[before - 1]))) --before;
    return before == 0 || json[before - 1] == '{' || json[before - 1] == ',';
}

std::size_t FindJsonKey(std::string_view json, std::string_view key) {
    std::size_t from = 0;
    while (from <= json.size()) {
        const std::size_t hit = json.find(key, from);
        if (hit == std::string_view::npos) return std::string_view::npos;
        if (IsKeyPosition(json, hit)) return hit;
        from = hit + 1;
    }
    return std::string_view::npos;
}

std::size_t FindJsonValue(std::string_view json, std::string_view key) {
    const std::size_t hit = FindJsonKey(json, key);
    if (hit == std::string_view::npos) return std::string_view::npos;
    const std::size_t colon = json.find(':', hit + key.size());
    if (colon == std::string_view::npos) return std::string_view::npos;
    std::size_t pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    return pos;
}

int Hex(char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool ReadHex4(std::string_view text, std::size_t pos, unsigned& cp) {
    if (pos + 4 > text.size()) return false;
    cp = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const int value = Hex(text[pos + i]);
        if (value < 0) return false;
        cp = (cp << 4) | static_cast<unsigned>(value);
    }
    return true;
}

void AppendCodepoint(std::string& out, unsigned cp) {
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

} // namespace

std::optional<int> ExtractJsonInt(std::string_view json, std::string_view key) {
    const std::size_t pos = FindJsonValue(json, key);
    if (pos == std::string_view::npos) return std::nullopt;
    int value = 0;
    const auto begin = json.data() + pos;
    const auto [ptr, ec] = std::from_chars(begin, json.data() + json.size(), value);
    if (ec != std::errc{} || ptr == begin) return std::nullopt;
    // 数字之后必须紧跟一个边界。from_chars 自己会停在第一个不是数字的字符上,
    // 所以 "1abc" 会被读成 1 —— 而一份写坏的 manifest 应该被拒绝,而不是被
    // 静默读成一个合法值。这里补上边界检查。
    if (ptr != json.data() + json.size()) {
        const char after = *ptr;
        const bool boundary = after == ',' || after == '}' || after == ']' ||
                              std::isspace(static_cast<unsigned char>(after));
        if (!boundary) return std::nullopt;
    }
    return value;
}

std::optional<double> ExtractJsonDouble(std::string_view json, std::string_view key) noexcept {
    const std::size_t pos = FindJsonValue(json, key);
    if (pos == std::string_view::npos) return std::nullopt;
    const auto begin = json.data() + pos;
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(begin, json.data() + json.size(), value);
    if (ec != std::errc{} || ptr == begin) return std::nullopt;
    // from_chars 对浮点接受 "1e5" / "0.30" / "-2",而它**不接受** "inf" / "nan" ——
    // 所以这里不需要再守一条有限性检查:换成 strtod 才会需要,而那一天还没来。
    // 加一条挡不动的守卫,只会得到一段测不出来的死代码(见 SceneClock::Pause 的注释)。
    if (ptr != json.data() + json.size()) {
        const char after = *ptr;
        const bool boundary = after == ',' || after == '}' || after == ']' ||
                              std::isspace(static_cast<unsigned char>(after));
        if (!boundary) return std::nullopt;
    }
    return value;
}

bool JsonHasStringKey(std::string_view json, std::string_view key) noexcept {
    const std::size_t hit = FindJsonKey(json, key);
    if (hit == std::string_view::npos) return false;
    const std::size_t colon = json.find(':', hit + key.size());
    if (colon == std::string_view::npos) return false;
    const std::size_t quote = json.find('"', colon + 1);
    return quote != std::string_view::npos;
}

// 从 json[position] 起读一个 JSON 字符串字面量,position 前进到它之后。
// 提出来是因为 ExtractJsonStringArray 要逐个读数组元素 —— 两份拷贝解转义不一致的
// 后果不是重复代码,是同一个 \n 在一个调用方下是换行、在另一个调用方下是字母 n。
std::optional<std::string> ParseJsonStringAt(std::string_view json, std::size_t* position) {
    if (!position || *position >= json.size() || json[*position] != '"') return std::nullopt;
    std::size_t pos = *position + 1;
    std::string out;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') break;
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
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
        case 'u': {
            unsigned cp = 0;
            if (!ReadHex4(json, pos, cp)) return out;
            pos += 4;
            // 代理对:高代理后面必须紧跟低代理,否则这个键值对就是坏的。
            // 拼错会得到一对乱码字符,而模型以为自己写的是中文。
            if (cp >= 0xd800 && cp <= 0xdbff && pos + 6 <= json.size() && json[pos] == '\\' &&
                json[pos + 1] == 'u') {
                unsigned low = 0;
                if (ReadHex4(json, pos + 2, low) && low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    pos += 6;
                }
            }
            AppendCodepoint(out, cp);
            break;
        }
        default: out.push_back(esc); break;
        }
    }
    *position = pos;
    return out;
}

std::string ExtractJsonString(std::string_view json, std::string_view key) {
    const std::size_t hit = FindJsonKey(json, key);
    if (hit == std::string_view::npos) return {};
    const std::size_t colon = json.find(':', hit + key.size());
    if (colon == std::string_view::npos) return {};
    std::size_t pos = json.find('"', colon + 1);
    if (pos == std::string_view::npos) return {};
    if (auto value = ParseJsonStringAt(json, &pos)) return *value;
    return {};
}

std::optional<std::vector<std::string>> ExtractJsonStringArray(std::string_view json,
                                                              std::string_view key) {
    const std::size_t start = FindJsonValue(json, key);
    if (start == std::string_view::npos) return std::nullopt;
    if (start >= json.size() || json[start] != '[') return std::nullopt;
    std::size_t pos = start + 1;
    std::vector<std::string> values;
    while (pos < json.size()) {
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
        if (pos < json.size() && json[pos] == ']') return values;
        auto value = ParseJsonStringAt(json, &pos);
        if (!value) return std::nullopt;
        values.push_back(std::move(*value));
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
        if (pos >= json.size()) return std::nullopt;
        if (json[pos] == ',') {
            ++pos;
            continue;
        }
        if (json[pos] == ']') return values;
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace miaodesk
