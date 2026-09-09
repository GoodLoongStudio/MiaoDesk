#pragma once

#include "miaodesk/ApiRuntimeProfile.h"
#include "miaodesk/RuntimeLogPaths.h"

#include <windows.h>
#include <wincred.h>

#include <cwchar>
#include <string>

namespace miaodesk::model_credential_guard {

constexpr wchar_t kActiveCredentialTarget[] = L"MiaoDesk/ModelApiKey";
constexpr wchar_t kProfileTargetPrefix[] = L"MiaoDesk/ApiProfile/";

inline BOOL RawCredRead(LPCWSTR target, DWORD type, DWORD flags, PCREDENTIALW* credential) {
    return ::CredReadW(target, type, flags, credential);
}

inline BOOL RawCredWrite(PCREDENTIALW credential, DWORD flags) {
    return ::CredWriteW(credential, flags);
}

inline bool SameTarget(LPCWSTR target, const wchar_t* expected) {
    return target && expected && _wcsicmp(target, expected) == 0;
}

inline void AppendCredentialLog(const std::wstring& message) {
    const auto path = RuntimeLogPath(L"model-credential.log");
    if (path.empty()) return;

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!file || file == INVALID_HANDLE_VALUE) return;

    const DWORD originalError = GetLastError();
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[64]{};
    swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
               now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond);

    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        std::string utf8(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                            utf8.data(), bytes, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
    SetLastError(originalError);
}

inline BOOL ReadDefaultProfileCredential(PCREDENTIALW* credential) {
    const auto profile = api_runtime_profile::LoadDefault();
    if (!profile.found || profile.id.empty() || !profile.configured) {
        AppendCredentialLog(profile.error.empty()
            ? L"runtime credential unavailable: API Configuration Center default profile is not configured"
            : L"runtime credential unavailable: " + profile.error);
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    const std::wstring target = std::wstring(kProfileTargetPrefix) + profile.id;
    PCREDENTIALW stored = nullptr;
    if (!RawCredRead(target.c_str(), CRED_TYPE_GENERIC, 0, &stored) || !stored) {
        AppendCredentialLog(L"runtime credential unavailable: default profile Credential Manager entry is missing");
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    // ApiRuntimeProfile already validated that the same credential is HTTP-header-safe.
    *credential = stored;
    AppendCredentialLog(L"runtime credential resolved from API Configuration Center default profile");
    return TRUE;
}

inline BOOL CredReadGuard(LPCWSTR target, DWORD type, DWORD flags, PCREDENTIALW* credential) {
    if (!credential) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *credential = nullptr;

    if (!SameTarget(target, kActiveCredentialTarget) || type != CRED_TYPE_GENERIC) {
        return RawCredRead(target, type, flags, credential);
    }

    // Transitional adapter for old L3/Pi call sites. Runtime state comes ONLY from the API
    // Configuration Center default Profile; the old MiaoDesk/ModelApiKey secret is never read.
    return ReadDefaultProfileCredential(credential);
}

inline BOOL CredWriteGuard(PCREDENTIALW credential, DWORD flags) {
    if (!credential || credential->Type != CRED_TYPE_GENERIC ||
        !SameTarget(credential->TargetName, kActiveCredentialTarget)) {
        return RawCredWrite(credential, flags);
    }

    // The shadow Active Credential is retired. New secrets are written only to
    // MiaoDesk/ApiProfile/<id> by the API Configuration Center.
    AppendCredentialLog(L"legacy MiaoDesk/ModelApiKey write rejected: API profiles are authoritative");
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

} // namespace miaodesk::model_credential_guard

#define CredReadW(...) ::miaodesk::model_credential_guard::CredReadGuard(__VA_ARGS__)
#define CredWriteW(...) ::miaodesk::model_credential_guard::CredWriteGuard(__VA_ARGS__)
