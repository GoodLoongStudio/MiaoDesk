#include "turingdesk/WallpaperWebRuntimeCoordinator.h"

#include "turingdesk/DesktopShellHost.h"
#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/WallpaperIndependentLayout.h"
#include "turingdesk/WallpaperMonitorAssignments.h"
#include "turingdesk/WallpaperMonitorLayout.h"
#include "turingdesk/WallpaperPerformancePolicy.h"
#include "turingdesk/WebWallpaperHost.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kWallpaperHostClass[] = L"TuringDesk.Native.WallpaperHost";
constexpr wchar_t kWallpaperSettingsClass[] = L"TuringDesk.Native.WallpaperSettings";
constexpr wchar_t kDesktopLibraryClass[] = L"TuringDesk.Native.DesktopLibrary";
constexpr std::chrono::milliseconds kTickInterval{250};
constexpr ULONGLONG kStateRefreshMs = 1000;
constexpr ULONGLONG kRecoveryCooldownMs = 3000;
constexpr ULONGLONG kRecoveryStableResetMs = 30000;
constexpr unsigned kMaxRecoveryAttempts = 3;

fs::path WallpaperConfigPath() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path dir = (length > 0 && length < std::size(local))
        ? fs::path(local) / L"TuringDesk"
        : fs::temp_directory_path() / L"TuringDesk";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / L"wallpaper.ini";
}

std::wstring ReadText(const fs::path& path, const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section, key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

int ReadInt(const fs::path& path, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(L"Wallpaper", key, static_cast<UINT>(fallback), path.c_str()));
}

struct RuntimeState {
    bool enabled{true};
    std::wstring scene{L"aurora"};
    std::wstring source;
    LayoutMode layout{LayoutMode::Span};
    PerformanceConfig performance;
};

RuntimeState LoadRuntimeState(const fs::path& path) {
    RuntimeState state;
    state.enabled = ReadInt(path, L"Enabled", 1) != 0;
    state.scene = ReadText(path, L"Wallpaper", L"Scene", L"aurora");
    state.source = ReadText(path, L"Wallpaper", L"Image", L"");
    state.layout = ParseLayoutMode(ReadText(path, L"Wallpaper", L"Layout", L"span"));
    state.performance.fpsCap = NormalizeFpsCap(ReadInt(path, L"FpsCap", 30));
    state.performance.throttleFps = NormalizeFpsCap(ReadInt(path, L"ThrottleFps", 15));
    state.performance.fullscreenAction = ParsePerformanceAction(ReadText(path, L"Wallpaper", L"FullscreenAction", L"pause"));
    state.performance.maximizedAction = ParsePerformanceAction(ReadText(path, L"Wallpaper", L"MaximizedAction", L"throttle"));
    state.performance.remoteSessionAction = ParsePerformanceAction(ReadText(path, L"Wallpaper", L"RemoteSessionAction", L"throttle"));
    state.performance.batterySaverAction = ParsePerformanceAction(ReadText(path, L"Wallpaper", L"BatterySaverAction", L"throttle"));
    state.performance.lockedSessionAction = ParsePerformanceAction(ReadText(path, L"Wallpaper", L"LockedSessionAction", L"stop"));
    state.performance.idleAction = ParsePerformanceAction(ReadText(path, L"Wallpaper", L"IdleAction", L"throttle"));
    state.performance.idleThresholdSeconds = static_cast<DWORD>(std::clamp(ReadInt(path, L"IdleThresholdSeconds", 120), 30, 3600));
    return state;
}

GlobalWallpaperDescriptor GlobalFallback(const RuntimeState& state) {
    GlobalWallpaperDescriptor result;
    if (_wcsicmp(state.scene.c_str(), L"web") == 0 && WebWallpaperProcessSet::IsSupportedSource(state.source)) {
        result.kind = ResolvedWallpaperKind::Web;
        result.source = state.source;
    } else {
        result.kind = ResolvedWallpaperKind::Scene;
        result.sceneKey = _wcsicmp(state.scene.c_str(), L"neon") == 0 ? L"neon" :
                          _wcsicmp(state.scene.c_str(), L"grid") == 0 ? L"grid" : L"aurora";
    }
    return result;
}

