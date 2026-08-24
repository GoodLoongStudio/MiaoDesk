#include "turingdesk/PiRuntime.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <wincred.h>
#include <shlobj.h>
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
constexpr DWORD kTurnTimeoutMs = 600000;

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
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

fs::path PiAgentDirectory() {
    return LocalAppDataRoot() / L"PiAgent";
}

fs::path PiLogPath() {
    return RuntimeLogPath(L"pi-runtime.log");
}

void AppendRuntimeLog(const std::wstring& text) {
    const auto path = PiLogPath();
    HANDLE handle = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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

std::wstring FindNodePath() {
    const auto bundled = ModuleDirectory() / L"Runtime" / L"Node" / L"node.exe";
    std::error_code ec;
    if (fs::exists(bundled, ec) && fs::is_regular_file(bundled, ec)) return bundled.wstring();
    return SearchExecutable(L"node.exe");
}

std::wstring FindPiPath() {
    const auto bundled = ModuleDirectory() / L"Pi" / L"node_modules" / L"@earendil-works" / L"pi-coding-agent" / L"dist" / L"cli.js";
    std::error_code ec;
    if (fs::exists(bundled, ec) && fs::is_regular_file(bundled, ec)) return bundled.wstring();
    return {};
}

std::wstring PowerShellPath() {
    wchar_t windowsDir[32768]{};
    const UINT count = GetWindowsDirectoryW(windowsDir, static_cast<UINT>(std::size(windowsDir)));
    if (count > 0 && count < std::size(windowsDir)) {
        const auto candidate = fs::path(std::wstring(windowsDir, count)) / L"System32" / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return candidate.wstring();
    }
    return SearchExecutable(L"powershell.exe");
}

std::wstring DesktopDirectory() {
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &known)) && known) {
        std::wstring result(known);
        CoTaskMemFree(known);
        if (!result.empty()) return result;
    }
    return ModuleDirectory().wstring();
}

std::wstring NormalizeBaseUrl(const L3Agent& agent) {
    if (!agent.Config().baseUrl.empty()) return agent.Config().baseUrl;
    auto url = agent.CurrentApiUrl();
    const auto lower = Lower(url);
    for (const wchar_t* suffix : {L"/chat/completions", L"/responses", L"/messages"}) {
        const std::wstring value(suffix);
        if (lower.size() >= value.size() && lower.substr(lower.size() - value.size()) == value) {
            url.resize(url.size() - value.size());
            break;
        }
    }
    return url;
}

std::wstring DetectApiType(const L3Agent& agent) {
    const auto provider = Lower(agent.Config().providerId);
    const auto endpoint = Lower(agent.Config().endpoint + L" " + agent.CurrentApiUrl());
    if (endpoint.find(L"responses") != std::wstring::npos) return L"openai-responses";
    if (endpoint.find(L"messages") != std::wstring::npos || provider.find(L"anthropic") != std::wstring::npos || provider.find(L"claude") != std::wstring::npos)
        return L"anthropic-messages";
    if (provider.find(L"google") != std::wstring::npos || provider.find(L"gemini") != std::wstring::npos || endpoint.find(L"generativelanguage") != std::wstring::npos)
        return L"google-generative-ai";
    return L"openai-completions";
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
    std::vector<wchar_t> result(chars, L'\0');
    std::size_t offset = 0;
    for (const auto& entry : entries) {
        std::copy(entry.begin(), entry.end(), result.begin() + static_cast<std::ptrdiff_t>(offset));
        offset += entry.size();
        result[offset++] = L'\0';
    }
    result[offset] = L'\0';
    return result;
}

bool ProcessAlive(HANDLE process) {
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

} // namespace

PiRuntime::~PiRuntime() {
    Stop();
    CleanupProcess();
}

PiRuntime::ProviderSetup PiRuntime::BuildProviderSetup(const L3Agent& agent) const {
    ProviderSetup setup;
    setup.nodePath = FindNodePath();
    setup.piPath = FindPiPath();
    setup.agentDir = PiAgentDirectory().wstring();
    setup.baseUrl = NormalizeBaseUrl(agent);
    setup.model = agent.Config().model;
    setup.apiType = DetectApiType(agent);
    setup.apiKey = LoadApiKey();

    if (setup.nodePath.empty()) { setup.message = L"未找到 Node Runtime"; return setup; }
    if (setup.piPath.empty()) { setup.message = L"未找到 Pi Runtime"; return setup; }
    if (PowerShellPath().empty()) { setup.message = L"未找到 Windows PowerShell"; return setup; }
    if (setup.baseUrl.empty()) { setup.message = L"未配置 Base URL"; return setup; }
    if (setup.model.empty()) { setup.message = L"未配置 Model"; return setup; }
    if (setup.apiKey.empty()) { setup.message = L"未配置 API Key"; return setup; }

    setup.signature = setup.apiType + L"|" + setup.baseUrl + L"|" + setup.model;
    setup.ok = true;
    setup.message = L"Pi Runtime 就绪";
    return setup;
}

