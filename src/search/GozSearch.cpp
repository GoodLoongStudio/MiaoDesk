#include "miaodesk/GozSearch.h"
#include "miaodesk/MiaoGozRecovery.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\goz-v1";
constexpr DWORD kQueryTimeoutMs = 5000;
constexpr DWORD kL3SyncQueryTimeoutMs = 2500;

#pragma pack(push, 1)
struct GozReplyHeader {
    std::uint32_t magic;
    std::uint32_t count;
};

struct GozReplyItem {
    std::uint32_t pathChars;
    std::uint32_t flags;
};
#pragma pack(pop)

constexpr std::uint32_t kReplyMagic = 0x315A4754; // TGZ1
constexpr std::uint32_t kFailedReplyMagic = 0x465A4754; // TGZF
constexpr std::uint32_t kDirectoryFlag = 0x1;

fs::path ModuleDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return fs::path(path).parent_path();
}

std::wstring SearchExecutable(const wchar_t* name) {
    std::wstring buffer(32768, L'\0');
    const DWORD count = SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (count == 0 || count >= buffer.size()) return {};
    buffer.resize(count);
    return buffer;
}

std::wstring QuoteArgument(const std::wstring& value) {
    if (value.empty()) return L"\"\"";
    if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) return value;

    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        if (ch == L'\"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            slashes = 0;
            continue;
        }
        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(ch);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0)
        count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

std::vector<std::wstring> SplitPaths(const std::string& output, DWORD maxResults) {
    std::vector<std::wstring> paths;
    std::size_t start = 0;
    while (start < output.size() && paths.size() < maxResults) {
        const auto end = output.find('\n', start);
        std::string line = output.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            auto path = Utf8ToWide(line);
            if (!path.empty()) paths.push_back(std::move(path));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return paths;
}

bool RunGozQuery(const std::wstring& binary, const std::wstring& query, DWORD maxResults,
                 std::vector<std::wstring>& paths, DWORD timeoutMs = kQueryTimeoutMs) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &security, 0)) return false;
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nullError = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? nullptr : nullInput;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = nullError == INVALID_HANDLE_VALUE ? stdoutWrite : nullError;

    PROCESS_INFORMATION process{};
    std::wstring command = QuoteArgument(binary) + L" -n " + std::to_wstring(maxResults) + L" " + QuoteArgument(query);
    const BOOL created = CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr, nullptr, &startup, &process);

    CloseHandle(stdoutWrite);
    if (nullInput && nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
    if (nullError && nullError != INVALID_HANDLE_VALUE) CloseHandle(nullError);

    if (!created) {
        CloseHandle(stdoutRead);
        return false;
    }

    const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }

    std::string output;
    char buffer[8192];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(stdoutRead, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) || read == 0) break;
        output.append(buffer, read);
        if (output.size() > 1024 * 1024) break;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(stdoutRead);

    if (wait == WAIT_TIMEOUT || exitCode != 0) return false;
    paths = SplitPaths(output, maxResults);
    return true;
}

std::vector<std::byte> EncodeReply(const std::vector<std::wstring>& paths, bool succeeded = true) {
    std::size_t bytes = sizeof(GozReplyHeader);
    for (const auto& path : paths)
        bytes += sizeof(GozReplyItem) + path.size() * sizeof(wchar_t);

    std::vector<std::byte> payload(bytes);
    auto* header = reinterpret_cast<GozReplyHeader*>(payload.data());
    header->magic = succeeded ? kReplyMagic : kFailedReplyMagic;
    header->count = static_cast<std::uint32_t>(paths.size());

    std::size_t offset = sizeof(GozReplyHeader);
    for (const auto& path : paths) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        GozReplyItem item{};
        item.pathChars = static_cast<std::uint32_t>(path.size());
        item.flags = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)
            ? kDirectoryFlag : 0;
        std::memcpy(payload.data() + offset, &item, sizeof(item));
        offset += sizeof(item);
        if (!path.empty()) {
            const auto pathBytes = path.size() * sizeof(wchar_t);
            std::memcpy(payload.data() + offset, path.data(), pathBytes);
            offset += pathBytes;
        }
    }
    return payload;
}

} // namespace

GozSearch::GozSearch() : state_(std::make_shared<SharedState>()) {}

std::wstring GozSearch::FindClientBinary() {
    wchar_t explicitPath[32768]{};
    const DWORD explicitCount = GetEnvironmentVariableW(L"MIAODESK_GOZ_CLI", explicitPath,
                                                         static_cast<DWORD>(std::size(explicitPath)));
    if (explicitCount > 0 && explicitCount < std::size(explicitPath)) {
        std::error_code ec;
        if (fs::exists(explicitPath, ec)) return explicitPath;
    }

    const auto bundled = ModuleDirectory() / L"Goz" / L"goz.exe";
    std::error_code ec;
    if (!bundled.empty() && fs::exists(bundled, ec)) return bundled.wstring();

    return SearchExecutable(L"goz.exe");
}

