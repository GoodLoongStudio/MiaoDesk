#include "turingdesk/CodexRuntime.h"
#include "turingdesk/NativeTools.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <wincred.h>
#include <winhttp.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr wchar_t kCredentialTarget[] = L"TuringDesk/ModelApiKey";
constexpr wchar_t kApiKeyEnvironment[] = L"TURINGDESK_MODEL_API_KEY";
constexpr DWORD kInitializeTimeoutMs = 30000;
constexpr DWORD kThreadStartTimeoutMs = 60000;
constexpr DWORD kTurnTimeoutMs = 120000;
constexpr DWORD kRelayReadyTimeoutMs = 10000;

std::wstring Trim(std::wstring value) {
    const auto notSpace = [](wchar_t ch) { return !std::iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool EndsWithInsensitive(const std::wstring& value, const std::wstring& suffix) {
    if (suffix.size() > value.size()) return false;
    return Lower(value.substr(value.size() - suffix.size())) == Lower(suffix);
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

std::string EscapeJson(const std::wstring& value) {
    const auto utf8 = WideToUtf8(value);
    std::string out;
    out.reserve(utf8.size() + 16);
    for (unsigned char ch : utf8) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buffer[7]{};
                sprintf_s(buffer, "\\u%04x", static_cast<unsigned>(ch));
                out += buffer;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    return out;
}

int Hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

void AppendCodepoint(std::string& out, unsigned cp) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

bool ReadHex4(std::string_view text, std::size_t pos, unsigned& cp) {
    if (pos + 4 > text.size()) return false;
    cp = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const int value = Hex(text[pos + i]);
        if (value < 0) return false;
        cp = (cp << 4) | static_cast<unsigned>(value);
    }
    return true;
}

std::string ExtractJsonString(std::string_view json, std::string_view key) {
    auto pos = json.find(key);
    if (pos == std::string_view::npos) return {};
    pos = json.find(':', pos + key.size());
    if (pos == std::string_view::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string_view::npos) return {};
    ++pos;
    std::string out;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') break;
        if (ch != '\\') { out.push_back(ch); continue; }
        if (pos >= json.size()) break;
        const char esc = json[pos++];
        switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            unsigned cp = 0;
            if (!ReadHex4(json, pos, cp)) return out;
            pos += 4;
            if (cp >= 0xd800 && cp <= 0xdbff && pos + 6 <= json.size() && json[pos] == '\\' && json[pos + 1] == 'u') {
                unsigned low = 0;
                if (ReadHex4(json, pos + 2, low) && low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    pos += 6;
                }
            }
            AppendCodepoint(out, cp);
            break;
        }
        default: out.push_back(esc); break;
        }
    }
    return out;
}

bool HasResponseId(std::string_view json, long long id) {
    auto pos = json.find("\"id\"");
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + 4);
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') { negative = true; ++pos; }
    long long value = 0;
    bool any = false;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        any = true;
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    if (negative) value = -value;
    return any && value == id;
}

bool TryReadRequestId(std::string_view json, long long& id) {
    auto pos = json.find("\"id\"");
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + 4);
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') { negative = true; ++pos; }
    long long value = 0;
    bool any = false;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        any = true;
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    if (!any) return false;
    id = negative ? -value : value;
    return true;
}

std::wstring TomlEscape(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        if (ch == L'\\' || ch == L'"') out.push_back(L'\\');
        if (ch == L'\n') { out += L"\\n"; continue; }
        if (ch == L'\r') { out += L"\\r"; continue; }
        out.push_back(ch);
    }
    return out;
}

std::wstring QuoteArg(std::wstring_view value) {
    std::wstring result = L"\"";
    unsigned backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++backslashes; continue; }
        if (ch == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

fs::path ModuleDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return fs::path(path).parent_path();
}

fs::path LocalAppDataRoot() {
    wchar_t localAppData[32768]{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (count > 0 && count < std::size(localAppData)) return fs::path(std::wstring(localAppData, count)) / L"TuringDesk";
    return fs::temp_directory_path() / L"TuringDesk";
}

fs::path CodexHomeDirectory() {
    return LocalAppDataRoot() / L"CodexHome";
}

fs::path CodexLogPath() {
    return RuntimeLogPath(L"codex-runtime.log");
}

HANDLE OpenRuntimeLogHandle(bool inheritable) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = inheritable ? TRUE : FALSE;
    const auto path = CodexLogPath();
    HANDLE handle = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                inheritable ? &security : nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle;
}

void AppendRuntimeLog(const std::wstring& text) {
    HANDLE handle = OpenRuntimeLogHandle(false);
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[64]{};
    swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u] ", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond);
    const auto utf8 = WideToUtf8(std::wstring(prefix) + text + L"\r\n");
    if (!utf8.empty()) {
        DWORD written = 0;
        WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(handle);
}

std::wstring LogHint() {
    return L" · 日志：" + CodexLogPath().wstring();
}

std::wstring DesktopDirectory() {
    wchar_t profile[32768]{};
    const DWORD count = GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile)));
    if (count > 0 && count < std::size(profile)) {
        const auto desktop = fs::path(std::wstring(profile, count)) / L"Desktop";
        std::error_code ec;
        if (fs::exists(desktop, ec) && fs::is_directory(desktop, ec)) return desktop.wstring();
    }
    return ModuleDirectory().wstring();
}

