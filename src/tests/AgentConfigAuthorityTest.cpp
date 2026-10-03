// P0-07 AI 侧:一次请求用的 URL 和它用的凭据,必须是同一个来源。
//
// 这个文件钉住一个**活着的**缺陷。用户在 AI 对话里敲:
//
//     /provider https://别的服务/v1 某个模型
//
// `L3Agent::TryHandleLocal` 就地改 `config_.baseUrl`,而 `config_.profileId`
// 一个字符都没动 —— `ReloadConfig` 之外没有任何代码写它。下一次请求:
//
//     URL      = https://别的服务/v1/...                ← 用户刚敲的
//     API Key  = MiaoDesk/ApiProfile/<原 profile 的 id>  ← 原来那个 profile 的 Key
//
// 一把签给 A 服务的 Key,被发到 B 服务上去。`/key` 那一侧更糟:它把用户敲的新 Key
// 写回 `MiaoDesk/ApiProfile/<原 profile>`,**覆盖了那个 profile 自己存的 Key**,
// 而设置页里一个字都不会提,其它每个用这个 profile 的 AI 窗口下一轮就都用上它。
//
// 判定之所以能在这里跑,是因为它不碰 Credential Manager、不碰盘:
// "URL 和凭据是不是同源"只取决于三个字符串加一个来源标记。
//
// 判据刻意把 LocalHost 与 LocalPath 分开:换主机才让凭据对不上,只换端点路径
// (主机没变)时 profile 的 Key 仍然是发给同一台主机的。合成一类会让 `/endpoint`
// 被误判成危险。
#include "miaodesk/MiaoAgentConfigAuthority.h"

#include <cstdio>
#include <string>

