#include "miaodesk/MiaoAgentConfigAuthority.h"

#include <algorithm>
#include <cwctype>

namespace miaodesk::agent_config {
namespace {

// 与 L3Agent.cpp 的 Trim 同一份规则:掐掉两端空白。
std::wstring TrimText(std::wstring value) {
    const auto notSpace = [](wchar_t ch) { return !std::iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool IsAbsoluteUrl(const std::wstring& endpoint) noexcept {
    return endpoint.starts_with(L"https://") || endpoint.starts_with(L"http://");
}

std::wstring JoinPrefix(std::wstring_view prefix, std::wstring_view suffix) {
    if (prefix.empty()) return std::wstring(suffix);
    if (suffix.empty()) return std::wstring(prefix);
    return std::wstring(prefix) + std::wstring(suffix);
}

} // namespace

std::wstring BuildAgentRequestUrl(const AgentRequestIdentity& identity) noexcept {
    // 先查 baseUrl 空。CurrentApiUrl 也是先查未加工的 baseUrl —— 于是
    // `baseUrl == L"   "` 能过这道检查,再由 Trim 变成空串、返回空。
    // 这里照抄这个次序:换成"先 Trim 再查空"会改变"空白 baseUrl 返回什么"的答案,
    // 而那不是本轮要动的东西。
    if (identity.baseUrl.empty()) return {};

    std::wstring endpoint = TrimText(identity.endpoint);
    // 绝对 URL 的 endpoint 原样返回。前导那个 `/` 是历史写法("`/https://...`"),
    // 于是这里两个形状都认。
    if (endpoint.starts_with(L"/https://") || endpoint.starts_with(L"/http://")) {
        endpoint.erase(endpoint.begin());
        return endpoint;
    }
    if (IsAbsoluteUrl(endpoint)) return endpoint;

    std::wstring base = TrimText(identity.baseUrl);
    while (base.size() > 1 && base.back() == L'/') base.pop_back();
    if (endpoint.empty() || endpoint == L"-") return base;
    if (endpoint.front() != L'/') endpoint.insert(endpoint.begin(), L'/');
    return base + endpoint;
}

std::wstring AgentCredentialTarget(const AgentRequestIdentity& identity) noexcept {
    if (identity.profileId.empty()) return std::wstring(kLegacyCredentialTarget);
    return JoinPrefix(kProfileCredentialPrefix, identity.profileId);
}

RequestCredentialBinding BindRequestCredential(const AgentRequestIdentity& identity,
                                               RequestTargetOwner owner) noexcept {
    RequestCredentialBinding binding;
    binding.url = BuildAgentRequestUrl(identity);
    binding.credentialTarget = AgentCredentialTarget(identity);
    binding.usesProfileCredential = !identity.profileId.empty();
    binding.safeCredentialTarget = binding.credentialTarget;

    // 只有"主机被本地命令换过、而 profile 身份还挂着"这一种组合是真的对不上:
    // 那时凭据还是原 profile 的 Key,而 URL 已经是用户刚敲的那台主机。
    //
    // 其余三种都是同源的,别误报:
    //   · Profile 来源 —— URL 和 Key 都来自同一个 profile;
    //   · LocalPath —— 只换了路径,主机没变,Key 仍发给同一台主机;
    //   · profileId 为空 —— 压根没有 profile 凭据可用。
    const bool hostOverriddenByLocalCommand = owner == RequestTargetOwner::LocalHost;
    binding.bound = !hostOverriddenByLocalCommand || identity.profileId.empty();
    if (!binding.bound) {
        binding.reason =
            L"本机命令换过请求主机,但凭据仍来自 API 配置「" + identity.profileId +
            L"」：这把 Key 会被发到 " + binding.url + L"，而不是签给它的那台主机。";
        // 回到同源只能靠换槽:主机已经不是 profile 的那台,profile 的 Key 就不该用。
        binding.safeCredentialTarget = std::wstring(kLegacyCredentialTarget);
    }
    return binding;
}

AgentRequestIdentity RebindForLocalCommand(AgentRequestIdentity identity,
                                           RequestTargetOwner owner) noexcept {
    // 只处理 LocalHost。LocalPath 不动身份 —— 见头文件里对那两个枚举值的说明。
    if (owner != RequestTargetOwner::LocalHost) return identity;
    if (identity.profileId.empty()) return identity;
    identity.profileId.clear();
    return identity;
}

} // namespace miaodesk::agent_config
