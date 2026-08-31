#pragma once

#include "miaodesk/RuntimeLogPaths.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cwchar>
#include <string>

namespace miaodesk::l3_winhttp_trace {

struct TraceContext {
    std::wstring host;
    INTERNET_PORT port{};
    std::wstring verb;
    std::wstring objectName;
};

inline thread_local TraceContext gContext;

inline std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), count, nullptr, nullptr);
    return out;
}

inline std::wstring ErrorMessage(DWORD error) {
    wchar_t* raw = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
    std::wstring text;
    if (count && raw) text.assign(raw, count);
    if (raw) LocalFree(raw);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    return text;
}

inline std::wstring SafeObjectName(LPCWSTR raw) {
    if (!raw || !*raw) return L"/";
    std::wstring value(raw);
    const auto secret = value.find_first_of(L"?#");
    if (secret != std::wstring::npos) {
        value.resize(secret);
        value += L"?<redacted>";
    }
    if (value.size() > 320) {
        value.resize(320);
        value += L"…";
    }
    return value;
}

inline std::wstring ContextText() {
    std::wstring text;
    if (!gContext.host.empty()) {
        text += L"host=" + gContext.host;
        if (gContext.port) text += L":" + std::to_wstring(gContext.port);
    }
    if (!gContext.verb.empty()) text += (text.empty() ? L"" : L"; ") + L"verb=" + gContext.verb;
    if (!gContext.objectName.empty()) text += (text.empty() ? L"" : L"; ") + L"path=" + gContext.objectName;
    return text;
}

inline void Append(const wchar_t* stage, DWORD error, const std::wstring& detail = {}) {
    const auto path = RuntimeLogPath(L"l3-winhttp.log");
    if (path.empty()) return;

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!file || file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[128]{};
    swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
               now.wMilliseconds, stage ? stage : L"WinHTTP");

    std::wstring line(prefix);
    if (error != ERROR_SUCCESS) {
        line += L" FAILED error=" + std::to_wstring(error);
        const auto message = ErrorMessage(error);
        if (!message.empty()) line += L" (" + message + L")";
    } else {
        line += L" OK";
    }
    const auto context = ContextText();
    if (!context.empty()) line += L"; " + context;
    if (!detail.empty()) line += L"; " + detail;
    line += L"\r\n";

    const auto utf8 = Utf8(line);
    if (!utf8.empty()) {
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}

inline HINTERNET Open(LPCWSTR userAgent, DWORD accessType, LPCWSTR proxyName,
                      LPCWSTR proxyBypass, DWORD flags) {
    HINTERNET result = ::WinHttpOpen(userAgent, accessType, proxyName, proxyBypass, flags);
    if (!result) Append(L"WinHttpOpen", GetLastError(), L"accessType=" + std::to_wstring(accessType));
    return result;
}

inline BOOL SetTimeouts(HINTERNET handle, int resolveTimeout, int connectTimeout,
                        int sendTimeout, int receiveTimeout) {
    const BOOL ok = ::WinHttpSetTimeouts(handle, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout);
    if (!ok) Append(L"WinHttpSetTimeouts", GetLastError());
    return ok;
}

inline BOOL CrackUrl(LPCWSTR url, DWORD urlLength, DWORD flags, LPURL_COMPONENTS parts) {
    const BOOL ok = ::WinHttpCrackUrl(url, urlLength, flags, parts);
    if (!ok) Append(L"WinHttpCrackUrl", GetLastError(), L"url=<configured API URL>");
    return ok;
}

inline HINTERNET Connect(HINTERNET session, LPCWSTR serverName, INTERNET_PORT port, DWORD reserved) {
    gContext.host = serverName ? serverName : L"";
    gContext.port = port;
    gContext.verb.clear();
    gContext.objectName.clear();
    HINTERNET result = ::WinHttpConnect(session, serverName, port, reserved);
    if (!result) Append(L"WinHttpConnect", GetLastError());
    return result;
}