std::vector<WebWallpaperRequest> DesiredRequests(HWND host, const RuntimeState& state) {
    std::vector<WebWallpaperRequest> requests;
    if (!host || !IsWindow(host) || !state.enabled) return requests;

    const MonitorTopology topology = QueryMonitorTopology();
    if (!topology.Valid()) return requests;

    if (state.layout != LayoutMode::Independent) {
        if (_wcsicmp(state.scene.c_str(), L"web") != 0 || !WebWallpaperProcessSet::IsSupportedSource(state.source)) return requests;
        const auto regions = DrawRegionsInHost(topology, state.layout);
        requests.reserve(regions.size());
        for (std::size_t i = 0; i < regions.size(); ++i) {
            WebWallpaperRequest request;
            request.region = regions[i];
            request.source = state.source;
            request.itemId = L"global-web-" + std::to_wstring(i);
            request.muted = true;
            requests.push_back(std::move(request));
        }
        return requests;
    }

    WallpaperLibrary library;
    WallpaperMonitorAssignments assignments;
    std::wstring ignored;
    if (!library.Load(&ignored) || !assignments.Load(&ignored)) return requests;
    const auto resolved = ResolveIndependentWallpapers(topology, assignments, library, GlobalFallback(state));
    for (const auto& item : resolved) {
        if (item.kind != ResolvedWallpaperKind::Web || !WebWallpaperProcessSet::IsSupportedSource(item.source.wstring())) continue;
        WebWallpaperRequest request;
        request.region = item.region;
        request.source = item.source.wstring();
        request.itemId = item.wallpaperId.empty() ? item.monitorId : item.wallpaperId;
        request.muted = true;
        requests.push_back(std::move(request));
    }
    return requests;
}

const MonitorInfo* PrimaryMonitor(const MonitorTopology& topology) {
    for (const auto& monitor : topology.monitors) if (monitor.primary) return &monitor;
    return topology.monitors.empty() ? nullptr : &topology.monitors.front();
}

RECT WidgetRegionInHost(HWND host, const MonitorInfo& monitor, const DesktopWidget& widget) {
    POINT corners[2] = {
        {monitor.desktopRect.left, monitor.desktopRect.top},
        {monitor.desktopRect.right, monitor.desktopRect.bottom},
    };
    MapWindowPoints(HWND_DESKTOP, host, corners, 2);
    const LONG monitorWidth = std::max<LONG>(1, corners[1].x - corners[0].x);
    const LONG monitorHeight = std::max<LONG>(1, corners[1].y - corners[0].y);
    RECT region{};
    region.left = corners[0].x + static_cast<LONG>(std::lround(widget.x * monitorWidth));
    region.top = corners[0].y + static_cast<LONG>(std::lround(widget.y * monitorHeight));
    region.right = region.left + static_cast<LONG>(std::lround(widget.width * monitorWidth));
    region.bottom = region.top + static_cast<LONG>(std::lround(widget.height * monitorHeight));
    return region;
}

std::vector<WebWallpaperRequest> DesiredWidgetRequests(HWND host, std::wstring& fingerprint) {
    fingerprint.clear();
    std::vector<WebWallpaperRequest> requests;
    if (!host || !IsWindow(host)) return requests;

    DesktopWidgetStore store;
    std::wstring ignored;
    if (!store.Load(&ignored)) return requests;
    const MonitorTopology topology = QueryMonitorTopology();
    if (!topology.Valid()) return requests;

    struct RankedRequest {
        int z{};
        WebWallpaperRequest request;
        std::wstring revision;
    };
    std::vector<RankedRequest> ranked;
    for (const auto& raw : store.Items()) {
        const DesktopWidget widget = DesktopWidgetStore::Normalize(raw);
        if (!widget.enabled || widget.kind != DesktopWidgetKind::Web ||
            !WebWallpaperProcessSet::IsSupportedSource(widget.source.wstring())) continue;

        const MonitorInfo* monitor = widget.monitorId.empty()
            ? PrimaryMonitor(topology)
            : FindMonitorByStableId(topology, widget.monitorId);
        if (!monitor) continue;

        WebWallpaperRequest request;
        request.region = WidgetRegionInHost(host, *monitor, widget);
        request.source = widget.source.wstring();
        request.itemId = L"widget-" + widget.id;
        request.muted = true;
        if (request.region.right <= request.region.left || request.region.bottom <= request.region.top) continue;

        std::error_code ec;
        const auto writeTime = fs::last_write_time(widget.source, ec);
        const auto revision = ec ? 0LL : static_cast<long long>(writeTime.time_since_epoch().count());
        ranked.push_back({widget.zIndex, std::move(request), widget.id + L":" + std::to_wstring(revision)});
    }

    std::stable_sort(ranked.begin(), ranked.end(), [](const RankedRequest& a, const RankedRequest& b) {
        return a.z < b.z;
    });
    requests.reserve(ranked.size());
    for (auto& item : ranked) {
        fingerprint += item.revision + L";";
        requests.push_back(std::move(item.request));
    }
    return requests;
}