PiRuntimeStatus PiRuntime::Status(const L3Agent& agent) const {
    PiRuntimeStatus status;
    const auto setup = BuildProviderSetup(agent);
    status.nodePath = setup.nodePath;
    status.piPath = setup.piPath;
    status.nodeAvailable = !setup.nodePath.empty();
    status.piAvailable = !setup.piPath.empty();
    {
        std::scoped_lock lock(processMutex_);
        status.running = ProcessAlive(process_);
    }
    status.message = setup.message;
    return status;
}

bool PiRuntime::CanHandle(const L3Agent& agent) const {
    return BuildProviderSetup(agent).ok;
}

bool PiRuntime::ConfigurePiAgent(const ProviderSetup& setup, std::wstring& error) const {
    std::error_code ec;
    fs::create_directories(setup.agentDir, ec);
    if (ec) { error = L"无法创建 Pi Agent 配置目录"; return false; }

    const auto modelsPath = fs::path(setup.agentDir) / L"models.json";
    const auto settingsPath = fs::path(setup.agentDir) / L"settings.json";
    const auto base = EscapeJson(setup.baseUrl);
    const auto api = EscapeJson(setup.apiType);
    const auto model = EscapeJson(setup.model);
    const auto shell = EscapeJson(PowerShellPath());

    std::string models = "{\n  \"providers\": {\n    \"turingdesk\": {\n";
    models += "      \"name\": \"TuringDesk Provider\",\n";
    models += "      \"baseUrl\": \"" + base + "\",\n";
    models += "      \"api\": \"" + api + "\",\n";
    models += "      \"apiKey\": \"$TURINGDESK_MODEL_API_KEY\",\n";
    models += "      \"models\": [{ \"id\": \"" + model + "\", \"name\": \"" + model + "\", \"input\": [\"text\", \"image\"], \"contextWindow\": 128000, \"maxTokens\": 16384 }]\n";
    models += "    }\n  }\n}\n";

    std::ofstream modelsFile(modelsPath, std::ios::binary | std::ios::trunc);
    if (!modelsFile) { error = L"无法写入 Pi models.json"; return false; }
    modelsFile.write(models.data(), static_cast<std::streamsize>(models.size()));
    if (!modelsFile) { error = L"写入 Pi models.json 失败"; return false; }

    std::string settings = "{\n";
    settings += "  \"defaultProjectTrust\": \"always\",\n";
    settings += "  \"defaultProvider\": \"turingdesk\",\n";
    settings += "  \"defaultModel\": \"" + model + "\",\n";
    settings += "  \"shellPath\": \"" + shell + "\",\n";
    settings += "  \"quietStartup\": true\n";
    settings += "}\n";
    std::ofstream settingsFile(settingsPath, std::ios::binary | std::ios::trunc);
    if (!settingsFile) { error = L"无法写入 Pi settings.json"; return false; }
    settingsFile.write(settings.data(), static_cast<std::streamsize>(settings.size()));
    if (!settingsFile) { error = L"写入 Pi settings.json 失败"; return false; }
    return true;
}

