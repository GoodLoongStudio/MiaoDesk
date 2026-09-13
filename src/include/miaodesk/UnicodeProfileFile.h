#pragma once

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace miaodesk::text {

namespace unicode_profile_detail {

inline bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

inline bool DecodeMultiByte(std::string_view bytes, UINT codePage, DWORD flags, std::wstring* output) {
    if (!output) return false;
    output->clear();
    if (bytes.empty()) return true;
    const int required = MultiByteToWideChar(
        codePage, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (required <= 0) return false;
    output->resize(static_cast<std::size_t>(required));
    return MultiByteToWideChar(
               codePage, flags, bytes.data(), static_cast<int>(bytes.size()),
               output->data(), required) == required;
}

inline bool ReadExistingText(const std::filesystem::path& path, std::wstring* text, bool* alreadyUtf16Le) {
    if (!text || !alreadyUtf16Le) return false;
    *alreadyUtf16Le = false;
    text->clear();

    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        *alreadyUtf16Le = true;
        return true;
    }

    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE &&
        static_cast<unsigned char>(bytes[1]) == 0xFF) {
        if (((bytes.size() - 2) % 2) != 0) return false;
        text->reserve((bytes.size() - 2) / 2);
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const auto hi = static_cast<unsigned char>(bytes[i]);
            const auto lo = static_cast<unsigned char>(bytes[i + 1]);
            text->push_back(static_cast<wchar_t>((static_cast<unsigned>(hi) << 8) | lo));
        }
        return true;
    }

    std::string_view payload(bytes);
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        payload.remove_prefix(3);
    }
    if (DecodeMultiByte(payload, CP_UTF8, MB_ERR_INVALID_CHARS, text)) return true;
    return DecodeMultiByte(payload, CP_ACP, 0, text);
}

inline bool WriteUtf16Le(const std::filesystem::path& path, std::wstring_view text, std::wstring* error) {
    static_assert(sizeof(wchar_t) == 2, "Windows profile persistence requires UTF-16 wchar_t.");
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return Fail(error, L"无法创建 Unicode 配置目录：" + path.parent_path().wstring());
    }

    const auto temp = path.parent_path() /
        (path.filename().wstring() + L".unicode-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L".tmp");
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return Fail(error, L"无法创建 Unicode 配置临时文件：" + temp.wstring());
        const unsigned char bom[] = {0xFF, 0xFE};
        output.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        if (!text.empty()) {
            output.write(reinterpret_cast<const char*>(text.data()),
                         static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
        }
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temp, ec);
            return Fail(error, L"写入 Unicode 配置失败：" + path.wstring());
        }
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        std::filesystem::remove(temp, ec);
        return Fail(error, L"替换 Unicode 配置失败，Win32=" + std::to_wstring(code));
    }
    if (error) error->clear();
    return true;
}

} // namespace unicode_profile_detail

// Win32 profile APIs only preserve non-ASCII text reliably when the INI file
// already carries a UTF-16LE BOM. Normalize before any Get/WritePrivateProfile*
// call so Chinese metadata behaves the same on Chinese, English and European
// Windows locales instead of being degraded through the active ANSI code page.
inline bool EnsureUtf16LeProfileFile(const std::filesystem::path& path, std::wstring* error = nullptr) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) return unicode_profile_detail::Fail(error, L"无法检查 Unicode 配置：" + path.wstring());
        return unicode_profile_detail::WriteUtf16Le(path, L"", error);
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        return unicode_profile_detail::Fail(error, L"Unicode 配置路径不是普通文件：" + path.wstring());

    std::wstring text;
    bool alreadyUtf16Le = false;
    if (!unicode_profile_detail::ReadExistingText(path, &text, &alreadyUtf16Le))
        return unicode_profile_detail::Fail(error, L"无法读取并转换 Unicode 配置：" + path.wstring());
    if (alreadyUtf16Le) {
        if (error) error->clear();
        return true;
    }
    return unicode_profile_detail::WriteUtf16Le(path, text, error);
}

} // namespace miaodesk::text
