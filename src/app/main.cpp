#include "miaodesk/AppSearch.h"
#include "miaodesk/CreatorToolRegistry.h"
#include "miaodesk/BuiltinWallpaperCatalog.h"
#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/DesktopWidgetTools.h"
#include "miaodesk/GeneratedDesktopPreview.h"
#include "miaodesk/GozSearch.h"
#include "miaodesk/HarnessProcessManager.h"
#include "miaodesk/L3Agent.h"
#include "miaodesk/CreatorToolWorker.h"
#include "miaodesk/ContentCreatorDialog.h"
#include "miaodesk/CreatorWorkspaceState.h"
#include "miaodesk/JsonStringField.h"
#include "miaodesk/MiaoSceneD2DRenderer.h"
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
    //
    // Creator tools go through the shared registry, NOT through another literal list
    // here. The reason is a silent one-way failure: Pi's --tools lives in
    // PiLaunchProfile.cpp and this table lives here, and the two never see each other.
    // When Pi offers a tool this table does not know, the worker exits 26 and the
    // model retries forever while the user sees "it never succeeds"; when this table
    // knows a tool Pi does not offer, the capability sits unused and nothing reports
    // it. One registry with two derivations is the only shape where that cannot happen.
    return miaodesk::creator::IsCreatorTool(tool) ||
           tool == "settings_open" ||
           tool == "ppt_create" ||
           tool == "file_create" ||
           tool == "folder_list" ||
           tool == "file_open" ||
           tool == "wallpaper_validate_package" ||
           tool == "wallpaper_state_get" ||
           tool == "desktop_widget_list" ||
           tool == "content_skill_get" ||
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

// CCA-04:把工作区策略接到真实文件系统。
//
// 它实现的正是测试里那个 in-memory port:决策逻辑一套,盘上动作一套。之所以要经过
// 这个接口而不是直接在分发函数里写文件,是因为「错误后无半写文件」这条只能在
// 决策层被断言 —— 把 <filesystem> 塞进分发函数,那些断言就只剩一句
// "Windows 真机待验",而它们恰好是最不该只留给真机的那批。
class FilesystemCreatorWorkspace : public miaodesk::creator::CreatorWorkspacePort {
public:
    explicit FilesystemCreatorWorkspace(std::wstring workspaceRoot)
        : root_(std::move(workspaceRoot)) {}

    bool ReadFile(const std::string& relativePath, std::string* bytes) override {
        const auto path = Resolve(relativePath);
        if (path.empty()) return false;
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        *bytes = std::string(std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>());
        return true;
    }