namespace miaodesk::agent_config {
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

// 判定函数的结论是不是真的会动。
//
// 这一段是反空洞自检:上面每一组断言绑的都是"当前是对的"某个具体输入,而一个
// 永远返回 bound=true 的函数在那些输入上同样全绿。喂进去的四个合成输入
// **每一个都明知该被抓到**,四个都抓不到就说明判定已经死了。
bool BindingVerdictStillMoves() {
    bool ok = true;
    auto probe = [](const wchar_t* profileId, RequestTargetOwner owner) {
        AgentRequestIdentity identity;
        identity.profileId = profileId;
        identity.baseUrl = L"https://other.example/v1";
        identity.endpoint = L"/chat/completions";
        return BindRequestCredential(identity, owner);
    };

    const auto unbound = probe(L"deepseek-main", RequestTargetOwner::LocalHost);
    ok = ok && !unbound.bound && !unbound.reason.empty();
    ok = ok && unbound.credentialTarget != unbound.safeCredentialTarget;

    // 主机被本地命令换过、而 profile 身份已按修法丢掉 —— 必须回到同源。
    const auto rebound = probe(L"", RequestTargetOwner::LocalHost);
    ok = ok && rebound.bound;

    // 只换路径 —— 同源,不该误报。
    const auto pathOnly = probe(L"deepseek-main", RequestTargetOwner::LocalPath);
    ok = ok && pathOnly.bound;

    // 一切来自 profile —— 同源。
    const auto fromProfile = probe(L"deepseek-main", RequestTargetOwner::Profile);
    ok = ok && fromProfile.bound;

    // 反向也要动:一个什么都不看、恒返回 false 的函数,得在这三个输入上露出来。
    ok = ok && rebound.bound && pathOnly.bound && fromProfile.bound;
    return ok;
}

} // namespace
} // namespace miaodesk::agent_config

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::agent_config;

    if (!BindingVerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:判定抓不住明知该抓的输入\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:抓得住'换过主机却仍用 profile 的 Key',也不误报只换路径\n");
    ++g_checks;

    // ---- 1. URL 拼装:与 L3Agent::CurrentApiUrl 逐条相同 ----
    {
        AgentRequestIdentity base;
        base.baseUrl = L"https://api.example.com/v1";
        base.endpoint = L"/chat/completions";
        Check(BuildAgentRequestUrl(base) == L"https://api.example.com/v1/chat/completions",
              "baseUrl + 相对端点直接拼");

        AgentRequestIdentity noSlash = base;
        noSlash.endpoint = L"chat/completions";
        Check(BuildAgentRequestUrl(noSlash) == L"https://api.example.com/v1/chat/completions",
              "端点没有前导斜杠时补一个(不拼出 /v1chat/...)");

        // 末尾斜杠:多段都要吃掉。拼出 // 的 URL 在部分网关上直接 404。
        AgentRequestIdentity slashes = base;
        slashes.baseUrl = L"https://api.example.com/v1///";
        slashes.endpoint = L"/chat/completions";
        Check(BuildAgentRequestUrl(slashes) == L"https://api.example.com/v1/chat/completions",
              "baseUrl 末尾多个斜杠吃掉到最多一个");

        AgentRequestIdentity keepRoot = base;
        keepRoot.baseUrl = L"https://api.example.com/";
        Check(BuildAgentRequestUrl(keepRoot) == L"https://api.example.com/chat/completions",
              "根路径保留那一个斜杠,不吃成 'https:/'");

        // 那条 `size > 1` 守卫的唯一可观察之处:baseUrl 只剩斜杠时不许被掏空。
        // 掏空了拼出来的是相对路径,WinHTTP 会拿着它去猜主机,报错指向的不是真问题。
        AgentRequestIdentity allSlashes = base;
        allSlashes.baseUrl = L"//";
        Check(BuildAgentRequestUrl(allSlashes) == L"//chat/completions",
              "baseUrl 全是斜杠时留下一个,不退化成相对路径");

        AgentRequestIdentity dash = base;
        dash.endpoint = L"-";
        Check(BuildAgentRequestUrl(dash) == L"https://api.example.com/v1",
              "endpoint 为 '-' 表示不拼路径,只给 baseUrl");

        AgentRequestIdentity emptyEndpoint = base;
        emptyEndpoint.endpoint = L"";
        Check(BuildAgentRequestUrl(emptyEndpoint) == L"https://api.example.com/v1",
              "端点为空时只给 baseUrl");

        // 绝对 URL 的端点原样返回:它是历史写法, profile 的 endpoint 字段就存过它。
        AgentRequestIdentity absolute = base;
        absolute.endpoint = L"https://other.example/v1/chat/completions";
        Check(BuildAgentRequestUrl(absolute) == L"https://other.example/v1/chat/completions",
              "端点本身是绝对 URL 时原样返回,不与 baseUrl 拼");

        AgentRequestIdentity absoluteSlash = base;
        absoluteSlash.endpoint = L"/https://other.example/v1/chat/completions";
        Check(BuildAgentRequestUrl(absoluteSlash) == L"https://other.example/v1/chat/completions",
              "绝对 URL 端点多带一个前导斜杠也认(历史写法)");

        AgentRequestIdentity spaces = base;
        spaces.baseUrl = L"  https://api.example.com/v1  ";
        spaces.endpoint = L"  /chat/completions  ";
        Check(BuildAgentRequestUrl(spaces) == L"https://api.example.com/v1/chat/completions",
              "两端空白先掐掉再拼(配置文件里带空格是常事)");

        AgentRequestIdentity emptyBase;
        emptyBase.baseUrl = L"";
        Check(BuildAgentRequestUrl(emptyBase).empty(), "baseUrl 为空时返回空 URL(不拼出孤零零的端点)");
    }

    // ---- 2. 凭据槽:两个位置,一个都不能猜错 ----
    {
        AgentRequestIdentity withProfile;
        withProfile.profileId = L"deepseek-main";
        Check(AgentCredentialTarget(withProfile) == L"MiaoDesk/ApiProfile/deepseek-main",
              "有 profile 时凭据槽是这个 profile 自己的那一个");

        AgentRequestIdentity withoutProfile;
        withoutProfile.profileId = L"";
        Check(AgentCredentialTarget(withoutProfile) == L"MiaoDesk/ModelApiKey",
              "没有 profile 时用旧槽");

        // 槽名分别钉住字面量。上面两条用的是同一套常量,只改常量不改字面量
        // 两条断言会一起变绿 —— 那正是 WPRO-01 上踩过的那个坑
        // (改 kWidgetLayoutDefaultX 让 104 项断言全绿)。
        Check(std::wstring(kProfileCredentialPrefix) == L"MiaoDesk/ApiProfile/",
              "profile 凭据槽前缀就是 ApiRuntimeProfile 读的那一个(换名前缀会让 Key 全部静默换槽)");
        Check(std::wstring(kLegacyCredentialTarget) == L"MiaoDesk/ModelApiKey",
              "旧凭据槽名没变");

        AgentRequestIdentity caseProfile;
        caseProfile.profileId = L"DeepSeek-Main";
        Check(AgentCredentialTarget(caseProfile) == L"MiaoDesk/ApiProfile/DeepSeek-Main",
              "profile id 原样进槽名,不折叠大小写(LoadById 比较时才忽略大小写)");
    }

    // ---- 3. 那个缺陷:换过主机,凭据却还是原来 profile 的 ----
    {
        AgentRequestIdentity stale;
        stale.profileId = L"deepseek-main";
        stale.baseUrl = L"https://other.example/v1";
        stale.endpoint = L"/chat/completions";

        const auto binding = BindRequestCredential(stale, RequestTargetOwner::LocalHost);
        Check(!binding.bound, "本地命令换过主机而 profile 身份还挂着 → 判为不同源");
        Check(binding.credentialTarget == L"MiaoDesk/ApiProfile/deepseek-main",
              "当前代码确实会去读原 profile 的凭据槽");
        Check(binding.safeCredentialTarget == L"MiaoDesk/ModelApiKey",
              "给出的安全槽是旧槽(主机已不是 profile 的那台)");
        Check(binding.url == L"https://other.example/v1/chat/completions",
              "URL 就是用户刚敲的那台主机");
        Check(binding.reason.find(L"deepseek-main") != std::wstring::npos,
              "原因里点出是哪一个 profile 的 Key(诊断要能指到人)");
        Check(binding.reason.find(L"other.example") != std::wstring::npos,
              "原因里点出这把 Key 会被发去哪");

        // 对照组:同样的三个字段,来源换成 profile,就必须同源。少了这一条,
        // 上面那个 !bound 可能只是"只要是 LocalHost 就不分青红皂白地报"。
        const auto fromProfile = BindRequestCredential(stale, RequestTargetOwner::Profile);
        Check(fromProfile.bound && fromProfile.reason.empty(),
              "同样的身份,来源是 profile 时同源且不报原因");
        Check(fromProfile.credentialTarget == fromProfile.safeCredentialTarget,
              "同源时不需要另给一个安全槽");
    }

    // ---- 4. 只换端点路径不算换主机 ----
    {
        AgentRequestIdentity pathOnly;
        pathOnly.profileId = L"deepseek-main";
        pathOnly.baseUrl = L"https://api.deepseek.com/v1";
        pathOnly.endpoint = L"/chat/completions";
        const auto binding = BindRequestCredential(pathOnly, RequestTargetOwner::LocalPath);
        Check(binding.bound, "只换端点路径(主机没变)时仍算同源 —— profile 的 Key 还是发给同一台主机");
        Check(binding.credentialTarget == L"MiaoDesk/ApiProfile/deepseek-main",
              "同源时凭据槽不动,还是 profile 那一个");
    }

    // ---- 5. 修法:丢掉 profile 身份之后必须真的同源 ----
    {
        AgentRequestIdentity stale;
        stale.profileId = L"deepseek-main";
        stale.baseUrl = L"https://other.example/v1";
        stale.endpoint = L"/chat/completions";

        const auto before = BindRequestCredential(stale, RequestTargetOwner::LocalHost);
        const auto rebound = RebindForLocalCommand(stale, RequestTargetOwner::LocalHost);
        const auto after = BindRequestCredential(rebound, RequestTargetOwner::LocalHost);

        Check(!before.bound, "修之前不同源");
        Check(rebound.profileId.empty(), "换过主机时丢掉 profile 身份");
        Check(rebound.baseUrl == stale.baseUrl, "修法只动身份,用户敲的地址原样保留");
        Check(rebound.endpoint == stale.endpoint, "端点也不动");
        Check(after.bound, "修之后同源");
        Check(after.credentialTarget == L"MiaoDesk/ModelApiKey",
              "修之后凭据落到旧槽 —— /key 写的也是旧槽,两边对得上");
        Check(after.usesProfileCredential == false, "不再从任何 profile 读凭据");

        // 绕过去不算修:只改 verdict 不改身份,before 仍是不同源。
        const auto pathRebind = RebindForLocalCommand(stale, RequestTargetOwner::LocalPath);
        Check(pathRebind.profileId == L"deepseek-main",
              "只换路径时不丢身份(丢了反而让用户莫名失去已配好的 Key)");

        // 幂等:已经空了的身份再 rebind 一次还是空,不放任任何额外状态。
        const auto twice = RebindForLocalCommand(rebound, RequestTargetOwner::LocalHost);
        Check(twice.profileId.empty(), "重复 rebind 不变");
    }

    // ---- 6. 没有 profile 时本地命令本来就是同源的 ----
    {
        AgentRequestIdentity noProfile;
        noProfile.baseUrl = L"https://other.example/v1";
        noProfile.endpoint = L"/chat/completions";
        const auto binding = BindRequestCredential(noProfile, RequestTargetOwner::LocalHost);
        Check(binding.bound, "压根没有 profile 时不同源这条判不上(没有可错配的凭据)");
        Check(binding.credentialTarget == L"MiaoDesk/ModelApiKey", "凭据就在旧槽");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\nURL 与凭据同源判定:全部 %d 项通过\n", g_checks);
    return 0;
}