std::wstring LoadApiKey() {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &credential)) return {};
    std::wstring key;
    if (credential && credential->CredentialBlob && credential->CredentialBlobSize > 0) {
        const auto* chars = reinterpret_cast<const wchar_t*>(credential->CredentialBlob);
        key.assign(chars, credential->CredentialBlobSize / sizeof(wchar_t));
    }
    if (credential) CredFree(credential);
    return key;
}

std::wstring SearchExecutable(const wchar_t* name) {
    std::wstring buffer(32768, L'\0');
    const DWORD count = SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (count == 0 || count >= buffer.size()) return {};
    buffer.resize(count);
    return buffer;
}

std::vector<wchar_t> BuildEnvironmentBlock(const std::vector<std::pair<std::wstring, std::wstring>>& overrides) {
    std::vector<std::wstring> entries;
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        for (const wchar_t* cursor = block; *cursor != L'\0'; cursor += std::wcslen(cursor) + 1) {
            std::wstring entry(cursor);
            const auto equal = entry.find(L'=');
            const std::wstring name = equal == std::wstring::npos ? entry : entry.substr(0, equal);
            bool replaced = false;
            for (const auto& [overrideName, _] : overrides) {
                if (_wcsicmp(name.c_str(), overrideName.c_str()) == 0) { replaced = true; break; }
            }
            if (!replaced) entries.push_back(std::move(entry));
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : overrides) entries.push_back(name + L"=" + value);
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    std::size_t chars = 1;
    for (const auto& entry : entries) chars += entry.size() + 1;
    std::vector<wchar_t> out(chars, L'\0');
    wchar_t* cursor = out.data();
    for (const auto& entry : entries) {
        std::copy(entry.begin(), entry.end(), cursor);
        cursor += entry.size();
        *cursor++ = L'\0';
    }
    *cursor = L'\0';
    return out;
}

std::vector<wchar_t> BuildCodexEnvironmentBlock(const std::wstring& codexHome, const std::wstring& apiKey) {
    return BuildEnvironmentBlock({
        {L"CODEX_HOME", codexHome},
        {kApiKeyEnvironment, apiKey},
        {L"RUST_LOG", L"codex_app_server=info,codex_core=warn"},
        {L"LOG_FORMAT", L"json"},
    });
}

std::vector<wchar_t> BuildRelayEnvironmentBlock(const std::wstring& upstream,
                                                 const std::wstring& apiKey,
                                                 unsigned short port,
                                                 const std::wstring& model) {
    wchar_t oldPath[32768]{};
    const DWORD count = GetEnvironmentVariableW(L"PATH", oldPath, static_cast<DWORD>(std::size(oldPath)));
    std::wstring path = (ModuleDirectory() / L"Codex").wstring();
    if (count > 0 && count < std::size(oldPath)) path += L";" + std::wstring(oldPath, count);

    std::vector<std::pair<std::wstring, std::wstring>> overrides{
        {L"CODEX_RELAY_BIND", L"127.0.0.1"},
        {L"CODEX_RELAY_PORT", std::to_wstring(port)},
        {L"CODEX_RELAY_UPSTREAM", upstream},
        {L"CODEX_RELAY_API_KEY", apiKey},
        {L"RUST_LOG", L"codex_relay=info"},
        {L"PATH", path},
    };

    const auto lowerUpstream = Lower(upstream);
    const auto lowerModel = Lower(model);
    const bool deepseekChat = lowerUpstream.find(L"deepseek") != std::wstring::npos &&
                              lowerModel.find(L"reasoner") == std::wstring::npos &&
                              lowerModel.find(L"reasoning") == std::wstring::npos;
    if (deepseekChat) {
        overrides.emplace_back(L"CODEX_RELAY_UPSTREAM_EXTRA_PARAMS", L"{\"thinking\":{\"type\":\"disabled\"}}");
        overrides.emplace_back(L"CODEX_RELAY_DROP_PARAMS", L"[\"reasoning_effort\"]");
    }
    return BuildEnvironmentBlock(overrides);
}

std::wstring StripResponsesSuffix(std::wstring url) {
    url = Trim(std::move(url));
    while (url.size() > 1 && url.back() == L'/') url.pop_back();
    if (EndsWithInsensitive(url, L"/responses")) url.resize(url.size() - 10);
    while (url.size() > 1 && url.back() == L'/') url.pop_back();
    return url;
}

std::wstring OpenAiResponsesBase(const L3Agent& agent) {
    auto url = Trim(agent.CurrentApiUrl());
    if (url.empty()) return {};
    if (EndsWithInsensitive(url, L"/responses")) return StripResponsesSuffix(url);
    return {};
}

std::wstring ChatCompletionsBase(const L3Agent& agent) {
    auto url = Trim(agent.CurrentApiUrl());
    while (url.size() > 1 && url.back() == L'/') url.pop_back();
    const std::wstring suffix = L"/chat/completions";
    if (!EndsWithInsensitive(url, suffix)) return {};
    url.resize(url.size() - suffix.size());
    while (url.size() > 1 && url.back() == L'/') url.pop_back();
    return url;
}

