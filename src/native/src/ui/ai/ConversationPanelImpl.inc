#include "turingdesk/L3CliWindow.h"
#include "turingdesk/PiRuntime.h"
#include "turingdesk/RuntimeLogPaths.h"

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

// This source path is retained temporarily as a build entry only.
// The legacy terminal UI is gone. The only user-facing AI surface implemented here is
// the Conversation Panel below; Pi Runtime and diagnostics remain independent of UI style.
constexpr wchar_t kConversationClass[] = L"TuringDesk.Native.ConversationPanel";
constexpr int kTranscriptId = 3101;
constexpr int kInputId = 3102;
constexpr int kSendId = 3103;
constexpr int kCloseId = 3104;
constexpr UINT kDeltaMessage = WM_APP + 31;
constexpr UINT kDirectDoneMessage = WM_APP + 32;
constexpr UINT kPiDoneMessage = WM_APP + 33;

enum class ActiveRuntime {
    Pi,
    DirectModel,
};

struct UiMessage {
    std::uint64_t generation{};
    std::wstring text;
};

struct ConversationState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    HWND transcript{};
    HWND input{};
    HWND send{};
    HWND close{};
    WNDPROC oldInputProc{};
    L3Agent* agent{};
    PiRuntime* pi{};
    HBRUSH backgroundBrush{};
    HBRUSH inputBrush{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    std::wstring transcriptPrefix;
    std::wstring streaming;
    std::wstring lastPrompt;
    std::wstring activePrompt;
    std::uint64_t generation{};
    ActiveRuntime activeRuntime{ActiveRuntime::Pi};
    bool busy{};
};

std::atomic_uint64_t gCliGeneration{0};
PiRuntime gPiRuntime;

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

fs::path L3RouteLogPath() {
    return RuntimeLogPath(L"l3-runtime.log");
}

