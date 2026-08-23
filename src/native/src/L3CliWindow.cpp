#include "turingdesk/L3CliWindow.h"
#include "turingdesk/CodexRuntime.h"
#include "turingdesk/ModelSettingsWindow.h"
#include <CommCtrl.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr wchar_t kCliClass[] = L"TuringDesk.Native.L3CliWindow";
constexpr int kTranscriptId = 3101;
constexpr int kInputId = 3102;
constexpr int kSettingsId = 3103;
constexpr int kCloseId = 3104;
constexpr UINT kDeltaMessage = WM_APP + 31;
constexpr UINT kDoneMessage = WM_APP + 32;
constexpr UINT kCodexDoneMessage = WM_APP + 33;

enum class ActiveRuntime {
    Codex,
    DirectModel,
};

struct UiMessage {
    std::uint64_t generation{};
    std::wstring text;
};

struct CliState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    HWND transcript{};
    HWND input{};
    HWND settings{};
    HWND close{};
    WNDPROC oldInputProc{};
    L3Agent* agent{};
    CodexRuntime* codex{};
    HBRUSH backgroundBrush{};
    HFONT monoFont{};
    HFONT uiFont{};
    std::wstring transcriptPrefix;
    std::wstring streaming;
    std::wstring lastPrompt;
    std::wstring activePrompt;
    std::uint64_t generation{};
    ActiveRuntime activeRuntime{ActiveRuntime::Codex};
    bool busy{};
};

std::atomic_uint64_t gCliGeneration{0};
CodexRuntime gCodexRuntime;

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

std::wstring SafeEndpoint(std::wstring value) {
    const auto query = value.find_first_of(L"?#");
    if (query != std::wstring::npos) value.resize(query);
    return value;
}

