#pragma once

// P0-07 AI 侧:一次请求用的 URL,和它用的凭据,必须是同一个来源。
//
// 为什么要有这个文件:`L3Agent` 有两套"配置从哪来"。
//
//   · `ReloadConfig()`(构造时、以及每次 `SetProfileId` 时都会跑)从
//     `api-profiles.ini` 读**权威**配置 —— API 配置中心是唯一真源,见 L3Agent.h 的注释;
//   · `/provider`、`/endpoint`、`/key` 这几个本地命令**就地改 `config_`**,然后
//     `SaveConfig` 把它写进 `model-settings.json`。
//
// 而 `L3Agent::LoadConfig()`(那个 json 的读者)**零调用方** —— L3Agent.h 自己写着
// "Legacy persistence helpers ... ReloadConfig and the normal runtime path do not
// consume their state."。也就是说:本地命令告诉用户"已保存",那个值在下次
// `ReloadConfig()` 时会被 profile 覆盖掉。
//
// 但这只算"说了不算"。真正的问题在凭据上:`SaveApiKey` 与 `LoadApiKey` 都按
// `config_.profileId` 选凭据槽:
//
//     profileId 非空 → MiaoDesk/ApiProfile/<profileId>   ← 就是那个 profile 的 Key
//     profileId 为空 → MiaoDesk/ModelApiKey              ← 旧位置
//
// 于是这条链是活的:用户在 AI 对话里敲 `/provider https://别的服务/v1 某个模型`,
// `config_.baseUrl` 换成了别的**主机**,而 `config_.profileId` 一个字符都没动 ——
// `ReloadConfig` 之外没有任何代码写它。下一次请求:
//
//     URL      = https://别的服务/v1/...        ← 用户刚敲的
//     API Key  = MiaoDesk/ApiProfile/<原 profile> ← 原来那个 profile 的 Key
//
// **一把签给 A 服务的 Key,被发到 B 服务上去。** 而 `/key` 的更糟:它把用户敲的新 Key
// 写进 `MiaoDesk/ApiProfile/<原 profile>`,也就是**覆盖了那个 profile 自己存的 Key**,
// 而设置页里一个字都不会提。其它每个用这个 profile 的 AI 窗口、以及 Harness,
// 下一轮就都用上了这把被换掉的 Key。
//
// 这个模块只做判定,不碰 Credential Manager、不碰盘、不 import 任何 Windows 头,
// 于是"URL 与凭据是否同源"这件事在本机就能真跑、真门。宿主照它的结论走。
#include <string>

namespace miaodesk::agent_config {

// 没有 API profile 时用的那个凭据槽。与 L3Agent.cpp 的 kCredentialTarget 一致 ——
// 那一个在 include 了 wincred.h 的文件里,纯逻辑这边引不到,只能各写一份并在此钉住。
inline constexpr wchar_t kLegacyCredentialTarget[] = L"MiaoDesk/ModelApiKey";

// 每个 API profile 的凭据槽前缀。ApiRuntimeProfile::ReadSection 读的也是这个位置。
inline constexpr wchar_t kProfileCredentialPrefix[] = L"MiaoDesk/ApiProfile/";

// 一次请求的身份:URL 由什么组成,以及它挂在哪一个 profile 身份下。
//
// 故意不引 L3Agent.h 的 ModelConfig:那个头为了 HINTERNET include 了 windows.h,
// 于是本机一行都编译不了。这里只留判定真正要用的三个字段。
struct AgentRequestIdentity {
    // 当前生效的 profile 身份(ReloadConfig 从 ini 填的)。空表示没有 profile。
    std::wstring profileId;
    // 请求用的基础 URL。末尾斜杠由这里负责吃掉。
    std::wstring baseUrl;
    // 端点。可能是绝对 URL、L"-"/空、或相对路径。规则见 BuildAgentRequestUrl。
    std::wstring endpoint;
};

// `config_` 里的请求目标是**谁**设定的。
//
// 区分 LocalHost 与 LocalPath 是有意的:它们是两件不同的事。
//   · LocalHost(`/provider` 换了**主机**)—— 凭据与主机从此对不上,必须换槽;
//   · LocalPath(`/endpoint` 只换了**路径**)—— 主机没变,profile 的 Key 仍然是
//     发给同一台主机的,同源关系完好。把这两类合成一类会让 `/endpoint` 误判成危险。
enum class RequestTargetOwner {
    // API 配置中心(profile)设定的。产品里的权威来源。
    Profile,
    // 本地命令换了主机。
    LocalHost,
    // 本地命令只换了端点路径。
    LocalPath,
};

// baseUrl + endpoint → 真正会请求的那个 URL。规则与 `L3Agent::CurrentApiUrl` 逐条相同:
//   · endpoint 是绝对 URL(可带一个前导 `/`)时原样返回,不与 baseUrl 拼接;
//   · endpoint 为空或 L"-" 时只返回 baseUrl(吃掉末尾斜杠);
//   · 否则补一个前导 `/` 再拼。
//
// 抽成纯函数是因为它同时是 P0-07 的一致性问题和诊断要显示的地址:
// OldApiUrl 少一条规则,历史记录里那个 URL 就是错的。
std::wstring BuildAgentRequestUrl(const AgentRequestIdentity& identity) noexcept;

// 这次请求会去读的凭据槽。与 `L3Agent::SaveApiKey` / `LoadApiKey` 的选择一致。
std::wstring AgentCredentialTarget(const AgentRequestIdentity& identity) noexcept;

// 一次请求的 URL 与凭据的绑定结论。
struct RequestCredentialBinding {
    // 真正会请求的 URL。
    std::wstring url;
    // 会去读的凭据槽。
    std::wstring credentialTarget;
    // 凭据是不是从某个 API profile 里读的。
    bool usesProfileCredential{};
    // URL 与凭据**同源**。false 表示"签给 A 的 Key 要发到 B 去"。
    bool bound{};
    // bound 为 false 时,这里说清哪里对不上(人看的一句话)。
    std::wstring reason;
    // 为了回到同源,应该改用的凭据槽。bound 为 true 时等于 credentialTarget。
    std::wstring safeCredentialTarget;
};

// 判一次:这个身份 + 这个来源,发出去的 Key 是不是发给它的那台主机的。
RequestCredentialBinding BindRequestCredential(const AgentRequestIdentity& identity,
                                               RequestTargetOwner owner) noexcept;

// 把本地命令改过的目标收拢成"与凭据同源"的身份:主机被本地命令换过时,
// 丢掉那个 profile 身份,让凭据回到旧槽(`/key` 写的也是旧槽)。
//
// 这就是修那个缺陷的那一步。它只动身份,不动 URL —— 用户敲的地址照旧生效。
AgentRequestIdentity RebindForLocalCommand(AgentRequestIdentity identity,
                                           RequestTargetOwner owner) noexcept;

} // namespace miaodesk::agent_config