bool SameRequest(const WebWallpaperRequest& a, const WebWallpaperRequest& b) {
    return a.region.left == b.region.left && a.region.top == b.region.top &&
           a.region.right == b.region.right && a.region.bottom == b.region.bottom &&
           a.source == b.source && a.itemId == b.itemId && a.muted == b.muted;
}

bool SameRequests(const std::vector<WebWallpaperRequest>& a, const std::vector<WebWallpaperRequest>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), SameRequest);
}

void WriteDiagnostics(WallpaperWebRuntimeScope scope, const std::wstring& value) {
    const fs::path path = WallpaperConfigPath();
    const wchar_t* key = scope == WallpaperWebRuntimeScope::Widgets ? L"WidgetRuntime" : L"WebRuntime";
    WritePrivateProfileStringW(L"Diagnostics", key, value.c_str(), path.c_str());
}

RECT MapRegionToParent(HWND host, HWND parent, RECT region) {
    if (!host || !parent || host == parent) return region;
    POINT corners[2] = {{region.left, region.top}, {region.right, region.bottom}};
    SetLastError(ERROR_SUCCESS);
    if (MapWindowPoints(host, parent, corners, 2) == 0 && GetLastError() != ERROR_SUCCESS) return region;
    return RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
}

std::vector<WebWallpaperRequest> MapRequestsToParent(HWND host, HWND parent,
                                                     const std::vector<WebWallpaperRequest>& requests) {
    std::vector<WebWallpaperRequest> mapped = requests;
    for (auto& request : mapped) request.region = MapRegionToParent(host, parent, request.region);
    return mapped;
}

bool PersistGlobalWeb(const WallpaperLibraryItem& item, std::wstring* error) {
    const fs::path path = WallpaperConfigPath();
    bool ok = true;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Enabled", L"1", path.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Scene", L"web", path.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Image", item.source.c_str(), path.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Video", L"", path.c_str()) != FALSE && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    if (!ok && error) *error = L"无法保存 Web 壁纸全局状态";
    return ok;
}

bool PersistMonitorWeb(const WallpaperLibraryItem& item, const std::wstring& targetMonitorId, std::wstring* error) {
    WallpaperMonitorAssignments assignments;
    if (!assignments.Load(error)) return false;
    if (!assignments.AssignById(targetMonitorId, item.id, {}, error)) return false;

    const fs::path path = WallpaperConfigPath();
    bool ok = true;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Enabled", L"1", path.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Layout", L"independent", path.c_str()) != FALSE && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    if (!ok && error) *error = L"无法切换到 Independent Web 壁纸布局";
    return ok;
}

} // namespace

struct WallpaperWebRuntimeCoordinator::Impl {
    explicit Impl(WallpaperWebRuntimeScope runtimeScope) : scope(runtimeScope) {}

    WallpaperWebRuntimeScope scope;
    std::jthread worker;
    std::atomic_bool running{};

