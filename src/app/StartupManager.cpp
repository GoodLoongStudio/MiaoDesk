#include "miaodesk/StartupManager.h"

#include "miaodesk/AppPaths.h"
#include "miaodesk/RuntimeLogger.h"

#include <windows.h>
#include <appmodel.h>
#include <shellapi.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/base.h>

#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>

namespace miaodesk::startup {
namespace {

constexpr wchar_t kStartupTaskId[] = L"MiaoDeskStartup";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"MiaoDesk";
constexpr wchar_t kStartupSection[] = L"Startup";
constexpr wchar_t kConsentKey[] = L"ConsentRecorded";

bool IsPackaged() {
    UINT32 length = 0;
    const LONG result = GetCurrentPackageFullName(&length, nullptr);
    return result == ERROR_INSUFFICIENT_BUFFER;
}

std::filesystem::path PreferencePath() {
    return paths::StateFile(L"startup.ini");
}

bool ConsentRecorded() {
    const auto path = PreferencePath();
    return !path.empty() &&
           GetPrivateProfileIntW(kStartupSection, kConsentKey, 0, path.c_str()) != 0;
}

bool RecordConsent() {
    const auto root = paths::EnsureStateRoot();
    const auto path = PreferencePath();
    if (root.empty() || path.empty()) return false;
    return WritePrivateProfileStringW(
               kStartupSection, kConsentKey, L"1", path.c_str()) != FALSE;
}

std::wstring ModulePath() {
    std::array<wchar_t, 32768> path{};
    const DWORD count = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    return std::wstring(path.data(), count);
}

std::wstring RunCommand(const std::wstring& executable) {
    return L"\"" + executable + L"\" --startup";
}

bool SetUnpackagedStartupEnabled(bool enabled, std::wstring* error) {
    if (!enabled) {
        const LONG result = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
        if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) return true;
        if (error) *error = L"无法删除当前用户的启动项，Win32=" + std::to_wstring(result);
        return false;
    }

    const std::wstring executable = ModulePath();
    if (executable.empty()) {
        if (error) *error = L"无法读取 MiaoDesk.exe 路径，Win32=" + std::to_wstring(GetLastError());
        return false;
    }

    HKEY key = nullptr;
    const LONG opened = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) {
        if (error) *error = L"无法打开当前用户的启动项，Win32=" + std::to_wstring(opened);
        return false;
    }

    const std::wstring command = RunCommand(executable);
    const LONG written = RegSetValueExW(
        key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (written == ERROR_SUCCESS) return true;
    if (error) *error = L"无法保存当前用户的启动项，Win32=" + std::to_wstring(written);
    return false;
}

bool SetPackagedStartupEnabled(bool enabled, std::wstring* error) {
    bool succeeded = false;
    std::wstring workerError;
    std::thread worker([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const auto task = winrt::Windows::ApplicationModel::StartupTask::GetAsync(
                kStartupTaskId).get();
            if (!task) {
                workerError = L"Windows 未找到 MiaoDesk 的启动任务。";
                return;
            }

            using winrt::Windows::ApplicationModel::StartupTaskState;
            if (!enabled) {
                task.Disable();
                succeeded = true;
                return;
            }

            const StartupTaskState current = task.State();
            if (current == StartupTaskState::Enabled) {
                succeeded = true;
                return;
            }
            if (current == StartupTaskState::DisabledByUser) {
                workerError = L"该启动项已在 Windows 中被用户关闭，请在“设置 > 应用 > 启动”中重新启用。";
                return;
            }
            if (current == StartupTaskState::DisabledByPolicy) {
                workerError = L"该启动项被 Windows 策略禁用。";
                return;
            }

            const StartupTaskState requested = task.RequestEnableAsync().get();
            succeeded = requested == StartupTaskState::Enabled;
            if (!succeeded) {
                workerError = L"Windows 没有启用 MiaoDesk 启动任务，状态=" +
                              std::to_wstring(static_cast<int>(requested));
            }
        } catch (const winrt::hresult_error& exception) {
            workerError = L"启动任务注册失败，HRESULT=" +
                          std::to_wstring(static_cast<long>(exception.code().value)) +
                          L"；" + std::wstring(exception.message().c_str());
        } catch (...) {
            workerError = L"启动任务注册发生未知错误。";
        }
    });
    worker.join();

