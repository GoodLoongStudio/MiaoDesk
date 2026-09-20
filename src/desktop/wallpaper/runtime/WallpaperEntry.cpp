#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "miaodesk/ContentWidgetHost.h"
#include "miaodesk/DesktopShellHost.h"
#include "miaodesk/BuiltinWallpaperCatalog.h"
#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/WallpaperLibrary.h"
#include "miaodesk/WallpaperPackage.h"
#include "miaodesk/WallpaperWebRuntimeCoordinator.h"
#include "miaodesk/WebDesktopSurfaceChild.h"
#include "miaodesk/WebWallpaperHost.h"
#include "miaodesk/NativeWidgetHost.h"

namespace fs = std::filesystem;

// WallpaperEngine.cpp is compiled with its historical WinMain symbol renamed to
// MiaoDeskWallpaperMain. The Windows headers declare wWinMain with C linkage,
// so the macro-renamed legacy entry keeps that linkage as well.
extern "C" int WINAPI MiaoDeskWallpaperMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand);

namespace {

constexpr wchar_t kWallpaperControlClass[] = L"MiaoDesk.Native.WallpaperControl";
constexpr wchar_t kDesktopLibraryClass[] = L"MiaoDesk.Native.DesktopLibrary";
constexpr wchar_t kWallpaperHostClass[] = L"MiaoDesk.Native.WallpaperHost";
constexpr wchar_t kWebHostClass[] = L"MiaoDesk.Native.WebWallpaperHost";
constexpr wchar_t kWorkbenchSubclassProperty[] = L"MiaoDesk.Settings.Workbench.OriginalProc";
constexpr int kApiNavId = 6116;
constexpr int kWorkbenchEntryId = 6180;

constexpr wchar_t kShellMode[] = L"--desktop-shell-supervisor";
constexpr wchar_t kWebRuntimeMode[] = L"--web-wallpaper-runtime";
constexpr wchar_t kWidgetRuntimeMode[] = L"--widget-runtime";

constexpr wchar_t kShellMutex[] = L"Local\\MiaoDesk.DesktopShellSupervisor.v1";
constexpr wchar_t kWebRuntimeMutex[] = L"Local\\MiaoDesk.WebWallpaperRuntime.v1";
constexpr wchar_t kWidgetRuntimeMutex[] = L"Local\\MiaoDesk.WidgetRuntime.v1";
constexpr wchar_t kShellStopEvent[] = L"Local\\MiaoDesk.DesktopShellSupervisor.Stop.v1";
constexpr wchar_t kWebRuntimeStopEvent[] = L"Local\\MiaoDesk.WebWallpaperRuntime.Stop.v1";
constexpr wchar_t kWidgetRuntimeStopEvent[] = L"Local\\MiaoDesk.WidgetRuntime.Stop.v1";

struct HelperSpec {
    const wchar_t* mode;
    const wchar_t* mutexName;
    const wchar_t* stopEventName;
};

constexpr HelperSpec kHelpers[] = {
    {kShellMode, kShellMutex, kShellStopEvent},
    {kWebRuntimeMode, kWebRuntimeMutex, kWebRuntimeStopEvent},
    {kWidgetRuntimeMode, kWidgetRuntimeMutex, kWidgetRuntimeStopEvent},
};

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
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

bool NamedMutexExists(const wchar_t* name) {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

class SingletonGuard {
public:
    explicit SingletonGuard(const wchar_t* name) {
        handle_ = CreateMutexW(nullptr, FALSE, name);
        owner_ = handle_ && GetLastError() != ERROR_ALREADY_EXISTS;
    }
    ~SingletonGuard() {
        if (handle_) CloseHandle(handle_);
    }
    bool Owner() const noexcept { return owner_; }
private:
    HANDLE handle_{};
    bool owner_{};
};

bool LaunchHelper(const HelperSpec& helper) {
    if (NamedMutexExists(helper.mutexName)) return true;
    const std::wstring executable = ExecutablePath();
    if (executable.empty()) return false;

    std::wstring command = QuoteArg(executable) + L" " + helper.mode;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr, nullptr, &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    const ULONGLONG deadline = GetTickCount64() + 3000;
    do {
        if (NamedMutexExists(helper.mutexName)) return true;
        Sleep(50);
    } while (GetTickCount64() < deadline);
    return NamedMutexExists(helper.mutexName);
}

void SignalHelperStop(const HelperSpec& helper) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, helper.stopEventName);
    if (!event) return;
    SetEvent(event);
    CloseHandle(event);
}

