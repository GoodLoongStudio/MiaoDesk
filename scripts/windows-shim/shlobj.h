// 最小 shlobj.h 替身 —— 只为在 macOS 上离线编译 AppPaths.h / WallpaperLibrary.cpp。
//
// 只覆盖这两个文件真实用到的面:SHGetKnownFolderPath、几个 FOLDERID_*、CoTaskMemFree。
// 真实的 GUID 值在这里只是不透明 id——shim 一律返回失败,于是调用方走 LOCALAPPDATA /
// GetTempPathW 回退分支,而那条分支正是单元测试用来隔离状态的路径。

#pragma once

#include <windows.h>

#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((hr) >= 0)
#endif

using HRESULT = long;
using PWSTR = wchar_t*;
using HANDLE = void*;

// GUID 只作不透明句柄用,给一个最小 POD 而非真的 COM 结构。
struct GUID { unsigned long data1; unsigned short data2, data3; unsigned char data4[8]; };
using KNOWNFOLDERID = GUID;
using REFKNOWNFOLDERID = const KNOWNFOLDERID&;

#define KF_FLAG_DEFAULT 0x00000000

// 取值任意但彼此不同即可:代码只把它们当句柄传递,从不比较具体值。
inline constexpr KNOWNFOLDERID FOLDERID_LocalAppData{0x11111111, 0x1111, 0x1111, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}};
inline constexpr KNOWNFOLDERID FOLDERID_Desktop{0x22222222, 0x2222, 0x2222, {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22}};
inline constexpr KNOWNFOLDERID FOLDERID_Documents{0x33333333, 0x3333, 0x3333, {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33}};
inline constexpr KNOWNFOLDERID FOLDERID_Downloads{0x44444444, 0x4444, 0x4444, {0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44}};

inline HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID, unsigned long, HANDLE, PWSTR* out) {
    if (out) *out = nullptr;
    return E_NOTIMPL;  // 迫使调用方走回退分支
}

inline void CoTaskMemFree(void*) {}