    void Run(std::stop_token stopToken) {
        running.store(true, std::memory_order_release);
        WebWallpaperProcessSet surfaces;
        WallpaperPerformancePolicy performance;
        DesktopShellHost shellHost;
        HWND host = nullptr;
        HWND surfaceParent = nullptr;
        std::vector<WebWallpaperRequest> activeRequests;
        std::wstring activeFingerprint;
        RuntimeState state;
        ULONGLONG nextRefresh = 0;
        ULONGLONG nextRecovery = 0;
        ULONGLONG healthySince = 0;
        unsigned recoveryAttempts = 0;

        auto resetRecovery = [&] {
            nextRecovery = 0;
            healthySince = 0;
            recoveryAttempts = 0;
        };

        auto repairStack = [&] {
            if (!host || !IsWindow(host)) return false;
            std::wstring error;
            if (!shellHost.RepairSurfaceStack(host, &error)) {
                if (!error.empty()) WriteDiagnostics(scope, L"DesktopShellHost stack repair failed: " + error);
                return false;
            }
            return true;
        };

        auto startRequests = [&](ULONGLONG now, bool recovery) {
            surfaces.Stop();
            if (activeRequests.empty()) {
                resetRecovery();
                WriteDiagnostics(scope, scope == WallpaperWebRuntimeScope::Widgets
                    ? L"未启用桌面小组件" : L"未启用 Web 壁纸");
                return true;
            }
            if (!surfaceParent || !IsWindow(surfaceParent)) {
                WriteDiagnostics(scope, scope == WallpaperWebRuntimeScope::Widgets
                    ? L"Widget runtime 找不到可用桌面 Surface parent"
                    : L"Web runtime 找不到可用桌面 Surface parent");
                return false;
            }
            if (recovery) ++recoveryAttempts;
            if (!surfaces.Start(surfaceParent, activeRequests)) {
                nextRecovery = now + kRecoveryCooldownMs;
                healthySince = 0;
                WriteDiagnostics(scope, (scope == WallpaperWebRuntimeScope::Widgets
                    ? L"Widget runtime 启动失败：" : L"Web runtime 启动失败：") + surfaces.LastErrorText());
                return false;
            }
            repairStack();
            nextRecovery = 0;
            healthySince = now;
            WriteDiagnostics(scope, surfaces.DiagnosticsText());
            return true;
        };

        while (!stopToken.stop_requested()) {
            HWND currentHost = FindWindowW(kWallpaperHostClass, nullptr);
            HWND currentSurfaceParent = nullptr;
            if (currentHost && IsWindow(currentHost)) {
                std::wstring shellError;
                if (shellHost.EnsureCurrent(&shellError)) {
                    auto health = shellHost.InspectSurface(currentHost, DesktopSurfaceRole::Wallpaper);
                    if (!health.parent) {
                        if (!shellHost.RecoverSurface(currentHost, DesktopSurfaceRole::Wallpaper, &shellError) && !shellError.empty())
                            WriteDiagnostics(scope, L"DesktopShellHost recovery failed: " + shellError);
                    }
                    currentSurfaceParent = shellHost.SurfaceParent();
                } else if (!shellError.empty()) {
                    WriteDiagnostics(scope, L"DesktopShellHost unavailable: " + shellError);
                }
            }

            if (currentHost != host || currentSurfaceParent != surfaceParent) {
                surfaces.Stop();
                activeRequests.clear();
                activeFingerprint.clear();
                host = currentHost;
                surfaceParent = currentSurfaceParent;
                nextRefresh = 0;
                resetRecovery();
            }

            const ULONGLONG now = GetTickCount64();
            if (host && surfaceParent && IsWindow(host) && IsWindow(surfaceParent) && now >= nextRefresh) {
                state = LoadRuntimeState(WallpaperConfigPath());
                if (state.layout == LayoutMode::Independent) {
                    const MonitorTopology topology = QueryMonitorTopology();
                    if (topology.Valid()) {
                        const RECT desktopBounds = HostDesktopBounds(topology, LayoutMode::Independent);
                        std::wstring shellError;
                        const bool visible = IsWindowVisible(host) != FALSE;
                        if (!shellHost.EnsureSurface(host, DesktopSurfaceRole::Wallpaper, desktopBounds, visible, &shellError)) {
                            if (!shellError.empty()) WriteDiagnostics(scope, L"DesktopShellHost independent geometry failed: " + shellError);
                        } else {
                            surfaceParent = shellHost.SurfaceParent();
                        }
                    }
                }

                std::wstring fingerprint;
                const auto desiredInHost = scope == WallpaperWebRuntimeScope::Widgets
                    ? DesiredWidgetRequests(host, fingerprint)
                    : DesiredRequests(host, state);
                const auto desired = MapRequestsToParent(host, surfaceParent, desiredInHost);
                const bool changed = !SameRequests(desired, activeRequests) ||
                                     (scope == WallpaperWebRuntimeScope::Widgets && fingerprint != activeFingerprint);
                if (changed) {
                    activeRequests = desired;
                    activeFingerprint = std::move(fingerprint);
                    resetRecovery();
                    startRequests(now, false);
                }
                repairStack();
                nextRefresh = now + kStateRefreshMs;
            }

            if (host && surfaceParent && IsWindow(host) && IsWindow(surfaceParent) && !activeRequests.empty()) {
                surfaces.Tick();
                if (!surfaces.Active()) {
                    healthySince = 0;
                    if (recoveryAttempts >= kMaxRecoveryAttempts) {
                        WriteDiagnostics(scope, (scope == WallpaperWebRuntimeScope::Widgets
                            ? L"Widget runtime 连续恢复 3 次失败，已停止自动恢复："
                            : L"Web runtime 连续恢复 3 次失败，已停止自动恢复：") + surfaces.LastErrorText());
                    } else {
                        if (nextRecovery == 0) nextRecovery = now + kRecoveryCooldownMs;
                        if (now >= nextRecovery) startRequests(now, true);
                    }
                } else {
                    if (healthySince == 0) healthySince = now;
                    if (recoveryAttempts > 0 && now - healthySince >= kRecoveryStableResetMs) {
                        recoveryAttempts = 0;
                        nextRecovery = 0;
                        WriteDiagnostics(scope, surfaces.DiagnosticsText() + L" · 已稳定运行，恢复计数已清零");
                    }
                }

                const auto error = surfaces.LastErrorText();
                if (!error.empty() && recoveryAttempts < kMaxRecoveryAttempts)
                    WriteDiagnostics(scope, L"运行异常：" + error);

                HWND settings = FindWindowW(kDesktopLibraryClass, nullptr);
                if (!settings) settings = FindWindowW(kWallpaperSettingsClass, nullptr);
                const auto snapshot = performance.Evaluate(host, settings, state.performance);
                const bool policyPause =
                    snapshot.action == PerformanceAction::Pause || snapshot.action == PerformanceAction::Stop;
                const bool hiddenWallpaper = scope == WallpaperWebRuntimeScope::WebWallpaper && !IsWindowVisible(host);
                surfaces.SetPaused(policyPause || hiddenWallpaper);
                repairStack();
            }

            std::this_thread::sleep_for(kTickInterval);
        }

        surfaces.Stop();
        WriteDiagnostics(scope, scope == WallpaperWebRuntimeScope::Widgets
            ? L"Widget runtime stopped" : L"Web runtime stopped");
        running.store(false, std::memory_order_release);
    }
};

