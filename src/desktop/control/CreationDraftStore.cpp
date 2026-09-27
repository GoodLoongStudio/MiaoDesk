#include "miaodesk/CreationDraftStore.h"

namespace miaodesk::creator {
namespace {

// 行式 key=value 的转义。值里可能出现换行(需求正文是用户自己写的话)与分隔符,
// 不转义的话一段多行需求会把后面的行解析成别的键 —— 表现为"草稿恢复出来一半,
// 而且改不掉"。\ 先转,否则 \n 会被二次解释。
std::string Escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '=': out += "\\e"; break;
        case ',': out += "\\c"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::string Unescape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out += value[i];
            continue;
        }
        const char next = value[++i];
        switch (next) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 'e': out += '='; break;
        case 'c': out += ','; break;
        default: out += next; break;
        }
    }
    return out;
}

bool ReadUInt(const std::string& value, std::uint64_t* out) {
    if (value.empty()) return false;
    std::uint64_t number = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return false;
        number = number * 10 + static_cast<std::uint64_t>(ch - '0');
    }
    *out = number;
    return true;
}

std::string Join(const std::vector<std::string>& items, char separator) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += separator;
        out += Escape(items[i]);
    }
    return out;
}

std::vector<std::string> Split(std::string_view value, char separator) {
    std::vector<std::string> items;
    std::string current;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            current += value[i];
            current += value[++i];
            continue;
        }
        if (value[i] == separator) {
            items.push_back(Unescape(current));
            current.clear();
            continue;
        }
        current += value[i];
    }
    if (!current.empty() || !items.empty()) items.push_back(Unescape(current));
    return items;
}

} // namespace

bool CreationDraft::Sufficient() const noexcept {
    return KindValid() && !goal.empty();
}

bool CreationDraft::KindValid() const noexcept {
    return kind == ContentCreatorKind::Wallpaper || kind == ContentCreatorKind::Widget;
}

std::string CreationDraft::MissingReason() const {
    if (!KindValid()) return "制作类型(壁纸 / 组件)";
    if (goal.empty()) return "你的目标(想要什么样的桌面效果)";
    return {};
}

const char* ToString(DraftRestoreAction action) noexcept {
    switch (action) {
    case DraftRestoreAction::StartFresh: return "StartFresh";
    case DraftRestoreAction::RestoreAndResume: return "RestoreAndResume";
    case DraftRestoreAction::RestoreAsDraft: return "RestoreAsDraft";
    case DraftRestoreAction::AskUserAgain: return "AskUserAgain";
    case DraftRestoreAction::RejectAmbiguous: return "RejectAmbiguous";
    }
    return "Unknown";
}

std::string SerializeCreationDraft(const CreationDraft& draft) {
    std::string out = "draft=1\n";
    out += "sessionId=" + Escape(draft.sessionId) + "\n";
    out += "epoch=" + std::to_string(draft.epoch) + "\n";
    out += "turnId=" + std::to_string(draft.turnId) + "\n";
    out += "briefRevision=" + std::to_string(draft.briefRevision) + "\n";
    out += "kind=" + std::to_string(static_cast<int>(draft.kind)) + "\n";
    out += "goal=" + Escape(draft.goal) + "\n";
    out += "visualDirection=" + Escape(draft.visualDirection) + "\n";
    out += "aspectOrSize=" + Escape(draft.aspectOrSize) + "\n";
    out += "dataAndInteraction=" + Escape(draft.dataAndInteraction) + "\n";
    out += "materialSource=" + Escape(draft.materialSource) + "\n";
    out += "capabilities=" + Join(draft.allowedCapabilities, ',') + "\n";
    for (const auto& parameter : draft.userParameters) {
        out += "parameter=" + Escape(parameter.first) + "," + Escape(parameter.second) + "\n";
    }
    out += "candidateDigest=" + Escape(draft.candidateDigest) + "\n";
    out += "candidateSummary=" + Escape(draft.candidateSummary) + "\n";
    out += "candidateRevision=" + std::to_string(draft.candidateRevision) + "\n";
    out += "cancelRequested=" + std::string(draft.cancelRequested ? "1" : "0") + "\n";
    out += "savedAtMs=" + std::to_string(draft.savedAtMs) + "\n";
    return out;
}

