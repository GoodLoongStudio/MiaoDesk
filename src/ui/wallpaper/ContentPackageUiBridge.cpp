#include "miaodesk/DesktopControlService.h"
#include "miaodesk/RuntimeLogger.h"
#include "miaodesk/WallpaperLibrary.h"
#include "miaodesk/WallpaperMonitorLayout.h"
#include "miaodesk/WallpaperRuntimeControl.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windows.h>
#include <wrl/client.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kDesktopLibraryClass[] = L"MiaoDesk.Native.DesktopLibrary";
constexpr UINT_PTR kSubclassId = 0x4D435055; // "MCPU"

constexpr int kAddId = 6102;
constexpr int kSearchId = 6101;
constexpr int kWidgetCreateId = 6140;
constexpr int kWidgetRefreshId = 6143;

constexpr UINT kMenuImportFile = 6201;
constexpr UINT kMenuImportWeb = 6202;
constexpr UINT kMenuWidgetGlassClock = 6220;
constexpr UINT kMenuWidgetTodayTasks = 6221;
constexpr UINT kMenuWidgetWeatherGlass = 6222;
constexpr UINT kMenuWidgetAuto = 6223;
constexpr UINT kMenuInstallWallpaperPackage = 6290;
constexpr UINT kMenuInstallWidgetPackage = 6291;

HHOOK g_cbtHook{};

std::wstring WideId(std::string_view id) {
    return std::wstring(id.begin(), id.end());
}

const wchar_t* KindLabel(content::ContentKind kind) noexcept {
    return kind == content::ContentKind::Widget ? L"小组件" : L"壁纸";
}

const wchar_t* RuntimeLabel(content::ContentRuntimeKind runtime) noexcept {
    switch (runtime) {
    case content::ContentRuntimeKind::Scene: return L"Scene";
    case content::ContentRuntimeKind::Web: return L"Web";
    case content::ContentRuntimeKind::Unknown: break;
    }
    return L"Unknown";
}

bool IsPackageDirectory(const fs::path& path, content::ContentKind* kind = nullptr) {
    std::error_code ec;
    if (!fs::is_directory(path, ec) || ec) return false;
    const std::wstring extension = path.extension().wstring();
    if (_wcsicmp(extension.c_str(), L".mdwall") == 0) {
        if (kind) *kind = content::ContentKind::Wallpaper;
        return true;
    }
    if (_wcsicmp(extension.c_str(), L".mdwidget") == 0) {
        if (kind) *kind = content::ContentKind::Widget;
        return true;
    }
    return false;
}

std::optional<fs::path> PickPackageDirectory(HWND owner, content::ContentKind kind) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialized);

    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.GetAddressOf())))) {
        if (uninitialize) CoUninitialize();
        return std::nullopt;
    }

    FILEOPENDIALOGOPTIONS options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(kind == content::ContentKind::Widget
                         ? L"选择 .mdwidget 内容包目录"
                         : L"选择 .mdwall 内容包目录");

    if (dialog->Show(owner) != S_OK) {
        if (uninitialize) CoUninitialize();
        return std::nullopt;
    }

    ComPtr<IShellItem> item;
    PWSTR rawPath = nullptr;
    fs::path selected;
    if (SUCCEEDED(dialog->GetResult(item.GetAddressOf())) && item &&
        SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) && rawPath) {
        selected = rawPath;
    }
    if (rawPath) CoTaskMemFree(rawPath);
    if (uninitialize) CoUninitialize();

    if (selected.empty()) return std::nullopt;
    content::ContentKind detected{};
    if (!IsPackageDirectory(selected, &detected) || detected != kind) {
        MessageBoxW(owner,
                    kind == content::ContentKind::Widget
                        ? L"请选择扩展名为 .mdwidget 的内容包目录。"
                        : L"请选择扩展名为 .mdwall 的内容包目录。",
                    L"MiaoDesk 内容包", MB_OK | MB_ICONWARNING);
        return std::nullopt;
    }
    return selected;
}

const MonitorInfo* PrimaryMonitor(const MonitorTopology& topology) {
    for (const auto& monitor : topology.monitors)
        if (monitor.primary) return &monitor;
    return topology.monitors.empty() ? nullptr : &topology.monitors.front();
}

void RefreshVisibleWidgets(HWND owner) {
    SendMessageW(owner, WM_COMMAND, MAKEWPARAM(kWidgetRefreshId, BN_CLICKED),
                 reinterpret_cast<LPARAM>(GetDlgItem(owner, kWidgetRefreshId)));
}

void NudgeWallpaperList(HWND owner) {
    // The active WallpaperLibrary object may already contain the package after
    // its normal refresh path. Trigger the existing search/list refresh without
    // reaching into WallpaperLibraryWindow::Impl.
    SendMessageW(owner, WM_COMMAND, MAKEWPARAM(kSearchId, EN_CHANGE),
                 reinterpret_cast<LPARAM>(GetDlgItem(owner, kSearchId)));
}

