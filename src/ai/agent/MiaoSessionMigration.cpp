#include "miaodesk/MiaoSessionMigration.h"

#include <algorithm>

namespace miaodesk::session_migration {
namespace {

std::wstring LowerText(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(::towlower(ch));
    });
    return value;
}

// 末尾斜杠不吃掉的话,"https://x/v1" 与 "https://x/v1/" 会被当成两个服务,
// 于是同一台机器上的同一次升级被误判成 GenuineChange —— 会话照样丢。
std::wstring NormalizeBase(std::wstring value) {
    while (value.size() > 1 && value.back() == L'/') value.pop_back();
    return value;
}

} // namespace

const wchar_t* MigrationKindName(MigrationKind kind) noexcept {
    switch (kind) {
    case MigrationKind::SourcePromotion: return L"SourcePromotion";
    case MigrationKind::GenuineChange: return L"GenuineChange";
    case MigrationKind::NoChange: break;
    }
    return L"NoChange";
}

std::uint64_t SessionIdentityHash(const LegacySessionIdentity& identity) noexcept {
    const std::wstring key = LowerText(identity.profileId) + L"\n" + LowerText(identity.providerId) +
                             L"\n" + LowerText(identity.baseUrl) + L"\n" + identity.endpoint + L"\n" +
                             identity.model;
    std::uint64_t hash = 1469598103934665603ull;
    for (wchar_t ch : key) {
        hash ^= static_cast<std::uint16_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

MigrationPlan PlanSessionMigration(const LegacySessionIdentity& before,
                                   const LegacySessionIdentity& after) noexcept {
    MigrationPlan plan;
    plan.fromHash = SessionIdentityHash(before);
    plan.toHash = SessionIdentityHash(after);
    if (plan.fromHash == plan.toHash) {
        plan.kind = MigrationKind::NoChange;
        plan.carrySession = false;
        plan.reason = L"会话身份没变";
        return plan;
    }

    // 只有"服务地址与模型都还是同一套"才算来源提升。baseUrl 归一化之后比,
    // endpoint 与 model 逐字比 —— 它们本来就是逐字进 URL 的。
    const bool sameEndpoint = NormalizeBase(before.baseUrl) == NormalizeBase(after.baseUrl) &&
                              before.endpoint == after.endpoint && before.model == after.model;
    if (sameEndpoint) {
        plan.kind = MigrationKind::SourcePromotion;
        plan.carrySession = true;
        plan.reason = L"配置来源变了(旧文件 → API 配置中心),但服务地址与模型是同一套;"
                      L"用户没有改任何东西,会话必须跟着搬,否则升级会把历史对话变成不可达的字节";
        return plan;
    }

    plan.kind = MigrationKind::GenuineChange;
    plan.carrySession = false;
    plan.reason = L"服务地址或模型真的变了,新会话是对的;旧会话留在盘上别动";
    return plan;
}

} // namespace miaodesk::session_migration