    std::vector<miaodesk::content::CandidatePart> Snapshot() override {
        std::vector<miaodesk::content::CandidatePart> parts;
        std::error_code ec;
        if (!fs::exists(root_, ec)) return parts;
        for (const auto& entry : fs::recursive_directory_iterator(
                 root_, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            const auto relative = RelativeToRoot(entry.path());
            if (relative.empty()) continue;
            std::string bytes;
            if (!ReadFile(relative, &bytes)) continue;   // 读不到就不算进摘要
            parts.push_back({RoleOf(relative), relative, std::move(bytes)});
        }
        return parts;
    }

    miaodesk::creator::CreatorFileFacts Facts(const std::string& relativePath) override {
        miaodesk::creator::CreatorFileFacts facts;
        const auto path = Resolve(relativePath);
        if (path.empty()) return facts;
        std::error_code ec;
        const auto status = fs::symlink_status(path, ec);
        if (ec || status.type() == fs::file_type::not_found) return facts;
        facts.exists = true;
        // reparse point 必须**真的去问文件系统**。模型说自己要写一个普通文件,
        // 而那个路径可能是联接点;不问的话,"解析后落在工作区之外"这一类
        // 就只能靠路径字符串猜,而猜不出 junction。
        facts.isReparsePoint = fs::is_symlink(status);
        facts.isDirectory = fs::is_directory(status);
        if (facts.isDirectory) {
            facts.byteCount = 0;
        } else {
            facts.byteCount = static_cast<std::uint64_t>(fs::file_size(path, ec));
            if (ec) facts.byteCount = 0;
        }
        return facts;
    }

    StagedBytes WriteStaged(const std::string& stagedPath, const std::string& content) override {
        StagedBytes staged;
        const auto path = fs::path(std::wstring(stagedPath.begin(), stagedPath.end()));
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return staged;
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!stream) return staged;
        }
        // **读回来**再报事实。写盘之后回读是这道工序的全部意义:
        // 报"我写了 N 字节"而没有回读,就绕过了事务对暂存内容的核对,
        // 于是换成另一个文件、或者只写进去一半,都不会被发现。
        std::ifstream back(path, std::ios::binary);
        if (!back) return staged;
        staged.bytes = std::string(std::istreambuf_iterator<char>(back),
                                   std::istreambuf_iterator<char>());
        staged.written = true;
        staged.facts = Facts(std::string(stagedPath.begin(), stagedPath.end()));
        staged.facts.exists = true;
        staged.facts.byteCount = staged.bytes.size();
        staged.facts.isReparsePoint = false;
        return staged;
    }

    bool ReplaceTarget(const std::string& stagedPath, const std::string& relativePath) override {
        const auto target = Resolve(relativePath);
        if (target.empty()) return false;
        const auto staged = std::wstring(stagedPath.begin(), stagedPath.end());
        // MOVEFILE_WRITE_THROUGH:落盘才算完成。去掉它的话,断电时会留下
        // 一个新旧混合的文件,而两边都以为自己写对了。
        return MoveFileExW(staged.c_str(), target.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }

    void DiscardStaged(const std::string& stagedPath) override {
        const auto path = std::wstring(stagedPath.begin(), stagedPath.end());
        std::error_code ec;
        fs::remove(path, ec);
    }

    bool SealSnapshot(const std::string& digest,
                      const std::vector<miaodesk::content::CandidatePart>& parts,
                      std::string* snapshotPath) override {
        // 封存必须是**工作区之外**的一份独立副本。指向工作区的话,"封存后修改源
        // 目录不能改变待应用候选"这条名存实亡 —— 因为封存的就是源目录。
        std::error_code ec;
        const auto revisions = root_.parent_path() / L"revisions";
        fs::create_directories(revisions, ec);
        if (ec) return false;
        const auto target = revisions /
                            (std::wstring(digest.begin(), digest.end()).substr(0, 16) + L".sealed");
        // 已经封存过就是幂等成功:同一份内容再次提交不该失败。
        if (fs::exists(target, ec) && !ec) {
            *snapshotPath = revisions.filename().string() + "/" +
                            Narrow(target.filename().wstring());
            return true;
        }
        auto temporary = target;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            // 写进来的就是调用方算摘要用的那一份。自己再扫一遍盘的话,
            // 两次读取之间的任何变动都会进到封存里,而封存本来的意义
            // 就是让已封存的东西不随源目录变。
            stream << "# candidate " << digest << "\n";
            for (const auto& part : parts) {
                stream << "-- " << part.relPath << " " << part.bytes.size() << "\n";
                stream.write(part.bytes.data(), static_cast<std::streamsize>(part.bytes.size()));
                stream << "\n";
            }
            if (!stream) return false;
        }
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            fs::remove(temporary, ec);
            return false;
        }
        *snapshotPath = revisions.filename().string() + "/" + Narrow(target.filename().wstring());
        return true;
    }

    bool LoadLedger(std::string* text) override {
        std::ifstream stream(root_.parent_path() / L"candidate-ledger.state", std::ios::binary);
        if (!stream) return false;
        *text = std::string(std::istreambuf_iterator<char>(stream),
                            std::istreambuf_iterator<char>());
        return true;
    }

    bool SaveLedger(const std::string& text) override {
        const auto path = root_.parent_path() / L"candidate-ledger.state";
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!stream) return false;
        }
        // 台账写一半比不写更糟:下一次调用会拿着半份记录重新分配 revision。
        return MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }

    bool CollectEvidence(const miaodesk::creator::RenderEvidenceRequirements& requirements,
                         std::vector<miaodesk::creator::RenderEvidenceSample>* samples,
                         std::string* detail) override {
        // 离屏采集:自建一个 DC render target,不碰用户屏幕上的任何窗口。
        // 计划验收的第四条(不需要抓取私人桌面)就是这一行保证的。
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     factory.GetAddressOf()))) {
            *detail = "无法创建 D2D 工厂,渲染取证失败。";
            return false;
        }
        const D2D1_RENDER_TARGET_PROPERTIES props =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                           D2D1_ALPHA_MODE_PREMULTIPLIED));
        const float width = 640.0f;
        const float height = 360.0f;
        Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
        if (FAILED(factory->CreateDCRenderTarget(&props, target.GetAddressOf()))) {
            *detail = "无法创建离屏渲染目标,渲染取证失败。";
            return false;
        }
        const RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        if (FAILED(target->BindDC(nullptr, &rect))) {
            *detail = "离屏渲染目标没有就绪,渲染取证失败。";
            return false;
        }

        // 渲染器要的是包根目录。工作区本身就是那个根。
        miaodesk::content::MiaoSceneD2DRenderer renderer;
        std::wstring renderError;
        const bool loaded = renderer.Load(root_, target.Get(), &renderError);

        // 摘要从**当前工作区**算,不是从模型那边取。这是"样本绑定正确摘要"的前提:
        // 换一个来源的话,拿到的是"某个候选"的证据,不一定是这一版的。
        //
        // 每帧的摘要由 MakeRenderedEvidenceSample 绑定,它只接受这一个字符串,
        // 所以调用点没有第二个变量可以填错 —— 上一版在这里另声明了一个从未赋值的
        // 局部 digest,结果每一帧都带空摘要,判据层只能整批拒绝。
        const auto computed = miaodesk::content::ComputeCandidateDigest(Snapshot());
        if (!computed.UsableAsIdentity()) {
            *detail = "当前工作区算不出可用的候选摘要,无法为它采集渲染证据。";
            return false;
        }

        miaodesk::creator::OffscreenEvidenceRequest offscreen;
        offscreen.width = static_cast<std::uint32_t>(width);
        offscreen.height = static_cast<std::uint32_t>(height);
        offscreen.backend = "d2d";
        offscreen.fixture = "offscreen:640x360";

        const std::uint32_t frames = requirements.minFrames == 0 ? 1u : requirements.minFrames;
        for (std::uint32_t index = 0; index < frames; ++index) {
            target->BeginDraw();
            target->Clear(D2D1::ColorF(D2D1::ColorF::Black, 0.0f));
            std::wstring frameError;
            const bool drew =
                loaded && renderer.Draw(static_cast<float>(index) * 0.1f,
                                        D2D1::SizeF(width, height), &frameError);
            const HRESULT hr = target->EndDraw();
            if (!drew || FAILED(hr)) {
                // 失败就是失败。**不用封面图、不用纯色、不用上一版顶替** ——
                // 那是计划验收点名禁止的一条:一个坏包不能看起来渲染得很好。
                *detail = WideToUtf8(frameError.empty() ? L"渲染器没有输出这一帧。"
                                                      : frameError);
                return false;
            }
            samples->push_back(miaodesk::creator::MakeRenderedEvidenceSample(
                index, static_cast<std::uint64_t>(::GetTickCount64()), offscreen,
                computed.value));
        }
        return true;
    }

    bool SaveState(const miaodesk::creator::CreatorWorkspaceState& state) override {
        const auto path = fs::path(root_) / fs::path(miaodesk::creator::kCreatorWorkspaceStateFileName);
        const auto text = miaodesk::creator::SerializeCreatorWorkspaceState(state);
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!stream) return false;
        }
        // 状态写一半比不写更糟:worker 下一次调用会拿到一个半填的状态,
        // 而半填的状态会因为缺 sessionId 被整体拒绝 —— 于是看起来像
        // "这一轮被取消了",而真实原因是写盘被打断。
        return MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }

    bool ReadManagedSource(const std::string& source, std::string* bytes,
                           std::string* resolvedName) override {
        // 只有宿主持有的来源,而"宿主持有"在这里有一个非常窄的含义:**已经在这个
        // 作品自己的工作区里**的文件。宿主验证它的方法是重新解析一次路径、
        // 确认它仍然落在工作区内,而不是相信模型给的那个字符串。
        //
        // 刻意不接受任意盘上路径:一个模型构造的路径字符串不是证据,接受它就等于
        // 把整个文件系统交出去了。content:cloud 那类托管素材库属于后面的工作,
        // 所以它在这里被明确拒绝,而不是默默当成一个路径去试。
        if (source.rfind("content:cloud", 0) == 0) return false;
        if (source.empty()) return false;
        const auto path = Resolve(source);
        if (path.empty()) return false;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec) return false;
        // 来源还要过一遍扩展名:不能借"导入素材"把一个 .js 弄进包里。
        const auto extension = path.extension().string();
        if (miaodesk::creator::CreatorWorkspacePolicy::IsForbiddenExtension(extension)) {
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        *bytes = std::string(std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>());
        *resolvedName = RelativeToRoot(path);
        return true;
    }

