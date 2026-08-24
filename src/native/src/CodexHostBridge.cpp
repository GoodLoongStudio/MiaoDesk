#include "turingdesk/CodexHostBridge.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace turingdesk {
namespace {

constexpr wchar_t kInputClass[] = L"TuringDesk.CodexHostInput";
constexpr int kEditId = 4101;

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), needed);
    return result;
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

std::string EscapeJson(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 16);
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buffer[7]{};
                sprintf_s(buffer, "\\u%04x", static_cast<unsigned>(ch));
                result += buffer;
            } else {
                result.push_back(static_cast<char>(ch));
            }
        }
    }
    return result;
}

std::size_t FindKey(std::string_view json, std::string_view key, std::size_t start = 0) {
    const std::string needle = "\"" + std::string(key) + "\"";
    return json.find(needle, start);
}

std::size_t ValueStart(std::string_view json, std::string_view key, std::size_t start = 0) {
    auto pos = FindKey(json, key, start);
    if (pos == std::string_view::npos) return pos;
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string_view::npos) return pos;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    return pos;
}

std::string ExtractString(std::string_view json, std::string_view key, std::size_t start = 0) {
    auto pos = ValueStart(json, key, start);
    if (pos == std::string_view::npos || pos >= json.size() || json[pos] != '"') return {};
    ++pos;
    std::string result;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') break;
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (pos >= json.size()) break;
        const char esc = json[pos++];
        switch (esc) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: result.push_back(esc); break;
        }
    }
    return result;
}

bool ExtractBool(std::string_view json, std::string_view key, bool fallback = false) {
    const auto pos = ValueStart(json, key);
    if (pos == std::string_view::npos) return fallback;
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return fallback;
}

std::string ExtractRawValue(std::string_view json, std::string_view key) {
    const auto start = ValueStart(json, key);
    if (start == std::string_view::npos || start >= json.size()) return {};
    const char first = json[start];
    if (first == '"') {
        bool escaped = false;
        for (std::size_t i = start + 1; i < json.size(); ++i) {
            const char ch = json[i];
            if (escaped) { escaped = false; continue; }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '"') return std::string(json.substr(start, i - start + 1));
        }
        return {};
    }
    if (first == '{' || first == '[') {
        const char open = first;
        const char close = first == '{' ? '}' : ']';
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (std::size_t i = start; i < json.size(); ++i) {
            const char ch = json[i];
            if (inString) {
                if (escaped) { escaped = false; continue; }
                if (ch == '\\') { escaped = true; continue; }
                if (ch == '"') inString = false;
                continue;
            }
            if (ch == '"') { inString = true; continue; }
            if (ch == open) ++depth;
            else if (ch == close && --depth == 0) return std::string(json.substr(start, i - start + 1));
        }
        return {};
    }
    auto end = start;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && !std::isspace(static_cast<unsigned char>(json[end]))) ++end;
    return std::string(json.substr(start, end - start));
}

bool ExtractRequestId(std::string_view json, long long& id) {
    const auto start = ValueStart(json, "id");
    if (start == std::string_view::npos) return false;
    std::size_t pos = start;
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') { negative = true; ++pos; }
    long long value = 0;
    bool any = false;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        any = true;
        value = value * 10 + (json[pos++] - '0');
    }
    if (!any) return false;
    id = negative ? -value : value;
    return true;
}

std::vector<std::string> SplitTopLevelObjects(std::string_view arrayJson) {
    std::vector<std::string> result;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    std::size_t objectStart = std::string_view::npos;
    for (std::size_t i = 0; i < arrayJson.size(); ++i) {
        const char ch = arrayJson[i];
        if (inString) {
            if (escaped) { escaped = false; continue; }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '"') inString = false;
            continue;
        }
        if (ch == '"') { inString = true; continue; }
        if (ch == '{') {
            if (depth == 0) objectStart = i;
            ++depth;
        } else if (ch == '}' && depth > 0) {
            --depth;
            if (depth == 0 && objectStart != std::string_view::npos) {
                result.emplace_back(arrayJson.substr(objectStart, i - objectStart + 1));
                objectStart = std::string_view::npos;
            }
        }
    }
    return result;
}