fs::path RuntimeLogDirectory() {
    wchar_t localAppData[32768]{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    fs::path root;
    if (count > 0 && count < std::size(localAppData)) root = fs::path(std::wstring(localAppData, count));
    else root = fs::temp_directory_path();
    std::error_code ec;
    const auto logs = root / L"TuringDesk" / L"Logs";
    fs::create_directories(logs, ec);
    return logs;
}

fs::path L3RouteLogPath() {
    return RuntimeLogDirectory() / L"l3-runtime.log";
}

fs::path CodexDetailLogPath() {
    return RuntimeLogDirectory() / L"codex-runtime.log";
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr);
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

bool ContainsHttpStatus(const std::wstring& text, int status) {
    return text.find(L"HTTP " + std::to_wstring(status)) != std::wstring::npos;
}

bool ShouldOfferRetry(const std::wstring& rawText) {
    if (rawText.empty()) return false;
    if (rawText.find(L"已停止") != std::wstring::npos) return false;
    if (ContainsHttpStatus(rawText, 400) || ContainsHttpStatus(rawText, 401) ||
        ContainsHttpStatus(rawText, 403) || ContainsHttpStatus(rawText, 404) ||
        ContainsHttpStatus(rawText, 405) || ContainsHttpStatus(rawText, 413) ||
        ContainsHttpStatus(rawText, 422)) return false;
    if (rawText.find(L"WinHTTP 错误 12175") != std::wstring::npos) return false;
    return true;
}

std::wstring ClassifyTransportFailure(std::wstring text) {
    if (text.empty()) return text;
    if (text.find(L"WinHTTP 错误 12002") != std::wstring::npos || ContainsHttpStatus(text, 408) || ContainsHttpStatus(text, 504)) {
        return L"L3 请求超时。模型服务在限定时间内没有完成响应；可输入 /retry 重试。";
    }
    if (text.find(L"WinHTTP 错误 12007") != std::wstring::npos) {
        return L"L3 无法解析模型服务地址（DNS）。请检查 Base URL 或网络连接，可输入 /retry 重试。";
    }
    if (text.find(L"WinHTTP 错误 12029") != std::wstring::npos ||
        text.find(L"WinHTTP 错误 12030") != std::wstring::npos ||
        text.find(L"WinHTTP 错误 12031") != std::wstring::npos) {
        return L"L3 无法连接模型服务，或连接被服务端中断。请检查网络/服务状态，可输入 /retry 重试。";
    }
    if (text.find(L"WinHTTP 错误 12175") != std::wstring::npos) {
        return L"L3 HTTPS/TLS 握手失败。请检查证书、系统时间或代理设置。";
    }
    if (ContainsHttpStatus(text, 401) || ContainsHttpStatus(text, 403)) {
        return L"L3 模型鉴权失败（HTTP 401/403）。请在 AI 设置中检查 API Key 和权限。";
    }
    if (ContainsHttpStatus(text, 429)) {
        return L"L3 模型服务限流（HTTP 429）。稍后可输入 /retry 重试。";
    }
    if (ContainsHttpStatus(text, 500) || ContainsHttpStatus(text, 502) || ContainsHttpStatus(text, 503)) {
        return L"L3 模型服务暂时不可用（HTTP 5xx）。稍后可输入 /retry 重试。";
    }
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

void RenderTranscript(CliState& state, const std::wstring& tail = {}) {
    const std::wstring text = state.transcriptPrefix + tail;
    SetWindowTextW(state.transcript, text.c_str());
    SendMessageW(state.transcript, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(state.transcript, EM_SCROLLCARET, 0, 0);
}

void AppendCompleted(CliState& state, const std::wstring& user, const std::wstring& assistant) {
    state.transcriptPrefix += L"> " + user + L"\r\n";
    state.transcriptPrefix += L"AI  " + (assistant.empty() ? L"[完成]" : assistant) + L"\r\n\r\n";
    RenderTranscript(state);
}

std::wstring RuntimeName(ActiveRuntime runtime) {
    switch (runtime) {
    case ActiveRuntime::Codex: return L"Codex CLI";
    case ActiveRuntime::DirectModel: return L"Direct Model Runtime";
    }
    return L"Unknown Runtime";
}

std::wstring RuntimeExecutionLabel(ActiveRuntime runtime) {
    switch (runtime) {
    case ActiveRuntime::Codex: return L"主路由 · Relay/API";
    case ActiveRuntime::DirectModel: return L"Fallback · SSE 流式";
    }
    return L"";
}

std::wstring RuntimeStatusText(CliState& state) {
    const auto status = state.codex->Status(*state.agent);
    std::wstring text = L"主路由：Codex CLI → Relay/API";
    text += L"\r\n失败回退：Direct Model Runtime → 当前配置 API";
    text += L"\r\nProvider：" + (state.agent->Config().providerId.empty() ? std::wstring(L"未识别") : state.agent->Config().providerId);
    text += L" · Model：" + (state.agent->Config().model.empty() ? std::wstring(L"未配置") : state.agent->Config().model);
    text += L"\r\nCodex：" + status.message;
    text += L"\r\n路由日志：" + L3RouteLogPath().wstring();
    text += L"\r\nCodex 详情：" + CodexDetailLogPath().wstring();
    return text;
}

void FinishTurn(CliState& state, const std::wstring& rawDone, bool classifyFailure) {
    const bool offerRetry = classifyFailure && ShouldOfferRetry(rawDone);
    const auto doneText = classifyFailure ? ClassifyTransportFailure(rawDone) : rawDone;
    if (!doneText.empty()) {
        if (state.streaming.empty()) state.streaming = doneText;
        else state.streaming += L"\r\n" + doneText;
        if (offerRetry && !state.lastPrompt.empty() && doneText.find(L"/retry") == std::wstring::npos)
            state.streaming += L"\r\n[可输入 /retry 重试上一请求]";
    }
    if (state.streaming.empty()) state.streaming = L"[完成，无可显示内容]";
    state.transcriptPrefix += state.streaming + L"\r\n\r\n";
    state.streaming.clear();
    state.activePrompt.clear();
    state.busy = false;
    EnableWindow(state.input, TRUE);
    RenderTranscript(state);
    SetFocus(state.input);
}

void StartDirectFallback(CliState& state, const std::wstring& codexError) {
    const auto generation = state.generation;
    const HWND hwnd = state.window;
    const std::wstring prompt = state.activePrompt;
    state.activeRuntime = ActiveRuntime::DirectModel;
    state.streaming.clear();

    const std::wstring reason = codexError.empty() ? L"Codex CLI 未返回有效结果" : codexError;
    AppendRouteLog(L"fallback: direct api start; reason=" + reason +
                   L"; provider=" + state.agent->Config().providerId +
                   L"; model=" + state.agent->Config().model +
                   L"; endpoint=" + SafeEndpoint(state.agent->CurrentApiUrl()));
    state.transcriptPrefix += L"[Fallback] Codex CLI 失败，已切换 Direct API；原因已写入日志。\r\nAI  ";
    RenderTranscript(state, L"…");

    auto onDelta = [hwnd, generation](std::wstring delta) {
        PostUi(hwnd, kDeltaMessage, generation, std::move(delta));
    };
    auto onDone = [hwnd, generation](std::wstring done) {
        PostUi(hwnd, kDoneMessage, generation, std::move(done));
    };
    state.agent->AskAsync(prompt, std::move(onDelta), std::move(onDone));
}

void StopTurn(CliState& state) {
    if (!state.busy) return;
    gCliGeneration.fetch_add(1, std::memory_order_relaxed);
    if (state.activeRuntime == ActiveRuntime::Codex) state.codex->Stop();
    else state.agent->Stop();
    AppendRouteLog(L"route: request cancelled; runtime=" + RuntimeName(state.activeRuntime));
    state.busy = false;
    if (state.streaming.empty()) state.streaming = L"[已停止]";
    else state.streaming += L"\r\n[已停止]";
    state.transcriptPrefix += state.streaming + L"\r\n\r\n";
    state.streaming.clear();
    state.activePrompt.clear();
    EnableWindow(state.input, TRUE);
    RenderTranscript(state);
    SetFocus(state.input);
}

void SendPrompt(CliState& state) {
    if (state.busy) return;
    const auto typedPrompt = Trim(ReadText(state.input));
    if (typedPrompt.empty()) return;
    SetWindowTextW(state.input, L"");

    const auto lower = Lower(typedPrompt);
    if (lower == L"/runtime") {
        AppendCompleted(state, typedPrompt, RuntimeStatusText(state));
        return;
    }

    std::wstring actualPrompt = typedPrompt;
    bool retry = false;
    if (lower == L"/retry") {
        if (state.lastPrompt.empty()) {
            AppendCompleted(state, typedPrompt, L"没有可重试的模型请求。");
            return;
        }
        actualPrompt = state.lastPrompt;
        retry = true;
    }

    if (!retry) {
        std::wstring localReply;
        bool consumedSecret = false;
        if (state.agent->TryHandleLocal(actualPrompt, localReply, consumedSecret)) {
            if (lower == L"/new" || lower == L"/new-chat" || lower == L"新对话") {
                state.lastPrompt.clear();
                state.codex->ResetSession();
                AppendRouteLog(L"route: Codex session reset by user");
            }
            AppendCompleted(state, typedPrompt, localReply);
            return;
        }
    }

    if (!state.agent->HasApiKey()) {
        AppendCompleted(state, typedPrompt, L"L3 未配置。点击右上角“AI 设置”填写 API 地址和 Key。");
        return;
    }

    state.lastPrompt = actualPrompt;
    state.activePrompt = actualPrompt;
    state.activeRuntime = ActiveRuntime::Codex;
    state.transcriptPrefix += L"> " + typedPrompt + (retry ? L"  [重试上一请求]" : L"") + L"\r\n";
    state.transcriptPrefix += L"[Runtime] " + RuntimeName(ActiveRuntime::Codex) + L" · " + RuntimeExecutionLabel(ActiveRuntime::Codex) + L"\r\n";
    state.transcriptPrefix += L"AI  ";
    state.streaming.clear();
    state.busy = true;
    state.generation = gCliGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    EnableWindow(state.input, FALSE);
    RenderTranscript(state, L"…");

    AppendRouteLog(L"route: primary codex start; provider=" + state.agent->Config().providerId +
                   L"; model=" + state.agent->Config().model +
                   L"; endpoint=" + SafeEndpoint(state.agent->CurrentApiUrl()));

    const auto generation = state.generation;
    const HWND hwnd = state.window;
    auto onDelta = [hwnd, generation](std::wstring delta) {
        PostUi(hwnd, kDeltaMessage, generation, std::move(delta));
    };
    auto onDone = [hwnd, generation](std::wstring done) {
        PostUi(hwnd, kCodexDoneMessage, generation, std::move(done));
    };
    state.codex->AskAsync(*state.agent, actualPrompt, std::move(onDelta), std::move(onDone));
}

LRESULT CALLBACK InputProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CliState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);
    if (message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            SendPrompt(*state);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (state->busy) StopTurn(*state);
            else DestroyWindow(state->window);
            return 0;
        }
    }
    return CallWindowProcW(state->oldInputProc, hwnd, message, wParam, lParam);
}

