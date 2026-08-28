#include "turingdesk/L3CliWindow.h"
#include "turingdesk/PiRuntime.h"
#include "turingdesk/GeneratedDesktopPreview.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr wchar_t kConversationClass[] = L"TuringDesk.Native.ConversationPanel";
constexpr int kInputId = 3102;
constexpr int kSendId = 3103; // Contract marker: send is custom drawn but keeps the stable semantic id.
constexpr UINT kDeltaMessage = WM_APP + 31;
constexpr UINT kDirectDoneMessage = WM_APP + 32;
constexpr UINT kPiDoneMessage = WM_APP + 33;
constexpr UINT kPreviewArtifactMessage = WM_APP + 35;
constexpr UINT_PTR kActivityTimer = 41;
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

enum class ActiveRuntime { Pi, DirectModel };
enum class EntryKind { User, Assistant, System };
enum class StepStatus { Running, Success, Failed };
enum class ChatArtifactKind { OpenFile, OpenPreview };

struct ChatArtifact {
    std::wstring title;
    std::wstring subtitle;
    std::wstring path;
    ChatArtifactKind kind{ChatArtifactKind::OpenFile};
};

struct ChatEntry {
    EntryKind kind{EntryKind::Assistant};
    std::wstring text;
    std::wstring time;
    std::vector<ChatArtifact> artifacts;
    std::vector<std::wstring> attachments;
};

struct ArtifactHitTarget {
    RECT bounds{};
    std::size_t entryIndex{kNoIndex};
    std::size_t artifactIndex{kNoIndex};
};

struct ActivityStep {
    std::wstring tool;
    std::wstring label;
    StepStatus status{StepStatus::Running};
};

struct UiMessage {
    std::uint64_t generation{};
    std::wstring text;
};

struct ActivityCard {
    bool visible{};
    bool terminal{};
    bool failed{};
    std::wstring title;
    std::wstring detail;
    std::vector<ActivityStep> steps;
};

struct ConversationState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    HWND input{};
    WNDPROC oldInputProc{};
    L3Agent* agent{};
    PiRuntime* pi{};
    HBRUSH inputBrush{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT bodyBoldFont{};
    HFONT smallFont{};
    HFONT symbolFont{};
    int dpi{96};
    std::vector<ChatEntry> entries;
    ActivityCard activity;
    std::vector<std::wstring> pendingAttachments;
    std::vector<ArtifactHitTarget> artifactHits;
    std::size_t assistantIndex{kNoIndex};
    std::wstring streaming;
    std::wstring lastPrompt;
    std::wstring activePrompt;
    std::wstring pendingTypedPrompt;
    std::wstring pendingActualPrompt;
    bool pendingRetry{};
    bool pendingConfirmation{};
    std::uint64_t generation{};
    std::uintmax_t activityLogOffset{};
    ActiveRuntime activeRuntime{ActiveRuntime::Pi};
    bool busy{};
    bool pinned{};
    bool scrollToBottom{true};
    int scrollOffset{};
    int contentHeight{};
    RECT sendRect{};
    RECT closeRect{};
    RECT pinRect{};
    RECT historyRect{};
    RECT moreRect{};
    RECT confirmRect{};
    RECT cancelRect{};
    RECT viewport{};
};

std::atomic_uint64_t gCliGeneration{0};
PiRuntime gPiRuntime;
ConversationState* gConversationState{};

int Px(const ConversationState& state, int value) {
    return MulDiv(value, state.dpi, 96);
}

std::wstring Trim(std::wstring value) {
    const auto notSpace = [](wchar_t ch) { return !std::iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring CleanMarkdown(std::wstring value) {
    for (const std::wstring marker : {L"**", L"__", L"### ", L"## "}) {
        std::size_t pos = 0;
        while ((pos = value.find(marker, pos)) != std::wstring::npos) value.erase(pos, marker.size());
    }
    return value;
}

std::wstring SafeEndpoint(std::wstring value) {
    const auto query = value.find_first_of(L"?#");
    if (query != std::wstring::npos) value.resize(query);
    return value;
}

fs::path L3RouteLogPath() { return RuntimeLogPath(L"l3-runtime.log"); }
fs::path PiDetailLogPath() { return RuntimeLogPath(L"pi-runtime.log"); }

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
    return out;
}

void AppendRouteLog(const std::wstring& text) {
    const auto path = L3RouteLogPath();
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

std::wstring CurrentTimeText() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[16]{};
    swprintf_s(text, L"%02u:%02u", now.wHour, now.wMinute);
    return text;
}

bool ContainsHttpStatus(const std::wstring& text, int status) {
    return text.find(L"HTTP " + std::to_wstring(status)) != std::wstring::npos;
}

bool ShouldOfferRetry(const std::wstring& rawText) {
    if (rawText.empty() || rawText.find(L"已停止") != std::wstring::npos) return false;
    if (ContainsHttpStatus(rawText, 400) || ContainsHttpStatus(rawText, 401) ||
        ContainsHttpStatus(rawText, 403) || ContainsHttpStatus(rawText, 404) ||
        ContainsHttpStatus(rawText, 405) || ContainsHttpStatus(rawText, 413) ||
        ContainsHttpStatus(rawText, 422)) return false;
    if (rawText.find(L"WinHTTP 错误 12175") != std::wstring::npos) return false;
    return true;
}

std::wstring ClassifyTransportFailure(std::wstring text) {
    if (text.empty()) return text;
    if (text.find(L"WinHTTP 错误 12002") != std::wstring::npos || ContainsHttpStatus(text, 408) || ContainsHttpStatus(text, 504))
        return L"请求超时。模型服务没有在限定时间内完成响应，可以输入 /retry 重试。";
    if (text.find(L"WinHTTP 错误 12007") != std::wstring::npos)
        return L"无法解析模型服务地址。请检查 API 地址或网络连接，可以输入 /retry 重试。";
    if (text.find(L"WinHTTP 错误 12029") != std::wstring::npos || text.find(L"WinHTTP 错误 12030") != std::wstring::npos ||
        text.find(L"WinHTTP 错误 12031") != std::wstring::npos)
        return L"无法连接模型服务，或连接被服务端中断。可以稍后输入 /retry 重试。";
    if (text.find(L"WinHTTP 错误 12175") != std::wstring::npos)
        return L"HTTPS/TLS 握手失败。请检查证书、系统时间或代理设置。";
    if (ContainsHttpStatus(text, 401) || ContainsHttpStatus(text, 403))
        return L"模型鉴权失败（HTTP 401/403）。请从系统托盘进入设置，检查 API Key 和权限。";
    if (ContainsHttpStatus(text, 429)) return L"模型服务正在限流（HTTP 429）。稍后可以输入 /retry 重试。";
    if (ContainsHttpStatus(text, 500) || ContainsHttpStatus(text, 502) || ContainsHttpStatus(text, 503))
        return L"模型服务暂时不可用（HTTP 5xx）。稍后可以输入 /retry 重试。";
    return text;
}

std::wstring ReadText(HWND control) {
    const int len = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(len) + 1, L'\0');
    if (len > 0) GetWindowTextW(control, value.data(), len + 1);
    value.resize(static_cast<std::size_t>(len));
    return value;
}

