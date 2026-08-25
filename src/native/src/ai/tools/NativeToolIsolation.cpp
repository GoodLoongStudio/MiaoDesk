#include "turingdesk/NativeTools.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr DWORD kNativeToolTimeoutMs = 30000;

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

std::wstring QuoteArg(std::wstring_view value) {
    std::wstring result = L"\"";
    unsigned backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

fs::path ModulePath() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return fs::path(path);
}

fs::path MakeTempPath(const wchar_t* prefix) {
    wchar_t tempDir[32768]{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(tempDir)), tempDir);
    if (length == 0 || length >= std::size(tempDir)) return {};
    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempFileNameW(tempDir, prefix, 0, tempFile)) return {};
    return fs::path(tempFile);
}

bool WriteBytes(const fs::path& path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void AppendToolLog(const std::wstring& text) {
    const auto path = RuntimeLogPath(L"pi-runtime.log");
    HANDLE handle = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!handle || handle == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[64]{};
    swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const std::wstring line = std::wstring(prefix) + text + L"\r\n";
    const int count = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count > 0) {
        std::string utf8(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                            utf8.data(), count, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(handle);
}

} // namespace

NativeToolResult ExecuteNativeToolIsolated(std::string_view toolName, std::string_view argumentsJson) {
    const auto module = ModulePath();
    if (module.empty()) return {false, L"无法定位 TuringDesk Native Tool worker。"};

    const auto input = MakeTempPath(L"tdi");
    if (input.empty()) return {false, L"无法创建 Native Tool 临时输入文件。"};
    fs::path output = input;
    output += L".out";

    std::error_code ec;
    auto cleanup = [&]() {
        fs::remove(input, ec);
        ec.clear();
        fs::remove(output, ec);
    };

    if (!WriteBytes(input, argumentsJson)) {
        cleanup();
        return {false, L"无法写入 Native Tool 临时输入。"};
    }

    const auto toolWide = Utf8ToWide(toolName);
    std::wstring command = QuoteArg(module.wstring());
    command += L" --native-tool-worker ";
    command += QuoteArg(toolWide);
    command += L" ";
    command += QuoteArg(input.wstring());
    command += L" ";
    command += QuoteArg(output.wstring());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto cwd = module.parent_path().wstring();

    AppendToolLog(L"native-tool: worker start tool=" + toolWide);
    const BOOL created = CreateProcessW(module.c_str(), command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr,
                                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    if (!created) {
        const DWORD code = GetLastError();
        cleanup();
        AppendToolLog(L"native-tool: worker launch failed tool=" + toolWide + L" win32=" + std::to_wstring(code));
        return {false, L"启动 Native Tool worker 失败，Win32=" + std::to_wstring(code)};
    }

    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, kNativeToolTimeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        cleanup();
        AppendToolLog(L"native-tool: worker timeout tool=" + toolWide + L" timeout=30s");
        return {false, L"Native Tool “" + toolWide + L"” 执行超过 30 秒，已终止工具 worker。"};
    }
    if (wait != WAIT_OBJECT_0) {
        const DWORD code = GetLastError();
        TerminateProcess(process.hProcess, 125);
        CloseHandle(process.hProcess);
        cleanup();
        AppendToolLog(L"native-tool: worker wait failed tool=" + toolWide + L" win32=" + std::to_wstring(code));
        return {false, L"等待 Native Tool worker 失败，Win32=" + std::to_wstring(code)};
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);

    const auto bytes = ReadBytes(output);
    cleanup();
    if (bytes.size() < 2 || bytes[1] != '\n' || (bytes[0] != '0' && bytes[0] != '1')) {
        AppendToolLog(L"native-tool: worker invalid result tool=" + toolWide + L" exit=" + std::to_wstring(exitCode));
        return {false, L"Native Tool worker 没有返回有效结果，ExitCode=" + std::to_wstring(exitCode)};
    }

    const bool success = bytes[0] == '1';
    const auto message = Utf8ToWide(std::string_view(bytes).substr(2));
    AppendToolLog(L"native-tool: worker completed tool=" + toolWide +
                  L" success=" + std::wstring(success ? L"true" : L"false") +
                  L" exit=" + std::to_wstring(exitCode));
    return {success, message.empty() ? (success ? L"Native Tool 执行成功。" : L"Native Tool 执行失败。") : message};
}

} // namespace turingdesk
