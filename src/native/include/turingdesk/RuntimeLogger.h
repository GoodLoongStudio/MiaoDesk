#pragma once

#include "turingdesk/RuntimeLogPaths.h"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
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
           << message << L"\n";

        const std::wstring line = ss.str();

        // 1. Windows debugger output (DebugView / VS Debugger)
        OutputDebugStringW(line.c_str());

        // 2. Real-time file output flushed on every write
        std::lock_guard<std::mutex> lock(mutex_);
        const auto path = turingdesk::RuntimeLogPath(L"desktop-debug.log");
        if (!path.empty()) {
            std::wofstream file(path, std::ios::app);
            if (file.is_open()) {
                file << line;
                file.flush();
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
