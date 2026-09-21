// 最小 windows.h 替身 —— 只为在 macOS 上编译并运行 MiaoDesk 的真实 C++ 源码做离线验证。
// 覆盖的符号是逐文件查出来的实际使用面,不多不少。
//
// 历史教训(写在这里以免再犯):
//   1. 最初 MultiByteToWideChar 写成有损窄化,把"网页壁纸"变成问号,
//      让含中文的 WallpaperPackage::SelfTest 假失败 —— 替身缺陷伪装成产品缺陷。
//   2. printf 的 %ls 在 C locale 下遇非 ASCII 会静默失败并截断整行,
//      一度让我以为某段输出"根本没执行"。
// 两者都差点被误判成产品 bug。改替身前先怀疑替身。
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <unistd.h>

#define FAILED(hr) ((hr) < 0)
#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((hr) >= 0)
#endif

using DWORD = unsigned long;
using HRESULT = long;
using PWSTR = wchar_t*;
using LPVOID = void*;
using HANDLE = void*;
using HMODULE = void*;
using UINT = unsigned int;
inline constexpr unsigned CP_ACP = 0;
inline constexpr int MAX_PATH = 260;
inline constexpr HRESULT E_NOTIMPL = 0x80004001L;

inline constexpr unsigned CP_UTF8 = 65001;
inline constexpr unsigned MB_ERR_INVALID_CHARS = 8;

struct SYSTEMTIME {
    std::uint16_t wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

inline void GetLocalTime(SYSTEMTIME* t) {
    const std::time_t now = std::time(nullptr);
    const std::tm lt = *std::localtime(&now);
    t->wYear = lt.tm_year + 1900; t->wMonth = lt.tm_mon + 1; t->wDay = lt.tm_mday;
    t->wDayOfWeek = lt.tm_wday;   t->wHour = lt.tm_hour;     t->wMinute = lt.tm_min;
    t->wSecond = lt.tm_sec;       t->wMilliseconds = 0;
}

inline std::uint64_t GetTickCount64() {
    return static_cast<std::uint64_t>(std::time(nullptr)) * 1000ull;
}

inline unsigned long GetCurrentProcessId() {
    return static_cast<unsigned long>(getpid());
}

// 真正的 UTF-8 -> UTF-16,含代理对。out == nullptr 时返回所需码元数。
inline int MultiByteToWideChar(unsigned, unsigned, const char* in, int inLen,
                              wchar_t* out, int outCap) {
    if (inLen < 0) inLen = static_cast<int>(std::char_traits<char>::length(in));
    if (!out) {
        int n = 0;
        for (int i = 0; i < inLen;) {
            const unsigned char c = static_cast<unsigned char>(in[i]);
            const int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
            n += (len == 4) ? 2 : 1;
            i += len;
        }
        return n;
    }
    int written = 0, i = 0;
    while (i < inLen && written < outCap) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        std::uint32_t cp = c & (len == 1 ? 0x7F : len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
        for (int k = 1; k < len && i + k < inLen; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3F);
        i += len;
        if (cp >= 0x10000 && written + 1 < outCap) {
            cp -= 0x10000;
            out[written++] = static_cast<wchar_t>(0xD800 + (cp >> 10));
            out[written++] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out[written++] = static_cast<wchar_t>(cp);
        }
    }
    return written;
}

// 真正的 UTF-16 -> UTF-8。
inline int WideCharToMultiByte(unsigned, unsigned, const wchar_t* in, int inLen,
                              char* out, int outCap, const char* = nullptr, bool* = nullptr) {
    if (inLen < 0) inLen = static_cast<int>(std::char_traits<wchar_t>::length(in));
    if (!out) {
        int n = 0;
        for (int i = 0; i < inLen; ++i) {
            const std::uint32_t cp = static_cast<std::uint32_t>(in[i]);
            n += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        }
        return n;
    }
    int written = 0;
    for (int i = 0; i < inLen && written < outCap; ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(in[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < inLen) {
            const std::uint32_t lo = static_cast<std::uint32_t>(in[++i]);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        if (cp < 0x80) {
            if (written + 1 > outCap) break;
            out[written++] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            if (written + 2 > outCap) break;
            out[written++] = static_cast<char>(0xC0 | (cp >> 6));
            out[written++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            if (written + 3 > outCap) break;
            out[written++] = static_cast<char>(0xE0 | (cp >> 12));
            out[written++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[written++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            if (written + 4 > outCap) break;
            out[written++] = static_cast<char>(0xF0 | (cp >> 18));
            out[written++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[written++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[written++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return written;
}

// MSVC secure 模板重载:缓冲区以数组引用传入,尺寸由模板推导。
template <std::size_t N>
inline int sprintf_s(char (&buf)[N], const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int n = std::vsnprintf(buf, N, fmt, ap);
    va_end(ap);
    return n;
}

template <std::size_t N>
inline int swprintf_s(wchar_t (&buf)[N], const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int n = std::vswprintf(buf, N, fmt, ap);
    va_end(ap);
    return n;
}

// AppPaths.h 用到的三个查询。环境变量走真实 getenv(UTF-16 版本从环境取值),
// GetTempPathW / GetModuleFileNameW 给一个确定性的目录,足够让测试跑起来。
inline DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, DWORD size) {
    if (!name) return 0;
    std::string narrow;
    for (const wchar_t* p = name; *p; ++p) {
        const std::uint32_t cp = static_cast<std::uint32_t>(*p);
        if (cp < 0x80) narrow.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { narrow.push_back(static_cast<char>(0xC0 | (cp >> 6))); narrow.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { narrow.push_back(static_cast<char>(0xE0 | (cp >> 12))); narrow.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); narrow.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }
    const char* value = std::getenv(narrow.c_str());
    if (!value || !buffer) return 0;
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (needed <= 0) return 0;
    if (static_cast<DWORD>(needed) > size) return static_cast<DWORD>(needed);
    MultiByteToWideChar(CP_UTF8, 0, value, -1, buffer, static_cast<int>(size));
    return static_cast<DWORD>(needed);
}

inline DWORD GetTempPathW(DWORD size, wchar_t* buffer) {
    const char* dir = std::getenv("TMPDIR");
    if (!dir) dir = "/tmp";
    const int needed = MultiByteToWideChar(CP_UTF8, 0, dir, -1, nullptr, 0);
    if (needed <= 0) return 0;
    if (static_cast<DWORD>(needed) > size) return static_cast<DWORD>(needed);
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, buffer, static_cast<int>(size));
    // GetTempPathW 会保证以反斜杠结尾
    std::size_t len = std::char_traits<wchar_t>::length(buffer);
    if (len > 0 && buffer[len - 1] != L'\\') { buffer[len] = L'\\'; buffer[len + 1] = L'\0'; }
    return static_cast<DWORD>(len + 1);
}

inline DWORD GetModuleFileNameW(HMODULE, wchar_t* buffer, DWORD size) {
    if (!buffer || size == 0) return 0;
    const char* exe = std::getenv("MIAODESK_SHIM_MODULE_PATH");
    if (!exe) exe = "/tmp/miaodesk-offline/MiaoDeskTest.exe";
    const int needed = MultiByteToWideChar(CP_UTF8, 0, exe, -1, nullptr, 0);
    if (needed <= 0) return 0;
    if (static_cast<DWORD>(needed) > size) return static_cast<DWORD>(needed);
    MultiByteToWideChar(CP_UTF8, 0, exe, -1, buffer, static_cast<int>(size));
    return static_cast<DWORD>(needed);
}