HANDLE CreateStopEvent(const wchar_t* name) {
    return CreateEventW(nullptr, TRUE, FALSE, name);
}

std::wstring ReadProfileValue(const fs::path& path, const wchar_t* key) {
    wchar_t buffer[32768]{};
    GetPrivateProfileStringW(L"Wallpaper", key, L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

RECT ClampRectToWorkArea(RECT windowRect, const RECT& work) {
    const LONG workWidth = std::max<LONG>(1, work.right - work.left);
    const LONG workHeight = std::max<LONG>(1, work.bottom - work.top);
    LONG width = std::clamp<LONG>(windowRect.right - windowRect.left, 1, workWidth);
    LONG height = std::clamp<LONG>(windowRect.bottom - windowRect.top, 1, workHeight);
    LONG left = std::clamp<LONG>(windowRect.left, work.left, work.right - width);
    LONG top = std::clamp<LONG>(windowRect.top, work.top, work.bottom - height);
    return RECT{left, top, left + width, top + height};
}

bool IsDesktopLibraryWindow(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[128]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, kDesktopLibraryClass) == 0;
}

void ClampDesktopLibraryToWorkArea(HWND window) {
    if (!IsDesktopLibraryWindow(window) || IsIconic(window) || IsZoomed(window)) return;
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    RECT current{};
    if (!monitor || !GetMonitorInfoW(monitor, &info) || !GetWindowRect(window, &current)) return;
    const RECT bounded = ClampRectToWorkArea(current, info.rcWork);
    if (EqualRect(&current, &bounded)) return;
    SetWindowPos(window, nullptr, bounded.left, bounded.top,
                 bounded.right - bounded.left, bounded.bottom - bounded.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

int ScaleForWindow(HWND window, int logicalPx) {
    const UINT dpi = window ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window))
                            : USER_DEFAULT_SCREEN_DPI;
    return MulDiv(logicalPx, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

fs::path HarnessExecutablePath() {
    const std::wstring executable = ExecutablePath();
    if (executable.empty()) return {};
    const fs::path directory = fs::path(executable).parent_path();
    std::error_code ec;
    for (const wchar_t* name : {L"MiaoDeskHarness.exe", L"MiaoDeskHarness.exe"}) {
        const fs::path candidate = directory / name;
        if (fs::is_regular_file(candidate, ec)) return candidate;
        ec.clear();
    }
    return {};
}

void LaunchDeepSeekHarness(HWND owner) {
    for (const wchar_t* className : {L"MiaoDesk.Native.HarnessWindow", L"MiaoDesk.Native.HarnessWindow"}) {
        const HWND existing = FindWindowW(className, nullptr);
        if (!existing) continue;
        SetWindowTextW(existing, L"妙喵工作台 · DeepSeek Harness");
        ShowWindow(existing, SW_SHOWNORMAL);
        SetForegroundWindow(existing);
        return;
    }

    const fs::path harness = HarnessExecutablePath();
    if (harness.empty()) {
        MessageBoxW(owner,
                    L"未找到 DeepSeek Harness 工作台程序。请确认安装包包含 Harness 运行时。",
                    L"妙喵工作台", MB_OK | MB_ICONERROR);
        return;
    }

    const fs::path directory = harness.parent_path();
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(owner, L"open", harness.c_str(), L"--ui", directory.c_str(), SW_SHOWNORMAL));
    if (result <= 32) {
        MessageBoxW(owner, L"DeepSeek Harness 工作台启动失败。", L"妙喵工作台", MB_OK | MB_ICONERROR);
    }
}

void LayoutWorkbenchEntry(HWND window) {
    if (!IsDesktopLibraryWindow(window)) return;
    const HWND entry = GetDlgItem(window, kWorkbenchEntryId);
    if (!entry) return;

    const HWND apiNav = GetDlgItem(window, kApiNavId);
    if (apiNav) {
        RECT anchor{};
        if (GetWindowRect(apiNav, &anchor)) {
            MapWindowPoints(HWND_DESKTOP, window, reinterpret_cast<POINT*>(&anchor), 2);
            const int gap = ScaleForWindow(window, 4);
            SetWindowPos(entry, nullptr, anchor.left, anchor.bottom + gap,
                         std::max(1L, anchor.right - anchor.left),
                         std::max(1L, anchor.bottom - anchor.top),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
    }

    RECT client{};
    if (!GetClientRect(window, &client)) return;
    const int x = ScaleForWindow(window, 12);
    const int y = ScaleForWindow(window, 202);
    const int width = std::max(1, ScaleForWindow(window, 184));
    const int height = std::max(1, ScaleForWindow(window, 38));
    SetWindowPos(entry, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK SettingsWorkbenchSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* original = reinterpret_cast<WNDPROC>(GetPropW(window, kWorkbenchSubclassProperty));
    if (!original) return DefWindowProcW(window, message, wParam, lParam);

    if (message == WM_COMMAND && LOWORD(wParam) == kWorkbenchEntryId && HIWORD(wParam) == BN_CLICKED) {
        LaunchDeepSeekHarness(window);
        return 0;
    }

    const LRESULT result = CallWindowProcW(original, window, message, wParam, lParam);
    if (message == WM_SIZE || message == WM_DPICHANGED || message == WM_WINDOWPOSCHANGED)
        LayoutWorkbenchEntry(window);
    if (message == WM_NCDESTROY)
        RemovePropW(window, kWorkbenchSubclassProperty);
    return result;
}

void EnsureSettingsWorkbenchEntry(HWND window) {
    if (!IsDesktopLibraryWindow(window)) return;

    HWND entry = GetDlgItem(window, kWorkbenchEntryId);
    if (!entry) {
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
        entry = CreateWindowExW(
            0, L"BUTTON", L"妙喵工作台  ↗",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_FLAT,
            0, 0, 10, 10, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWorkbenchEntryId)), instance, nullptr);
        if (!entry) return;

        const HWND apiNav = GetDlgItem(window, kApiNavId);
        const HFONT font = apiNav ? reinterpret_cast<HFONT>(SendMessageW(apiNav, WM_GETFONT, 0, 0)) : nullptr;
        if (font) SendMessageW(entry, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    if (!GetPropW(window, kWorkbenchSubclassProperty)) {
        const auto original = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SettingsWorkbenchSubclassProc)));
        if (original) SetPropW(window, kWorkbenchSubclassProperty, reinterpret_cast<HANDLE>(original));
    }

    LayoutWorkbenchEntry(window);
}

void CALLBACK WorkAreaEventProc(HWINEVENTHOOK, DWORD event, HWND window,
                                LONG objectId, LONG childId, DWORD, DWORD) {
    if (event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_LOCATIONCHANGE) return;
    if (childId != CHILDID_SELF || (objectId != OBJID_WINDOW && objectId != OBJID_CLIENT)) return;
    ClampDesktopLibraryToWorkArea(window);
    EnsureSettingsWorkbenchEntry(window);
}

HWINEVENTHOOK InstallWorkAreaGuard() {
    return SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WorkAreaEventProc,
                           GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
}

bool WorkAreaSelfTest() {
    const RECT work{0, 0, 1920, 1040};
    const RECT tooLarge{-20, -10, 2000, 1100};
    const RECT clamped = ClampRectToWorkArea(tooLarge, work);
    if (clamped.left != 0 || clamped.top != 0 || clamped.right != 1920 || clamped.bottom != 1040) return false;
    const RECT normal{100, 100, 1200, 800};
    const RECT same = ClampRectToWorkArea(normal, work);
    return EqualRect(&normal, &same) != FALSE;
}

bool WebLibrarySelfTest() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-WebLibrary-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root, ec);
    if (ec) return false;

    wchar_t previousLocalAppData[32768]{};
    const DWORD previousLocalAppDataLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA", previousLocalAppData, static_cast<DWORD>(std::size(previousLocalAppData)));
    const bool hadLocalAppData = previousLocalAppDataLength > 0 && previousLocalAppDataLength < std::size(previousLocalAppData);
    const fs::path isolatedLocalAppData = root / L"LocalAppData";
    fs::create_directories(isolatedLocalAppData, ec);
    bool ok = !ec && SetEnvironmentVariableW(L"LOCALAPPDATA", isolatedLocalAppData.c_str()) != FALSE;

    miaodesk::wallpaper::WallpaperLibrary library(root / L"Library");
    std::wstring error;
    ok = ok && library.Load(&error);
    ok = ok && miaodesk::wallpaper::WallpaperLibrary::IsTrustedWebUrl(L"https://example.com/wallpaper");
    ok = ok && !miaodesk::wallpaper::WallpaperLibrary::IsTrustedWebUrl(L"http://example.com/wallpaper");
    ok = ok && !miaodesk::wallpaper::WallpaperLibrary::IsTrustedWebUrl(L"https://user:pass@example.com/wallpaper");

    const auto imported = library.ImportWebUrl(L"https://example.com/wallpaper", L"Web Self Test", &error);
    ok = ok && imported.has_value() && imported->kind == miaodesk::wallpaper::LibraryWallpaperKind::Web;

    miaodesk::wallpaper::WallpaperLibrary reloaded(root / L"Library");
    ok = ok && reloaded.Load(&error);
    if (imported) {
        const auto persisted = reloaded.Find(imported->id);
        ok = ok && persisted.has_value() && persisted->source.wstring() == L"https://example.com/wallpaper";
        ok = ok && miaodesk::wallpaper::ActivateWebWallpaperItem(*imported, L"", &error);

        const fs::path wallpaperConfig = isolatedLocalAppData / L"MiaoDesk" / L"wallpaper.ini";
        ok = ok && ReadProfileValue(wallpaperConfig, L"Enabled") == L"1";
        ok = ok && _wcsicmp(ReadProfileValue(wallpaperConfig, L"Scene").c_str(), L"web") == 0;
        ok = ok && ReadProfileValue(wallpaperConfig, L"Image") == L"https://example.com/wallpaper";
        ok = ok && ReadProfileValue(wallpaperConfig, L"Video").empty();
    }

    if (hadLocalAppData)
        SetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData);
    else
        SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);

    fs::remove_all(root, ec);
    return ok;
}