bool IsLocalHttpBase(const std::wstring& url) {
    const auto lower = Lower(url);
    return lower.starts_with(L"http://127.0.0.1") || lower.starts_with(L"http://localhost") ||
           lower.starts_with(L"https://127.0.0.1") || lower.starts_with(L"https://localhost") ||
           lower.starts_with(L"http://[::1]") || lower.starts_with(L"https://[::1]");
}

bool ProcessAlive(HANDLE process) {
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool ProbeLoopbackHttp(unsigned short port) {
    HINTERNET session = WinHttpOpen(L"TuringDesk.CodexRelay.Readiness/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 250, 250, 250, 250);
    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", L"/__turingdesk_ready", nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    bool ready = false;
    if (request) {
        ready = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr);
        WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ready;
}

} // namespace

CodexRuntime::~CodexRuntime() {
    ResetSession();
}

CodexRuntime::ProviderSetup CodexRuntime::BuildProviderSetup(const L3Agent& agent) const {
    ProviderSetup setup;
    setup.model = Trim(agent.Config().model);
    setup.apiKey = LoadApiKey();
    if (setup.model.empty()) {
        setup.message = L"模型未配置";
        return setup;
    }

    const auto directResponses = OpenAiResponsesBase(agent);
    if (!directResponses.empty()) {
        setup.baseUrl = directResponses;
    } else {
        if (Lower(agent.Config().providerId) == L"anthropic") {
            setup.message = L"Anthropic Messages Provider 暂不属于 OpenAI-compatible Chat Completions，无法交给 Codex Relay";
            return setup;
        }
        setup.upstreamBaseUrl = ChatCompletionsBase(agent);
        if (setup.upstreamBaseUrl.empty()) {
            setup.message = L"当前 Provider 既不是 Responses API，也不是可桥接的 Chat Completions API";
            return setup;
        }
        setup.relayRequired = true;
        setup.relayPort = static_cast<unsigned short>(45000 + (GetCurrentProcessId() % 10000));
        setup.baseUrl = L"http://127.0.0.1:" + std::to_wstring(setup.relayPort) + L"/v1";
    }

    if (setup.apiKey.empty()) {
        if (setup.relayRequired && IsLocalHttpBase(setup.upstreamBaseUrl)) setup.apiKey = L"turingdesk-local";
        else {
            setup.message = L"Codex Runtime 当前需要 API Key";
            return setup;
        }
    }
    setup.signature = (setup.relayRequired ? L"relay\n" + setup.upstreamBaseUrl : L"responses\n" + setup.baseUrl) +
                      L"\n" + setup.model;
    setup.ok = true;
    return setup;
}

std::wstring CodexRuntime::FindBinary(bool& isCliBinary) const {
    isCliBinary = false;
    wchar_t explicitPath[32768]{};
    const DWORD explicitCount = GetEnvironmentVariableW(L"TURINGDESK_CODEX_APP_SERVER", explicitPath,
                                                         static_cast<DWORD>(std::size(explicitPath)));
    if (explicitCount > 0 && explicitCount < std::size(explicitPath)) {
        std::error_code ec;
        if (fs::exists(explicitPath, ec)) return explicitPath;
    }
    std::error_code ec;
    const auto bundledCli = ModuleDirectory() / L"Codex" / L"codex.exe";
    if (fs::exists(bundledCli, ec)) { isCliBinary = true; return bundledCli.wstring(); }
    const auto bundledServer = ModuleDirectory() / L"Codex" / L"codex-app-server.exe";
    if (fs::exists(bundledServer, ec)) return bundledServer.wstring();
    auto found = SearchExecutable(L"codex.exe");
    if (!found.empty()) { isCliBinary = true; return found; }
    return SearchExecutable(L"codex-app-server.exe");
}

std::wstring CodexRuntime::FindRelayBinary() const {
    wchar_t explicitPath[32768]{};
    const DWORD explicitCount = GetEnvironmentVariableW(L"TURINGDESK_CODEX_RELAY", explicitPath,
                                                         static_cast<DWORD>(std::size(explicitPath)));
    if (explicitCount > 0 && explicitCount < std::size(explicitPath)) {
        std::error_code ec;
        if (fs::exists(explicitPath, ec)) return explicitPath;
    }
    std::error_code ec;
    const auto bundled = ModuleDirectory() / L"CodexRelay" / L"codex-relay.exe";
    if (fs::exists(bundled, ec)) return bundled.wstring();
    return SearchExecutable(L"codex-relay.exe");
}

CodexRuntimeStatus CodexRuntime::Status(const L3Agent& agent) const {
    CodexRuntimeStatus status;
    bool cli = false;
    status.binaryPath = FindBinary(cli);
    status.binaryAvailable = !status.binaryPath.empty();
    const auto setup = BuildProviderSetup(agent);
    const bool relayAvailable = !setup.relayRequired || !FindRelayBinary().empty();
    status.providerCompatible = setup.ok && relayAvailable;
    {
        std::scoped_lock lock(processMutex_);
        status.running = ProcessAlive(process_);
    }
    if (!status.binaryAvailable) status.message = L"Codex CLI 未安装";
    else if (!setup.ok) status.message = setup.message;
    else if (!relayAvailable) status.message = L"当前 Provider 需要 Codex Relay，但 codex-relay.exe 尚未部署";
    else if (status.running) status.message = setup.relayRequired
        ? L"Codex CLI Agent Runtime 正在通过本地 Codex Relay 运行"
        : L"Codex CLI Agent Runtime 正在直连 Responses API";
    else status.message = setup.relayRequired
        ? L"Codex CLI 可用；下一次请求将通过本地 Codex Relay 接入 Chat Completions"
        : L"Codex CLI 可用；下一次请求将直连 Responses API";
    return status;
}

bool CodexRuntime::CanHandle(const L3Agent& agent) const {
    bool cli = false;
    const auto setup = BuildProviderSetup(agent);
    if (FindBinary(cli).empty() || !setup.ok) return false;
    return !setup.relayRequired || !FindRelayBinary().empty();
}

bool CodexRuntime::ConfigureCodexHome(const ProviderSetup& setup, std::wstring& codexHome, std::wstring& error) const {
    const auto home = CodexHomeDirectory();
    std::error_code ec;
    fs::create_directories(home, ec);
    if (ec) {
        error = L"无法创建 Codex Runtime 目录：" + Utf8ToWide(ec.message());
        return false;
    }
    codexHome = home.wstring();

    fs::path catalogPath;
    if (setup.relayRequired) {
        catalogPath = home / L"codex-relay-models.json";
        fs::remove(catalogPath, ec);
        ec.clear();
        const auto relay = FindRelayBinary();
        if (relay.empty()) {
            error = L"生成 Codex 模型目录时没有找到 codex-relay.exe";
            return false;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE logHandle = OpenRuntimeLogHandle(true);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? GetStdHandle(STD_INPUT_HANDLE) : nullInput;
        startup.hStdOutput = logHandle == INVALID_HANDLE_VALUE ? GetStdHandle(STD_OUTPUT_HANDLE) : logHandle;
        startup.hStdError = logHandle == INVALID_HANDLE_VALUE ? GetStdHandle(STD_ERROR_HANDLE) : logHandle;
        PROCESS_INFORMATION process{};
        std::wstring command = QuoteArg(relay) + L" --print-config --model-catalog " + QuoteArg(catalogPath.wstring());
        auto environment = BuildRelayEnvironmentBlock(setup.upstreamBaseUrl, setup.apiKey, setup.relayPort, setup.model);
        AppendRuntimeLog(L"catalog: generating model catalog for model=" + setup.model + L" upstream=" + setup.upstreamBaseUrl);
        const BOOL created = CreateProcessW(relay.c_str(), command.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                            environment.data(), home.c_str(), &startup, &process);
        if (nullInput && nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
        if (logHandle && logHandle != INVALID_HANDLE_VALUE) CloseHandle(logHandle);
        if (!created) {
            error = L"Codex Relay 模型目录生成进程启动失败，Win32=" + std::to_wstring(GetLastError()) + LogHint();
            return false;
        }
        const DWORD wait = WaitForSingleObject(process.hProcess, 20000);
        DWORD exitCode = STILL_ACTIVE;
        if (wait == WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess, 1);
            error = L"Codex Relay 模型目录生成超时" + LogHint();
        } else {
            GetExitCodeProcess(process.hProcess, &exitCode);
            if (exitCode != 0) error = L"Codex Relay 模型目录生成失败，ExitCode=" + std::to_wstring(exitCode) + LogHint();
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (!error.empty()) return false;
        if (!fs::is_regular_file(catalogPath, ec)) {
            error = L"Codex Relay 没有生成模型目录文件" + LogHint();
            return false;
        }
        AppendRuntimeLog(L"catalog: ready at " + catalogPath.wstring());
    }

    std::wofstream stream(home / L"config.toml", std::ios::trunc);
    if (!stream) {
        error = L"无法写入 Codex Runtime 配置";
        return false;
    }
    stream << L"model = \"" << TomlEscape(setup.model) << L"\"\n"
           << L"model_provider = \"turingdesk\"\n"
           << L"approval_policy = \"never\"\n"
           << L"sandbox_mode = \"workspace-write\"\n";
    if (!catalogPath.empty())
        stream << L"model_catalog_json = \"" << TomlEscape(catalogPath.wstring()) << L"\"\n";
    stream << L"\n[model_providers.turingdesk]\n"
           << L"name = \"TuringDesk Codex Provider\"\n"
           << L"base_url = \"" << TomlEscape(setup.baseUrl) << L"\"\n"
           << L"env_key = \"TURINGDESK_MODEL_API_KEY\"\n"
           << L"wire_api = \"responses\"\n"
           << L"requires_openai_auth = false\n";
    if (!stream) {
        error = L"Codex Runtime 配置写入失败";
        return false;
    }
    AppendRuntimeLog(L"config: provider=turingdesk model=" + setup.model +
                     (setup.relayRequired ? L" route=relay" : L" route=responses"));
    return true;
}

bool CodexRuntime::WaitForRelayReady(const ProviderSetup& setup, DWORD timeoutMs, std::wstring& error) const {
    if (!setup.relayRequired) return true;
    const ULONGLONG started = GetTickCount64();
    for (;;) {
        HANDLE relay = nullptr;
        {
            std::scoped_lock lock(processMutex_);
            relay = relayProcess_;
        }
        if (!ProcessAlive(relay)) {
            DWORD exitCode = 0;
            if (relay) GetExitCodeProcess(relay, &exitCode);
            error = L"Codex Relay 在就绪前退出，ExitCode=" + std::to_wstring(exitCode) + LogHint();
            return false;
        }
        if (ProbeLoopbackHttp(setup.relayPort)) return true;
        if (GetTickCount64() - started >= timeoutMs) {
            error = L"Codex Relay 监听端口在限定时间内没有就绪" + LogHint();
            return false;
        }
        Sleep(80);
    }
}

bool CodexRuntime::LaunchRelay(const ProviderSetup& setup, std::wstring& error) {
    if (!setup.relayRequired) return true;
    const auto binary = FindRelayBinary();
    if (binary.empty()) {
        error = L"当前 Provider 需要 Codex Relay，但没有找到 codex-relay.exe";
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE logHandle = OpenRuntimeLogHandle(true);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? GetStdHandle(STD_INPUT_HANDLE) : nullInput;
    startup.hStdOutput = logHandle == INVALID_HANDLE_VALUE ? GetStdHandle(STD_OUTPUT_HANDLE) : logHandle;
    startup.hStdError = logHandle == INVALID_HANDLE_VALUE ? GetStdHandle(STD_ERROR_HANDLE) : logHandle;
    PROCESS_INFORMATION process{};
    std::wstring command = QuoteArg(binary);
    auto environment = BuildRelayEnvironmentBlock(setup.upstreamBaseUrl, setup.apiKey, setup.relayPort, setup.model);
    const auto cwd = ModuleDirectory().wstring();
    AppendRuntimeLog(L"relay: starting port=" + std::to_wstring(setup.relayPort) + L" upstream=" + setup.upstreamBaseUrl + L" model=" + setup.model);
    const BOOL created = CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        environment.data(), cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    if (nullInput && nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
    if (logHandle && logHandle != INVALID_HANDLE_VALUE) CloseHandle(logHandle);
    if (!created) {
        error = L"启动 Codex Relay 失败，Win32=" + std::to_wstring(GetLastError()) + LogHint();
        return false;
    }
    {
        std::scoped_lock lock(processMutex_);
        relayProcess_ = process.hProcess;
        relayThread_ = process.hThread;
    }
    if (!WaitForRelayReady(setup, kRelayReadyTimeoutMs, error)) {
        AppendRuntimeLog(L"relay: readiness failed: " + error);
        return false;
    }
    AppendRuntimeLog(L"relay: listener ready");
    return true;
}

bool CodexRuntime::LaunchProcess(const ProviderSetup& setup, std::wstring& error) {
    bool cliBinary = false;
    const auto binary = FindBinary(cliBinary);
    if (binary.empty()) {
        error = L"未找到 Codex CLI / app-server";
        return false;
    }

    std::wstring codexHome;
    if (!ConfigureCodexHome(setup, codexHome, error)) return false;

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE childStdoutRead = nullptr;
    HANDLE childStdoutWrite = nullptr;
    HANDLE childStdinRead = nullptr;
    HANDLE childStdinWrite = nullptr;
    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &security, 0) ||
        !CreatePipe(&childStdinRead, &childStdinWrite, &security, 0)) {
        error = L"创建 Codex stdio 管道失败";
        if (childStdoutRead) CloseHandle(childStdoutRead);
        if (childStdoutWrite) CloseHandle(childStdoutWrite);
        if (childStdinRead) CloseHandle(childStdinRead);
        if (childStdinWrite) CloseHandle(childStdinWrite);
        return false;
    }
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);

    HANDLE logHandle = OpenRuntimeLogHandle(true);
    HANDLE nullError = INVALID_HANDLE_VALUE;
    if (!logHandle || logHandle == INVALID_HANDLE_VALUE) {
        nullError = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childStdinRead;
    startup.hStdOutput = childStdoutWrite;
    startup.hStdError = logHandle && logHandle != INVALID_HANDLE_VALUE ? logHandle : nullError;
    PROCESS_INFORMATION process{};
    std::wstring command = QuoteArg(binary);
    command += cliBinary ? L" app-server --stdio" : L" --stdio";
    auto environment = BuildCodexEnvironmentBlock(codexHome, setup.apiKey);
    const auto cwd = DesktopDirectory();
    AppendRuntimeLog(L"app-server: starting binary=" + binary + L" cwd=" + cwd);
    const BOOL created = CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        environment.data(), cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    if (logHandle && logHandle != INVALID_HANDLE_VALUE) CloseHandle(logHandle);
    if (nullError && nullError != INVALID_HANDLE_VALUE) CloseHandle(nullError);
    if (!created) {
        const DWORD code = GetLastError();
        CloseHandle(childStdoutRead);
        CloseHandle(childStdinWrite);
        error = L"启动 Codex app-server 失败，Win32=" + std::to_wstring(code) + LogHint();
        return false;
    }
    {
        std::scoped_lock lock(processMutex_);
        process_ = process.hProcess;
        processThread_ = process.hThread;
        inputWrite_ = childStdinWrite;
        outputRead_ = childStdoutRead;
    }
    readBuffer_.clear();
    threadId_.clear();
    nextRequestId_ = 1;

    const long long initializeId = nextRequestId_++;
    const std::string initialize =
        "{\"id\":" + std::to_string(initializeId) +
        ",\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"turingdesk\",\"title\":\"Turing Intelligent Desktop\",\"version\":\"0.1\"},\"capabilities\":{\"experimentalApi\":true}}}";
    AppendRuntimeLog(L"app-server: initialize ->");
    if (!WriteLine(initialize)) {
        error = L"Codex initialize 写入失败" + LogHint();
        CleanupProcess();
        return false;
    }
    std::string response;
    if (!WaitForResponse(initializeId, response, error, kInitializeTimeoutMs)) {
        error = L"Codex initialize 失败：" + error + LogHint();
        CleanupProcess();
        return false;
    }
    AppendRuntimeLog(L"app-server: initialize <- ok");
    if (!WriteLine("{\"method\":\"initialized\"}")) {
        error = L"Codex initialized 写入失败" + LogHint();
        CleanupProcess();
        return false;
    }

    const long long threadRequest = nextRequestId_++;
    const std::wstring developerInstructions =
        L"You are 图灵智能桌面 (Turing Intelligent Desktop), the AI agent built into TuringDesk. "
        L"When the user asks who you are, identify yourself as 图灵智能桌面 / Turing Intelligent Desktop, not as raw Codex CLI. "
        L"Use TuringDesk native dynamic tools for supported desktop operations. Never report an action as successful until its tool result reports success. "
        L"Prefer native tools over shell commands for supported operations.";
    const std::string threadStart =
        "{\"id\":" + std::to_string(threadRequest) +
        ",\"method\":\"thread/start\",\"params\":{\"model\":\"" + EscapeJson(setup.model) +
        "\",\"modelProvider\":\"turingdesk\",\"cwd\":\"" + EscapeJson(DesktopDirectory()) +
        "\",\"developerInstructions\":\"" + EscapeJson(developerInstructions) +
        "\",\"dynamicTools\":" + NativeToolDefinitionsJson() +
        ",\"ephemeral\":true}}";
    AppendRuntimeLog(L"app-server: thread/start -> model=" + setup.model);
    if (!WriteLine(threadStart) || !WaitForResponse(threadRequest, response, error, kThreadStartTimeoutMs)) {
        error = L"Codex thread/start 失败：" + error + LogHint();
        CleanupProcess();
        return false;
    }
    const auto threadPos = response.find("\"thread\"");
    const auto id = ExtractJsonString(threadPos == std::string::npos ? std::string_view(response) : std::string_view(response).substr(threadPos), "\"id\"");
    threadId_ = Utf8ToWide(id);
    if (threadId_.empty()) {
        error = L"Codex thread/start 没有返回 thread id" + LogHint();
        CleanupProcess();
        return false;
    }
    AppendRuntimeLog(L"app-server: thread/start <- ok thread=" + threadId_);
    sessionSignature_ = setup.signature;
    return true;
}

bool CodexRuntime::EnsureSession(const ProviderSetup& setup, std::wstring& error) {
    {
        std::scoped_lock lock(processMutex_);
        const bool codexAlive = ProcessAlive(process_);
        const bool relayAlive = !setup.relayRequired || ProcessAlive(relayProcess_);
        if (codexAlive && relayAlive && sessionSignature_ == setup.signature && !threadId_.empty()) return true;
    }
    CleanupProcess();
    AppendRuntimeLog(L"session: creating new Codex session");
    if (setup.relayRequired && !LaunchRelay(setup, error)) {
        CleanupProcess();
        return false;
    }
    if (!LaunchProcess(setup, error)) {
        CleanupProcess();
        return false;
    }
    return true;
}

bool CodexRuntime::WriteLine(const std::string& line) {
    HANDLE handle = nullptr;
    {
        std::scoped_lock lock(processMutex_);
        handle = inputWrite_;
    }
    if (!handle) return false;
    std::string payload = line;
    payload.push_back('\n');
    const char* cursor = payload.data();
    DWORD remaining = static_cast<DWORD>(payload.size());
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, cursor, remaining, &written, nullptr) || written == 0) return false;
        cursor += written;
        remaining -= written;
    }
    return true;
}

