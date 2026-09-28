#include "miaodesk/CreatorWorkspaceState.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace miaodesk::creator {
namespace {

// 已知的键。解析时用它区分"没见过的新字段"和"重复的已知字段"。
constexpr const char* kKeys[] = {
    "sessionId", "epoch", "stage", "cancelRequested",
    "candidateDigest", "revision", "updatedAtMs",
};

bool IsKnownKey(std::string_view key) {
    for (const char* known : kKeys) {
        if (key == known) return true;
    }
    return false;
}

// 值里不能有换行、回车或等号。前两个会破坏行结构,等号会让"键=值=值"出现歧义。
bool ValueIsSafe(std::string_view value) {
    for (const char ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '=') return false;
    }
    return true;
}

bool KeyIsSafe(std::string_view key) {
    return !key.empty() && ValueIsSafe(key);
}

} // namespace

std::string SerializeCreatorWorkspaceState(const CreatorWorkspaceState& state) {
    // 故意不做转义:调用方要序列化的东西本来就该是会话 ID、十六进制摘要、整数。
    // 真出现一个带换行的会话 ID,那是上游的 bug,让它在这里炸比让它悄悄写坏更好。
    std::string out;
    out += "sessionId=";
    out += state.sessionId;
    out += "\n";
    out += "epoch=";
    out += std::to_string(state.epoch);
    out += "\n";
    out += "stage=";
    out += std::to_string(state.stage);
    out += "\n";
    out += "cancelRequested=";
    out += state.cancelRequested ? "1" : "0";
    out += "\n";
    if (!state.candidateDigest.empty()) {
        out += "candidateDigest=";
        out += state.candidateDigest;
        out += "\n";
    }
    out += "revision=";
    out += std::to_string(state.revision);
    out += "\n";
    out += "updatedAtMs=";
    out += std::to_string(state.updatedAtMs);
    out += "\n";
    return out;
}

bool ParseCreatorWorkspaceState(std::string_view text, CreatorWorkspaceState* out) {
    if (!out) return false;
    CreatorWorkspaceState parsed;
    std::vector<bool> seen(sizeof(kKeys) / sizeof(kKeys[0]), false);

    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty()) {
            if (end == text.size()) break;
            continue;
        }
        const std::size_t equal = line.find('=');
        if (equal == std::string_view::npos) return false;
        const std::string_view key = line.substr(0, equal);
        const std::string_view value = line.substr(equal + 1);
        if (!KeyIsSafe(key) || !ValueIsSafe(value)) return false;

        // 已知键出现两次 = 两份矛盾的值,没有该信的那一份。
        bool knownIndex = false;
        for (std::size_t i = 0; i < seen.size(); ++i) {
            if (key != kKeys[i]) continue;
            if (seen[i]) return false;
            seen[i] = true;
            knownIndex = true;
            break;
        }
        if (!knownIndex) continue;   // 未知键:留给将来的版本

        if (key == "sessionId") {
            parsed.sessionId = std::string(value);
        } else if (key == "epoch" || key == "revision" || key == "updatedAtMs") {
            // 纯十进制。空串或带垃圾都算解析失败 —— 一个"不知道是几"的 epoch
            // 会让 SameRoundAs 给出随机答案。
            if (value.empty()) return false;
            std::uint64_t number = 0;
            for (const char ch : value) {
                if (ch < '0' || ch > '9') return false;
                const auto digit = static_cast<std::uint64_t>(ch - '0');
                if (number > (0xFFFFFFFFFFFFFFFFull - digit) / 10) return false;
                number = number * 10 + digit;
            }
            if (key == "epoch") parsed.epoch = number;
            else if (key == "revision") {
                if (number > 0xFFFFFFFFull) return false;
                parsed.revision = static_cast<std::uint32_t>(number);
            } else {
                parsed.updatedAtMs = number;
            }
        } else if (key == "stage") {
            // 阶段也要纯十进制。第一版这里用了 atoi,而 atoi 对 "abc" 返回 0 ——
            // 于是一份写坏的状态文件会被静默读成 Draft。Draft 恰好是写入最严的
            // 阶段,所以"读不懂"看起来是安全的;但那是因为运气,不是因为设计。
            // 读不懂就该整份不认(hasState=false),而不是猜一个阶段。
            if (value.empty()) return false;
            for (const char ch : value) {
                if (ch < '0' || ch > '9') return false;
            }
            const long stage = std::strtol(std::string(value).c_str(), nullptr, 10);
            if (stage < 0 || stage > 14) return false;   // CreationStage 的可枚举范围
            parsed.stage = static_cast<int>(stage);
        } else if (key == "cancelRequested") {
            if (value == "1") parsed.cancelRequested = true;
            else if (value == "0") parsed.cancelRequested = false;
            else return false;
        } else if (key == "candidateDigest") {
            parsed.candidateDigest = std::string(value);
        }
        if (end == text.size()) break;
    }

    // sessionId 是归属的锚。没有它,这段状态无法和任何作品对上,
    // 而 worker 会把它当成"没有状态"而不是当成一个空会话。
    if (parsed.sessionId.empty()) return false;
    *out = parsed;
    return true;
}

bool IsUsableSessionChar(char ch) noexcept {
    const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9');
    return alnum || ch == '-' || ch == '_';
}

std::string NewCreatorSessionId(std::uint64_t nowMs) {
    // 三段各管一件事(顺序固定,便于人工扫一眼就知道是什么):
    //   * 时间 —— 重启之后能和上次打开的旧作品分辨开;
    //   * 序号 —— **确定性**的一半:同一进程里取一百个也两两不同;
    //   * 随机尾巴 —— 确定性照看不到的那一半:进程重启后序号会回到 0,而
    //     GetTickCount64 也可能撞上同一个值。它还顺带让别人的会话 ID 猜不到 ——
    //     会话 ID 是归属判据,猜得到就相当于能申请替别人写。
    // 只留前两段或只留最后一段都能跑,而且单看输出分不出差别;两者都得在,这条
    // 在 CreatorToolWorkerTest 里逐段断言过。
    static std::uint64_t counter = 0;
    const std::uint64_t sequence = ++counter;

    std::string id = "s";
    id += std::to_string(nowMs);
    id += "-";
    id += std::to_string(sequence);
    id += "-";
    // 8 个十六进制位。std::random_device 在 Windows 上不是密码学强度的,这里靠
    // 它挡住的是"撞车"和"被顺手猜到",靠它防蓄意伪造是不现实的。
    std::string entropy;
    {
        std::random_device device;
        const char digits[] = "0123456789abcdef";
        for (int i = 0; i < 4; ++i) {
            const unsigned value = device();
            for (int shift = 12; shift >= 0; shift -= 4) {
                entropy += digits[(value >> shift) & 0xF];
            }
        }
        entropy.resize(8);
    }
    id += entropy;
    return id;
}

// 只保留可用的字符:换行、回车、tab、空格、别人手写的一行注释都不算。会话身份要当
// 目录名,也要当工具参数里的 sessionId 比对 —— 多一个字符就两头都对不上。
std::string SanitizeCreatorSessionId(const std::string& text) {
    std::string session;
    session.reserve(text.size());
    for (const char ch : text) {
        if (IsUsableSessionChar(ch)) session += ch;
    }
    return session;
}

} // namespace miaodesk::creator