struct InputState {
    std::wstring prompt;
    std::wstring value;
    bool secret{};
    bool accepted{};
    HWND edit{};
};

LRESULT CALLBACK InputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<InputState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<InputState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE: {
        if (!state) return -1;
        CreateWindowExW(0, L"STATIC", state->prompt.c_str(), WS_CHILD | WS_VISIBLE,
                        16, 16, 488, 48, hwnd, nullptr, nullptr, nullptr);
        const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | (state->secret ? ES_PASSWORD : 0);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
                                      16, 72, 488, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        336, 116, 80, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
                        424, 116, 80, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), nullptr, nullptr);
        SetFocus(state->edit);
        return 0;
    }
    case WM_COMMAND:
        if (!state) break;
        if (LOWORD(wParam) == IDOK) {
            const int len = GetWindowTextLengthW(state->edit);
            std::wstring value(static_cast<std::size_t>(len) + 1, L'\0');
            if (len > 0) GetWindowTextW(state->edit, value.data(), len + 1);
            value.resize(static_cast<std::size_t>(len));
            state->value = std::move(value);
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            state->accepted = false;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state) state->accepted = false;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool PromptText(const std::wstring& title, const std::wstring& prompt, bool secret, std::wstring& value) {
    static std::once_flag once;
    std::call_once(once, [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = InputWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kInputClass;
        RegisterClassW(&wc);
    });

    InputState state{prompt, {}, secret, false, nullptr};
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    constexpr int width = 540;
    constexpr int height = 195;
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kInputClass, title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return false;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);

    MSG msg{};
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (state.accepted) value = std::move(state.value);
    return state.accepted;
}

bool Confirm(const std::wstring& title, const std::wstring& body) {
    return MessageBoxW(nullptr, body.c_str(), title.c_str(),
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND) == IDYES;
}

std::wstring ApprovalDescription(std::string_view requestJson, const wchar_t* fallback) {
    std::wstring text = Utf8ToWide(ExtractString(requestJson, "reason"));
    const auto command = Utf8ToWide(ExtractString(requestJson, "command"));
    const auto cwd = Utf8ToWide(ExtractString(requestJson, "cwd"));
    if (text.empty()) text = fallback;
    if (!command.empty()) text += L"\r\n\r\n命令：\r\n" + command;
    if (!cwd.empty()) text += L"\r\n\r\n目录：" + cwd;
    text += L"\r\n\r\n是否允许这一次操作？";
    return text;
}

bool HandleApproval(std::string_view method, std::string_view requestJson, long long id, std::string& replyJson) {
    if (method != "item/commandExecution/requestApproval" && method != "item/fileChange/requestApproval") return false;
    const bool command = method == "item/commandExecution/requestApproval";
    const bool accepted = Confirm(command ? L"Codex 命令执行审批" : L"Codex 文件修改审批",
                                  ApprovalDescription(requestJson, command ? L"Codex 请求执行一个命令。" : L"Codex 请求修改文件。"));
    replyJson = "{\"id\":" + std::to_string(id) + ",\"result\":{\"decision\":\"" +
                std::string(accepted ? "accept" : "decline") + "\"}}";
    return true;
}

bool HandlePermissions(std::string_view method, std::string_view requestJson, long long id, std::string& replyJson) {
    if (method != "item/permissions/requestApproval") return false;
    const auto permissions = ExtractRawValue(requestJson, "permissions");
    const auto reason = Utf8ToWide(ExtractString(requestJson, "reason"));
    std::wstring body = reason.empty() ? L"Codex 请求临时扩展当前任务的文件或网络权限。" : reason;
    body += L"\r\n\r\n允许后仅授予 Codex 本次请求中列出的权限。是否允许？";
    const bool accepted = Confirm(L"Codex 权限请求", body);
    const std::string granted = accepted && !permissions.empty() ? permissions : "{}";
    replyJson = "{\"id\":" + std::to_string(id) + ",\"result\":{\"scope\":\"turn\",\"permissions\":" + granted + "}}";
    return true;
}