fs::path PiDetailLogPath() {
    return RuntimeLogPath(L"pi-runtime.log");
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
    if (text.find(L"WinHTTP 错误 12029") != std::wstring::npos ||
        text.find(L"WinHTTP 错误 12030") != std::wstring::npos ||
        text.find(L"WinHTTP 错误 12031") != std::wstring::npos)
        return L"无法连接模型服务，或连接被服务端中断。可以稍后输入 /retry 重试。";
    if (text.find(L"WinHTTP 错误 12175") != std::wstring::npos)
        return L"HTTPS/TLS 握手失败。请检查证书、系统时间或代理设置。";
    if (ContainsHttpStatus(text, 401) || ContainsHttpStatus(text, 403))
        return L"模型鉴权失败（HTTP 401/403）。请从系统托盘进入设置，检查 API Key 和权限。";
    if (ContainsHttpStatus(text, 429))
        return L"模型服务正在限流（HTTP 429）。稍后可以输入 /retry 重试。";
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

void RenderTranscript(ConversationState& state, const std::wstring& tail = {}) {
    const std::wstring text = state.transcriptPrefix + tail;
    SetWindowTextW(state.transcript, text.c_str());
    SendMessageW(state.transcript, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(state.transcript, EM_SCROLLCARET, 0, 0);
}

void AppendCompleted(ConversationState& state, const std::wstring& user, const std::wstring& assistant) {
    state.transcriptPrefix += L"你\r\n" + user + L"\r\n\r\n";
    state.transcriptPrefix += L"图灵\r\n" + (assistant.empty() ? L"已完成" : assistant) + L"\r\n\r\n";
    RenderTranscript(state);
}

std::wstring RuntimeName(ActiveRuntime runtime) {
    switch (runtime) {
    case ActiveRuntime::Pi: return L"Pi Agent";
    case ActiveRuntime::DirectModel: return L"Direct Model Runtime";
    }
    return L"Unknown Runtime";
}

std::wstring RuntimeStatusText(ConversationState& state) {
    const auto status = state.pi->Status(*state.agent);
    std::wstring text = L"主路由：Pi Agent → 当前配置 API";
    text += L"\r\n失败回退：Direct Model Runtime → 当前配置 API";
    text += L"\r\nProvider：" + (state.agent->Config().providerId.empty() ? std::wstring(L"未识别") : state.agent->Config().providerId);
    text += L" · Model：" + (state.agent->Config().model.empty() ? std::wstring(L"未配置") : state.agent->Config().model);
    text += L"\r\nPi：" + status.message;
    if (!status.nodePath.empty()) text += L"\r\nNode：" + status.nodePath;
    if (!status.piPath.empty()) text += L"\r\nPi CLI：" + status.piPath;
    text += L"\r\n路由日志：" + L3RouteLogPath().wstring();
    text += L"\r\nPi 详情：" + PiDetailLogPath().wstring();
    return text;
}

std::wstring UserFacingLocalReply(const std::wstring& command, const ConversationState& state, const std::wstring& raw) {
    if (command == L"/status") {
        const auto& config = state.agent->Config();
        std::wstring text = L"图灵智能桌面 AI";
        text += L" · Provider=" + (config.providerId.empty() ? std::wstring(L"未识别") : config.providerId);
        text += L" · Model=" + (config.model.empty() ? std::wstring(L"未配置") : config.model);
        text += L" · API Key=" + std::wstring(state.agent->HasStoredApiKey() ? L"已配置" : L"未配置");
        return text;
    }
    if (command == L"/help") {
        return L"可用命令：/status、/time、/apps <关键词>、/files <关键词>、/open <应用名>、/open-file <文件名>、/new。模型和 API Key 请从系统托盘进入设置。";
    }
    if (command == L"/new" || command == L"/new-chat" || command == L"新对话") {
        return L"已开始新的对话。";
    }
    return raw;
}

void SetBusyVisual(ConversationState& state, bool busy) {
    state.busy = busy;
    EnableWindow(state.input, busy ? FALSE : TRUE);
    EnableWindow(state.send, busy ? FALSE : TRUE);
    SetWindowTextW(state.send, busy ? L"…" : L"发送");
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
    state.transcriptPrefix += state.streaming + L"\r\n\r\n";
    state.streaming.clear();
    state.activePrompt.clear();
    SetBusyVisual(state, false);
    RenderTranscript(state);
    SetFocus(state.input);
}

void StartDirectFallback(ConversationState& state, const std::wstring& piError) {
    const auto generation = state.generation;
    const HWND hwnd = state.window;
    const std::wstring prompt = state.activePrompt;
    state.activeRuntime = ActiveRuntime::DirectModel;
    state.streaming.clear();

    const std::wstring reason = piError.empty() ? L"Pi Agent 未返回有效结果" : piError;
    AppendRouteLog(L"fallback: direct api start; reason=" + reason +
                   L"; provider=" + state.agent->Config().providerId +
                   L"; model=" + state.agent->Config().model +
                   L"; endpoint=" + SafeEndpoint(state.agent->CurrentApiUrl()));
    state.transcriptPrefix += L"正在恢复连接…\r\n";
    state.transcriptPrefix += L"图灵\r\n";
    RenderTranscript(state, L"正在思考…");

    auto onDelta = [hwnd, generation](std::wstring delta) {
        PostUi(hwnd, kDeltaMessage, generation, std::move(delta));
    };
    auto onDone = [hwnd, generation](std::wstring done) {
        PostUi(hwnd, kDirectDoneMessage, generation, std::move(done));
    };
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
    state.transcriptPrefix += state.streaming + L"\r\n\r\n";
    state.streaming.clear();
    state.activePrompt.clear();
    SetBusyVisual(state, false);
    RenderTranscript(state);
    SetFocus(state.input);
}

void SendPrompt(ConversationState& state) {
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
                state.pi->ResetSession();
                AppendRouteLog(L"route: Pi session reset by user");
            }
            AppendCompleted(state, typedPrompt, UserFacingLocalReply(lower, state, localReply));
            return;
        }
    }

    if (!state.agent->HasApiKey()) {
        AppendCompleted(state, typedPrompt, L"图灵 AI 尚未配置。请从右下角系统托盘进入设置，填写 API 地址和 Key。");
        return;
    }

    state.lastPrompt = actualPrompt;
    state.activePrompt = actualPrompt;
    state.activeRuntime = ActiveRuntime::Pi;
    state.transcriptPrefix += L"你\r\n" + typedPrompt + (retry ? L"  · 重试上一请求" : L"") + L"\r\n\r\n";
    state.transcriptPrefix += L"图灵\r\n";
    state.streaming.clear();
    state.generation = gCliGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    SetBusyVisual(state, true);
    RenderTranscript(state, L"正在思考…");

    AppendRouteLog(L"route: primary pi start; provider=" + state.agent->Config().providerId +
                   L"; model=" + state.agent->Config().model +
                   L"; endpoint=" + SafeEndpoint(state.agent->CurrentApiUrl()));

    const auto generation = state.generation;
    const HWND hwnd = state.window;
    auto onDelta = [hwnd, generation](std::wstring delta) {
        PostUi(hwnd, kDeltaMessage, generation, std::move(delta));
    };
    auto onDone = [hwnd, generation](std::wstring done) {
        PostUi(hwnd, kPiDoneMessage, generation, std::move(done));
    };
    state.pi->AskAsync(*state.agent, actualPrompt, std::move(onDelta), std::move(onDone));
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
            else DestroyWindow(state->window);
            return 0;
        }
    }
    return CallWindowProcW(state->oldInputProc, hwnd, message, wParam, lParam);
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
    case WM_COMMAND:
        if (LOWORD(wParam) == kSendId && HIWORD(wParam) == BN_CLICKED) {
            SendPrompt(*state);
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
        RenderTranscript(*state, state->streaming.empty() ? L"正在思考…" : state->streaming);
        return 0;
    }

    case kPiDoneMessage: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(lParam));
        if (!payload || payload->generation != state->generation ||
            payload->generation != gCliGeneration.load(std::memory_order_relaxed)) return 0;
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
        if (state->transcript)
            MoveWindow(state->transcript, 24, 66, std::max(120, width - 48), std::max(120, height - 154), TRUE);
        if (state->input)
            MoveWindow(state->input, 24, std::max(100, height - 72), std::max(120, width - 150), 42, TRUE);
        if (state->send)
            MoveWindow(state->send, std::max(120, width - 116), std::max(100, height - 72), 92, 42, TRUE);
        if (state->close)
            MoveWindow(state->close, std::max(20, width - 52), 18, 28, 28, TRUE);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(42, 45, 52));
        SetBkColor(dc, RGB(248, 249, 251));
        return reinterpret_cast<LRESULT>(state->inputBrush ? state->inputBrush : state->backgroundBrush);
    }

    case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(248, 249, 251));
        return reinterpret_cast<LRESULT>(state->backgroundBrush);

    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, state->backgroundBrush);
        return 1;
    }

    case WM_CLOSE:
        if (state->busy) StopTurn(*state);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        gCliGeneration.fetch_add(1, std::memory_order_relaxed);
        state->agent->Stop();
        state->pi->Stop();
        return 0;

    case WM_NCDESTROY:
        if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
        if (state->inputBrush) DeleteObject(state->inputBrush);
        if (state->titleFont) DeleteObject(state->titleFont);
        if (state->bodyFont) DeleteObject(state->bodyFont);
        if (state->smallFont) DeleteObject(state->smallFont);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