void PostUi(HWND hwnd, UINT message, std::uint64_t generation, std::wstring text) {
    auto payload = std::make_unique<UiMessage>();
    payload->generation = generation;
    payload->text = std::move(text);
    if (PostMessageW(hwnd, message, 0, reinterpret_cast<LPARAM>(payload.get()))) payload.release();
}

bool PointIn(const RECT& rect, POINT point) {
    return PtInRect(&rect, point) != FALSE;
}

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void StrokeRounded(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(dc, hollow);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
}

void DrawAvatar(HDC dc, int cx, int cy, int radius, bool user) {
    HBRUSH outer = CreateSolidBrush(user ? RGB(250, 218, 198) : RGB(102, 151, 255));
    HBRUSH inner = CreateSolidBrush(RGB(255, 255, 255));
    HPEN noPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
    auto oldPen = SelectObject(dc, noPen);
    auto oldBrush = SelectObject(dc, outer);
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, inner);
    const int inset = std::max(2, radius / 4);
    Ellipse(dc, cx - radius + inset, cy - radius + inset, cx + radius - inset, cy + radius - inset);
    if (!user) {
        HBRUSH core = CreateSolidBrush(RGB(129, 112, 255));
        SelectObject(dc, core);
        const int coreR = std::max(2, radius / 3);
        Ellipse(dc, cx - coreR, cy - coreR, cx + coreR, cy + coreR);
        DeleteObject(core);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(outer);
    DeleteObject(inner);
    DeleteObject(noPen);
}

int MeasureTextHeight(HDC dc, HFONT font, const std::wstring& text, int width) {
    HGDIOBJ old = SelectObject(dc, font);
    RECT calc{0, 0, std::max(20, width), 0};
    const std::wstring clean = CleanMarkdown(text.empty() ? L" " : text);
    DrawTextW(dc, clean.c_str(), static_cast<int>(clean.size()), &calc,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, old);
    return std::max(20, calc.bottom - calc.top);
}

void DrawWrapped(HDC dc, HFONT font, COLORREF color, const std::wstring& text, RECT rect, UINT extraFlags = 0) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const std::wstring clean = CleanMarkdown(text);
    DrawTextW(dc, clean.c_str(), static_cast<int>(clean.size()), &rect,
              DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | extraFlags);
    SelectObject(dc, old);
}

std::wstring FriendlyToolName(const std::wstring& raw) {
    if (raw == L"ppt_create") return L"制作 PowerPoint";
    if (raw == L"file_create") return L"创建文件";
    if (raw == L"folder_list") return L"查看文件夹";
    if (raw == L"file_open") return L"打开文件";
    if (raw == L"image_generate") return L"生成图片";
    if (raw == L"settings_open") return L"打开图灵设置";
    if (raw == L"wallpaper_create_web_package") return L"制作动态壁纸";
    if (raw == L"wallpaper_validate_package") return L"检查壁纸包";
    if (raw == L"wallpaper_state_get") return L"读取桌面状态";
    if (raw == L"wallpaper_apply_web_package") return L"应用动态壁纸";
    if (raw == L"desktop_widget_create_web") return L"创建桌面组件";
    if (raw == L"desktop_widget_update") return L"调整桌面组件";
    if (raw == L"desktop_widget_remove") return L"移除桌面组件";
    if (raw == L"desktop_widget_list") return L"查看桌面组件";
    if (raw == L"read") return L"读取文件";
    if (raw == L"write" || raw == L"edit") return L"编辑文件";
    if (raw == L"find" || raw == L"grep" || raw == L"ls") return L"查找内容";
    if (raw == L"bash") return L"执行系统操作";
    return raw.empty() ? L"执行操作" : raw;
}

std::wstring FileNameOnly(const std::wstring& path) {
    const auto name = fs::path(path).filename().wstring();
    return name.empty() ? path : name;
}

std::wstring ExtractPathSuffix(const std::wstring& message) {
    const auto colon = message.find_last_of(L":：");
    if (colon == std::wstring::npos || colon + 1 >= message.size()) return {};
    return Trim(message.substr(colon + 1));
}

bool TryBuildChatArtifact(const std::wstring& tool, const std::wstring& message, ChatArtifact& out) {
    const std::wstring text = Trim(message);
    if (text.empty()) return false;

    if (const auto marker = text.find(L"preview="); marker != std::wstring::npos) {
        out.kind = ChatArtifactKind::OpenPreview;
        out.path = Trim(text.substr(marker + 8));
        out.title = tool.find(L"wallpaper") != std::wstring::npos ? L"壁纸预览" : L"小组件预览";
        out.subtitle = L"点击查看并应用";
        return !out.path.empty();
    }

    const bool looksLikePath = text.find(L":\\") != std::wstring::npos || text.find(L"\\\\") != std::wstring::npos;
    if (text.find(L"已生成") != std::wstring::npos || text.find(L"已创建") != std::wstring::npos || looksLikePath) {
        const auto path = ExtractPathSuffix(text);
        if (path.empty()) return false;
        out.kind = ChatArtifactKind::OpenFile;
        out.path = path;
        out.title = FriendlyToolName(tool);
        out.subtitle = FileNameOnly(path);
        if (out.subtitle.empty()) out.subtitle = L"点击打开";
        return true;
    }
    return false;
}

void AppendAssistantArtifact(ConversationState& state, ChatArtifact artifact) {
    if (state.assistantIndex != kNoIndex && state.assistantIndex < state.entries.size()) {
        state.entries[state.assistantIndex].artifacts.push_back(std::move(artifact));
    } else if (!state.entries.empty() && state.entries.back().kind == EntryKind::Assistant) {
        state.entries.back().artifacts.push_back(std::move(artifact));
    } else {
        ChatEntry entry;
        entry.kind = EntryKind::Assistant;
        entry.time = CurrentTimeText();
        entry.artifacts.push_back(std::move(artifact));
        state.entries.push_back(std::move(entry));
        state.assistantIndex = state.entries.size() - 1;
    }
    InvalidateConversation(state, true);
}

void TryAppendAssistantArtifact(ConversationState& state, const std::wstring& tool, const std::wstring& message) {
    ChatArtifact artifact;
    if (!TryBuildChatArtifact(tool, message, artifact)) return;
    AppendAssistantArtifact(state, std::move(artifact));
}