private:
    static std::string Narrow(const std::wstring& value) {
        if (value.empty()) return {};
        const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                              static_cast<int>(value.size()), nullptr, 0,
                                              nullptr, nullptr);
        if (count <= 0) return {};
        std::string out(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            out.data(), count, nullptr, nullptr);
        return out;
    }

    // 相对路径 -> 工作区内的绝对路径。**先规范化再过策略**:
    // 一个没规范化的路径可以让 ".." 藏在 "scene\..\..\x" 里,而字符串比对看不出来。
    fs::path Resolve(const std::string& relativePath) const {
        std::string normalized;
        if (!miaodesk::creator::NormalizeCreatorRelativePath(relativePath, &normalized)) return {};
        fs::path candidate = root_;
        for (const auto& segment : fs::path(normalized)) candidate /= segment;
        std::error_code ec;
        const auto weak = fs::weakly_canonical(candidate, ec);
        if (ec) return candidate;
        const auto rootWeak = fs::weakly_canonical(root_, ec);
        if (!ec) {
            // 规范化之后必须**仍然在工作区内**。这一步挡的是联接点与大小写变体:
            // 路径字符串看起来在里,解析出来不在。
            const auto rootText = rootWeak.wstring();
            const auto candidateText = weak.wstring();
            const bool sameDrive = rootText.size() > 1 && candidateText.size() > 1 &&
                                   rootText[1] == L':' && candidateText[1] == L':' &&
                                   std::towlower(rootText[0]) == std::towlower(candidateText[0]);
            if (!sameDrive) return {};
            if (candidateText.size() < rootText.size() ||
                candidateText.compare(0, rootText.size(), rootText) != 0) {
                return {};
            }
        }
        return candidate;
    }

    std::string RelativeToRoot(const fs::path& path) const {
        std::error_code ec;
        const auto relative = fs::relative(path, root_, ec);
        if (ec) return {};
        auto text = relative.generic_string();
        if (text.rfind("./", 0) == 0) text.erase(0, 2);
        return text;
    }

    static miaodesk::content::CandidatePartRole RoleOf(const std::string& relativePath) {
        if (relativePath == "manifest.json") return miaodesk::content::CandidatePartRole::Manifest;
        if (relativePath == "parameters.json") return miaodesk::content::CandidatePartRole::Parameters;
        if (relativePath.rfind("scene/", 0) == 0) return miaodesk::content::CandidatePartRole::Scene;
        if (relativePath.rfind("preview.", 0) == 0) return miaodesk::content::CandidatePartRole::Preview;
        if (relativePath.rfind("assets/", 0) == 0) return miaodesk::content::CandidatePartRole::Asset;
        return miaodesk::content::CandidatePartRole::Other;
    }

    fs::path root_;
};