bool CreateInstalledWidgetInstance(HWND owner,
                                   const content::ManagedContentPackageInfo& package) {
    desktop::ContentWidgetCreateRequest request;
    request.definitionId = WideId(package.id);
    request.title = package.name;

    const auto topology = QueryMonitorTopology();
    if (const auto* primary = PrimaryMonitor(topology))
        request.monitorId = StableMonitorKey(*primary);

    DesktopWidget created;
    desktop::DesktopControlService control;
    const auto result = control.CreateContentWidget(request, &created);
    if (!result.success) {
        MessageBoxW(owner,
                    result.message.empty() ? L"内容包已安装，但创建桌面小组件失败。" : result.message.c_str(),
                    L"MiaoDesk 内容包", MB_OK | MB_ICONERROR);
        return false;
    }
    RefreshVisibleWidgets(owner);
    return true;
}

void IndexInstalledWallpaper(const content::ManagedContentPackageInfo& package) {
    // Keep library.ini synchronized even though the running UI may have loaded
    // its WallpaperLibrary before this install operation.
    WallpaperLibrary library;
    std::wstring error;
    if (!library.Load(&error) && !error.empty())
        miaodesk::log::Warn(L"ContentPackageUI", L"壁纸包已安装，但库索引刷新失败: " + error);
}

