#include "turingdesk/AppSearch.h"
#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/DesktopWidgetTools.h"
#include "turingdesk/GozSearch.h"
#include "turingdesk/HarnessProcessManager.h"
#include "turingdesk/L3Agent.h"
#include "turingdesk/NativeTools.h"
#include "turingdesk/PiNativeToolsExtension.h"
#include "turingdesk/SearchWindow.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace turingdesk {
bool RunL3PersistenceSelfTest();
}

namespace {

constexpr wchar_t kSearchWindowClass[] = L"TuringDesk.Native.SearchWindow";
constexpr wchar_t kLoopbackNoProxy[] = L"localhost,127.0.0.1,::1";

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
    return tool == "settings_open" ||
           tool == "ppt_create" ||
           tool == "file_create" ||
           tool == "folder_list" ||
           tool == "file_open" ||
           tool == "wallpaper_create_web_package" ||
           tool == "wallpaper_validate_package" ||
           turingdesk::IsDesktopControlTool(tool);
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

    const auto result = turingdesk::IsDesktopControlTool(toolUtf8)
        ? turingdesk::ExecuteDesktopControlTool(toolUtf8, arguments)
        : turingdesk::ExecuteNativeToolRaw(toolUtf8, arguments);
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
    if (!turingdesk::wallpaper::DesktopWidgetStore::SelfTest()) return false;

    turingdesk::AppSearch apps;
    apps.BuildIndex();
    const auto appResults = apps.Query(L"Notepad", 5);
    if (apps.Count() < 5 || appResults.empty()) return false;

    turingdesk::GozSearch files;
    if (!files.SelfTest()) return false;

    if (!turingdesk::HarnessProcessManager::SelfTest()) return false;
    if (!turingdesk::RunL3PersistenceSelfTest()) return false;

    turingdesk::L3Agent l3;
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

    for (const wchar_t* command : {L"/apps Notepad", L"/files TuringDesk"}) {
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
    EnsureLoopbackProxyBypass();
    EnsureBundledRuntimePath();

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return 3;

    bool workerHandled = false;
    const int workerResult = RunNativeToolWorkerIfRequested(workerHandled);
    if (workerHandled) {
        if (SUCCEEDED(com)) CoUninitialize();
        return workerResult;
    }

    std::wstring piExtensionError;
    const bool piExtensionReady = turingdesk::EnsurePiNativeToolsExtension(&piExtensionError);
    if (!piExtensionReady && !piExtensionError.empty()) {
        OutputDebugStringW((L"TuringDesk Pi extension bootstrap failed: " + piExtensionError + L"\r\n").c_str());
    }

    const std::wstring_view args = commandLine ? std::wstring_view(commandLine) : std::wstring_view{};
    if (args.find(L"--self-test") != std::wstring_view::npos) {
        const int result = piExtensionReady && RunNativeSelfTest() ? 0 : 5;
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\TuringDesk.Native.Search.Singleton");
    if (!mutex) {
        if (SUCCEEDED(com)) CoUninitialize();
        return 2;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ActivateExistingSearchWindow();
        CloseHandle(mutex);
        if (SUCCEEDED(com)) CoUninitialize();
        return 0;
    }

    turingdesk::SearchWindow window(instance);
    if (!window.Create()) {
        if (SUCCEEDED(com)) CoUninitialize();
        CloseHandle(mutex);
        return 4;
    }

    const int result = window.RunMessageLoop();
    if (SUCCEEDED(com)) CoUninitialize();
    CloseHandle(mutex);
    return result;
}
