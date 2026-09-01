#pragma once

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <iterator>
#include <string>

namespace miaodesk {

inline std::filesystem::path RuntimeLogDirectory() {
    std::filesystem::path desktop;

    PWSTR knownDesktop = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &knownDesktop)) && knownDesktop) {
        desktop = knownDesktop;
        CoTaskMemFree(knownDesktop);
    } else {
        if (knownDesktop) CoTaskMemFree(knownDesktop);
        wchar_t profile[32768]{};
        const DWORD count = GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile)));
        if (count > 0 && count < std::size(profile)) {
            desktop = std::filesystem::path(std::wstring(profile, count)) / L"Desktop";
        }
    }

    if (desktop.empty()) return {};

    const auto logs = desktop / L"MiaoDesk-Logs";
    std::error_code ec;
    std::filesystem::create_directories(logs, ec);
    if (ec) return {};
    return logs;
}

inline std::filesystem::path RuntimeLogPath(const wchar_t* filename) {
    const auto directory = RuntimeLogDirectory();
    if (directory.empty() || !filename || !*filename) return {};
    return directory / filename;
}

} // namespace miaodesk