bool CodexRuntime::ReadLine(std::string& line, DWORD timeoutMs, std::wstring& error) {
    const ULONGLONG started = GetTickCount64();
    for (;;) {
        const auto newline = readBuffer_.find('\n');
        if (newline != std::string::npos) {
            line = readBuffer_.substr(0, newline);
            readBuffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }

        HANDLE handle = nullptr;
        HANDLE process = nullptr;
        {
            std::scoped_lock lock(processMutex_);
            handle = outputRead_;
            process = process_;
        }
        if (!handle) {
            error = L"Codex app-server 输出管道不存在";
            return false;
        }

        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD code = GetLastError();
            error = L"Codex app-server 输出管道断开，Win32=" + std::to_wstring(code);
            return false;
        }
        if (available > 0) {
            char buffer[4096];
            const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
            DWORD read = 0;
            if (!ReadFile(handle, buffer, toRead, &read, nullptr) || read == 0) {
                error = L"Codex app-server 输出读取失败，Win32=" + std::to_wstring(GetLastError());
                return false;
            }
            readBuffer_.append(buffer, read);
            if (readBuffer_.size() > 4 * 1024 * 1024) {
                error = L"Codex app-server 单条输出异常过大";
                return false;
            }
            continue;
        }
        if (!ProcessAlive(process)) {
            DWORD exitCode = 0;
            if (process) GetExitCodeProcess(process, &exitCode);
            error = L"Codex app-server 已退出，ExitCode=" + std::to_wstring(exitCode);
            return false;
        }
        if (GetTickCount64() - started >= timeoutMs) {
            error = L"Codex app-server 等待输出超时（" + std::to_wstring(timeoutMs / 1000) + L" 秒）";
            return false;
        }
        Sleep(15);
    }
}