// 创作工具的真实分发。到这里说明 IsAllowedPiNativeTool 已经放行了它,
// 但"放行"和"能执行"是两件事:此前这些工具落进 ExecuteNativeToolRaw,
// 回给模型的是一句"未知工具",而调用看起来是被接受的。
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), count);
    return out;
}

miaodesk::NativeToolResult RunCreatorTool(const std::string& tool, const std::string& arguments) {
    using namespace miaodesk::creator;

    // 会话与工作区从宿主的环境变量取,不从参数里取。参数是模型写的,
    // 它说什么都不是事实来源;环境变量是宿主这一侧导出的。
    const auto workspaceRoot = ReadEnvironmentValue(miaodesk::kCreatorWorkspaceEnvironment);
    const auto sessionId = ReadEnvironmentValue(miaodesk::kCreatorSessionEnvironment);

    CreatorToolArgs args;
    args.sessionId = miaodesk::ExtractJsonString(arguments, "\"sessionId\"");
    args.workspaceRoot = miaodesk::ExtractJsonString(arguments, "\"workspaceRoot\"");
    args.relativePath = miaodesk::ExtractJsonString(arguments, "\"relativePath\"");
    args.content = miaodesk::ExtractJsonString(arguments, "\"content\"");
    args.source = miaodesk::ExtractJsonString(arguments, "\"source\"");
    args.digest = miaodesk::ExtractJsonString(arguments, "\"digest\"");
    args.backend = miaodesk::ExtractJsonString(arguments, "\"backend\"");
    args.summary = miaodesk::ExtractJsonString(arguments, "\"summary\"");

    CreatorWorkerInput input;
    input.workspaceRoot = WideToUtf8(workspaceRoot);
    input.sessionId = WideToUtf8(sessionId);
    // 状态文件读不懂时 hasState 保持 false:分发层会因此拒绝,而不是
    // 拿一个半填的状态去放行一次写入。
    {
        std::ifstream stateStream(fs::path(workspaceRoot) /
                                  fs::path(kCreatorWorkspaceStateFileName),
                                  std::ios::binary);
        if (stateStream) {
            const std::string text((std::istreambuf_iterator<char>(stateStream)),
                                   std::istreambuf_iterator<char>());
            input.hasState = ParseCreatorWorkspaceState(text, &input.state);
        }
    }

    FilesystemCreatorWorkspace port(workspaceRoot);
    std::optional<CreatorPackageTransaction> transaction;
    const auto reply = DispatchCreatorTool(tool, args, input, port, &transaction);

    miaodesk::NativeToolResult result;
    result.success = reply.ok;
    result.message = Utf8ToWide(reply.ToModelText());
    return result;
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
    if (miaodesk::creator::IsCreatorTool(toolUtf8)) {
        result = RunCreatorTool(toolUtf8, arguments);
    } else if (miaodesk::preview::IsGeneratedPreviewTool(toolUtf8)) {
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
    if (args.find(L"--creator-window-self-test") != std::wstring_view::npos) {
        const int result = miaodesk::creator::ContentCreatorWindowSelfTest(instance) ? 0 : 8;
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    }
    if (args.find(L"--self-test") != std::wstring_view::npos) {
        const int result = piExtensionReady && RunNativeSelfTest() &&
                           miaodesk::startup::SelfTest() ? 0 : 5;
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    }

    const bool startupLaunch = miaodesk::startup::IsStartupLaunch(args);
    const auto creatorKind = miaodesk::creator::ParseCommandLine(args);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\MiaoDesk.Native.Search.Singleton");
    if (!mutex) {
        const DWORD mutexError = GetLastError();
        if (SUCCEEDED(com)) CoUninitialize();
        return ReportUnexpectedExit(
            2, L"无法创建单实例锁，Win32=" + std::to_wstring(mutexError));
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        int forwardedResult = 0;
        if (creatorKind != miaodesk::creator::ContentCreatorKind::None) {
            // The single running MiaoDesk process owns L3/Pi and the conversation
            // surface. Forward creator mode instead of starting a second AI stack.
            // A creator request is successful only after the existing host reports
            // that the requested top-level window really exists.
            if (!miaodesk::creator::SendToRunningApp(creatorKind)) {
                ActivateExistingSearchWindow();
                miaodesk::log::Error(
                    L"CreatorIPC",
                    L"单实例创作请求转发失败；返回非零退出码。");
                forwardedResult = 9;
            }
        } else if (!startupLaunch) {
            ActivateExistingSearchWindow();
        }
        CloseHandle(mutex);
        if (SUCCEEDED(com)) CoUninitialize();
        return forwardedResult;
    }

    if (!startupLaunch) miaodesk::startup::PromptForConsentIfNeeded();

    miaodesk::SearchWindow window(instance);
    const bool showSearchOnLaunch =
        !startupLaunch && creatorKind == miaodesk::creator::ContentCreatorKind::None;
    if (!window.Create(showSearchOnLaunch)) {
        const std::wstring reason = window.LastCreateError().empty()
            ? L"搜索窗口初始化失败" : window.LastCreateError();
        if (SUCCEEDED(com)) CoUninitialize();
        CloseHandle(mutex);
        return ReportUnexpectedExit(4, reason);
    }
    miaodesk::log::Info(L"App", startupLaunch
        ? L"MiaoDesk 登录启动成功，已静默驻留托盘"
        : L"MiaoDesk 主窗口启动成功");

    if (creatorKind != miaodesk::creator::ContentCreatorKind::None)
        window.OpenContentCreator(creatorKind);

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