bool ShowL3CliWindow(HINSTANCE instance, HWND owner, L3Agent& agent, const std::wstring& initialPrompt) {
    // Compatibility function name retained while SearchWindow is migrated. It creates only the
    // new Conversation Panel; the old terminal window class and rendering path no longer exist.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &ConversationProc;
    wc.lpszClassName = kConversationClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    auto* state = new ConversationState{};
    state->instance = instance;
    state->owner = owner;
    state->agent = &agent;
    state->pi = &gPiRuntime;
    state->backgroundBrush = CreateSolidBrush(RGB(248, 249, 251));
    state->inputBrush = CreateSolidBrush(RGB(248, 249, 251));
    state->titleFont = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
    state->bodyFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    state->smallFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    constexpr int width = 780;
    constexpr int height = 540;
    const int x = ownerRect.left;
    const int y = ownerRect.top + 12;

    HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW, kConversationClass, L"图灵 AI",
        WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,
        x, y, width, height, owner, nullptr, instance, state);
    if (!window) {
        if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
        if (state->inputBrush) DeleteObject(state->inputBrush);
        if (state->titleFont) DeleteObject(state->titleFont);
        if (state->bodyFont) DeleteObject(state->bodyFont);
        if (state->smallFont) DeleteObject(state->smallFont);
        delete state;
        return false;
    }

    HWND title = CreateWindowExW(0, L"STATIC", L"图灵 AI", WS_CHILD | WS_VISIBLE,
                                 24, 19, 360, 32, window, nullptr, instance, nullptr);
    HWND subtitle = CreateWindowExW(0, L"STATIC", L"由 Pi 驱动", WS_CHILD | WS_VISIBLE,
                                    112, 25, 200, 24, window, nullptr, instance, nullptr);

    state->close = CreateWindowExW(0, L"BUTTON", L"×",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   width - 52, 18, 28, 28, window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseId)), instance, nullptr);

    state->transcript = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        24, 66, width - 48, height - 154,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTranscriptId)), instance, nullptr);

    state->input = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        24, height - 72, width - 150, 42,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInputId)), instance, nullptr);

    state->send = CreateWindowExW(
        0, L"BUTTON", L"发送", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        width - 116, height - 72, 92, 42, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSendId)), instance, nullptr);

    if (!title || !subtitle || !state->close || !state->transcript || !state->input || !state->send) {
        DestroyWindow(window);
        return false;
    }

    SetWindowLongPtrW(state->input, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    state->oldInputProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(state->input, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InputProc)));

    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
    SendMessageW(subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(state->smallFont), TRUE);
    SendMessageW(state->close, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
    SendMessageW(state->transcript, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
    SendMessageW(state->input, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
    SendMessageW(state->send, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
    SendMessageW(state->input, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"问图灵…  Enter 发送 · Esc 停止/关闭"));

    state->transcriptPrefix = L"图灵\r\n你好。直接告诉我你想做什么。\r\n\r\n";
    RenderTranscript(*state);

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