bool CodexRuntime::WaitForResponse(long long id, std::string& response, std::wstring& error, DWORD timeoutMs) {
    const ULONGLONG started = GetTickCount64();
    std::string line;
    for (;;) {
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= timeoutMs) {
            error = L"等待 JSON-RPC response id=" + std::to_wstring(id) + L" 超时";
            return false;
        }
        const DWORD remaining = static_cast<DWORD>(timeoutMs - elapsed);
        if (!ReadLine(line, remaining, error)) return false;
        if (!HasResponseId(line, id)) continue;
        response = line;
        if (line.find("\"result\"") == std::string::npos && line.find("\"error\"") != std::string::npos) {
            const auto message = ExtractJsonString(line, "\"message\"");
            error = message.empty() ? L"Codex JSON-RPC 请求失败" : Utf8ToWide(message);
            return false;
        }
        return true;
    }
}

void CodexRuntime::AskAsync(const L3Agent& agent, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone) {
    if (busy_.exchange(true)) {
        if (onDone) onDone(L"Codex Runtime 正在处理上一条请求");
        return;
    }
    if (worker_.joinable()) worker_.join();
    const auto setup = BuildProviderSetup(agent);
    if (!setup.ok) {
        busy_.store(false);
        if (onDone) onDone(setup.message);
        return;
    }
    worker_ = std::jthread([this, setup, prompt = std::move(prompt), onDelta = std::move(onDelta), onDone = std::move(onDone)](std::stop_token stopToken) mutable {
        RunTurn(setup, std::move(prompt), std::move(onDelta), std::move(onDone), stopToken);
        busy_.store(false);
    });
}