bool GozSearch::PipeAvailable() {
    if (WaitNamedPipeW(kPipeName, 0)) return true;
    const DWORD error = GetLastError();
    return error == ERROR_SEM_TIMEOUT || error == ERROR_PIPE_BUSY;
}

bool GozSearch::EnsurePipeAvailable(DWORD waitMs,
                                     goz_recovery::GozRecoveryOutcome* outcome) {
    // 观测值,按执行顺序收集。之后一次性交给 MiaoGozRecovery 判定 ——
    // 判定是纯逻辑,本机可测 61 项;这里只负责诚实地记录每一步发生了什么。
    const bool pipeUpAtEntry = PipeAvailable();
    if (pipeUpAtEntry) {
        if (outcome) *outcome = goz_recovery::GozRecoveryOutcome::NotNeeded;
        return true;
    }

    // The installer registers gozd as the "goz" auto-start service, but Windows
    // upgrades / resume can leave it stopped briefly. Recover it on demand instead
    // of silently removing file search from the launcher for the whole session.
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    bool scmOpened = manager != nullptr;
    bool serviceOpened = false;
    auto state = goz_recovery::GozServiceState::Unknown;
    bool startIssued = false;
    if (manager) {
        SC_HANDLE service = OpenServiceW(
            manager, L"goz", SERVICE_QUERY_STATUS | SERVICE_START);
        if (service) {
            serviceOpened = true;
            SERVICE_STATUS_PROCESS status{};
            DWORD bytesNeeded = 0;
            if (QueryServiceStatusEx(
                    service, SC_STATUS_PROCESS_INFO,
                    reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
                state = goz_recovery::DescribeGozServiceState(status.dwCurrentState);
                // 只有停着才拉。START_PENDING 上再叫一次会拿到
                // ERROR_SERVICE_ALREADY_RUNNING,而调用方一律忽略返回值 ——
                // 于是"其实已经在起"会被记成"启动被拒绝",用户看到一个假的原因。
                if (status.dwCurrentState == SERVICE_STOPPED) {
                    startIssued = StartServiceW(service, 0, nullptr) != FALSE;
                }
            }
            CloseServiceHandle(service);
        }
        CloseServiceHandle(manager);
    }

    // 轮询。原来是 `do { ... } while (now < deadline)` —— 那会让 waitMs == 0
    // 也先睡满 100ms。改成先判再睡,0 就是 0。
    bool pipeUpAtExit = PipeAvailable();
    const ULONGLONG deadline = GetTickCount64() + waitMs;
    while (!pipeUpAtExit && GetTickCount64() < deadline) {
        Sleep(100);
        pipeUpAtExit = PipeAvailable();
    }

    const auto decided = goz_recovery::DecideGozRecovery(pipeUpAtEntry, scmOpened,
                                                          serviceOpened, state, startIssued,
                                                          pipeUpAtExit);
    if (outcome) *outcome = decided;
    return pipeUpAtExit;
}

bool GozSearch::Available() const {
    // "Available" means the file-search client is installed. Service readiness is
    // recovered asynchronously by Query(), so the UI never disables the feature
    // merely because the named pipe is late during login/resume.
    return !FindClientBinary().empty();
}

bool GozSearch::Query(HWND replyWindow, const std::wstring& query, DWORD maxResults) const {
    if (!replyWindow || !IsWindow(replyWindow) || query.empty() || maxResults == 0) return false;
    const auto binary = FindClientBinary();
    if (binary.empty()) return false;

    const auto state = state_;
    // 起一次查询先占一个代号。用户多敲一个字符就再占一个,而在飞的那次就此作废 ——
    // gozd 是另一个进程,快的那次完全可能后到,不设防就会把新结果盖掉。
    const SearchGeneration claimed = state->generation.Claim();
    std::thread([state, claimed, binary, replyWindow, query, maxResults,
                 this]() {
        std::vector<std::wstring> paths;
        // 恢复结论写进成员,让界面那句话能说真话。worker 线程写、UI 线程读,
        // 与 generation 同一个模式;它是诊断快照,不是状态机输入(见头注释)。
        goz_recovery::GozRecoveryOutcome recovery{goz_recovery::GozRecoveryOutcome::NotNeeded};
        const bool pipeReady = EnsurePipeAvailable(2000, &recovery);
        lastRecovery_ = recovery;
        const bool succeeded = pipeReady && RunGozQuery(binary, query, maxResults, paths);
        if (!state->generation.ShouldDeliver(claimed) || !IsWindow(replyWindow)) return;

        // Always notify the UI. Returning silently on a CLI timeout/error leaves
        // the current query stuck in its pending state indefinitely.
        auto payload = EncodeReply(paths, succeeded);
        COPYDATASTRUCT copyData{};
        copyData.dwData = GozSearch::kReplyId;
        copyData.cbData = static_cast<DWORD>(payload.size());
        copyData.lpData = payload.data();
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(replyWindow, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copyData),
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &ignored);
    }).detach();
    return true;
}