LRESULT CALLBACK CliProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CliState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<CliState*>(create->lpCreateParams);
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wParam) == kSettingsId && HIWORD(wParam) == BN_CLICKED) {
            if (state->busy) StopTurn(*state);
            if (ShowModelSettingsWindow(state->instance, hwnd, *state->agent)) {
                state->codex->ResetSession();
                state->lastPrompt.clear();
                AppendRouteLog(L"route: model settings changed; provider=" + state->agent->Config().providerId +
                               L"; model=" + state->agent->Config().model +
                               L"; endpoint=" + SafeEndpoint(state->agent->CurrentApiUrl()));
                state->transcriptPrefix += L"[配置] " + state->agent->Config().model + L" 已保存\r\n";
                state->transcriptPrefix += L"[Runtime] " + RuntimeStatusText(*state) + L"\r\n\r\n";
                RenderTranscript(*state);
            }
            SetFocus(state->input);
            return 0;
        }
        if (LOWORD(wParam) == kCloseId && HIWORD(wParam) == BN_CLICKED) {
            if (state->busy) StopTurn(*state);
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case kDeltaMessage: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(lParam));
        if (!payload || payload->generation != state->generation ||
            payload->generation != gCliGeneration.load(std::memory_order_relaxed)) return 0;
        state->streaming += payload->text;
        RenderTranscript(*state, state->streaming.empty() ? L"…" : state->streaming);
        return 0;
    }
    case kCodexDoneMessage: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(lParam));
        if (!payload || payload->generation != state->generation ||
            payload->generation != gCliGeneration.load(std::memory_order_relaxed)) return 0;
        if (payload->text.empty()) {
            AppendRouteLog(L"route: primary codex success");
            FinishTurn(*state, L"", false);
        } else {
            AppendRouteLog(L"route: primary codex failed; error=" + payload->text);
            StartDirectFallback(*state, payload->text);
        }
        return 0;
    }
    case kDoneMessage: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(lParam));
        if (!payload || payload->generation != state->generation ||
            payload->generation != gCliGeneration.load(std::memory_order_relaxed)) return 0;
        if (payload->text.empty()) AppendRouteLog(L"fallback: direct api success");
        else AppendRouteLog(L"fallback: direct api failed; error=" + payload->text);
        FinishTurn(*state, payload->text, true);
        return 0;
    }
    case WM_SIZE: {
        const int width = static_cast<int>(LOWORD(lParam));
        const int height = static_cast<int>(HIWORD(lParam));
        if (state->transcript) MoveWindow(state->transcript, 16, 52, std::max(100, width - 32), std::max(80, height - 118), TRUE);
        if (state->input) MoveWindow(state->input, 16, std::max(80, height - 50), std::max(100, width - 76), 34, TRUE);
        if (state->settings) MoveWindow(state->settings, std::max(120, width - 112), 14, 96, 28, TRUE);
        if (state->close) MoveWindow(state->close, std::max(20, width - 52), std::max(80, height - 48), 36, 30, TRUE);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(240, 240, 240));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(24, 26, 31));
        return reinterpret_cast<LRESULT>(state->backgroundBrush);
    case WM_CLOSE:
        if (state->busy) StopTurn(*state);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        gCliGeneration.fetch_add(1, std::memory_order_relaxed);
        state->agent->Stop();
        state->codex->Stop();
        return 0;
    case WM_NCDESTROY:
        if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
        if (state->monoFont) DeleteObject(state->monoFont);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