inline HINTERNET OpenRequest(HINTERNET connect, LPCWSTR verb, LPCWSTR objectName,
                             LPCWSTR version, LPCWSTR referrer, LPCWSTR const* acceptTypes,
                             DWORD flags) {
    gContext.verb = verb ? verb : L"";
    gContext.objectName = SafeObjectName(objectName);
    HINTERNET result = ::WinHttpOpenRequest(connect, verb, objectName, version, referrer, acceptTypes, flags);
    if (!result) Append(L"WinHttpOpenRequest", GetLastError(), L"flags=" + std::to_wstring(flags));
    return result;
}

inline BOOL SendRequest(HINTERNET request, LPCWSTR headers, DWORD headersLength,
                        LPVOID optional, DWORD optionalLength, DWORD totalLength,
                        DWORD_PTR context) {
    // Some OpenAI-compatible endpoints exposed ERROR_INVALID_PARAMETER (87) while the
    // caller supplied DWORD(-1) for a null-terminated Unicode header block. Passing the
    // explicit character count is equivalent but removes that ambiguity in WinHTTP.
    DWORD normalizedHeaderLength = headersLength;
    if (headers && headersLength == static_cast<DWORD>(-1L)) {
        const auto length = std::wcslen(headers);
        normalizedHeaderLength = length > MAXDWORD ? MAXDWORD : static_cast<DWORD>(length);
    }

    const BOOL ok = ::WinHttpSendRequest(request, headers, normalizedHeaderLength,
                                         optional, optionalLength, totalLength, context);
    if (!ok) {
        Append(L"WinHttpSendRequest", GetLastError(),
               L"headerChars=" + std::to_wstring(normalizedHeaderLength) +
               L"; bodyBytes=" + std::to_wstring(optionalLength) +
               L"; totalBytes=" + std::to_wstring(totalLength));
    }
    return ok;
}

inline BOOL ReceiveResponse(HINTERNET request, LPVOID reserved) {
    const BOOL ok = ::WinHttpReceiveResponse(request, reserved);
    if (!ok) Append(L"WinHttpReceiveResponse", GetLastError());
    return ok;
}

inline BOOL QueryDataAvailable(HINTERNET request, LPDWORD available) {
    const BOOL ok = ::WinHttpQueryDataAvailable(request, available);
    if (!ok) Append(L"WinHttpQueryDataAvailable", GetLastError());
    return ok;
}

inline BOOL ReadData(HINTERNET request, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead) {
    const BOOL ok = ::WinHttpReadData(request, buffer, bytesToRead, bytesRead);
    if (!ok) Append(L"WinHttpReadData", GetLastError(), L"requestedBytes=" + std::to_wstring(bytesToRead));
    return ok;
}

} // namespace miaodesk::l3_winhttp_trace

#define WinHttpOpen(...) ::miaodesk::l3_winhttp_trace::Open(__VA_ARGS__)
#define WinHttpSetTimeouts(...) ::miaodesk::l3_winhttp_trace::SetTimeouts(__VA_ARGS__)
#define WinHttpCrackUrl(...) ::miaodesk::l3_winhttp_trace::CrackUrl(__VA_ARGS__)
#define WinHttpConnect(...) ::miaodesk::l3_winhttp_trace::Connect(__VA_ARGS__)
#define WinHttpOpenRequest(...) ::miaodesk::l3_winhttp_trace::OpenRequest(__VA_ARGS__)
#define WinHttpSendRequest(...) ::miaodesk::l3_winhttp_trace::SendRequest(__VA_ARGS__)
#define WinHttpReceiveResponse(...) ::miaodesk::l3_winhttp_trace::ReceiveResponse(__VA_ARGS__)
#define WinHttpQueryDataAvailable(...) ::miaodesk::l3_winhttp_trace::QueryDataAvailable(__VA_ARGS__)
#define WinHttpReadData(...) ::miaodesk::l3_winhttp_trace::ReadData(__VA_ARGS__)
