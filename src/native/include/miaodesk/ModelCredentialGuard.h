#pragma once

#include "miaodesk/ApiRuntimeProfile.h"
#include "miaodesk/RuntimeLogPaths.h"

#include <windows.h>
#include <wincred.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <string>

namespace miaodesk::model_credential_guard {
namespace fs = std::filesystem;

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
}

inline bool HeaderSafeCredential(PCREDENTIALW credential,
                                 std::size_t* invalidIndex = nullptr,
                                 unsigned* invalidCodepoint = nullptr) {
    if (!credential || !credential->CredentialBlob || credential->CredentialBlobSize == 0) {
        return false;
    }
    if ((credential->CredentialBlobSize % sizeof(wchar_t)) != 0) {
        if (invalidIndex) *invalidIndex = 0;
        if (invalidCodepoint) *invalidCodepoint = 0xffffffffu;
        return false;
    }

    const auto* chars = reinterpret_cast<const wchar_t*>(credential->CredentialBlob);
    const std::size_t count = credential->CredentialBlobSize / sizeof(wchar_t);
    if (count == 0) return false;

    for (std::size_t i = 0; i < count; ++i) {
        const unsigned value = static_cast<unsigned>(chars[i]);
        // U+0020 is a legal HTTP header-value character. Reject controls and non-ASCII only.
        // This catches the original U+8BBE Fetch ByteString failure without rejecting old keys
        // merely because they contain an ordinary space.
        if (value < 0x20u || value > 0x7eu) {
            if (invalidIndex) *invalidIndex = i;
            if (invalidCodepoint) *invalidCodepoint = value;
            return false;
        }
    }
    return true;
}

inline std::wstring DefaultProfileCredentialTarget() {
    const auto profile = api_runtime_profile::LoadDefault();
    if (!profile.found || profile.id.empty()) return {};
    return std::wstring(kProfileTargetPrefix) + profile.id;
}

inline BOOL ReadProfileCredential(PCREDENTIALW* credential) {
    const std::wstring target = DefaultProfileCredentialTarget();
    if (target.empty()) return FALSE;
    PCREDENTIALW profile = nullptr;
    if (!RawCredRead(target.c_str(), CRED_TYPE_GENERIC, 0, &profile) || !profile) return FALSE;

    std::size_t invalidIndex = 0;
    unsigned invalidCodepoint = 0;
    if (!HeaderSafeCredential(profile, &invalidIndex, &invalidCodepoint)) {
        AppendCredentialLog(
            L"default API profile credential rejected; bytes=" +
            std::to_wstring(profile->CredentialBlobSize) +
            L"; invalidIndex=" + std::to_wstring(invalidIndex) +
            L"; codepoint=" + std::to_wstring(invalidCodepoint));
        CredFree(profile);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    *credential = profile;
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

    // The API Configuration Center's default Profile is now the runtime source of truth.
    // MiaoDesk/ModelApiKey is only a legacy compatibility mirror; do not require the UI to
    // continually synchronize a second secret database for Pi/Direct/Harness consumers.
    if (ReadProfileCredential(credential)) {
        AppendCredentialLog(L"runtime credential resolved from default API profile");
        return TRUE;
    }

    // Migration fallback for users who have not created api-profiles.ini yet.
    PCREDENTIALW legacy = nullptr;
    const BOOL legacyRead = RawCredRead(target, type, flags, &legacy);
    if (legacyRead && legacy) {
        std::size_t invalidIndex = 0;
        unsigned invalidCodepoint = 0;
        if (HeaderSafeCredential(legacy, &invalidIndex, &invalidCodepoint)) {
            AppendCredentialLog(L"runtime credential resolved from legacy active credential");
            *credential = legacy;
            return TRUE;
        }
        AppendCredentialLog(
            L"legacy active model credential rejected; bytes=" +
            std::to_wstring(legacy->CredentialBlobSize) +
            L"; invalidIndex=" + std::to_wstring(invalidIndex) +
            L"; codepoint=" + std::to_wstring(invalidCodepoint));
        CredFree(legacy);
    }

    AppendCredentialLog(L"no HTTP-header-safe credential is available from API profiles or legacy state");
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
}

inline BOOL CredWriteGuard(PCREDENTIALW credential, DWORD flags) {
    if (!credential || credential->Type != CRED_TYPE_GENERIC ||
        !SameTarget(credential->TargetName, kActiveCredentialTarget)) {
        return RawCredWrite(credential, flags);
    }

    std::size_t invalidIndex = 0;
    unsigned invalidCodepoint = 0;
    if (!HeaderSafeCredential(credential, &invalidIndex, &invalidCodepoint)) {
        AppendCredentialLog(
            L"unsafe legacy active model credential write rejected; bytes=" +
            std::to_wstring(credential ? credential->CredentialBlobSize : 0) +
            L"; invalidIndex=" + std::to_wstring(invalidIndex) +
            L"; codepoint=" + std::to_wstring(invalidCodepoint));
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    // Keep accepting the old mirror while older settings builds still write it, but runtime
    // reads no longer depend on this target when a configured/default API profile exists.
    return RawCredWrite(credential, flags);
}

} // namespace miaodesk::model_credential_guard

#define CredReadW(...) ::miaodesk::model_credential_guard::CredReadGuard(__VA_ARGS__)
#define CredWriteW(...) ::miaodesk::model_credential_guard::CredWriteGuard(__VA_ARGS__)
