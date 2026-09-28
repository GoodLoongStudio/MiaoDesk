#include "miaodesk/L3Agent.h"
#include <windows.h>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

constexpr std::uint32_t kSessionMagic = 0x334c4454;
constexpr std::uint32_t kSessionVersion = 1;

std::wstring Lower(std::wstring value) {
    for (auto& ch : value) ch = static_cast<wchar_t>(std::towlower(ch));
    return value;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}

std::uint64_t SessionHash(const ModelConfig& config) {
    // Must stay byte-for-byte aligned with L3Agent.cpp. The profile id is part of
    // session identity now: two central API profiles may point at the same endpoint
    // and model but still represent intentionally separate AI-window sessions.
    const std::wstring key = Lower(config.profileId) + L"\n" +
                             Lower(config.providerId) + L"\n" + Lower(config.baseUrl) + L"\n" +
                             config.endpoint + L"\n" + config.model;
    std::uint64_t hash = 1469598103934665603ull;
    for (wchar_t ch : key) {
        hash ^= static_cast<std::uint16_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

fs::path SessionPath(const fs::path& localAppData, const ModelConfig& config) {
    wchar_t name[32]{};
    swprintf_s(name, L"%016llx.bin", static_cast<unsigned long long>(SessionHash(config)));
    return localAppData / L"MiaoDesk" / L"l3-sessions" / name;
}

void WriteU32(std::ostream& stream, std::uint32_t value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool WriteField(std::ostream& stream, const std::wstring& value) {
    const auto utf8 = WideToUtf8(value);
    WriteU32(stream, static_cast<std::uint32_t>(utf8.size()));
    if (!utf8.empty()) stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(stream);
}

bool SeedSession(const fs::path& path, const std::wstring& user, const std::wstring& assistant) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    WriteU32(stream, kSessionMagic);
    WriteU32(stream, kSessionVersion);
    WriteU32(stream, 1);
    return WriteField(stream, user) && WriteField(stream, assistant);
}

class LocalAppDataScope {
public:
    LocalAppDataScope() {
        wchar_t current[32768]{};
        const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", current, static_cast<DWORD>(std::size(current)));
        hadOriginal_ = count > 0 && count < std::size(current);
        if (hadOriginal_) original_.assign(current, count);

        wchar_t temp[MAX_PATH]{};
        const DWORD tempCount = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
        if (!tempCount || tempCount >= std::size(temp)) return;
        root_ = fs::path(temp) / (L"MiaoDesk-L3SelfTest-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
        if (ec) return;
        active_ = SetEnvironmentVariableW(L"LOCALAPPDATA", root_.c_str()) != FALSE;
    }

    ~LocalAppDataScope() {
        if (active_) SetEnvironmentVariableW(L"LOCALAPPDATA", hadOriginal_ ? original_.c_str() : nullptr);
        std::error_code ec;
        if (!root_.empty()) fs::remove_all(root_, ec);
    }

    bool Active() const noexcept { return active_; }
    const fs::path& Root() const noexcept { return root_; }

private:
    bool active_{};
    bool hadOriginal_{};
    std::wstring original_;
    fs::path root_;
};

bool SetProvider(L3Agent& agent, const wchar_t* model) {
    std::wstring reply;
    bool consumedSecret = false;
    const std::wstring command = std::wstring(L"/provider http://127.0.0.1:11434 ") + model;
    return agent.TryHandleLocal(command, reply, consumedSecret) && !reply.empty() && !consumedSecret;
}


bool WriteProfile(const wchar_t* id, const wchar_t* name,
                  const wchar_t* baseUrl, const wchar_t* model,
                  bool legacyDefault = false) {
    const fs::path path = api_runtime_profile::ProfilesPath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;

    const std::wstring section = std::wstring(L"profile:") + id;
    return WritePrivateProfileStringW(section.c_str(), L"name", name, path.c_str()) &&
           WritePrivateProfileStringW(section.c_str(), L"type", L"OpenAI Compatible", path.c_str()) &&
           WritePrivateProfileStringW(section.c_str(), L"baseUrl", baseUrl, path.c_str()) &&
           WritePrivateProfileStringW(section.c_str(), L"model", model, path.c_str()) &&
           WritePrivateProfileStringW(section.c_str(), L"default", legacyDefault ? L"1" : L"0", path.c_str());
}

bool DeleteProfile(const wchar_t* id) {
    const fs::path path = api_runtime_profile::ProfilesPath();
    const std::wstring section = std::wstring(L"profile:") + id;
    return WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, path.c_str()) != FALSE;
}

bool RunCentralProfileRoutingSelfTest() {
    // Both endpoints are loopback, so the profiles are fully configured without
    // touching the real Windows Credential Manager. This test exercises the same
    // ApiRuntimeProfile + L3Agent path the actual AI windows use.
    if (!WriteProfile(L"first", L"First API", L"http://127.0.0.1:11434", L"model-first")) return false;
    // Deliberately mark the second profile as the legacy "default". Product policy
    // now says list order wins, so this must NOT override the first profile.
    if (!WriteProfile(L"second", L"Second API", L"http://127.0.0.1:11435", L"model-second", true)) return false;

    const auto profiles = api_runtime_profile::LoadAll();
    if (profiles.size() != 2) return false;
    if (_wcsicmp(profiles[0].id.c_str(), L"first") != 0 ||
        _wcsicmp(profiles[1].id.c_str(), L"second") != 0) return false;
    if (!profiles[0].configured || !profiles[1].configured) return false;

    const auto fallback = api_runtime_profile::LoadDefault();
    if (!fallback.found || _wcsicmp(fallback.id.c_str(), L"first") != 0 ||
        fallback.model != L"model-first") return false;

    // Model two independent AI windows. They share the central profile database,
    // but selection state lives on each L3Agent/window.
    L3Agent chatWindow;
    L3Agent creatorWindow;
    if (_wcsicmp(chatWindow.ProfileId().c_str(), L"first") != 0 ||
        _wcsicmp(creatorWindow.ProfileId().c_str(), L"first") != 0) return false;

    creatorWindow.SetProfileId(L"second");
    if (_wcsicmp(creatorWindow.ProfileId().c_str(), L"second") != 0 ||
        creatorWindow.ProfileName() != L"Second API" ||
        creatorWindow.Config().model != L"model-second") return false;

    // Switching one AI window must never rewrite or redirect another one.
    if (_wcsicmp(chatWindow.ProfileId().c_str(), L"first") != 0 ||
        chatWindow.ProfileName() != L"First API" ||
        chatWindow.Config().model != L"model-first") return false;

    // If a selected central profile is deleted while a window stays open, the next
    // reload must fall back to the first configured profile.
    if (!DeleteProfile(L"second")) return false;
    creatorWindow.ReloadConfig();
    if (_wcsicmp(creatorWindow.ProfileId().c_str(), L"first") != 0 ||
        creatorWindow.ProfileName() != L"First API" ||
        creatorWindow.Config().model != L"model-first") return false;

    // Empty selection is also defined as "use the first configured profile".
    creatorWindow.SetProfileId({});
    return _wcsicmp(creatorWindow.ProfileId().c_str(), L"first") == 0;
}

} // namespace

bool RunL3PersistenceSelfTest() {
    LocalAppDataScope local;
    if (!local.Active()) return false;

    // Legacy direct-provider persistence remains supported for users who have not
    // created central profiles yet.
    {
        L3Agent agent;
        if (!SetProvider(agent, L"miaodesk-selftest-a")) return false;
        const ModelConfig configA = agent.Config();
        if (!SetProvider(agent, L"miaodesk-selftest-b")) return false;
        const ModelConfig configB = agent.Config();

        const auto pathA = SessionPath(local.Root(), configA);
        const auto pathB = SessionPath(local.Root(), configB);
        if (pathA == pathB) return false;
        if (!SeedSession(pathA, L"user-a", L"assistant-a") || !SeedSession(pathB, L"user-b", L"assistant-b")) return false;

        if (!SetProvider(agent, L"miaodesk-selftest-a") || agent.ConversationTurnCountForSelfTest() != 1) return false;
        if (!SetProvider(agent, L"miaodesk-selftest-b") || agent.ConversationTurnCountForSelfTest() != 1) return false;

        std::wstring reply;
        bool consumedSecret = false;
        if (!agent.TryHandleLocal(L"/new", reply, consumedSecret) || consumedSecret) return false;
        if (agent.ConversationTurnCountForSelfTest() != 0 || fs::exists(pathB) || !fs::exists(pathA)) return false;

        if (!SetProvider(agent, L"miaodesk-selftest-a") || agent.ConversationTurnCountForSelfTest() != 1) return false;
    }

    // Start with an empty product state for the central multi-profile test.
    std::error_code ec;
    fs::remove_all(local.Root() / L"MiaoDesk", ec);
    if (ec) return false;
    return RunCentralProfileRoutingSelfTest();
}

} // namespace miaodesk