    if (!succeeded && error) *error = std::move(workerError);
    return succeeded;
}

bool EnableStartup(std::wstring* error) {
    return IsPackaged()
        ? SetPackagedStartupEnabled(true, error)
        : SetUnpackagedStartupEnabled(true, error);
}

bool IsStartupArgument(std::wstring_view commandLine) {
    constexpr std::wstring_view argument = L"--startup";
    std::size_t position = commandLine.find(argument);
    while (position != std::wstring_view::npos) {
        const bool startsToken = position == 0 || std::iswspace(commandLine[position - 1]);
        const std::size_t end = position + argument.size();
        const bool endsToken = end == commandLine.size() || std::iswspace(commandLine[end]);
        if (startsToken && endsToken) return true;
        position = commandLine.find(argument, position + 1);
    }
    return false;
}

} // namespace

bool IsStartupLaunch(std::wstring_view commandLine) {
    if (IsStartupArgument(commandLine)) return true;
    if (!IsPackaged()) return false;

    try {
        const auto activation = winrt::Windows::ApplicationModel::AppInstance::GetActivatedEventArgs();
        return activation && activation.Kind() ==
            winrt::Windows::ApplicationModel::Activation::ActivationKind::StartupTask;
    } catch (const winrt::hresult_error& exception) {
        log::Warn(L"Startup", L"无法读取包激活类型，HRESULT=" +
            std::to_wstring(static_cast<long>(exception.code().value)));
        return false;
    }
}

void PromptForConsentIfNeeded() {
    if (ConsentRecorded()) return;

    const int choice = MessageBoxW(
        nullptr,
        L"是否允许 MiaoDesk 在你登录 Windows 后自动启动？\r\n\r\n"
        L"启用后，MiaoDesk 只会驻留在系统托盘，不会自动弹出搜索框；"
        L"你可以随时在 Windows“设置 > 应用 > 启动”中关闭。",
        L"MiaoDesk · 登录后自动启动",
        MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_DEFBUTTON1);

    if (choice != IDYES) {
        if (!RecordConsent()) log::Warn(L"Startup", L"无法保存启动授权选择");
        log::Info(L"Startup", L"用户未启用登录后自动启动");
        return;
    }

    std::wstring error;
    if (EnableStartup(&error)) {
        if (!RecordConsent()) log::Warn(L"Startup", L"无法保存启动授权选择");
        log::Info(L"Startup", IsPackaged()
            ? L"已启用 MSIX 登录启动任务" : L"已启用当前用户登录启动项");
        return;
    }

    if (error.empty()) error = L"Windows 没有返回具体原因。";
    log::Error(L"Startup", error);
    MessageBoxW(
        nullptr,
        (L"无法启用登录后自动启动。\r\n\r\n" + error +
         L"\r\n\r\n你仍可在 Windows“设置 > 应用 > 启动”中检查该功能。").c_str(),
        L"MiaoDesk · 启动设置失败",
        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

void OpenWindowsStartupSettings() {
    const HINSTANCE opened = ShellExecuteW(
        nullptr, L"open", L"ms-settings:startupapps", nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(opened) <= 32) {
        log::Warn(L"Startup", L"无法打开 Windows 启动应用设置");
    }
}

bool SelfTest() {
    return IsStartupArgument(L"--startup") &&
           IsStartupArgument(L"--foo --startup --bar") &&
           !IsStartupArgument(L"--self-test") &&
           !IsStartupArgument(L"--startup-disabled") &&
           RunCommand(L"C:\\Program Files\\MiaoDesk\\MiaoDesk.exe") ==
               L"\"C:\\Program Files\\MiaoDesk\\MiaoDesk.exe\" --startup";
}

} // namespace miaodesk::startup