bool HandleUserInput(std::string_view method, std::string_view requestJson, long long id, std::string& replyJson) {
    if (method != "item/tool/requestUserInput") return false;
    const auto questionsRaw = ExtractRawValue(requestJson, "questions");
    const auto questions = SplitTopLevelObjects(questionsRaw);
    std::string answers = "{";
    bool first = true;
    for (const auto& questionJson : questions) {
        const auto questionId = ExtractString(questionJson, "id");
        if (questionId.empty()) continue;
        auto prompt = Utf8ToWide(ExtractString(questionJson, "question"));
        const auto header = Utf8ToWide(ExtractString(questionJson, "header"));
        if (prompt.empty()) prompt = header.empty() ? L"Codex 需要你的输入。" : header;

        const auto optionsRaw = ExtractRawValue(questionJson, "options");
        const auto options = SplitTopLevelObjects(optionsRaw);
        if (!options.empty()) {
            prompt += L"\r\n\r\n可选项：";
            for (const auto& option : options) {
                const auto label = Utf8ToWide(ExtractString(option, "label"));
                if (!label.empty()) prompt += L"\r\n• " + label;
            }
        }

        std::wstring value;
        const bool accepted = PromptText(L"Codex 需要输入", prompt, ExtractBool(questionJson, "isSecret"), value);
        if (!accepted) continue;
        if (!first) answers += ',';
        first = false;
        answers += "\"" + EscapeJson(questionId) + "\":{\"answers\":[\"" + EscapeJson(WideToUtf8(value)) + "\"]}";
    }
    answers += '}';
    replyJson = "{\"id\":" + std::to_string(id) + ",\"result\":{\"answers\":" + answers + "}}";
    return true;
}

bool HandleMcpElicitation(std::string_view method, std::string_view requestJson, long long id, std::string& replyJson) {
    if (method != "mcpServer/elicitation/request") return false;
    const auto mode = ExtractString(requestJson, "mode");
    const auto message = Utf8ToWide(ExtractString(requestJson, "message"));
    const auto url = Utf8ToWide(ExtractString(requestJson, "url"));
    bool accepted = false;
    if (mode == "url" && !url.empty()) {
        std::wstring body = message.empty() ? L"MCP 服务请求打开一个网页继续操作。" : message;
        body += L"\r\n\r\n" + url + L"\r\n\r\n是否打开？";
        accepted = Confirm(L"Codex MCP 请求", body);
        if (accepted) ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        const std::wstring body = (message.empty() ? L"MCP 服务请求结构化输入。" : message) +
                                  L"\r\n\r\n当前原生宿主无法安全推断此 MCP 表单字段，将拒绝本次表单而不是伪造数据。";
        MessageBoxW(nullptr, body.c_str(), L"Codex MCP 请求", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
    }
    replyJson = "{\"id\":" + std::to_string(id) + ",\"result\":{\"action\":\"" +
                std::string(accepted ? "accept" : "decline") + "\",\"content\":null}}";
    return true;
}

} // namespace

bool HandleCodexHostRequest(std::string_view method, std::string_view requestJson, std::string& replyJson) {
    long long id = 0;
    if (!ExtractRequestId(requestJson, id)) return false;
    if (HandleApproval(method, requestJson, id, replyJson)) return true;
    if (HandlePermissions(method, requestJson, id, replyJson)) return true;
    if (HandleUserInput(method, requestJson, id, replyJson)) return true;
    if (HandleMcpElicitation(method, requestJson, id, replyJson)) return true;
    return false;
}

} // namespace turingdesk
