#pragma once

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

inline bool SameTarget(LPCWSTR target, const wchar_t* expected) {
    return target && expected && _wcsicmp(target, expected) == 0;
}

inline fs::path ProfilesPath() {
    wchar_t localAppData[32768]{};
    const DWORD count = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (count > 0 && count < std::size(localAppData)) {
        return fs::path(std::wstring(localAppData, count)) / L"MiaoDesk" / L"api-profiles.ini";
    }
    return {};
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
        // Model API keys are inserted into HTTP header values. Keep this deliberately strict:
        // visible ASCII only, no whitespace/control characters and no Unicode. This prevents
        // WHATWG Fetch ByteString failures in Pi and ERROR_INVALID_PARAMETER in WinHTTP.
        if (value < 0x21u || value > 0x7eu) {
            if (invalidIndex) *invalidIndex = i;
            if (invalidCodepoint) *invalidCodepoint = value;
            return false;
        }
    }
    return true;
}

inline std::wstring DefaultProfileCredentialTarget() {
    const auto path = ProfilesPath();
    if (path.empty()) return {};

    std::array<wchar_t, 32768> sections{};
    GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path.c_str());
    for (const wchar_t* cursor = sections.data(); *cursor; cursor += std::wcslen(cursor) + 1) {
        if (wcsncmp(cursor, L"profile:", 8) != 0) continue;
        std::array<wchar_t, 32> value{};
        GetPrivateProfileStringW(cursor, L"default", L"0", value.data(),
                                 static_cast<DWORD>(value.size()), path.c_str());
        if (_wcsicmp(value.data(), L"1") != 0 && _wcsicmp(value.data(), L"true") != 0) continue;
        return std::wstring(kProfileTargetPrefix) + (cursor + 8);
    }
    return {};
}

inline bool RepairActiveCredentialFrom(PCREDENTIALW source) {
    if (!HeaderSafeCredential(source)) return false;

    std::wstring target(kActiveCredentialTarget);
    CREDENTIALW repaired{};
    repaired.Type = CRED_TYPE_GENERIC;
    repaired.TargetName = target.data();
    repaired.CredentialBlobSize = source->CredentialBlobSize;
    repaired.CredentialBlob = source->CredentialBlob;
    repaired.Persist = CRED_PERSIST_LOCAL_MACHINE;
    repaired.UserName = const_cast<wchar_t*>(L"MiaoDesk");
    return CredWriteW(&repaired, 0) != FALSE;
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

    PCREDENTIALW active = nullptr;
    const BOOL activeRead = RawCredRead(target, type, flags, &active);
    if (activeRead && active) {
        std::size_t invalidIndex = 0;
        unsigned invalidCodepoint = 0;
        if (HeaderSafeCredential(active, &invalidIndex, &invalidCodepoint)) {
            *credential = active;
            return TRUE;
        }
        AppendCredentialLog(
            L"active model credential rejected; bytes=" +
            std::to_wstring(active->CredentialBlobSize) +
            L"; invalidIndex=" + std::to_wstring(invalidIndex) +
            L"; codepoint=" + std::to_wstring(invalidCodepoint));
        CredFree(active);
        active = nullptr;
    }

    const std::wstring profileTarget = DefaultProfileCredentialTarget();
    if (!profileTarget.empty()) {
        PCREDENTIALW profile = nullptr;
        if (RawCredRead(profileTarget.c_str(), CRED_TYPE_GENERIC, 0, &profile) && profile) {
            if (HeaderSafeCredential(profile)) {
                const bool repaired = RepairActiveCredentialFrom(profile);
                AppendCredentialLog(repaired
                    ? L"active model credential recovered from default API profile"
                    : L"default API profile credential is valid, but active credential repair failed");
                *credential = profile;
                return TRUE;
            }
            std::size_t invalidIndex = 0;
            unsigned invalidCodepoint = 0;
            HeaderSafeCredential(profile, &invalidIndex, &invalidCodepoint);
            AppendCredentialLog(
                L"default API profile credential also rejected; bytes=" +
                std::to_wstring(profile->CredentialBlobSize) +
                L"; invalidIndex=" + std::to_wstring(invalidIndex) +
                L"; codepoint=" + std::to_wstring(invalidCodepoint));
            CredFree(profile);
        }
    }

    AppendCredentialLog(L"no HTTP-header-safe model credential is available");
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
}

} // namespace miaodesk::model_credential_guard

#define CredReadW(...) ::miaodesk::model_credential_guard::CredReadGuard(__VA_ARGS__)
