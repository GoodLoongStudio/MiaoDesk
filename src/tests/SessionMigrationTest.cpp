// P0-09:旧配置升级不许丢会话。
//
// 这个文件钉住一个**会让用户历史对话变成不可达字节**的链:
//
//   1. 用户用旧的 `/provider http://x/v1 model-y` 配好 AI,profileId 为空,
//      会话落在 `<StateRoot>/l3-sessions/<hex(哈希)>.bin`;
//   2. 他在 AI 窗口的 API 下拉里选一个中央 profile(ConversationPanelImpl.inc
//      调 SetProfileId(selected.id))→ ReloadConfig 从 api-profiles.ini 填 config_
//      → profileId 从空变成 "main" → **哈希变;
//   3. ReloadConfig 里 RetireLegacyShadowState() 把 model-settings.json
//      (L3Agent 自己的配置文件)删掉 —— 于是连旧路径都没法再算出来。
//
// 旧 .bin 还在盘上,但再也没有人找得到它。P0-09 验收:"旧配置升级不丢 … 会话"。
// L3PersistenceSelfTest 没有覆盖这条路(只有 legacy 一轮、profile 一轮,没有"换")。
//
// 判定与 Node/管道/WinHTTP 全都无关,只取决于两组字段,所以本机真的能跑。
#include "miaodesk/MiaoSessionMigration.h"

#include <cstdio>
#include <string>

namespace miaodesk::session_migration {
namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL  %s\n", what.c_str());
    }
}

