#pragma once

#include "turingdesk/RuntimeLogPaths.h"

#include <windows.h>
#include <shellapi.h>

#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace turingdesk::log {

enum class Level {
    Debug,
    Info,
    Warn,
    Error,
};

inline const wchar_t* LevelText(Level level) noexcept {
    switch (level) {
    case Level::Debug: return L"DEBUG";
    case Level::Info:  return L"INFO ";
    case Level::Warn:  return L"WARN ";
    case Level::Error: return L"ERROR";
    }
    return L"INFO ";
}

inline std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string utf8(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        utf8.data(), required, nullptr, nullptr);
    return utf8;
}

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Write(Level level, std::wstring_view tag, std::wstring_view message) {
        SYSTEMTIME st{};
        GetLocalTime(&st);

        std::wostringstream ss;
        ss << L'['
           << std::setfill(L'0')
           << std::setw(4) << st.wYear << L'-'
           << std::setw(2) << st.wMonth << L'-'
           << std::setw(2) << st.wDay << L' '
           << std::setw(2) << st.wHour << L':'
           << std::setw(2) << st.wMinute << L':'
           << std::setw(2) << st.wSecond << L'.'
           << std::setw(3) << st.wMilliseconds
           << L"] [" << LevelText(level) << L"] "
           << L'[' << tag << L"] "
           << message << L"\r\n";

        const std::wstring line = ss.str();

        // 1. Windows debugger output (DebugView / VS Debugger)
        OutputDebugStringW(line.c_str());

        // 2. Real-time multi-process safe UTF-8 file append
        std::lock_guard<std::mutex> lock(mutex_);
        const auto path = turingdesk::RuntimeLogPath(L"desktop-debug.log");
        if (!path.empty()) {
            const std::string utf8Line = WideToUtf8(line);
            HANDLE file = CreateFileW(
                path.c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(file, utf8Line.data(), static_cast<DWORD>(utf8Line.size()), &written, nullptr);
                CloseHandle(file);
            }
        }
    }

private:
    std::mutex mutex_;
};

inline void Write(Level level, std::wstring_view tag, std::wstring_view message) {
    Logger::Instance().Write(level, tag, message);
}

inline void Debug(std::wstring_view tag, std::wstring_view message) {
    Write(Level::Debug, tag, message);
}

inline void Info(std::wstring_view tag, std::wstring_view message) {
    Write(Level::Info, tag, message);
}

inline void Warn(std::wstring_view tag, std::wstring_view message) {
    Write(Level::Warn, tag, message);
}

inline void Error(std::wstring_view tag, std::wstring_view message) {
    Write(Level::Error, tag, message);
}

inline void OpenLogDirectory() {
    const auto dir = turingdesk::RuntimeLogDirectory();
    if (!dir.empty()) {
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

inline void OpenDebugLogFile() {
    const auto path = turingdesk::RuntimeLogPath(L"desktop-debug.log");
    if (!path.empty()) {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        OpenLogDirectory();
    }
}

} // namespace turingdesk::log