void OpenChatArtifact(HWND owner, const ChatArtifact& artifact) {
    if (artifact.path.empty()) return;
    if (artifact.kind == ChatArtifactKind::OpenPreview) {
        if (!preview::OpenPreviewById(owner, artifact.path)) {
            MessageBoxW(owner, L"找不到对应的预览内容，可能已被关闭或清理。", L"妙喵 AI", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }
    const HINSTANCE result = ShellExecuteW(owner, L"open", artifact.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(owner, (L"无法打开：" + artifact.path).c_str(), L"妙喵 AI", MB_OK | MB_ICONERROR);
    }
}

int ArtifactStackHeight(const ConversationState& state, const ChatEntry& entry) {
    if (entry.artifacts.empty()) return 0;
    return static_cast<int>(entry.artifacts.size()) * Px(state, 58) + Px(state, 6);
}

int AttachmentStackHeight(const ConversationState& state, const ChatEntry& entry) {
    if (entry.attachments.empty()) return 0;
    return static_cast<int>(entry.attachments.size()) * Px(state, 26) + Px(state, 6);
}

void InvalidateConversation(ConversationState& state, bool bottom = false) {
    if (bottom) state.scrollToBottom = true;
    if (state.window) InvalidateRect(state.window, nullptr, FALSE);
}

void AddEntry(ConversationState& state, EntryKind kind, std::wstring text) {
    state.entries.push_back(ChatEntry{kind, std::move(text), CurrentTimeText()});
    InvalidateConversation(state, true);
}

void AppendCompleted(ConversationState& state, const std::wstring& user, const std::wstring& assistant) {
    AddEntry(state, EntryKind::User, user);
    AddEntry(state, EntryKind::Assistant, assistant.empty() ? L"已完成。" : assistant);
}

std::wstring RuntimeName(ActiveRuntime runtime) {
    return runtime == ActiveRuntime::Pi ? L"Pi Agent" : L"Direct Model Runtime";
}

std::wstring RuntimeStatusText(ConversationState& state) {
    const auto status = state.pi->Status(*state.agent);
    std::wstring text = L"主路由：Pi Agent（固定 Agent 工具模式）→ 当前配置 API";
    text += L"\r\n失败回退：Direct Model Runtime（无工具）→ 当前配置 API";
    text += L"\r\nProvider：" + (state.agent->Config().providerId.empty() ? std::wstring(L"未识别") : state.agent->Config().providerId);
    text += L" · Model：" + (state.agent->Config().model.empty() ? std::wstring(L"未配置") : state.agent->Config().model);
    text += L"\r\nPi：" + status.message;
    return text;
}

std::wstring UserFacingLocalReply(const std::wstring& command, const ConversationState& state, const std::wstring& raw) {
    if (command == L"/status") {
        const auto& config = state.agent->Config();
        std::wstring text = L"妙喵 AI";
        text += L" · Provider=" + (config.providerId.empty() ? std::wstring(L"未识别") : config.providerId);
        text += L" · Model=" + (config.model.empty() ? std::wstring(L"未配置") : config.model);
        text += L" · API Key=" + std::wstring(state.agent->HasStoredApiKey() ? L"已配置" : L"未配置");
        return text;
    }
    if (command == L"/help")
        return L"可用命令：/status、/runtime、/time、/apps、/files、/open、/open-file、/new。模型和 API Key 请从系统托盘进入设置。";
    if (command == L"/new" || command == L"/new-chat" || command == L"新对话") return L"已开始新的对话。";
    return raw;
}

bool LooksDestructive(const std::wstring& prompt) {
    const auto lower = Lower(prompt);
    for (const wchar_t* keyword : {L"删除", L"清空", L"卸载", L"格式化", L"永久移除", L"覆盖原文件",
                                   L"delete ", L"remove ", L"uninstall", L"format ", L"wipe ", L"erase ", L"overwrite"}) {
        if (lower.find(keyword) != std::wstring::npos) return true;
    }
    return false;
}

void SetBusyVisual(ConversationState& state, bool busy) {
    state.busy = busy;
    EnableWindow(state.input, busy ? FALSE : TRUE);
    InvalidateConversation(state, true);
}

std::uintmax_t CurrentPiLogSize() {
    std::error_code ec;
    const auto size = fs::file_size(PiDetailLogPath(), ec);
    return ec ? 0 : size;
}

void ResetActivity(ConversationState& state) {
    state.activity = ActivityCard{};
    state.activity.visible = true;
    state.activity.title = L"正在理解你的请求";
    state.activity.detail = L"Pi Agent 正在分析并准备执行";
    state.activityLogOffset = CurrentPiLogSize();
}

void ClearActivityCard(ConversationState& state) {
    state.activity = {};
    state.activity.visible = false;
}

void UpdateToolStart(ConversationState& state, const std::wstring& tool) {
    state.activity.steps = {{tool, FriendlyToolName(tool), StepStatus::Running}};
    state.activity.visible = true;
    state.activity.terminal = false;
    state.activity.failed = false;
    state.activity.title = L"正在执行：" + FriendlyToolName(tool);
    state.activity.detail = L"图灵正在操作你的电脑";
    InvalidateConversation(state, true);
}

void UpdateToolEnd(ConversationState& state, const std::wstring& tool, bool failed, const std::wstring& resultText = {}) {
    if (failed) {
        for (auto& step : state.activity.steps) {
            if (step.tool == tool && step.status == StepStatus::Running) step.status = StepStatus::Failed;
        }
        state.activity.title = L"操作遇到问题";
        state.activity.detail = FriendlyToolName(tool) + L" 未成功完成";
        state.activity.failed = true;
    } else {
        state.activity.steps.clear();
        state.activity.title = L"正在继续";
        state.activity.detail = L"图灵正在处理下一步";
        state.activity.failed = false;
        if (!resultText.empty()) TryAppendAssistantArtifact(state, tool, resultText);
    }
    InvalidateConversation(state, true);
}

void PollPiActivity(ConversationState& state) {
    if (!state.busy || state.activeRuntime != ActiveRuntime::Pi) return;
    const auto path = PiDetailLogPath();
    std::error_code ec;
    const auto current = fs::file_size(path, ec);
    if (ec || current <= state.activityLogOffset) return;

    std::ifstream stream(path, std::ios::binary);
    if (!stream) return;
    stream.seekg(static_cast<std::streamoff>(state.activityLogOffset));
    std::string appended((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    state.activityLogOffset = current;
    const auto wide = Utf8ToWide(appended);
    std::wstringstream lines(wide);
    std::wstring line;
    while (std::getline(lines, line)) {
        if (const auto pos = line.find(L"tool start: "); pos != std::wstring::npos) {
            auto tool = Trim(line.substr(pos + 12));
            const auto semi = tool.find(L';');
            if (semi != std::wstring::npos) tool.resize(semi);
            UpdateToolStart(state, tool);
        } else if (const auto pos = line.find(L"tool end: "); pos != std::wstring::npos) {
            auto rest = Trim(line.substr(pos + 10));
            const auto semi = rest.find(L';');
            const auto tool = semi == std::wstring::npos ? rest : rest.substr(0, semi);
            UpdateToolEnd(state, tool, rest.find(L"error=true") != std::wstring::npos);
        } else if (line.find(L"auto-retry started") != std::wstring::npos) {
            state.activity.visible = true;
            state.activity.title = L"正在重试";
            state.activity.detail = L"服务暂时没有响应，图灵正在自动恢复";
            InvalidateConversation(state, true);
        }
    }
}

void FinishTurn(ConversationState& state, const std::wstring& rawDone, bool classifyFailure) {
    const bool offerRetry = classifyFailure && ShouldOfferRetry(rawDone);
    const auto doneText = classifyFailure ? ClassifyTransportFailure(rawDone) : rawDone;
    if (!doneText.empty()) {
        if (state.streaming.empty()) state.streaming = doneText;
        else state.streaming += L"\r\n" + doneText;
        if (offerRetry && !state.lastPrompt.empty() && doneText.find(L"/retry") == std::wstring::npos)
            state.streaming += L"\r\n可以输入 /retry 重试上一请求。";
    }
    if (state.streaming.empty()) state.streaming = L"已完成。";
    if (state.assistantIndex < state.entries.size()) state.entries[state.assistantIndex].text = state.streaming;
    state.streaming.clear();
    state.activePrompt.clear();
    state.assistantIndex = kNoIndex;
    ClearActivityCard(state);
    SetBusyVisual(state, false);
    InvalidateConversation(state, true);
    if (IsWindowVisible(state.window)) SetFocus(state.input);
}

void StartDirectFallback(ConversationState& state, const std::wstring& piError) {
    const auto generation = state.generation;
    const HWND hwnd = state.window;
    const std::wstring prompt = state.activePrompt;
    state.activeRuntime = ActiveRuntime::DirectModel;
    state.streaming.clear();
    state.activity.visible = true;
    state.activity.title = L"Agent 暂不可用";
    state.activity.detail = L"已切换到无工具备用通道，本轮无法执行桌面/文件操作";
    InvalidateConversation(state, true);

    const std::wstring reason = piError.empty() ? L"Pi Agent 未返回有效结果" : piError;
    AppendRouteLog(L"fallback: direct api start; reason=" + reason + L"; provider=" + state.agent->Config().providerId +
                   L"; model=" + state.agent->Config().model + L"; endpoint=" + SafeEndpoint(state.agent->CurrentApiUrl()));

    auto onDelta = [hwnd, generation](std::wstring delta) { PostUi(hwnd, kDeltaMessage, generation, std::move(delta)); };
    auto onDone = [hwnd, generation](std::wstring done) { PostUi(hwnd, kDirectDoneMessage, generation, std::move(done)); };
    state.agent->AskAsync(prompt, std::move(onDelta), std::move(onDone));
}

void StopTurn(ConversationState& state) {
    if (!state.busy) return;
    gCliGeneration.fetch_add(1, std::memory_order_relaxed);
    if (state.activeRuntime == ActiveRuntime::Pi) state.pi->Stop();
    else state.agent->Stop();
    AppendRouteLog(L"route: request cancelled; runtime=" + RuntimeName(state.activeRuntime));
    if (state.streaming.empty()) state.streaming = L"已停止。";
    else state.streaming += L"\r\n已停止。";
    if (state.assistantIndex < state.entries.size()) state.entries[state.assistantIndex].text = state.streaming;
    state.streaming.clear();
    state.activePrompt.clear();
    state.assistantIndex = kNoIndex;
    ClearActivityCard(state);
    SetBusyVisual(state, false);
}

void StartModelTurn(ConversationState& state, const std::wstring& typedPrompt,
                    const std::wstring& actualPrompt, bool retry, bool appendUser,
                    std::vector<std::wstring> attachments = {}) {
    if (appendUser) {
        std::wstring userText = typedPrompt + (retry ? L"  · 重试上一请求" : L"");
        ChatEntry userEntry{EntryKind::User, std::move(userText), CurrentTimeText()};
        userEntry.attachments = std::move(attachments);
        state.entries.push_back(std::move(userEntry));
    }
    state.entries.push_back({EntryKind::Assistant, L"", CurrentTimeText()});
    state.assistantIndex = state.entries.size() - 1;
    state.lastPrompt = actualPrompt;
    state.activePrompt = actualPrompt;
    state.activeRuntime = ActiveRuntime::Pi;
    state.streaming.clear();
    state.generation = gCliGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    ResetActivity(state);
    SetBusyVisual(state, true);

    AppendRouteLog(L"route: primary pi start; provider=" + state.agent->Config().providerId +
                   L"; model=" + state.agent->Config().model + L"; endpoint=" + SafeEndpoint(state.agent->CurrentApiUrl()));

    const auto generation = state.generation;
    const HWND hwnd = state.window;
    auto onDelta = [hwnd, generation](std::wstring delta) { PostUi(hwnd, kDeltaMessage, generation, std::move(delta)); };
    auto onDone = [hwnd, generation](std::wstring done) { PostUi(hwnd, kPiDoneMessage, generation, std::move(done)); };
    state.pi->AskAsync(*state.agent, actualPrompt, std::move(onDelta), std::move(onDone));
}

void ClearConversation(ConversationState& state) {
    state.lastPrompt.clear();
    state.streaming.clear();
    state.entries.clear();
    state.activity = {};
    state.pendingAttachments.clear();
    state.artifactHits.clear();
    state.pendingConfirmation = false;
    state.pi->ResetSession();
    if (!state.agent->HasApiKey() && demo::IsStoreDemoScopeEnabled()) {
        AddEntry(state, EntryKind::Assistant,
                 L"你好，我是妙喵。当前是演示模式：可以说「一键体验」「给我一个动态壁纸」或「右上角加个待办小组件」。"
                 L"配置 API Key 后解锁完整 Agent。");
    } else {
        AddEntry(state, EntryKind::Assistant, L"你好，我在这里。直接告诉我你想做什么。" );
    }
    AppendRouteLog(L"route: Pi session reset by user");
}

void SendPrompt(ConversationState& state) {
    if (state.busy || state.pendingConfirmation) return;
    std::wstring typedPrompt = Trim(ReadText(state.input));
    auto attachments = std::move(state.pendingAttachments);
    state.pendingAttachments.clear();
    SetWindowTextW(state.input, L"");
    if (typedPrompt.empty() && attachments.empty()) return;

    const auto lower = Lower(typedPrompt);
    if (lower == L"/runtime") { AppendCompleted(state, typedPrompt, RuntimeStatusText(state)); return; }

    std::wstring actualPrompt = typedPrompt;
    if (!attachments.empty()) {
        actualPrompt += L"\n\n[用户附件]\n";
        for (const auto& path : attachments) actualPrompt += L"- " + path + L"\n";
    }
    bool retry = false;
    if (lower == L"/retry") {
        if (state.lastPrompt.empty()) { AppendCompleted(state, typedPrompt, L"没有可重试的模型请求。"); return; }
        actualPrompt = state.lastPrompt;
        retry = true;
    }

    if (!retry) {
        std::wstring localReply;
        bool consumedSecret = false;
        if (state.agent->TryHandleLocal(actualPrompt, localReply, consumedSecret)) {
            if (lower == L"/new" || lower == L"/new-chat" || lower == L"新对话") {
                ClearConversation(state);
                return;
            }
            AppendCompleted(state, typedPrompt, UserFacingLocalReply(lower, state, localReply));
            return;
        }
    }

    if (!retry && widget_intent::LooksLikeOneSentenceWidgetRequest(actualPrompt)) {
        const auto preview = preview::ShowWidgetPreviewForPrompt(state.window, actualPrompt);
        ChatEntry userEntry{EntryKind::User, typedPrompt, CurrentTimeText()};
        userEntry.attachments = std::move(attachments);
        state.entries.push_back(std::move(userEntry));
        ChatEntry reply{EntryKind::Assistant, preview.message.empty() ? L"已完成。" : preview.message, CurrentTimeText()};
        if (!preview.previewId.empty()) {
            ChatArtifact artifact;
            artifact.kind = ChatArtifactKind::OpenPreview;
            artifact.path = preview.previewId;
            artifact.title = L"小组件预览";
            artifact.subtitle = L"点击打开";
            reply.artifacts.push_back(std::move(artifact));
        }
        state.entries.push_back(std::move(reply));
        InvalidateConversation(state, true);
        return;
    }

    if (!state.agent->HasApiKey()) {
        std::wstring demoReply;
        if (demo::TryHandleDemoPrompt(actualPrompt, &demoReply)) {
            AppendCompleted(state, typedPrompt, demoReply);
            return;
        }
        AppendCompleted(state, typedPrompt,
                        L"妙喵 AI 尚未配置。演示模式可说「一键体验」「换壁纸」「右上角加个待办小组件」；"
                        L"完整能力请从系统托盘进入设置 → 妙喵 AI。");
        return;
    }

    if (LooksDestructive(actualPrompt)) {
        AddEntry(state, EntryKind::User, typedPrompt);
        state.pendingTypedPrompt = typedPrompt;
        state.pendingActualPrompt = actualPrompt;
        state.pendingRetry = retry;
        state.pendingConfirmation = true;
        state.activity = {};
        InvalidateConversation(state, true);
        return;
    }

    StartModelTurn(state, typedPrompt, actualPrompt, retry, true, std::move(attachments));
}

void ConfirmPending(ConversationState& state) {
    if (!state.pendingConfirmation) return;
    const auto typed = state.pendingTypedPrompt;
    const auto actual = state.pendingActualPrompt;
    const bool retry = state.pendingRetry;
    state.pendingConfirmation = false;
    state.pendingTypedPrompt.clear();
    state.pendingActualPrompt.clear();
    StartModelTurn(state, typed, actual, retry, false);
}

void CancelPending(ConversationState& state) {
    if (!state.pendingConfirmation) return;
    state.pendingConfirmation = false;
    state.pendingTypedPrompt.clear();
    state.pendingActualPrompt.clear();
    AddEntry(state, EntryKind::System, L"已取消这项可能影响文件或系统状态的操作。" );
}

LRESULT CALLBACK InputProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);
    if (message == WM_KEYDOWN) {
        if (wParam == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
            SendPrompt(*state);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (state->busy) StopTurn(*state);
            else ShowWindow(state->window, SW_HIDE);
            return 0;
        }
    }
    return CallWindowProcW(state->oldInputProc, hwnd, message, wParam, lParam);
}

int EntryHeight(HDC dc, ConversationState& state, const ChatEntry& entry, int viewportWidth) {
    if (entry.kind == EntryKind::System) return Px(state, 52);
    const int avatar = Px(state, 34);
    const int maxBubble = std::max(Px(state, 180), viewportWidth * 72 / 100);
    const int textWidth = maxBubble - Px(state, 32);
    const int textHeight = MeasureTextHeight(dc, state.bodyFont, entry.text.empty() ? L"正在思考…" : entry.text, textWidth);
    int height = std::max(avatar + Px(state, 10), textHeight + Px(state, 38));
    height += ArtifactStackHeight(state, entry);
    height += AttachmentStackHeight(state, entry);
    return height;
}

int ActivityHeight(const ConversationState& state) {
    if (!state.activity.visible) return 0;
    return Px(state, 74 + static_cast<int>(state.activity.steps.size()) * 25);
}

int ConfirmationHeight(const ConversationState& state) {
    return state.pendingConfirmation ? Px(state, 126) : 0;
}

int ComputeContentHeight(HDC dc, ConversationState& state, int viewportWidth) {
    int total = Px(state, 8);
    for (const auto& entry : state.entries) total += EntryHeight(dc, state, entry, viewportWidth) + Px(state, 10);
    total += ActivityHeight(state);
    if (state.activity.visible) total += Px(state, 12);
    total += ConfirmationHeight(state);
    if (state.pendingConfirmation) total += Px(state, 12);
    return total;
}

void DrawHeaderIcon(HDC dc, ConversationState& state, RECT rect, const wchar_t* glyph, bool active = false) {
    if (active) FillRounded(dc, rect, Px(state, 12), RGB(225, 235, 252));
    HGDIOBJ old = SelectObject(dc, state.symbolFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(72, 82, 101));
    DrawTextW(dc, glyph, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

void DrawActivityCard(HDC dc, ConversationState& state, int left, int top, int width) {
    const int height = ActivityHeight(state);
    if (height <= 0) return;
    RECT shadow{left + Px(state, 2), top + Px(state, 3), left + width + Px(state, 2), top + height + Px(state, 3)};
    FillRounded(dc, shadow, Px(state, 16), RGB(216, 224, 236));
    RECT card{left, top, left + width, top + height};
    FillRounded(dc, card, Px(state, 16), state.activity.failed ? RGB(255, 246, 244) : RGB(251, 253, 255));
    StrokeRounded(dc, card, Px(state, 16), state.activity.failed ? RGB(245, 204, 198) : RGB(222, 229, 240));

    RECT title{left + Px(state, 18), top + Px(state, 14), left + width - Px(state, 18), top + Px(state, 38)};
    DrawWrapped(dc, state.bodyBoldFont, RGB(43, 50, 63), state.activity.title, title, DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT detail{left + Px(state, 18), top + Px(state, 38), left + width - Px(state, 18), top + Px(state, 60)};
    DrawWrapped(dc, state.smallFont, RGB(111, 121, 139), state.activity.detail, detail, DT_SINGLELINE | DT_END_ELLIPSIS);

    int y = top + Px(state, 62);
    for (const auto& step : state.activity.steps) {
        const COLORREF color = step.status == StepStatus::Failed ? RGB(219, 77, 65) :
                               step.status == StepStatus::Success ? RGB(86, 184, 109) : RGB(91, 139, 244);
        HBRUSH dot = CreateSolidBrush(color);
        HGDIOBJ oldBrush = SelectObject(dc, dot);
        HPEN noPen = CreatePen(PS_NULL, 0, color);
        HGDIOBJ oldPen = SelectObject(dc, noPen);
        const int r = Px(state, 4);
        Ellipse(dc, left + Px(state, 19) - r, y + Px(state, 7) - r, left + Px(state, 19) + r, y + Px(state, 7) + r);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(noPen);
        DeleteObject(dot);
        RECT line{left + Px(state, 32), y - Px(state, 3), left + width - Px(state, 18), y + Px(state, 20)};
        const std::wstring prefix = step.status == StepStatus::Success ? L"已完成 · " : step.status == StepStatus::Failed ? L"失败 · " : L"进行中 · ";
        DrawWrapped(dc, state.smallFont, RGB(77, 87, 104), prefix + step.label, line, DT_SINGLELINE | DT_END_ELLIPSIS);
        y += Px(state, 25);
    }
}

void DrawConfirmationCard(HDC dc, ConversationState& state, int left, int top, int width) {
    if (!state.pendingConfirmation) return;
    const int h = ConfirmationHeight(state);
    RECT card{left, top, left + width, top + h};
    FillRounded(dc, card, Px(state, 16), RGB(255, 250, 244));
    StrokeRounded(dc, card, Px(state, 16), RGB(244, 211, 169));
    RECT title{left + Px(state, 18), top + Px(state, 14), left + width - Px(state, 18), top + Px(state, 38)};
    DrawWrapped(dc, state.bodyBoldFont, RGB(74, 57, 44), L"需要你确认", title, DT_SINGLELINE);
    RECT detail{left + Px(state, 18), top + Px(state, 42), left + width - Px(state, 18), top + Px(state, 72)};
    DrawWrapped(dc, state.smallFont, RGB(116, 92, 72), L"这项操作可能删除、覆盖或改变本机内容。确认后图灵才会继续。", detail);

    const int buttonW = Px(state, 92);
    const int buttonH = Px(state, 34);
    state.confirmRect = {left + width - buttonW - Px(state, 16), top + h - buttonH - Px(state, 14), left + width - Px(state, 16), top + h - Px(state, 14)};
    state.cancelRect = {state.confirmRect.left - buttonW - Px(state, 10), state.confirmRect.top, state.confirmRect.left - Px(state, 10), state.confirmRect.bottom};
    FillRounded(dc, state.cancelRect, Px(state, 10), RGB(239, 241, 245));
    FillRounded(dc, state.confirmRect, Px(state, 10), RGB(237, 104, 78));
    RECT cancelText = state.cancelRect;
    RECT confirmText = state.confirmRect;
    DrawWrapped(dc, state.smallFont, RGB(67, 72, 82), L"取消", cancelText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawWrapped(dc, state.smallFont, RGB(255, 255, 255), L"确认执行", confirmText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawTimeline(HDC dc, ConversationState& state, const RECT& viewport) {
    const int viewportWidth = viewport.right - viewport.left;
    state.contentHeight = ComputeContentHeight(dc, state, viewportWidth);
    const int maxScroll = std::max(0, state.contentHeight - (viewport.bottom - viewport.top));
    if (state.scrollToBottom) {
        state.scrollOffset = maxScroll;
        state.scrollToBottom = false;
    }
    state.scrollOffset = std::clamp(state.scrollOffset, 0, maxScroll);

    HRGN clip = CreateRectRgn(viewport.left, viewport.top, viewport.right, viewport.bottom);
    SelectClipRgn(dc, clip);
    int y = viewport.top + Px(state, 8) - state.scrollOffset;
    const int avatar = Px(state, 30);
    const int gap = Px(state, 10);
    const int maxBubble = std::max(Px(state, 180), viewportWidth * 72 / 100);

    for (const auto& entry : state.entries) {
        const int rowH = EntryHeight(dc, state, entry, viewportWidth);
        if (entry.kind == EntryKind::System) {
            RECT note{viewport.left + Px(state, 28), y + Px(state, 4), viewport.right - Px(state, 28), y + rowH - Px(state, 4)};
            FillRounded(dc, note, Px(state, 13), RGB(232, 244, 235));
            RECT text = note;
            InflateRect(&text, -Px(state, 14), -Px(state, 9));
            DrawWrapped(dc, state.smallFont, RGB(76, 104, 84), entry.text, text, DT_VCENTER);
            y += rowH + gap;
            continue;
        }

        const bool user = entry.kind == EntryKind::User;
        const std::wstring display = entry.text.empty() ? L"正在思考…" : entry.text;
        const int textW = maxBubble - Px(state, 32);
        const int textH = MeasureTextHeight(dc, state.bodyFont, display, textW);
        const int bubbleH = textH + Px(state, 34);
        const int bubbleW = std::min(maxBubble, std::max(Px(state, 150), textW));
        const int avatarX = user ? viewport.right - avatar / 2 : viewport.left + avatar / 2;
        const int bubbleLeft = user ? viewport.right - avatar - Px(state, 10) - bubbleW : viewport.left + avatar + Px(state, 10);
        RECT shadow{bubbleLeft + Px(state, 2), y + Px(state, 3), bubbleLeft + bubbleW + Px(state, 2), y + bubbleH + Px(state, 3)};
        FillRounded(dc, shadow, Px(state, 15), RGB(218, 225, 236));
        RECT bubble{bubbleLeft, y, bubbleLeft + bubbleW, y + bubbleH};
        FillRounded(dc, bubble, Px(state, 15), user ? RGB(218, 233, 255) : RGB(255, 255, 255));
        StrokeRounded(dc, bubble, Px(state, 15), user ? RGB(196, 219, 252) : RGB(228, 233, 241));
        DrawAvatar(dc, avatarX, y + avatar / 2 + Px(state, 2), avatar / 2, user);

        RECT textRect{bubble.left + Px(state, 16), bubble.top + Px(state, 12), bubble.right - Px(state, 16), bubble.bottom - Px(state, 18)};
        DrawWrapped(dc, state.bodyFont, RGB(42, 48, 60), display, textRect);
        RECT timeRect{bubble.left + Px(state, 16), bubble.bottom - Px(state, 18), bubble.right - Px(state, 12), bubble.bottom - Px(state, 4)};
        HGDIOBJ old = SelectObject(dc, state.smallFont);
        SetTextColor(dc, RGB(130, 140, 157));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, entry.time.c_str(), -1, &timeRect, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        y += rowH + gap;
    }

    if (state.activity.visible) {
        const int cardW = viewportWidth * 76 / 100;
        DrawActivityCard(dc, state, viewport.left + avatar + Px(state, 10), y, cardW);
        y += ActivityHeight(state) + Px(state, 12);
    }
    if (state.pendingConfirmation) {
        const int cardW = viewportWidth * 78 / 100;
        DrawConfirmationCard(dc, state, viewport.left + avatar + Px(state, 10), y, cardW);
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);

    if (maxScroll > 0) {
        const int trackTop = viewport.top + Px(state, 8);
        const int trackBottom = viewport.bottom - Px(state, 8);
        const int trackH = trackBottom - trackTop;
        const int thumbH = std::max(Px(state, 32), trackH * (viewport.bottom - viewport.top) / std::max(1, state.contentHeight));
        const int travel = std::max(1, trackH - thumbH);
        const int thumbTop = trackTop + travel * state.scrollOffset / maxScroll;
        RECT thumb{viewport.right - Px(state, 4), thumbTop, viewport.right - Px(state, 1), thumbTop + thumbH};
        FillRounded(dc, thumb, Px(state, 3), RGB(174, 184, 201));
    }
}

void PaintConversation(ConversationState& state, HDC target) {
    RECT client{};
    GetClientRect(state.window, &client);
    const int width = client.right;
    const int height = client.bottom;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1, width), std::max(1, height));
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    HBRUSH background = CreateSolidBrush(RGB(239, 244, 251));
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT header{0, 0, width, Px(state, 70)};
    HBRUSH headerBrush = CreateSolidBrush(RGB(245, 248, 253));
    FillRect(dc, &header, headerBrush);
    DeleteObject(headerBrush);
    HPEN divider = CreatePen(PS_SOLID, 1, RGB(216, 224, 236));
    HGDIOBJ oldPen = SelectObject(dc, divider);
    MoveToEx(dc, 0, header.bottom - 1, nullptr);
    LineTo(dc, width, header.bottom - 1);
    SelectObject(dc, oldPen);
    DeleteObject(divider);

    const int avatarR = Px(state, 17);
    DrawAvatar(dc, Px(state, 34), Px(state, 34), avatarR, false);
    RECT title{Px(state, 60), Px(state, 15), Px(state, 220), Px(state, 42)};
    DrawWrapped(dc, state.titleFont, RGB(36, 43, 55), L"妙喵 AI", title, DT_SINGLELINE);
    HBRUSH online = CreateSolidBrush(RGB(83, 190, 104));
    HGDIOBJ oldBrush = SelectObject(dc, online);
    HPEN noPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
    oldPen = SelectObject(dc, noPen);
    const int dotR = Px(state, 4);
    Ellipse(dc, Px(state, 62) - dotR, Px(state, 49) - dotR, Px(state, 62) + dotR, Px(state, 49) + dotR);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(noPen);
    DeleteObject(online);
    RECT onlineText{Px(state, 72), Px(state, 39), Px(state, 180), Px(state, 60)};
    DrawWrapped(dc, state.smallFont, RGB(99, 112, 130), state.busy ? L"正在工作" : L"在线", onlineText, DT_SINGLELINE);

    const int iconW = Px(state, 34);
    const int top = Px(state, 17);
    state.closeRect = {width - Px(state, 48), top, width - Px(state, 14), top + iconW};
    state.moreRect = {state.closeRect.left - Px(state, 42), top, state.closeRect.left - Px(state, 8), top + iconW};
    state.historyRect = {state.moreRect.left - Px(state, 42), top, state.moreRect.left - Px(state, 8), top + iconW};
    state.pinRect = {state.historyRect.left - Px(state, 42), top, state.historyRect.left - Px(state, 8), top + iconW};
    DrawHeaderIcon(dc, state, state.pinRect, L"⌖", state.pinned);
    DrawHeaderIcon(dc, state, state.historyRect, L"↶");
    DrawHeaderIcon(dc, state, state.moreRect, L"•••");
    DrawHeaderIcon(dc, state, state.closeRect, L"×");

    const int inputContainerH = Px(state, 64);
    const int inputTop = height - inputContainerH - Px(state, 16);
    state.viewport = {Px(state, 22), header.bottom + Px(state, 6), width - Px(state, 22), inputTop - Px(state, 10)};
    DrawTimeline(dc, state, state.viewport);

    RECT inputContainer{Px(state, 22), inputTop, width - Px(state, 22), inputTop + inputContainerH};
    FillRounded(dc, inputContainer, Px(state, 20), RGB(255, 255, 255));
    StrokeRounded(dc, inputContainer, Px(state, 20), RGB(216, 224, 236));
    const int sendSize = Px(state, 42);
    state.sendRect = {inputContainer.right - sendSize - Px(state, 10), inputContainer.top + Px(state, 11), inputContainer.right - Px(state, 10), inputContainer.top + Px(state, 11) + sendSize};
    FillRounded(dc, state.sendRect, sendSize, state.busy ? RGB(199, 210, 230) : RGB(91, 139, 244));
    RECT arrow = state.sendRect;
    DrawWrapped(dc, state.symbolFont, RGB(255, 255, 255), state.busy ? L"■" : L"↑", arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT footer{Px(state, 22), height - Px(state, 13), width - Px(state, 22), height};
    DrawWrapped(dc, state.smallFont, RGB(121, 132, 150), L"Enter 发送 · Shift + Enter 换行 · Esc 停止 / 收起", footer, DT_CENTER | DT_SINGLELINE);

    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void PositionInput(ConversationState& state) {
    RECT client{};
    GetClientRect(state.window, &client);
    const int inputTop = client.bottom - Px(state, 64) - Px(state, 16);
    const int sendArea = Px(state, 62);
    MoveWindow(state.input, Px(state, 38), inputTop + Px(state, 13),
               std::max(Px(state, 120), client.right - Px(state, 76) - sendArea), Px(state, 38), TRUE);
}

void ApplyWindowGlass(ConversationState& state) {
    const int cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(state.window, static_cast<DWMWINDOWATTRIBUTE>(33), &cornerPreference, sizeof(cornerPreference));
    const int backdrop = 3; // DWMSBT_TRANSIENTWINDOW
    DwmSetWindowAttribute(state.window, static_cast<DWMWINDOWATTRIBUTE>(38), &backdrop, sizeof(backdrop));
    SetLayeredWindowAttributes(state.window, 0, 250, LWA_ALPHA);
}

void PlacePanel(ConversationState& state) {
    RECT ownerRect{};
    GetWindowRect(state.owner, &ownerRect);
    HMONITOR monitor = MonitorFromWindow(state.owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    const int panelW = Px(state, 560);
    const int desiredH = Px(state, 620);
    const int available = info.rcWork.bottom - info.rcWork.top - Px(state, 24);
    const int panelH = std::min(desiredH, available);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - panelW) / 2;
    x = std::clamp(x, info.rcWork.left + Px(state, 8), info.rcWork.right - panelW - Px(state, 8));
    int y = ownerRect.bottom + Px(state, 10);
    if (y + panelH > info.rcWork.bottom - Px(state, 8)) y = ownerRect.top - panelH - Px(state, 10);
    if (y < info.rcWork.top + Px(state, 8)) y = info.rcWork.top + Px(state, 8);
    SetWindowPos(state.window, state.pinned ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, panelW, panelH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    HRGN region = CreateRoundRectRgn(0, 0, panelW + 1, panelH + 1, Px(state, 24), Px(state, 24));
    SetWindowRgn(state.window, region, TRUE);
    PositionInput(state);
}

void ShowMoreMenu(ConversationState& state) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"新对话");
    AppendMenuW(menu, MF_STRING, 2, L"清空当前对话");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"运行状态");
    POINT point{state.moreRect.left, state.moreRect.bottom};
    ClientToScreen(state.window, &point);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, state.window, nullptr);
    DestroyMenu(menu);
    if (command == 1 || command == 2) ClearConversation(state);
    else if (command == 3) AddEntry(state, EntryKind::Assistant, RuntimeStatusText(state));
}

LRESULT CALLBACK ConversationProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ConversationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<ConversationState*>(create->lpCreateParams);
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_TIMER:
        if (wParam == kActivityTimer) { PollPiActivity(*state); return 0; }
        break;

    case kDeltaMessage: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(lParam));
        if (!payload || payload->generation != state->generation || payload->generation != gCliGeneration.load(std::memory_order_relaxed)) return 0;
        state->streaming += payload->text;
        if (state->assistantIndex < state->entries.size()) state->entries[state->assistantIndex].text = state->streaming;
        if (state->activity.visible && state->activity.steps.empty()) {
            state->activity.title = L"正在回复";
            state->activity.detail = L"图灵正在组织回答";
        }
        InvalidateConversation(*state, true);
        return 0;
    }

    case kPiDoneMessage: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(lParam));
        if (!payload || payload->generation != state->generation || payload->generation != gCliGeneration.load(std::memory_order_relaxed)) return 0;
        PollPiActivity(*state);
        if (payload->text.empty()) {
            AppendRouteLog(L"route: primary pi success");
            FinishTurn(*state, L"", false);
        } else {
            AppendRouteLog(L"route: primary pi failed; error=" + payload->text);
            StartDirectFallback(*state, payload->text);
        }
        return 0;
    }

    case kDirectDoneMessage: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(lParam));
        if (!payload || payload->generation != state->generation || payload->generation != gCliGeneration.load(std::memory_order_relaxed)) return 0;
        if (payload->text.empty()) AppendRouteLog(L"fallback: direct api success");
        else AppendRouteLog(L"fallback: direct api failed; error=" + payload->text);
        FinishTurn(*state, payload->text, true);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        state->scrollToBottom = false;
        state->scrollOffset = std::max(0, state->scrollOffset - MulDiv(delta, Px(*state, 54), WHEEL_DELTA));
        InvalidateConversation(*state);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointIn(state->sendRect, point)) {
            if (state->busy) StopTurn(*state); else SendPrompt(*state);
            return 0;
        }
        if (PointIn(state->closeRect, point)) { ShowWindow(hwnd, SW_HIDE); return 0; }
        if (PointIn(state->pinRect, point)) {
            state->pinned = !state->pinned;
            SetWindowPos(hwnd, state->pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            InvalidateConversation(*state);
            return 0;
        }
        if (PointIn(state->historyRect, point)) {
            state->scrollToBottom = false;
            state->scrollOffset = 0;
            InvalidateConversation(*state);
            return 0;
        }
        if (PointIn(state->moreRect, point)) { ShowMoreMenu(*state); return 0; }
        if (state->pendingConfirmation && PointIn(state->confirmRect, point)) { ConfirmPending(*state); return 0; }
        if (state->pendingConfirmation && PointIn(state->cancelRect, point)) { CancelPending(*state); return 0; }
        break;
    }

    case WM_SETCURSOR: {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        if (PointIn(state->sendRect, point) || PointIn(state->closeRect, point) || PointIn(state->pinRect, point) ||
            PointIn(state->historyRect, point) || PointIn(state->moreRect, point) ||
            (state->pendingConfirmation && (PointIn(state->confirmRect, point) || PointIn(state->cancelRect, point)))) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_SIZE:
        PositionInput(*state);
        InvalidateConversation(*state, true);
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(48, 55, 68));
        SetBkColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(state->inputBrush);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        PaintConversation(*state, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        // Closing the panel only hides the surface. Pi/session state keeps running and can be resumed.
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kActivityTimer);
        gCliGeneration.fetch_add(1, std::memory_order_relaxed);
        state->agent->Stop();
        state->pi->Stop();
        return 0;

    case WM_NCDESTROY:
        if (state->inputBrush) DeleteObject(state->inputBrush);
        if (state->titleFont) DeleteObject(state->titleFont);
        if (state->bodyFont) DeleteObject(state->bodyFont);
        if (state->bodyBoldFont) DeleteObject(state->bodyBoldFont);
        if (state->smallFont) DeleteObject(state->smallFont);
        if (state->symbolFont) DeleteObject(state->symbolFont);
        if (gConversationState == state) gConversationState = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void CreateFonts(ConversationState& state) {
    state.titleFont = CreateFontW(-Px(state, 20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    state.bodyFont = CreateFontW(-Px(state, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    state.bodyBoldFont = CreateFontW(-Px(state, 15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    state.smallFont = CreateFontW(-Px(state, 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    state.symbolFont = CreateFontW(-Px(state, 17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
}

} // namespace

bool ShowL3CliWindow(HINSTANCE instance, HWND owner, L3Agent& agent, const std::wstring& initialPrompt) {
    if (gConversationState && IsWindow(gConversationState->window)) {
        gConversationState->owner = owner;
        gConversationState->agent = &agent;
        PlacePanel(*gConversationState);
        ShowWindow(gConversationState->window, SW_SHOWNORMAL);
        SetForegroundWindow(gConversationState->window);
        if (!Trim(initialPrompt).empty() && !gConversationState->busy && !gConversationState->pendingConfirmation) {
            SetWindowTextW(gConversationState->input, initialPrompt.c_str());
            SendPrompt(*gConversationState);
        }
        SetFocus(gConversationState->input);
        return true;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &ConversationProc;
    wc.lpszClassName = kConversationClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    auto* state = new ConversationState{};
    state->instance = instance;
    state->owner = owner;
    state->agent = &agent;
    state->pi = &gPiRuntime;
    state->dpi = std::max(96u, GetDpiForWindow(owner));
    state->inputBrush = CreateSolidBrush(RGB(255, 255, 255));
    CreateFonts(*state);

    HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED, kConversationClass, L"妙喵 AI",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, Px(*state, 560), Px(*state, 620), owner, nullptr, instance, state);
    if (!window) {
        if (state->inputBrush) DeleteObject(state->inputBrush);
        if (state->titleFont) DeleteObject(state->titleFont);
        if (state->bodyFont) DeleteObject(state->bodyFont);
        if (state->bodyBoldFont) DeleteObject(state->bodyBoldFont);
        if (state->smallFont) DeleteObject(state->smallFont);
        if (state->symbolFont) DeleteObject(state->symbolFont);
        delete state;
        return false;
    }

    state->input = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL,
        Px(*state, 38), Px(*state, 530), Px(*state, 420), Px(*state, 38), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInputId)), instance, nullptr);
    if (!state->input) {
        DestroyWindow(window);
        return false;
    }
    SetWindowLongPtrW(state->input, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    state->oldInputProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(state->input, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InputProc)));
    SendMessageW(state->input, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
    SendMessageW(state->input, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Px(*state, 2), Px(*state, 2)));
    SendMessageW(state->input, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"输入消息…"));

    gConversationState = state;
    if (!state->agent->HasApiKey() && demo::IsStoreDemoScopeEnabled()) {
        AddEntry(*state, EntryKind::Assistant,
                 L"你好，我是妙喵。当前是演示模式：可以说「一键体验」「给我一个动态壁纸」或「右上角加个待办小组件」。"
                 L"配置 API Key 后解锁完整 Agent。");
    } else {
        AddEntry(*state, EntryKind::Assistant, L"你好，我在这里。直接告诉我你想做什么。" );
    }
    ApplyWindowGlass(*state);
    PlacePanel(*state);
    SetTimer(window, kActivityTimer, 180, nullptr);
    AppendRouteLog(L"window: conversation panel opened; primary=Pi Agent -> Provider API; fallback=Direct API");
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    SetFocus(state->input);

    if (!Trim(initialPrompt).empty()) {
        SetWindowTextW(state->input, initialPrompt.c_str());
        SendPrompt(*state);
    }
    return true;
}

} // namespace turingdesk