// 反空洞自检:喂一个**明知**该被搬的升级,和一个明知不该搬的真换配置。
// 上面每组断言都绑"当前是对的"某一个具体输入,而一个恒返回 carrySession=true
// 的函数在它们上面同样全绿 —— 一个永远失败的门与一道好门在通过时一模一样。
bool VerdictStillMoves() {
    LegacySessionIdentity legacy;
    legacy.baseUrl = L"https://api.example.com/v1";
    legacy.model = L"model-y";
    LegacySessionIdentity promoted = legacy;
    promoted.profileId = L"main";
    LegacySessionIdentity changed = legacy;
    changed.model = L"model-z";
    return PlanSessionMigration(legacy, promoted).carrySession &&
           !PlanSessionMigration(legacy, changed).carrySession;
}

} // namespace
} // namespace miaodesk::session_migration

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::session_migration;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:判不出该搬与不该搬\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:来源提升要搬,真换配置不搬\n");
    ++g_checks;

    // ---- 1. 哈希与 L3Agent::SessionHash 逐字一致 ----
    // 这一段是把算法钉住,不是为了覆盖率:两处各写一遍时,改一边不改另一边,
    // 搬运会静默失败(算出的路径对不上),而那比不搬更难查。
    {
        LegacySessionIdentity id;
        id.providerId = L"openai-compatible";
        id.baseUrl = L"https://api.example.com/v1";
        id.model = L"model-y";
        // 与 L3Agent 的 SessionHash 是**同一个函数**(L3Agent.cpp 转一手调到
        // session_migration::SessionIdentityHash),所以钉住这里就是钉住两边。
        // 期望值**是磁盘上的格式**,不是随便一个数。
        // `<StateRoot>/l3-sessions/<这个十六进制>.bin` 就是用户的历史对话。
        // 改哈希常量或改参与字段,等于把每个老用户的会话全部变成不可达字节 ——
        // 这正是本轮修的那个缺陷的反面。所以这里钉死具体数值。
        Check(SessionIdentityHash(id) == 0x0f35aa0fcbfe58bdull,
              "哈希的具体数值被钉住:它决定磁盘上的会话文件名(改它=全员历史对话不可达)");
        LegacySessionIdentity same = id;
        Check(SessionIdentityHash(same) == SessionIdentityHash(id), "同身份同哈希");
        LegacySessionIdentity other = id;
        other.profileId = L"main";
        Check(SessionIdentityHash(other) != SessionIdentityHash(id),
              "profileId 由空变成 'main' 时哈希必须变 —— 这正是会话丢失的机制");
        Check(SessionIdentityHash(id) != SessionIdentityHash(other), "换个方向也一样");
    }

    // ---- 2. 那个缺陷:只换来源,什么都不该丢 ----
    {
        LegacySessionIdentity legacy;
        legacy.providerId = L"openai-compatible";
        legacy.baseUrl = L"https://api.deepseek.com/v1";
        legacy.model = L"deepseek-chat";

        LegacySessionIdentity promoted = legacy;
        promoted.profileId = L"main";           // ← 唯一的变化

        const auto plan = PlanSessionMigration(legacy, promoted);
        Check(plan.kind == MigrationKind::SourcePromotion, "只换来源判为来源提升");
        Check(plan.carrySession, "★ 会话必须跟着搬(P0-09)");
        Check(plan.fromHash != plan.toHash, "搬之前哈希确实不同(所以不搬就会丢)");
        Check(plan.reason.find(L"同一套") != std::wstring::npos, "原因说清是同一套服务");
        // 第二个数值:另一组常见配置。两个都钉住,少一个不足以证明字段没被增删。
        LegacySessionIdentity anthropic;
        anthropic.providerId = L"anthropic";
        anthropic.baseUrl = L"https://api.anthropic.com";
        anthropic.endpoint = L"/messages";
        anthropic.model = L"claude";
        Check(SessionIdentityHash(anthropic) == 0x8e5a5b9113f29b04ull,
              "第二组配置的哈希数值也被钉住");
        Check(plan.reason.find(L"不可达") != std::wstring::npos, "原因说清不搬的后果");
    }

    // ---- 3. 用户真的换了配置:新会话是对的,旧的别动 ----
    {
        LegacySessionIdentity legacy;
        legacy.providerId = L"openai-compatible";
        legacy.baseUrl = L"https://api.deepseek.com/v1";
        legacy.model = L"deepseek-chat";

        LegacySessionIdentity otherModel = legacy;
        otherModel.profileId = L"main";
        otherModel.model = L"another-model";
        const auto modelPlan = PlanSessionMigration(legacy, otherModel);
        Check(modelPlan.kind == MigrationKind::GenuineChange, "换了模型判为真换配置");
        Check(!modelPlan.carrySession, "真换配置不搬会话");

        LegacySessionIdentity otherHost = legacy;
        otherHost.profileId = L"main";
        otherHost.baseUrl = L"https://other.example/v1";
        const auto hostPlan = PlanSessionMigration(legacy, otherHost);
        Check(hostPlan.kind == MigrationKind::GenuineChange, "换了主机判为真换配置");
        Check(!hostPlan.carrySession, "换主机不搬会话 —— 搬到别的服务上去更糟");

        LegacySessionIdentity otherEndpoint = legacy;
        otherEndpoint.profileId = L"main";
        otherEndpoint.endpoint = L"/messages";
        Check(PlanSessionMigration(legacy, otherEndpoint).kind == MigrationKind::GenuineChange,
              "换了端点路径也算真换配置");
    }

    // ---- 4. 没有变化时什么都不做 ----
    {
        LegacySessionIdentity id;
        id.profileId = L"main";
        id.providerId = L"anthropic";
        id.baseUrl = L"https://api.anthropic.com";
        id.endpoint = L"/messages";
        id.model = L"claude";
        const auto plan = PlanSessionMigration(id, id);
        Check(plan.kind == MigrationKind::NoChange, "完全相同的身份没有任何变化");
        Check(!plan.carrySession, "没有变化就不搬");
        Check(plan.fromHash == plan.toHash, "两个哈希相等");
    }

    // ---- 5. 末尾斜杠不改变"同一套服务"的结论 ----
    // 不归一化的话,"https://x/v1" 与 "https://x/v1/" 会被当成两个服务,
    // 于是同一台机器上的同一次升级被误判成真换配置 —— 会话照样丢。
    {
        LegacySessionIdentity legacy;
        legacy.baseUrl = L"https://api.example.com/v1";
        legacy.model = L"model-y";
        LegacySessionIdentity promoted = legacy;
        promoted.profileId = L"main";
        promoted.baseUrl = L"https://api.example.com/v1/";
        const auto plan = PlanSessionMigration(legacy, promoted);
        Check(plan.kind == MigrationKind::SourcePromotion,
              "baseUrl 只差一个末尾斜杠仍是同一台服务");
        Check(plan.carrySession, "所以会话照样要搬");
    }

    // ---- 6. profileId 相同而其余相同:身份一致(幂等)----
    {
        LegacySessionIdentity a;
        a.profileId = L"main";
        a.providerId = L"openai-compatible";
        a.baseUrl = L"https://api.example.com/v1";
        a.model = L"model-y";
        const auto plan = PlanSessionMigration(a, a);
        Check(plan.kind == MigrationKind::NoChange, "重复判定收敛");
        Check(!plan.carrySession, "收敛到不搬");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n升级迁移判定:全部 %d 项通过\n", g_checks);
    return 0;
}