bool IsDesktopSurfaceWindow(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[160]{};
    if (!GetClassNameW(window, className, static_cast<int>(std::size(className)))) return false;
    if (_wcsicmp(className, kWallpaperHostClass) == 0 || _wcsicmp(className, kWebHostClass) == 0) return true;
    return _wcsicmp(className, miaodesk::wallpaper::kNativeWidgetSurfaceClass) == 0 &&
           miaodesk::wallpaper::DesktopShellHost::InferRole(window) ==
               miaodesk::wallpaper::DesktopSurfaceRole::Widget;
}

void CollectDescendantSurfaces(HWND root, std::vector<HWND>& surfaces) {
    if (!root || !IsWindow(root)) return;
    for (HWND child = GetWindow(root, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (IsDesktopSurfaceWindow(child)) surfaces.push_back(child);
        CollectDescendantSurfaces(child, surfaces);
    }
}

std::vector<HWND> CollectDesktopSurfaceWindows() {
    std::vector<HWND> surfaces;
    EnumWindows([](HWND top, LPARAM raw) -> BOOL {
        auto* output = reinterpret_cast<std::vector<HWND>*>(raw);
        if (IsDesktopSurfaceWindow(top)) output->push_back(top);
        CollectDescendantSurfaces(top, *output);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&surfaces));
    std::sort(surfaces.begin(), surfaces.end());
    surfaces.erase(std::unique(surfaces.begin(), surfaces.end()), surfaces.end());
    return surfaces;
}

int RunDesktopShellSupervisor() {
    SingletonGuard singleton(kShellMutex);
    if (!singleton.Owner()) return 0;
    HANDLE stopEvent = CreateStopEvent(kShellStopEvent);
    if (!stopEvent) return 45;

    miaodesk::wallpaper::DesktopShellHost shell;
    ULONGLONG lastStackRepair = 0;
    while (WaitForSingleObject(stopEvent, 250) == WAIT_TIMEOUT) {
        std::wstring ignored;
        if (!shell.EnsureCurrent(&ignored)) continue;
        bool stackRepairNeeded = false;
        for (HWND surface : CollectDesktopSurfaceWindows()) {
            if (!surface || !IsWindow(surface)) continue;
            const auto role = miaodesk::wallpaper::DesktopShellHost::InferRole(surface);
            auto health = shell.InspectSurface(surface, role);
            RECT screenRect{};
            if (!GetWindowRect(surface, &screenRect) ||
                screenRect.right <= screenRect.left || screenRect.bottom <= screenRect.top) continue;

            const bool layeredRequired = role != miaodesk::wallpaper::DesktopSurfaceRole::Widget ||
                !miaodesk::wallpaper::DesktopShellHost::IsWidgetNativeSurface(surface) ||
                (GetWindowLongPtrW(GetParent(surface), GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) == 0;
            if (!health.parent || !health.childStyle || (layeredRequired && !health.layered) || !health.geometry) {
                shell.AttachSurface(surface, role, screenRect,
                                    IsWindowVisible(surface) != FALSE, nullptr);
                stackRepairNeeded = true;
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (stackRepairNeeded || (lastStackRepair != 0 && now - lastStackRepair >= 10000)) {
            shell.RepairKnownMiaoDeskSurfaces();
            lastStackRepair = now;
        }
    }

    CloseHandle(stopEvent);
    return 0;
}

int RunScopedWebCoordinator(miaodesk::wallpaper::WallpaperWebRuntimeScope scope,
                            const wchar_t* mutexName, const wchar_t* stopEventName) {
    SingletonGuard singleton(mutexName);
    if (!singleton.Owner()) return 0;
    HANDLE stopEvent = CreateStopEvent(stopEventName);
    if (!stopEvent) return 46;

    miaodesk::wallpaper::WallpaperWebRuntimeCoordinator coordinator(scope);
    if (!coordinator.Start()) {
        CloseHandle(stopEvent);
        return 47;
    }
    while (WaitForSingleObject(stopEvent, 500) == WAIT_TIMEOUT) {}
    coordinator.Stop();
    CloseHandle(stopEvent);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand) {
    // There is one production WebView2 desktop-surface child implementation.
    // The old WebWallpaperChildHost remains compiled only as migration source
    // reference and is no longer routed from the executable entrypoint.
    const int surfaceWebResult = miaodesk::wallpaper::TryRunWebDesktopSurfaceChild(instance);
    if (surfaceWebResult >= 0) return surfaceWebResult;

    const int contentWidgetResult = miaodesk::wallpaper::TryRunContentWidgetHost(instance);
    if (contentWidgetResult >= 0) return contentWidgetResult;

    const int nativeWidgetResult = miaodesk::wallpaper::TryRunNativeWidgetHost(instance);
    if (nativeWidgetResult >= 0) return nativeWidgetResult;

    const std::wstring_view args = commandLine ? std::wstring_view(commandLine) : std::wstring_view{};
    if (args.find(kShellMode) != std::wstring_view::npos)
        return RunDesktopShellSupervisor();
    if (args.find(kWebRuntimeMode) != std::wstring_view::npos)
        return RunScopedWebCoordinator(miaodesk::wallpaper::WallpaperWebRuntimeScope::WebWallpaper,
                                       kWebRuntimeMutex, kWebRuntimeStopEvent);
    if (args.find(kWidgetRuntimeMode) != std::wstring_view::npos)
        return RunScopedWebCoordinator(miaodesk::wallpaper::WallpaperWebRuntimeScope::Widgets,
                                       kWidgetRuntimeMutex, kWidgetRuntimeStopEvent);

    const bool selfTest = args.find(L"--self-test") != std::wstring_view::npos;
    if (selfTest) {
        if (!miaodesk::wallpaper::WebWallpaperProcessSet::SelfTest()) return 37;
        if (!WebLibrarySelfTest()) return 38;
        if (!miaodesk::wallpaper::WallpaperWebRuntimeCoordinator::SelfTest()) return 39;
        if (!WorkAreaSelfTest()) return 40;
        if (!miaodesk::wallpaper::WallpaperPackage::SelfTest()) return 41;
        if (!miaodesk::wallpaper::DesktopWidgetStore::SelfTest()) return 42;
        if (!miaodesk::wallpaper::BuiltinWallpaperCatalogSelfTest()) return 43;
        if (!miaodesk::wallpaper::DesktopShellHost::SelfTest()) return 44;
        return MiaoDeskWallpaperMain(instance, previous, commandLine, showCommand);
    }

    // A second MiaoDeskWallpaper invocation is only a command sender for the
    // already-running native wallpaper singleton. It must not own or stop the
    // shared helper fault domains.
    if (FindWindowW(kWallpaperControlClass, nullptr))
        return MiaoDeskWallpaperMain(instance, previous, commandLine, showCommand);

    for (const auto& helper : kHelpers) LaunchHelper(helper);

    const HWINEVENTHOOK workAreaHook = InstallWorkAreaGuard();
    const int result = MiaoDeskWallpaperMain(instance, previous, commandLine, showCommand);

    // This path is reached on an orderly native wallpaper shutdown. An abnormal
    // crash never sends these events, so Shell/Web/Widget helpers survive long
    // enough for the next native runtime instance to reconnect to them.
    for (const auto& helper : kHelpers) SignalHelperStop(helper);
    if (workAreaHook) UnhookWinEvent(workAreaHook);
    return result;
}
