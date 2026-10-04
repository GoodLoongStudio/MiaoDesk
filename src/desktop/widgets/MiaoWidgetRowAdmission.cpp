#include "miaodesk/MiaoWidgetRowAdmission.h"

namespace miaodesk::widget_row {

const char* ToString(WidgetRowOutcome outcome) noexcept {
    switch (outcome) {
        case WidgetRowOutcome::Admit: return "admit";
        case WidgetRowOutcome::RejectUnknownKind: return "reject-unknown-kind";
        case WidgetRowOutcome::RejectMissingSource: return "reject-missing-source";
        case WidgetRowOutcome::RejectUnsafeSource: return "reject-unsafe-source";
        case WidgetRowOutcome::SupersedeDisabled: return "supersede-disabled";
        case WidgetRowOutcome::KeepExisting: return "keep-existing";
    }
    return "unknown";
}

WidgetRowOutcome DecideWidgetRowAdmission(const WidgetRowFacts& facts) noexcept {
    // 1. Kind 不认识 —— 版本差异（旧构建写过本构建不认识的类型）。
    if (!facts.kindRecognised) return WidgetRowOutcome::RejectUnknownKind;
    // 2. Source 空 —— 配置损坏。
    if (!facts.sourcePresent) return WidgetRowOutcome::RejectMissingSource;
    // 3. Source 不受认 —— 这是安全边界（不接受任意路径），单独一类：
    //    它和"损坏"对用户是两回事，该修配置 vs 该看有没有人改过文件。
    if (!facts.sourceValid) return WidgetRowOutcome::RejectUnsafeSource;

    // 4. 撞 singleton 键。只有 Native 预设有"同一块屏只一个"的约束；
    //    Content 组件键为空，从不走到这里（多开是允许的）。
    if (facts.hasSingletonKey && facts.singletonClashes) {
        // 启用状态优先：库里那条禁用着而这一行启用着 → 用新的替换。
        // 用户主动启用过一次，那是最新的意图，不该被一条更早的禁用行盖掉。
        if (!facts.existingEnabled && facts.candidateEnabled) return WidgetRowOutcome::SupersedeDisabled;
        // 其余全部保留库里那条：同状态时先出现者胜。
        // 两边都启用 → 第一条胜（它先到，加载顺序即用户看到顺序）；
        // 库里启用而这条禁用 → 启用胜；两边都禁用 → 第一条胜。
        return WidgetRowOutcome::KeepExisting;
    }

    return WidgetRowOutcome::Admit;
}

bool WidgetRowOutcomeMutatesStore(WidgetRowOutcome outcome) noexcept {
    return outcome == WidgetRowOutcome::Admit || outcome == WidgetRowOutcome::SupersedeDisabled;
}

std::wstring ExplainWidgetRowOutcome(WidgetRowOutcome outcome) {
    switch (outcome) {
        case WidgetRowOutcome::Admit: return {};
        case WidgetRowOutcome::RejectUnknownKind:
            return L"这条组件记录的类型本版本不认识，已跳过（升级后旧类型可能不再支持）";
        case WidgetRowOutcome::RejectMissingSource:
            return L"这条组件记录缺少来源，已跳过（配置文件可能损坏）";
        case WidgetRowOutcome::RejectUnsafeSource:
            return L"这条组件记录的来源不在受认范围内，已跳过";
        case WidgetRowOutcome::SupersedeDisabled:
            return L"同一块屏幕上已有这个组件的禁用记录，已用启用的这一条替换";
        case WidgetRowOutcome::KeepExisting:
            return L"同一块屏幕上已有这个组件的记录，已保留先出现的那一条";
    }
    return {};
}

} // namespace miaodesk::widget_row
