#include "miaodesk/MiaoLockRecord.h"

#include <cstdio>
#include <cstdlib>

namespace miaodesk::lock_record {
namespace {

// 取 key=value 的一行。找不到返回空。
std::string ValueOf(std::string_view text, std::string_view key) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        const std::string_view line = text.substr(start, stop - start);
        const std::size_t equal = line.find('=');
        if (equal != std::string_view::npos) {
            const std::string_view name = line.substr(0, equal);
            if (name == key) return std::string(line.substr(equal + 1));
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return {};
}

bool ParseU32(const std::string& text, std::uint32_t* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || !end || *end != '\0') return false;
    if (parsed > 0xFFFFFFFFul) return false;
    *out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseU64(const std::string& text, std::uint64_t* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (end == text.c_str() || !end || *end != '\0') return false;
    *out = static_cast<std::uint64_t>(parsed);
    return true;
}

} // namespace

std::string Encode(const LockOwnership& ownership) {
    std::string text;
    text += "pid=";
    text += std::to_string(ownership.pid);
    text += "\nheartbeat=";
    text += std::to_string(ownership.heartbeatSeconds);
    text += "\n";
    return text;
}

bool Decode(std::string_view text, LockOwnership* out) {
    if (!out) return false;
    LockOwnership parsed;
    if (!ParseU32(ValueOf(text, "pid"), &parsed.pid)) return false;
    if (!ParseU64(ValueOf(text, "heartbeat"), &parsed.heartbeatSeconds)) return false;
    *out = parsed;
    return true;
}

std::wstring EncodeWide(const LockOwnership& ownership) {
    const std::string narrow = Encode(ownership);
    return std::wstring(narrow.begin(), narrow.end());
}

bool DecodeWide(std::wstring_view text, LockOwnership* out) {
    if (!out) return false;
    const std::string narrow(text.begin(), text.end());
    return Decode(narrow, out);
}

bool SelfCheck() {
    LockOwnership bad;
    // 坏处一:pid 缺失。
    if (Decode("heartbeat=100\n", &bad)) return false;
    // 坏处二:pid 不是数字。
    if (Decode("pid=abc\nheartbeat=100\n", &bad)) return false;
    // 坏处三:heartbeat 尾巴有垃圾 —— 半行被截断的文本不许被当成有效记录。
    if (Decode("pid=100\nheartbeat=1x\n", &bad)) return false;
    // 好的一半也要真的好:一条完整记录必须解得回来,且字段对得上。
    LockOwnership good;
    good.pid = 4242;
    good.heartbeatSeconds = 1700000000ull;
    if (!Decode(Encode(good), &good)) return false;
    return good.pid == 4242 && good.heartbeatSeconds == 1700000000ull;
}

} // namespace miaodesk::lock_record