bool PiRuntime::LaunchProcess(const ProviderSetup& setup, std::wstring& error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE childInRead = nullptr;
    HANDLE childOutWrite = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;

    if (!CreatePipe(&childInRead, &inputWrite, &security, 0) ||
        !CreatePipe(&outputRead, &childOutWrite, &security, 0)) {
        error = L"无法创建 Pi stdio 管道";
        if (childInRead) CloseHandle(childInRead);
        if (inputWrite) CloseHandle(inputWrite);
        if (outputRead) CloseHandle(outputRead);
        if (childOutWrite) CloseHandle(childOutWrite);
        return false;
    }
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE childErrWrite = CreateFileW(PiLogPath().c_str(), FILE_APPEND_DATA,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!childErrWrite || childErrWrite == INVALID_HANDLE_VALUE) childErrWrite = childOutWrite;

    const std::wstring systemPrompt =
        L"You are Turing Intelligent Desktop AI. Use tools for real file and desktop actions. "
        L"The tool named bash is backed by Windows PowerShell 5.1 in TuringDesk; use PowerShell syntax, not POSIX shell syntax. "
        L"Never claim an action succeeded unless the tool result confirms it.";

    std::wstring command = QuoteArg(setup.nodePath) + L" " + QuoteArg(setup.piPath) +
        L" --mode rpc --no-session --approve --provider turingdesk --model " + QuoteArg(setup.model) +
        L" --tools read,bash,edit,write,grep,find,ls --append-system-prompt " + QuoteArg(systemPrompt);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    auto environment = BuildEnvironmentBlock({
        {L"PI_CODING_AGENT_DIR", setup.agentDir},
        {L"PI_OFFLINE", L"1"},
        {L"PI_SKIP_VERSION_CHECK", L"1"},
        {L"PI_TELEMETRY", L"0"},
        {kApiKeyEnvironment, setup.apiKey},
    });

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInRead;
    startup.hStdOutput = childOutWrite;
    startup.hStdError = childErrWrite;

    PROCESS_INFORMATION processInfo{};
    const auto cwd = DesktopDirectory();
    const BOOL created = CreateProcessW(setup.nodePath.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(),
                                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &processInfo);

    CloseHandle(childInRead);
    CloseHandle(childOutWrite);
    if (childErrWrite && childErrWrite != INVALID_HANDLE_VALUE && childErrWrite != childOutWrite) CloseHandle(childErrWrite);

    if (!created) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        error = L"Pi Runtime 启动失败，Win32=" + std::to_wstring(GetLastError());
        return false;
    }

    {
        std::scoped_lock lock(processMutex_);
        process_ = processInfo.hProcess;
        processThread_ = processInfo.hThread;
        inputWrite_ = inputWrite;
        outputRead_ = outputRead;
        readBuffer_.clear();
        sessionSignature_ = setup.signature;
    }

    AppendRuntimeLog(L"Pi process started; node=" + setup.nodePath + L"; pi=" + setup.piPath +
                     L"; api=" + setup.apiType + L"; model=" + setup.model + L"; shell=PowerShell");
    return true;
}

bool PiRuntime::EnsureSession(const ProviderSetup& setup, std::wstring& error) {
    {
        std::scoped_lock lock(processMutex_);
        if (ProcessAlive(process_) && sessionSignature_ == setup.signature && inputWrite_ && outputRead_) return true;
    }
    CleanupProcess();
    if (!ConfigurePiAgent(setup, error)) return false;
    return LaunchProcess(setup, error);
}

bool PiRuntime::WriteLine(const std::string& line) {
    std::scoped_lock lock(processMutex_);
    if (!inputWrite_ || !ProcessAlive(process_)) return false;
    std::string payload = line;
    payload.push_back('\n');
    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(payload.size());
    return WriteFile(inputWrite_, payload.data(), size, &written, nullptr) && written == size;
}