std::vector<SearchResult> GozSearch::QuerySync(const std::wstring& query, DWORD maxResults) const {
    std::vector<SearchResult> results;
    if (query.empty() || maxResults == 0) return results;
    const auto binary = FindClientBinary();
    if (binary.empty()) return results;
    // 同步查询也记恢复结论:同一个对象上,后一次异步查询会覆盖它,
    // 而那是正确的 —— 最近的才是用户要看的。
    if (!EnsurePipeAvailable(1500, &lastRecovery_)) return results;

    std::vector<std::wstring> paths;
    if (!RunGozQuery(binary, query, maxResults, paths, kL3SyncQueryTimeoutMs)) return results;
    results.reserve(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto& fullPath = paths[i];
        if (fullPath.empty()) continue;
        fs::path path(fullPath);
        std::wstring title = path.filename().wstring();
        if (title.empty()) title = fullPath;
        const DWORD attributes = GetFileAttributesW(fullPath.c_str());
        const bool directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
        results.push_back({directory ? ResultKind::Folder : ResultKind::File,
                           std::move(title), fullPath, fullPath,
                           500.0 - static_cast<double>(i)});
    }
    return results;
}

bool GozSearch::HandleCopyData(const COPYDATASTRUCT* copyData, std::vector<SearchResult>& results,
                               bool* querySucceeded) const {
    if (!copyData || copyData->dwData != kReplyId || !copyData->lpData) return false;
    if (copyData->cbData < sizeof(GozReplyHeader)) return true;

    const auto* base = reinterpret_cast<const std::byte*>(copyData->lpData);
    GozReplyHeader header{};
    std::memcpy(&header, base, sizeof(header));
    if (header.magic != kReplyMagic && header.magic != kFailedReplyMagic) return true;

    const bool succeeded = header.magic == kReplyMagic;
    if (querySucceeded) *querySucceeded = succeeded;
    results.clear();
    if (!succeeded) return true;

    std::size_t offset = sizeof(GozReplyHeader);
    results.reserve(header.count);
    for (std::uint32_t i = 0; i < header.count; ++i) {
        if (offset > copyData->cbData || copyData->cbData - offset < sizeof(GozReplyItem)) break;
        GozReplyItem item{};
        std::memcpy(&item, base + offset, sizeof(item));
        offset += sizeof(item);

        const std::size_t pathBytes = static_cast<std::size_t>(item.pathChars) * sizeof(wchar_t);
        if (offset > copyData->cbData || pathBytes > copyData->cbData - offset) break;
        std::wstring fullPath(item.pathChars, L'\0');
        if (pathBytes) std::memcpy(fullPath.data(), base + offset, pathBytes);
        offset += pathBytes;
        if (fullPath.empty()) continue;

        fs::path path(fullPath);
        std::wstring title = path.filename().wstring();
        if (title.empty()) title = fullPath;
        const bool directory = (item.flags & kDirectoryFlag) != 0;
        results.push_back({directory ? ResultKind::Folder : ResultKind::File,
                           std::move(title), fullPath, fullPath,
                           500.0 - static_cast<double>(i)});
    }
    return true;
}

bool GozSearch::SelfTest() const {
    const std::vector<std::wstring> expected{L"C:\\MiaoDesk\\verify.txt", L"C:\\MiaoDesk\\Folder"};
    auto payload = EncodeReply(expected);
    auto* second = reinterpret_cast<GozReplyItem*>(payload.data() + sizeof(GozReplyHeader) +
        sizeof(GozReplyItem) + expected[0].size() * sizeof(wchar_t));
    second->flags |= kDirectoryFlag;

    COPYDATASTRUCT copyData{};
    copyData.dwData = kReplyId;
    copyData.cbData = static_cast<DWORD>(payload.size());
    copyData.lpData = payload.data();
    std::vector<SearchResult> results;
    bool succeeded = false;
    if (!HandleCopyData(&copyData, results, &succeeded) || !succeeded || results.size() != 2 ||
        results[0].kind != ResultKind::File || results[0].title != L"verify.txt" ||
        results[1].kind != ResultKind::Folder || results[1].title != L"Folder") {
        return false;
    }

    auto failedPayload = EncodeReply({}, false);
    copyData.cbData = static_cast<DWORD>(failedPayload.size());
    copyData.lpData = failedPayload.data();
    succeeded = true;
    results.push_back({ResultKind::File, L"stale", L"", L"", 0});
    return HandleCopyData(&copyData, results, &succeeded) && !succeeded && results.empty();
}

void GozSearch::Shutdown() const {
    // 无条件作废在飞的那次查询 —— 界面每收到一次输入都调这里,包括**这次起不来
    // 新查询**的分支(空输入、`/` 命令、goz 客户端没装)。那些分支都自然会 return,
    // 于是必须先作废再早退,否则一次在飞的旧回包会在几十毫秒后盖在用户已经看到的
    // 命令提示或空状态上。见 MiaoSearchGeneration.h。
    if (state_) state_->generation.Invalidate();
}

} // namespace miaodesk
