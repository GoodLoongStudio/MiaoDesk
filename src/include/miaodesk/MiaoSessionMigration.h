#pragma once

// P0-09:旧配置升级不许丢会话。
//
// 这条链是活的,而且用户看不见:
//
//   1. 用户用旧的 `/provider http://x/v1 model-y` 配好 AI。config_.profileId 是空的,
//      会话以 FNV-1a(profileId, providerId, baseUrl, endpoint, model) 的哈希落在
//      `<StateRoot>/l3-sessions/<hex>.bin`。
//   2. 后来他在 AI 窗口的 API 下拉里选了一个中央 profile(ConversationPanelImpl.inc
//      调 SetProfileId(selected.id))。ReloadConfig 从 api-profiles.ini 填 config_ ——
//      于是 profileId 从空变成 "main",**哈希随之改变**。
//   3. ReloadConfig 里 `if (profile.configured) RetireLegacyShadowState();`
//      把 model-settings.json(也就是 L3Agent 自己的配置文件)删掉。
//
// 结果:旧的 .bin 还在盘上,但再也没有人能算出它的路径 —— 升级把用户累积的对话
// 变成了不可达的字节。P0-09 的验收原话是"旧配置升级不丢 API profile、内容库、
// **会话**、组件布局"。
//
// 现有自测完全没有覆盖这条路:L3PersistenceSelfTest 只测 legacy 自己一轮和
// 中央 profile 一轮,没有"从 legacy 换到中央"。
//
// 这个模块只做判定,不碰盘、不碰 Credential Manager:换来源与真换配置是两件事,
// 取决于两组字段,所以在本机就能真跑、真门。
#include <cstdint>
#include <string>

namespace miaodesk::session_migration {

// 迁移前那一份的身份(来自即将被退休的旧配置文件)。
// 与 L3Agent 的 ModelConfig 同构,但只留判定要用的五个字段 ——
// 引 L3Agent.h 会拖进 winhttp.h。
struct LegacySessionIdentity {
    std::wstring profileId;      // 旧路径下恒为空
    std::wstring providerId;
    std::wstring baseUrl;
    std::wstring endpoint;
    std::wstring model;
};

// 换配置的两种性质,必须分开。
enum class MigrationKind {
    // 来源变了,但服务地址与模型是同一套 —— 用户什么都没改,
    // 只是从此由 API 配置中心负责。**会话必须跟着搬。**
    SourcePromotion,
    // 用户真的换了 endpoint 或模型。新会话是对的,旧会话留在盘上别动。
    GenuineChange,
    // 身份没变,不需要搬。
    NoChange,
};

const wchar_t* MigrationKindName(MigrationKind kind) noexcept;

struct MigrationPlan {
    MigrationKind kind{MigrationKind::NoChange};
    // 是否需要把会话文件搬到新身份下。
    bool carrySession{};
    // 旧身份与新身份的会话路径键(FNV-1a,与 L3Agent::SessionHash 同算法)。
    std::uint64_t fromHash{};
    std::uint64_t toHash{};
    // 给日志/诊断用的一句话:这次到底是什么变化。
    std::wstring reason;
};

// L3Agent::SessionHash 的同一份实现。**必须逐字一致** ——
// 两处各写一遍时,改一边不改另一边,算出的路径就对不上,而那时搬运会静默失败。
// 两侧的相等性由 MigrationHashParityTest 钉住(见 tests/SessionMigrationTest.cpp)。
std::uint64_t SessionIdentityHash(const LegacySessionIdentity& identity) noexcept;

// 判一次:这次换配置要不要把会话搬过去。
MigrationPlan PlanSessionMigration(const LegacySessionIdentity& before,
                                   const LegacySessionIdentity& after) noexcept;

} // namespace miaodesk::session_migration