bool PiRuntime::ReadLine(std::string& line, DWORD timeoutMs, std::wstring& error) {
    const ULONGLONG started = GetTickCount64();
    for (;;) {
        {
            std::scoped_lock lock(processMutex_);
            const auto newline = readBuffer_.find('\n');
            if (newline != std::string::npos) {
                line = readBuffer_.substr(0, newline);
                readBuffer_.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            if (!outputRead_) { error = L"Pi stdout 管道已关闭"; return false; }

            DWORD available = 0;
            if (!PeekNamedPipe(outputRead_, nullptr, 0, nullptr, &available, nullptr)) {
                error = L"读取 Pi stdout 状态失败";
                return false;
            }
            if (available > 0) {
                char buffer[4096];
                DWORD read = 0;
                const DWORD wanted = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
                if (!ReadFile(outputRead_, buffer, wanted, &read, nullptr) || read == 0) {
                    error = L"读取 Pi stdout 失败";
                    return false;
                }
                readBuffer_.append(buffer, buffer + read);
                continue;
            }
            if (!ProcessAlive(process_)) {
                DWORD code = 0;
                if (process_) GetExitCodeProcess(process_, &code);
                error = L"Pi Runtime 已退出，code=" + std::to_wstring(code);
                return false;
            }
        }

        if (GetTickCount64() - started >= timeoutMs) {
            error = L"Pi RPC 等待超时";
            return false;
        }
        Sleep(15);
    }
}

void PiRuntime::RunTurn(ProviderSetup setup, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone, std::stop_token stopToken) {
    std::wstring error;
    if (!EnsureSession(setup, error)) {
        AppendRuntimeLog(L"Pi session failed: " + error);
        if (onDone) onDone(error);
        busy_.store(false);
        return;
    }

    const std::string request = "{\"id\":\"turingdesk-turn\",\"type\":\"prompt\",\"message\":\"" + EscapeJson(prompt) + "\"}";
    if (!WriteLine(request)) {
        error = L"发送 Pi prompt 失败";
        AppendRuntimeLog(error);
        if (onDone) onDone(error);
        busy_.store(false);
        return;
    }

    AppendRuntimeLog(L"Pi turn started; model=" + setup.model);
    const ULONGLONG turnStarted = GetTickCount64();
    bool sawAgentEnd = false;
    bool sawText = false;

    while (!stopToken.stop_requested()) {
        if (GetTickCount64() - turnStarted >= kTurnTimeoutMs) {
            error = L"Pi Agent turn 超时";
            break;
        }

        std::string line;
        std::wstring readError;
        if (!ReadLine(line, 1000, readError)) {
            if (readError == L"Pi RPC 等待超时") continue;
            error = readError;
            break;
        }
        if (line.empty()) continue;

        if (line.find("\"type\":\"message_update\"") != std::string::npos &&
            line.find("\"type\":\"text_delta\"") != std::string::npos) {
            const auto delta = ExtractJsonString(line, "\"delta\"");
            if (!delta.empty()) {
                sawText = true;
                if (onDelta) onDelta(Utf8ToWide(delta));
            }
            continue;
        }

        if (line.find("\"type\":\"tool_execution_start\"") != std::string::npos) {
            const auto tool = Utf8ToWide(ExtractJsonString(line, "\"toolName\""));
            AppendRuntimeLog(L"tool start: " + (tool.empty() ? std::wstring(L"unknown") : tool));
            continue;
        }
        if (line.find("\"type\":\"tool_execution_end\"") != std::string::npos) {
            const auto tool = Utf8ToWide(ExtractJsonString(line, "\"toolName\""));
            const bool isError = line.find("\"isError\":true") != std::string::npos;
            AppendRuntimeLog(L"tool end: " + (tool.empty() ? std::wstring(L"unknown") : tool) + (isError ? L"; error=true" : L"; error=false"));
            continue;
        }
        if (line.find("\"type\":\"response\"") != std::string::npos &&
            line.find("\"command\":\"prompt\"") != std::string::npos &&
            line.find("\"success\":false") != std::string::npos) {
            const auto rpcError = Utf8ToWide(ExtractJsonString(line, "\"error\""));
            error = rpcError.empty() ? L"Pi RPC 拒绝 prompt" : rpcError;
            break;
        }
        if (line.find("\"type\":\"agent_end\"") != std::string::npos) {
            sawAgentEnd = true;
            break;
        }
    }

    if (stopToken.stop_requested()) {
        WriteLine("{\"type\":\"abort\"}");
        error = L"已停止";
    }

    if (!error.empty()) {
        AppendRuntimeLog(L"Pi turn failed: " + error);
        if (onDone) onDone(error);
    } else if (sawAgentEnd) {
        AppendRuntimeLog(L"Pi turn completed");
        if (onDone) onDone(sawText ? L"" : L"[Pi 已完成，无文本输出]");
    } else {
        AppendRuntimeLog(L"Pi turn ended without agent_end");
        if (onDone) onDone(L"Pi turn 未正常结束");
    }
    busy_.store(false);
}

void PiRuntime::AskAsync(const L3Agent& agent, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone) {
    if (busy_.exchange(true)) {
        if (onDone) onDone(L"Pi Runtime 正忙");
        return;
    }
    const auto setup = BuildProviderSetup(agent);
    if (!setup.ok) {
        busy_.store(false);
        if (onDone) onDone(setup.message);
        return;
    }
    if (worker_.joinable()) worker_.request_stop();
    worker_ = std::jthread([this, setup, prompt = std::move(prompt), onDelta = std::move(onDelta), onDone = std::move(onDone)](std::stop_token token) mutable {
        RunTurn(setup, std::move(prompt), std::move(onDelta), std::move(onDone), token);
    });
}

void PiRuntime::Stop() {
    if (worker_.joinable()) worker_.request_stop();
    WriteLine("{\"type\":\"abort\"}");
    busy_.store(false);
}

void PiRuntime::ResetSession() {
    if (!WriteLine("{\"type\":\"new_session\"}")) {
        CleanupProcess();
        return;
    }
    AppendRuntimeLog(L"Pi session reset requested");
}

void PiRuntime::CleanupProcess() {
    std::scoped_lock lock(processMutex_);
    if (inputWrite_) { CloseHandle(inputWrite_); inputWrite_ = nullptr; }
    if (outputRead_) { CloseHandle(outputRead_); outputRead_ = nullptr; }
    if (process_) {
        if (ProcessAlive(process_)) {
            TerminateProcess(process_, 0);
            WaitForSingleObject(process_, 2000);
        }
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (processThread_) { CloseHandle(processThread_); processThread_ = nullptr; }
    readBuffer_.clear();
    sessionSignature_.clear();
}

} // namespace turingdesk