void CodexRuntime::RunTurn(ProviderSetup setup, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone, std::stop_token stopToken) {
    std::wstring error;
    if (!EnsureSession(setup, error)) {
        AppendRuntimeLog(L"session: failed: " + error);
        if (onDone) onDone(L"Codex Runtime 启动失败：" + error);
        return;
    }
    const long long requestId = nextRequestId_++;
    const std::string request =
        "{\"id\":" + std::to_string(requestId) +
        ",\"method\":\"turn/start\",\"params\":{\"threadId\":\"" + EscapeJson(threadId_) +
        "\",\"input\":[{\"type\":\"text\",\"text\":\"" + EscapeJson(prompt) + "\"}]}}";
    AppendRuntimeLog(L"app-server: turn/start ->");
    if (!WriteLine(request)) {
        if (onDone) onDone(L"Codex turn/start 写入失败" + LogHint());
        CleanupProcess();
        return;
    }

    bool startAccepted = false;
    bool emittedAgentText = false;
    std::wstring finalAgentText;
    std::string line;
    const ULONGLONG turnStarted = GetTickCount64();
    for (;;) {
        if (stopToken.stop_requested()) break;
        const ULONGLONG elapsed = GetTickCount64() - turnStarted;
        if (elapsed >= kTurnTimeoutMs) {
            error = L"Codex turn 等待模型响应超过 " + std::to_wstring(kTurnTimeoutMs / 1000) + L" 秒";
            break;
        }
        if (!ReadLine(line, static_cast<DWORD>(kTurnTimeoutMs - elapsed), error)) break;

        if (HasResponseId(line, requestId)) {
            if (line.find("\"result\"") == std::string::npos && line.find("\"error\"") != std::string::npos) {
                const auto message = ExtractJsonString(line, "\"message\"");
                const std::wstring done = message.empty() ? L"Codex turn/start 失败" : Utf8ToWide(message);
                AppendRuntimeLog(L"app-server: turn/start <- error: " + done);
                if (onDone) onDone(done + LogHint());
                return;
            }
            startAccepted = true;
            AppendRuntimeLog(L"app-server: turn/start <- accepted");
            continue;
        }

        const auto method = ExtractJsonString(line, "\"method\"");
        if (method == "item/tool/call") {
            long long serverRequestId = 0;
            const auto tool = ExtractJsonString(line, "\"tool\"");
            AppendRuntimeLog(L"app-server: dynamic tool call=" + Utf8ToWide(tool));
            NativeToolResult result;
            if (!TryReadRequestId(line, serverRequestId)) result = {false, L"TuringDesk 无法解析 Codex tool request id。"};
            else if (tool.empty()) result = {false, L"Codex tool request 缺少 tool 名称。"};
            else result = ExecuteNativeTool(tool, line);
            if (serverRequestId != 0) {
                const std::string reply =
                    "{\"id\":" + std::to_string(serverRequestId) +
                    ",\"result\":{\"contentItems\":[{\"type\":\"inputText\",\"text\":\"" + EscapeJson(result.message) +
                    "\"}],\"success\":" + (result.success ? "true" : "false") + "}}";
                if (!WriteLine(reply)) {
                    if (onDone) onDone(L"Codex Native Tool 结果回传失败" + LogHint());
                    CleanupProcess();
                    return;
                }
            }
            continue;
        }
        if (method == "item/agentMessage/delta") {
            const auto delta = ExtractJsonString(line, "\"delta\"");
            if (!delta.empty()) {
                emittedAgentText = true;
                if (onDelta) onDelta(Utf8ToWide(delta));
            }
            continue;
        }
        if (method == "item/completed") {
            const auto type = ExtractJsonString(line, "\"type\"");
            if (type == "agentMessage") {
                const auto text = ExtractJsonString(line, "\"text\"");
                if (!text.empty()) finalAgentText = Utf8ToWide(text);
            }
            continue;
        }
        if (method == "error") {
            const auto message = ExtractJsonString(line, "\"message\"");
            const std::wstring notification = message.empty() ? L"Codex app-server 返回 error 通知" : Utf8ToWide(message);
            if (notification.starts_with(L"Reconnecting...")) {
                bool relayAlive = true;
                DWORD relayExitCode = STILL_ACTIVE;
                std::wstring relayState = L"; relay=not-required";
                if (setup.relayRequired) {
                    std::scoped_lock lock(processMutex_);
                    relayAlive = ProcessAlive(relayProcess_);
                    if (relayProcess_ && !relayAlive) GetExitCodeProcess(relayProcess_, &relayExitCode);
                    relayState = relayAlive ? L"; relay=alive" : L"; relay=exited code=" + std::to_wstring(relayExitCode);
                }
                AppendRuntimeLog(L"app-server: transient reconnect notification: " + notification + relayState);
                if (relayAlive) continue;
                error = L"Codex Relay 已退出，ExitCode=" + std::to_wstring(relayExitCode) + L"；" + notification;
                break;
            }
            error = notification;
            AppendRuntimeLog(L"app-server: error notification: " + error);
            break;
        }
        if (method == "turn/completed") {
            std::wstring done;
            const auto status = ExtractJsonString(line, "\"status\"");
            if (status == "failed") {
                const auto message = ExtractJsonString(line, "\"message\"");
                done = message.empty() ? L"Codex turn 失败" : Utf8ToWide(message);
            } else if (status == "interrupted") {
                done = L"Codex turn 已中断";
            }
            if (done.empty() && !emittedAgentText && !finalAgentText.empty()) {
                emittedAgentText = true;
                if (onDelta) onDelta(finalAgentText);
            }
            if (done.empty() && !emittedAgentText)
                done = L"Codex turn 已完成，但模型没有返回可显示文本";
            AppendRuntimeLog(L"app-server: turn/completed status=" + Utf8ToWide(status));
            if (onDone) onDone(std::move(done));
            return;
        }
    }

    if (stopToken.stop_requested()) {
        AppendRuntimeLog(L"app-server: turn cancelled");
        if (onDone) onDone(L"已取消 Codex 请求");
    } else {
        AppendRuntimeLog(L"app-server: turn aborted: " + error);
        if (onDone) {
            std::wstring done = startAccepted ? L"Codex turn 中断：" : L"Codex turn 未启动：";
            done += error.empty() ? L"app-server 没有继续输出" : error;
            done += LogHint();
            onDone(std::move(done));
        }
    }
    CleanupProcess();
}