bool ShowL3CliWindow(HINSTANCE instance, HWND owner, L3Agent& agent, const std::wstring& initialPrompt) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &CliProc;
    wc.lpszClassName = kCliClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    auto* state = new CliState{};
    state->instance = instance;
    state->owner = owner;
    state->agent = &agent;
    state->codex = &gCodexRuntime;
    state->backgroundBrush = CreateSolidBrush(RGB(24, 26, 31));
    state->monoFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
    state->uiFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    constexpr int width = 760;
    constexpr int height = 480;
    const int x = ownerRect.left;
    const int y = ownerRect.top;

    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, kCliClass, L"图灵智能桌面 · AI Agent",
                                  WS_POPUP | WS_BORDER,
                                  x, y, width, height, owner, nullptr, instance, state);
    if (!window) {
        DeleteObject(state->backgroundBrush);
        if (state->monoFont) DeleteObject(state->monoFont);
        delete state;
        return false;
    }

    HWND title = CreateWindowExW(0, L"STATIC", L"图灵智能桌面 · L3 AI", WS_CHILD | WS_VISIBLE,
                                 16, 16, 360, 24, window, nullptr, instance, nullptr);
    state->settings = CreateWindowExW(0, L"BUTTON", L"AI 设置", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     width - 112, 14, 96, 28, window,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsId)), instance, nullptr);
    state->transcript = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                       16, 52, width - 32, height - 118,
                                       window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTranscriptId)), instance, nullptr);
    state->input = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  16, height - 50, width - 76, 34,
                                  window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInputId)), instance, nullptr);
    state->close = CreateWindowExW(0, L"BUTTON", L"×", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                  width - 52, height - 48, 36, 30,
                                  window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseId)), instance, nullptr);
    if (!title || !state->settings || !state->transcript || !state->input || !state->close) {
        DestroyWindow(window);
        return false;
    }

    SetWindowLongPtrW(state->input, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    state->oldInputProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state->input, GWLP_WNDPROC,
                                                                     reinterpret_cast<LONG_PTR>(&InputProc)));
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(state->uiFont), TRUE);
    SendMessageW(state->settings, WM_SETFONT, reinterpret_cast<WPARAM>(state->uiFont), TRUE);
    SendMessageW(state->close, WM_SETFONT, reinterpret_cast<WPARAM>(state->uiFont), TRUE);
    SendMessageW(state->transcript, WM_SETFONT, reinterpret_cast<WPARAM>(state->monoFont), TRUE);
    SendMessageW(state->input, WM_SETFONT, reinterpret_cast<WPARAM>(state->monoFont), TRUE);
    SendMessageW(state->input, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"继续对话… Enter 发送 · /retry 重试 · /runtime 查看运行时 · Esc 返回"));

    AppendRouteLog(L"window: L3 opened; primary=Codex CLI -> Relay/API; fallback=Direct API");
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