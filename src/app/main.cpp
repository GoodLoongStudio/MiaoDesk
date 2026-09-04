#include "miaodesk/AppSearch.h"
#include "miaodesk/BuiltinWallpaperCatalog.h"
#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/DesktopWidgetTools.h"
#include "miaodesk/GeneratedDesktopPreview.h"
#include "miaodesk/GozSearch.h"
#include "miaodesk/HarnessProcessManager.h"
#include "miaodesk/L3Agent.h"
#include "miaodesk/NativeTools.h"
#include "miaodesk/PiNativeToolsExtension.h"
#include "miaodesk/RuntimeLogger.h"
#include "miaodesk/SearchWindow.h"
#include "miaodesk/StartupManager.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk {
bool RunL3PersistenceSelfTest();
}

namespace {

constexpr wchar_t kSearchWindowClass[] = L"MiaoDesk.Native.SearchWindow";
constexpr wchar_t kLoopbackNoProxy[] = L"localhost,127.0.0.1,::1";
constexpr wchar_t kHarnessBackgroundMutex[] = L"Local\\MiaoDesk.Native.Harness.Background.Singleton";
constexpr wchar_t kHarnessBackgroundStopEvent[] = L"Local\\MiaoDesk.Native.Harness.Background.Stop";

int ReportUnexpectedExit(int exitCode, std::wstring reason) {
    const std::wstring details = L"原因：" + reason +
        L"；退出代码=" + std::to_wstring(exitCode);
    miaodesk::log::Error(L"AppExit", details);

    const auto logPath = miaodesk::RuntimeLogPath(L"desktop-debug.log");
    std::wstring message = L"MiaoDesk 因非用户操作而退出。\r\n\r\n" + details;
    if (!logPath.empty()) {
        message += L"\r\n日志：" + logPath.wstring();
        message += L"\r\n\r\n请截图此窗口，并将日志文件一并反馈。"
                   L"按“确定”后将打开日志目录。";
    } else {
        message += L"\r\n\r\n日志目录创建失败，请截图此窗口并反馈。";
    }
    MessageBoxW(nullptr, message.c_str(), L"MiaoDesk · 意外退出",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    if (!logPath.empty()) miaodesk::log::OpenLogDirectory();
    return exitCode;
}

LONG WINAPI ReportUnhandledException(EXCEPTION_POINTERS* exception) {
    const DWORD code = exception && exception->ExceptionRecord
        ? exception->ExceptionRecord->ExceptionCode : 0;
    const auto address = exception && exception->ExceptionRecord
        ? exception->ExceptionRecord->ExceptionAddress : nullptr;
    wchar_t details[256]{};
    swprintf_s(details, L"未处理的系统异常；异常代码=0x%08X；地址=%p", code, address);
    ReportUnexpectedExit(static_cast<int>(code ? code : 0xE0000001), details);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void ReportUnexpectedTerminate() noexcept {
    try {
        ReportUnexpectedExit(70, L"发生未处理的 C++ 异常");
    } catch (...) {
        MessageBoxW(nullptr,
                    L"MiaoDesk 发生未处理异常，即将退出。请截图此窗口。",
                    L"MiaoDesk · 意外退出", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    TerminateProcess(GetCurrentProcess(), 70);
    __assume(false);
}

std::wstring ReadEnvironmentValue(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::wstring value(static_cast<std::size_t>(needed), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0 || written >= needed) return {};
    value.resize(written);
    return value;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), count, nullptr, nullptr);
    return out;
}

bool IsAllowedPiNativeTool(std::string_view tool) {
    // Product-state mutation is intentionally NOT allowed through the worker.
    // Pi may read state and create sandbox previews; only the host-owned Apply
    // button can cross the commit boundary.
    return tool == "settings_open" ||
           tool == "ppt_create" ||
           tool == "file_create" ||
           tool == "folder_list" ||
           tool == "file_open" ||
           tool == "wallpaper_validate_package" ||
           tool == "wallpaper_state_get" ||
           tool == "desktop_widget_list" ||
           miaodesk::preview::IsGeneratedPreviewTool(tool);
}

bool NoProxyContains(const std::wstring& raw, std::wstring_view token) {
    const auto lower = Lower(raw);
    const auto wanted = Lower(std::wstring(token));
    std::size_t start = 0;
    while (start <= lower.size()) {
        const auto comma = lower.find(L',', start);
        const auto end = comma == std::wstring::npos ? lower.size() : comma;
        auto item = lower.substr(start, end - start);
        while (!item.empty() && std::iswspace(item.front())) item.erase(item.begin());
        while (!item.empty() && std::iswspace(item.back())) item.pop_back();
        if (item == wanted) return true;
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return false;
}

void EnsureLoopbackProxyBypass() {
    std::wstring noProxy = ReadEnvironmentValue(L"NO_PROXY");
    if (noProxy.empty()) noProxy = ReadEnvironmentValue(L"no_proxy");
    for (const wchar_t* host : {L"localhost", L"127.0.0.1", L"::1"}) {
        if (NoProxyContains(noProxy, host)) continue;
        if (!noProxy.empty() && noProxy.back() != L',') noProxy.push_back(L',');
        noProxy += host;
    }
    if (noProxy.empty()) noProxy = kLoopbackNoProxy;
    SetEnvironmentVariableW(L"NO_PROXY", noProxy.c_str());
}

bool HasLoopbackProxyBypass() {
    const auto noProxy = ReadEnvironmentValue(L"NO_PROXY");
    return NoProxyContains(noProxy, L"localhost") &&
           NoProxyContains(noProxy, L"127.0.0.1") &&
           NoProxyContains(noProxy, L"::1");
}

fs::path ModuleDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return fs::path(path).parent_path();
}

bool NamedMutexExists(const wchar_t* name) {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

bool LaunchHarnessBackgroundOwner() {
    if (NamedMutexExists(kHarnessBackgroundMutex)) return true;
    const fs::path harness = ModuleDirectory() / L"MiaoDeskHarness.exe";
    std::error_code ec;
    if (!fs::is_regular_file(harness, ec)) return false;

    std::wstring command = L"\"" + harness.wstring() + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(harness.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr, harness.parent_path().c_str(), &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void SignalHarnessBackgroundStop() {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kHarnessBackgroundStopEvent);
    if (!event) return;
    SetEvent(event);
    CloseHandle(event);
}

bool PathContainsDirectory(const std::wstring& rawPath, const fs::path& directory) {
    const auto target = Lower(directory.wstring());
    std::size_t start = 0;
    while (start <= rawPath.size()) {
        const auto semicolon = rawPath.find(L';', start);
        const auto end = semicolon == std::wstring::npos ? rawPath.size() : semicolon;
        auto item = rawPath.substr(start, end - start);
        if (item.size() >= 2 && item.front() == L'"' && item.back() == L'"') item = item.substr(1, item.size() - 2);
        while (!item.empty() && std::iswspace(item.front())) item.erase(item.begin());
        while (!item.empty() && std::iswspace(item.back())) item.pop_back();
        if (Lower(item) == target) return true;
        if (semicolon == std::wstring::npos) break;
        start = semicolon + 1;
    }
    return false;
}

void EnsureBundledRuntimePath() {
    const auto module = ModuleDirectory();
    if (module.empty()) return;
    const auto bundledNode = module / L"Runtime" / L"Node";
    std::error_code ec;
    if (!fs::exists(bundledNode, ec) || !fs::is_directory(bundledNode, ec)) return;

    std::wstring path = ReadEnvironmentValue(L"PATH");
    if (PathContainsDirectory(path, bundledNode)) return;
    std::wstring updated = bundledNode.wstring();
    if (!path.empty()) updated += L";" + path;
    SetEnvironmentVariableW(L"PATH", updated.c_str());
}

bool HasBundledRuntimePathIfInstalled() {
    const auto module = ModuleDirectory();
    if (module.empty()) return true;
    const auto bundledNode = module / L"Runtime" / L"Node";
    std::error_code ec;
    if (!fs::exists(bundledNode, ec)) return true;
    return PathContainsDirectory(ReadEnvironmentValue(L"PATH"), bundledNode);
}

int RunNativeToolWorkerIfRequested(bool& handled) {
    handled = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 20;
    if (argc < 2 || _wcsicmp(argv[1], L"--native-tool-worker") != 0) {
        LocalFree(argv);
        return 0;
    }

    handled = true;
    if (argc != 5) {
        LocalFree(argv);
        return 21;
    }

    const std::wstring tool = argv[2];
    const fs::path inputPath(argv[3]);
    const fs::path outputPath(argv[4]);
    LocalFree(argv);

    const auto toolUtf8 = WideToUtf8(tool);
    if (!IsAllowedPiNativeTool(toolUtf8)) return 26;

    std::ifstream input(inputPath, std::ios::binary);
    if (!input) return 22;
    const std::string arguments((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) return 23;

    miaodesk::NativeToolResult result;
    if (miaodesk::preview::IsGeneratedPreviewTool(toolUtf8)) {
        result = miaodesk::preview::ExecuteGeneratedPreviewTool(toolUtf8, arguments);
    } else if (toolUtf8 == "wallpaper_state_get" || toolUtf8 == "desktop_widget_list") {
        result = miaodesk::ExecuteDesktopControlTool(toolUtf8, arguments);
    } else {
        result = miaodesk::ExecuteNativeToolRaw(toolUtf8, arguments);
    }

    std::string payload = result.success ? "1\n" : "0\n";
    payload += WideToUtf8(result.message);

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) return 24;
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    if (!output) return 25;
    return 0;
}

bool RunNativeSelfTest() {
    if (!HasLoopbackProxyBypass()) return false;
    if (!HasBundledRuntimePathIfInstalled()) return false;
    if (!miaodesk::wallpaper::BuiltinWallpaperCatalogSelfTest()) return false;
    if (!miaodesk::wallpaper::DesktopWidgetStore::SelfTest()) return false;

    miaodesk::AppSearch apps;
    apps.BuildIndex();
    const auto appResults = apps.Query(L"Notepad", 5);
    if (apps.Count() < 5 || appResults.empty()) return false;

    miaodesk::GozSearch files;
    if (!files.SelfTest()) return false;

    if (!miaodesk::HarnessProcessManager::SelfTest()) return false;
    if (!miaodesk::RunL3PersistenceSelfTest()) return false;

    miaodesk::L3Agent l3;
    std::wstring reply;
    bool consumedSecret = false;
    if (!l3.TryHandleLocal(L"/time", reply, consumedSecret) || reply.empty() || consumedSecret) return false;

    reply.clear();
    consumedSecret = false;
    if (!l3.TryHandleLocal(L"/status", reply, consumedSecret) || reply.empty() || consumedSecret) return false;
    if (reply.find(L"Harness=未参与") == std::wstring::npos) return false;
    if (reply.find(L"4317") != std::wstring::npos || reply.find(L"4318") != std::wstring::npos || reply.find(L"MCP") != std::wstring::npos) return false;

    reply.clear();
    consumedSecret = false;
    if (!l3.TryHandleLocal(L"/help", reply, consumedSecret) || reply.empty() || consumedSecret) return false;
    if (reply.find(L"/status") == std::wstring::npos || reply.find(L"/time") == std::wstring::npos ||
        reply.find(L"/apps") == std::wstring::npos || reply.find(L"/files") == std::wstring::npos ||
        reply.find(L"/open") == std::wstring::npos || reply.find(L"/open-file") == std::wstring::npos ||
        reply.find(L"/new") == std::wstring::npos) return false;
    if (reply.find(L"4317") != std::wstring::npos || reply.find(L"4318") != std::wstring::npos || reply.find(L"MCP") != std::wstring::npos) return false;

    for (const wchar_t* command : {L"/apps Notepad", L"/files MiaoDesk"}) {
        reply.clear();
        consumedSecret = false;
        if (!l3.TryHandleLocal(command, reply, consumedSecret) || reply.empty() || consumedSecret) return false;
    }

    for (const wchar_t* command : {L"/new", L"/new-chat", L"新对话"}) {
        reply.clear();
        consumedSecret = false;
        if (!l3.TryHandleLocal(command, reply, consumedSecret) || reply.empty() || consumedSecret) return false;
        if (reply.find(L"L3") == std::wstring::npos) return false;
    }

    return true;
}

void ActivateExistingSearchWindow() {
    const HWND existing = FindWindowW(kSearchWindowClass, nullptr);
    if (!existing) return;

    ShowWindow(existing, SW_SHOWNORMAL);
    SetForegroundWindow(existing);
    const HWND edit = FindWindowExW(existing, nullptr, L"EDIT", nullptr);
    if (edit) SetFocus(edit);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetUnhandledExceptionFilter(&ReportUnhandledException);
    std::set_terminate(&ReportUnexpectedTerminate);
    EnsureLoopbackProxyBypass();
    EnsureBundledRuntimePath();

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        return ReportUnexpectedExit(
            3, L"COM 初始化失败，HRESULT=" + std::to_wstring(static_cast<long>(com)));
    }

    bool workerHandled = false;
    const int workerResult = RunNativeToolWorkerIfRequested(workerHandled);
    if (workerHandled) {
        if (SUCCEEDED(com)) CoUninitialize();
        return workerResult;
    }

    std::wstring piExtensionError;
    const bool piExtensionReady = miaodesk::EnsurePiNativeToolsExtension(&piExtensionError);
    if (!piExtensionReady && !piExtensionError.empty()) {
        OutputDebugStringW((L"MiaoDesk Pi extension bootstrap failed: " + piExtensionError + L"\r\n").c_str());
        miaodesk::log::Error(L"PiExtension", piExtensionError);
    }

    const std::wstring_view args = commandLine ? std::wstring_view(commandLine) : std::wstring_view{};
    if (args.find(L"--self-test") != std::wstring_view::npos) {
        const int result = piExtensionReady && RunNativeSelfTest() &&
                           miaodesk::startup::SelfTest() ? 0 : 5;
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    }

    const bool startupLaunch = miaodesk::startup::IsStartupLaunch(args);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\MiaoDesk.Native.Search.Singleton");
    if (!mutex) {
        const DWORD mutexError = GetLastError();
        if (SUCCEEDED(com)) CoUninitialize();
        return ReportUnexpectedExit(
            2, L"无法创建单实例锁，Win32=" + std::to_wstring(mutexError));
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!startupLaunch) ActivateExistingSearchWindow();
        CloseHandle(mutex);
        if (SUCCEEDED(com)) CoUninitialize();
        return 0;
    }

    if (!startupLaunch) miaodesk::startup::PromptForConsentIfNeeded();

    miaodesk::SearchWindow window(instance);
    if (!window.Create(!startupLaunch)) {
        const std::wstring reason = window.LastCreateError().empty()
            ? L"搜索窗口初始化失败" : window.LastCreateError();
        if (SUCCEEDED(com)) CoUninitialize();
        CloseHandle(mutex);
        return ReportUnexpectedExit(4, reason);
    }
    miaodesk::log::Info(L"App", startupLaunch
        ? L"MiaoDesk 登录启动成功，已静默驻留托盘"
        : L"MiaoDesk 主窗口启动成功");

    // Keep MiaoDesk startup responsive, then prewarm the full DeepSeek workbench in the
    // background. If the user opens it earlier, the UI launches the same singleton owner.
    std::jthread harnessWarmup;
    if (!startupLaunch) {
        harnessWarmup = std::jthread([](std::stop_token stopToken) {
            for (int i = 0; i < 30 && !stopToken.stop_requested(); ++i) Sleep(100);
            if (!stopToken.stop_requested()) LaunchHarnessBackgroundOwner();
        });
    }

    const int result = window.RunMessageLoop();
    harnessWarmup.request_stop();
    SignalHarnessBackgroundStop();
    if (SUCCEEDED(com)) CoUninitialize();
    CloseHandle(mutex);
    if (result == -1) {
        return ReportUnexpectedExit(
            6, L"Windows 消息循环失败，Win32=" +
               std::to_wstring(window.MessageLoopError()));
    }
    if (!window.ExitExpected()) {
        return ReportUnexpectedExit(7, L"主窗口被意外销毁");
    }
    miaodesk::log::Info(
        L"AppExit", L"MiaoDesk 正常退出；退出代码=" + std::to_wstring(result));
    return result;
}