bool ParseCreationDraft(std::string_view text, CreationDraft* out, std::string* error) {
    if (out == nullptr) {
        if (error) *error = "没有输出位置";
        return false;
    }
    CreationDraft draft;
    bool sawVersion = false;
    bool sawSession = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) {
            if (end == text.size()) break;
            continue;
        }
        // 第一个 '=' 是分隔符:值本身可能含 '='(转义过,但键名里没有)。
        const std::size_t equal = line.find('=');
        if (equal == std::string_view::npos) {
            if (error) *error = "有一行不是 key=value:" + std::string(line);
            return false;
        }
        const std::string_view key = line.substr(0, equal);
        const std::string_view raw = line.substr(equal + 1);
        if (key == "draft") {
            if (Unescape(raw) != "1") {
                if (error) *error = "草稿版本不是 1";
                return false;
            }
            sawVersion = true;
        } else if (key == "sessionId") {
            draft.sessionId = Unescape(raw);
            sawSession = !draft.sessionId.empty();
        } else if (key == "epoch" || key == "turnId" || key == "briefRevision" ||
                   key == "candidateRevision" || key == "savedAtMs") {
            std::uint64_t number = 0;
            if (!ReadUInt(std::string(raw), &number)) {
                if (error) *error = std::string(key) + " 不是十进制数:" + std::string(raw);
                return false;
            }
            if (key == "epoch") draft.epoch = number;
            else if (key == "turnId") draft.turnId = number;
            else if (key == "briefRevision") draft.briefRevision = static_cast<std::uint32_t>(number);
            else if (key == "candidateRevision")
                draft.candidateRevision = static_cast<std::uint32_t>(number);
            else draft.savedAtMs = number;
        } else if (key == "kind") {
            std::uint64_t number = 0;
            if (!ReadUInt(std::string(raw), &number) || number > 2) {
                if (error) *error = "kind 不是 0/1/2:" + std::string(raw);
                return false;
            }
            draft.kind = static_cast<ContentCreatorKind>(number);
        } else if (key == "goal") {
            draft.goal = Unescape(raw);
        } else if (key == "visualDirection") {
            draft.visualDirection = Unescape(raw);
        } else if (key == "aspectOrSize") {
            draft.aspectOrSize = Unescape(raw);
        } else if (key == "dataAndInteraction") {
            draft.dataAndInteraction = Unescape(raw);
        } else if (key == "materialSource") {
            draft.materialSource = Unescape(raw);
        } else if (key == "capabilities") {
            if (!raw.empty()) draft.allowedCapabilities = Split(raw, ',');
        } else if (key == "parameter") {
            const std::vector<std::string> parts = Split(raw, ',');
            if (parts.size() != 2) {
                if (error) *error = "parameter 行不是 name,value 两段";
                return false;
            }
            draft.userParameters.emplace_back(parts[0], parts[1]);
        } else if (key == "candidateDigest") {
            draft.candidateDigest = Unescape(raw);
        } else if (key == "candidateSummary") {
            draft.candidateSummary = Unescape(raw);
        } else if (key == "cancelRequested") {
            const std::string flag = Unescape(raw);
            if (flag != "0" && flag != "1") {
                if (error) *error = "cancelRequested 不是 0/1:" + flag;
                return false;
            }
            draft.cancelRequested = flag == "1";
        }
        // 认不出的键不报错:新版本加字段时旧版本还能读。报错会让"加一个字段"
        // 变成"所有人的草稿全失效"。
        if (end == text.size()) break;
    }
    // 版本与归属是两件必须有的东西。缺版本说明这不是草稿(可能是一段别的配置),
    // 缺 sessionId 说明它无从归属 —— 两种都不能当成"一份空草稿"恢复,
    // 那会让用户看到一个全新窗口,而他记得自己写了一整段需求。
    if (!sawVersion) {
        if (error) *error = "没有 draft=1 版本行,这不是一份草稿";
        return false;
    }
    if (!sawSession) {
        if (error) *error = "草稿没有 sessionId,无法归属";
        return false;
    }
    *out = std::move(draft);
    return true;
}

DraftRestorePlan PlanDraftRestore(std::string_view savedDraft, std::string_view sessionId) {
    DraftRestorePlan plan;
    if (savedDraft.empty()) {
        plan.reason = "没有草稿,从新的一份开始。";
        return plan;
    }
    std::string error;
    if (!ParseCreationDraft(savedDraft, &plan.draft, &error)) {
        plan.action = DraftRestoreAction::RejectAmbiguous;
        plan.reason = "这份草稿接不上:" + error + "。不当成新草稿处理 —— "
                      "那样用户会看到一个全新窗口,而他记得自己写了一整段需求。";
        return plan;
    }
    if (plan.draft.sessionId != sessionId) {
        plan.action = DraftRestoreAction::RejectAmbiguous;
        plan.reason = "这份草稿属于 " + plan.draft.sessionId + ",不是当前这个作品(" +
                      std::string(sessionId) + ")。";
        return plan;
    }
    if (plan.draft.cancelRequested) {
        // 用户取消过。恢复它只是为了让他看见自己上次写了什么,不自动接着做 ——
        // 取消之后自己动起来,是他取消没生效。
        plan.action = DraftRestoreAction::RestoreAsDraft;
        plan.reason = "这一份上次被你取消了,只把内容显示回来,不自动继续。";
        return plan;
    }
    const std::string missing = plan.draft.MissingReason();
    if (!missing.empty()) {
        // 缺哪一项要说出来。"需求不足"四个字等于让用户猜自己上次漏了什么。
        plan.action = DraftRestoreAction::AskUserAgain;
        plan.reason = "这一份草稿还缺:" + missing + "。重开窗口要再问一次。";
        return plan;
    }
    plan.action = DraftRestoreAction::RestoreAndResume;
    plan.reason = "草稿完整(" + std::to_string(plan.draft.briefRevision) + " 版需求)可以直接接上,"
                  "不必再问一遍。";
    return plan;
}

} // namespace miaodesk::creator