void CodexRuntime::Stop() {
    if (worker_.joinable()) worker_.request_stop();
    HANDLE process = nullptr;
    HANDLE relay = nullptr;
    {
        std::scoped_lock lock(processMutex_);
        process = process_;
        relay = relayProcess_;
    }
    if (process) TerminateProcess(process, 0);
    if (relay) TerminateProcess(relay, 0);
}

void CodexRuntime::ResetSession() {
    Stop();
    if (worker_.joinable()) worker_.join();
    busy_.store(false);
    CleanupProcess();
}

void CodexRuntime::CleanupProcess() {
    std::scoped_lock lock(processMutex_);
    if (inputWrite_) { CloseHandle(inputWrite_); inputWrite_ = nullptr; }
    if (outputRead_) { CloseHandle(outputRead_); outputRead_ = nullptr; }
    if (processThread_) { CloseHandle(processThread_); processThread_ = nullptr; }
    if (process_) {
        if (ProcessAlive(process_)) TerminateProcess(process_, 0);
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (relayThread_) { CloseHandle(relayThread_); relayThread_ = nullptr; }
    if (relayProcess_) {
        if (ProcessAlive(relayProcess_)) TerminateProcess(relayProcess_, 0);
        CloseHandle(relayProcess_);
        relayProcess_ = nullptr;
    }
    readBuffer_.clear();
    threadId_.clear();
    sessionSignature_.clear();
}

} // namespace turingdesk