WallpaperWebRuntimeCoordinator::WallpaperWebRuntimeCoordinator(WallpaperWebRuntimeScope scope)
    : impl_(std::make_unique<Impl>(scope)) {}
WallpaperWebRuntimeCoordinator::~WallpaperWebRuntimeCoordinator() { Stop(); }

bool WallpaperWebRuntimeCoordinator::Start() {
    if (!impl_) return false;
    if (impl_->worker.joinable()) return true;
    impl_->worker = std::jthread([impl = impl_.get()](std::stop_token token) { impl->Run(token); });
    return true;
}

void WallpaperWebRuntimeCoordinator::Stop() {
    if (!impl_ || !impl_->worker.joinable()) return;
    impl_->worker.request_stop();
    impl_->worker.join();
}

bool WallpaperWebRuntimeCoordinator::Running() const noexcept {
    return impl_ && impl_->running.load(std::memory_order_acquire);
}

WallpaperWebRuntimeScope WallpaperWebRuntimeCoordinator::Scope() const noexcept {
    return impl_ ? impl_->scope : WallpaperWebRuntimeScope::WebWallpaper;
}

bool WallpaperWebRuntimeCoordinator::SelfTest() {
    if (!WebWallpaperProcessSet::SelfTest()) return false;
    if (!DesktopWidgetStore::SelfTest()) return false;
    if (!WallpaperLibrary::IsTrustedWebUrl(L"https://example.com/wallpaper")) return false;
    if (WallpaperLibrary::IsTrustedWebUrl(L"http://example.com/wallpaper")) return false;

    WebWallpaperRequest a;
    a.region = {0, 0, 1920, 1080};
    a.source = L"https://example.com/wallpaper";
    a.itemId = L"web-a";
    WebWallpaperRequest b = a;
    if (!SameRequests({a}, {b})) return false;
    b.region.right = 1280;
    return !SameRequests({a}, {b});
}

bool ActivateWebWallpaperItem(const WallpaperLibraryItem& item,
                              const std::wstring& targetMonitorId,
                              std::wstring* error) {
    if (error) error->clear();
    if (item.kind != LibraryWallpaperKind::Web) {
        if (error) *error = L"该壁纸库项目不是 Web 类型";
        return false;
    }
    if (!WebWallpaperProcessSet::IsSupportedSource(item.source.wstring())) {
        if (error) *error = L"Web 壁纸源不可用；只允许存在的本地 HTML 或受信 HTTPS URL";
        return false;
    }
    return targetMonitorId.empty() ? PersistGlobalWeb(item, error)
                                   : PersistMonitorWeb(item, targetMonitorId, error);
}

} // namespace turingdesk::wallpaper