void MaybeAssignWallpaperToPrimary(HWND owner,
                                   const content::ManagedContentPackageInfo& package) {
    if (package.runtime != content::ContentRuntimeKind::Scene) return;
    if (MessageBoxW(owner, L"壁纸内容包已安装。是否立即应用到主显示器？",
                    L"MiaoDesk 内容包", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    const auto topology = QueryMonitorTopology();
    const auto* primary = PrimaryMonitor(topology);
    if (!primary) {
        MessageBoxW(owner, L"没有检测到可用显示器。", L"MiaoDesk 内容包", MB_OK | MB_ICONWARNING);
        return;
    }

    WallpaperLibraryItem item;
    item.id = package.source;
    item.kind = LibraryWallpaperKind::Scene;
    item.title = package.name;
    item.source = package.packageRoot;
    item.managedCopy = package.origin == content::ManagedContentPackageOrigin::UserManaged;

    desktop::DesktopControlService control;
    const auto result = control.AssignLibraryItemToMonitor(
        item, StableMonitorKey(*primary), primary->friendlyName);
    if (!result.success) {
        MessageBoxW(owner,
                    result.message.empty() ? L"内容包已安装，但应用到主显示器失败。" : result.message.c_str(),
                    L"MiaoDesk 内容包", MB_OK | MB_ICONERROR);
        return;
    }
    NotifyWallpaperRuntimeReload();
}

void InstallPackage(HWND owner, const fs::path& path, content::ContentKind expectedKind) {
    desktop::DesktopControlService control;
    content::ManagedContentPackageInfo inspected;
    const auto inspectedResult = control.InspectContentPackage(path, &inspected);
    if (!inspectedResult.success) {
        MessageBoxW(owner,
                    inspectedResult.message.empty() ? L"内容包校验失败。" : inspectedResult.message.c_str(),
                    L"MiaoDesk 内容包", MB_OK | MB_ICONERROR);
        return;
    }
    if (inspected.kind != expectedKind) {
        const std::wstring message = L"内容包类型不匹配。当前包是“" +
            std::wstring(KindLabel(inspected.kind)) + L"”，这里需要“" +
            KindLabel(expectedKind) + L"”。";
        MessageBoxW(owner, message.c_str(), L"MiaoDesk 内容包", MB_OK | MB_ICONWARNING);
        return;
    }

    content::ManagedContentPackageInfo existing;
    const auto existingResult = control.ResolveContentPackage(expectedKind, inspected.source, &existing);
    if (existingResult.success && existing.origin == content::ManagedContentPackageOrigin::BuiltIn) {
        const std::wstring message = L"内置内容已经占用该 ID，不能由用户包覆盖：\n\n" + inspected.source;
        MessageBoxW(owner, message.c_str(), L"MiaoDesk 内容包", MB_OK | MB_ICONWARNING);
        return;
    }
    const bool replacing = existingResult.success;

    std::wostringstream prompt;
    prompt << (replacing ? L"将替换/升级已安装内容包：\n\n" : L"将安装内容包：\n\n")
           << L"名称：" << (inspected.name.empty() ? L"(未命名)" : inspected.name) << L"\n"
           << L"作者：" << (inspected.author.empty() ? L"(未知)" : inspected.author) << L"\n"
           << L"版本：" << (inspected.version.empty() ? L"(未声明)" : inspected.version) << L"\n"
           << L"ID：" << inspected.source << L"\n"
           << L"类型：" << KindLabel(inspected.kind) << L"\n"
           << L"Runtime：" << RuntimeLabel(inspected.runtime) << L"\n\n"
           << (replacing
                   ? L"同 ID 的桌面引用不会改变，升级后继续使用 content:<id>。"
                   : L"安装后原下载目录可以移动或删除，MiaoDesk 使用托管副本。")
           << L"\n\n继续吗？";

    if (MessageBoxW(owner, prompt.str().c_str(), L"MiaoDesk 内容包",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    content::ContentPackageInstallResult installed;
    content::ContentPackageInstallOptions options;
    options.replaceExisting = true;
    const auto result = control.InstallContentPackage(path, &installed, options);
    if (!result.success) {
        MessageBoxW(owner,
                    result.message.empty() ? L"内容包安装失败。" : result.message.c_str(),
                    L"MiaoDesk 内容包", MB_OK | MB_ICONERROR);
        return;
    }

    miaodesk::log::Info(
        L"ContentPackageUI",
        std::wstring(replacing ? L"替换内容包完成: " : L"安装内容包完成: ") + installed.package.source);

    if (expectedKind == content::ContentKind::Wallpaper) {
        IndexInstalledWallpaper(installed.package);
        NudgeWallpaperList(owner);
        MaybeAssignWallpaperToPrimary(owner, installed.package);
        MessageBoxW(owner,
                    (std::wstring(replacing ? L"壁纸内容包已替换：" : L"壁纸内容包已安装：") +
                     installed.package.name + L"\n" + installed.package.source).c_str(),
                    L"MiaoDesk 内容包", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const int create = MessageBoxW(owner,
        (std::wstring(replacing ? L"小组件内容包已替换：" : L"小组件内容包已安装：") +
         installed.package.name + L"\n" + installed.package.source +
         L"\n\n是否现在创建一个桌面小组件实例？").c_str(),
        L"MiaoDesk 内容包", MB_YESNO | MB_ICONINFORMATION);
    if (create == IDYES && CreateInstalledWidgetInstance(owner, installed.package)) {
        MessageBoxW(owner, L"桌面小组件实例已创建。", L"MiaoDesk 内容包",
                    MB_OK | MB_ICONINFORMATION);
    }
}

void InstallFromPicker(HWND owner, content::ContentKind kind) {
    const auto selected = PickPackageDirectory(owner, kind);
    if (selected) InstallPackage(owner, *selected, kind);
}

UINT TrackMenuAtControl(HWND owner, int controlId, HMENU menu) {
    HWND control = GetDlgItem(owner, controlId);
    RECT rect{};
    if (!control || !GetWindowRect(control, &rect)) GetWindowRect(owner, &rect);
    return TrackPopupMenu(menu,
                          TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_TOPALIGN,
                          rect.right, rect.bottom, 0, owner, nullptr);
}

void ShowWallpaperAddMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuInstallWallpaperPackage, L"安装 .mdwall 内容包…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuImportFile, L"从普通文件导入…");
    AppendMenuW(menu, MF_STRING, kMenuImportWeb, L"添加 HTTPS Web 地址…");
    const UINT command = TrackMenuAtControl(owner, kAddId, menu);
    DestroyMenu(menu);
    if (command == kMenuInstallWallpaperPackage) {
        InstallFromPicker(owner, content::ContentKind::Wallpaper);
    } else if (command != 0) {
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
}

void ShowWidgetCreateMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuWidgetGlassClock, L"玻璃时钟");
    AppendMenuW(menu, MF_STRING, kMenuWidgetTodayTasks, L"今日待办");
    AppendMenuW(menu, MF_STRING, kMenuWidgetWeatherGlass, L"玻璃天气");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuInstallWidgetPackage, L"安装 .mdwidget 内容包…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuWidgetAuto, L"自动轮换下一个");
    const UINT command = TrackMenuAtControl(owner, kWidgetCreateId, menu);
    DestroyMenu(menu);
    if (command == kMenuInstallWidgetPackage) {
        InstallFromPicker(owner, content::ContentKind::Widget);
    } else if (command != 0) {
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
}

LRESULT CALLBACK LibrarySubclassProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR) {
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == kAddId && notification == BN_CLICKED) {
            ShowWallpaperAddMenu(hwnd);
            return 0;
        }
        if (id == kWidgetCreateId && notification == BN_CLICKED) {
            ShowWidgetCreateMenu(hwnd);
            return 0;
        }
    } else if (message == WM_DROPFILES) {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        if (DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) == 1) {
            wchar_t path[32768]{};
            if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0) {
                content::ContentKind kind{};
                if (IsPackageDirectory(path, &kind)) {
                    DragFinish(drop);
                    InstallPackage(hwnd, path, kind);
                    return 0;
                }
            }
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, LibrarySubclassProc, subclassId);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK CbtHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_CREATEWND) {
        HWND hwnd = reinterpret_cast<HWND>(wParam);
        wchar_t className[128]{};
        if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) > 0 &&
            wcscmp(className, kDesktopLibraryClass) == 0) {
            SetWindowSubclass(hwnd, LibrarySubclassProc, kSubclassId, 0);
        }
    }
    return CallNextHookEx(g_cbtHook, code, wParam, lParam);
}

struct ContentPackageUiHook final {
    ContentPackageUiHook() {
        g_cbtHook = SetWindowsHookExW(WH_CBT, CbtHookProc, nullptr, GetCurrentThreadId());
    }
    ~ContentPackageUiHook() {
        if (g_cbtHook) UnhookWindowsHookEx(g_cbtHook);
        g_cbtHook = nullptr;
    }
};

ContentPackageUiHook g_contentPackageUiHook;

} // namespace
} // namespace miaodesk::wallpaper
