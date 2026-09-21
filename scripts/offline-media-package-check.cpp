// 定位 CreateVideo 在哪一步返回 false。
//
// 不用 -fshort-wchar 读中文错误信息:那会让宽字符字面量损坏(一字节一字却按 2 字节
// wchar_t 读),printf 在第一个 NUL 截断,得到的是假消息。改为把 CreateMediaPackage 的
// 每一步前置条件在自己的 driver 里复刻一遍,只输出 ASCII。
#include "miaodesk/WallpaperPackage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;
using miaodesk::wallpaper::WallpaperPackage;
using miaodesk::wallpaper::WallpaperPackageManifest;
using miaodesk::wallpaper::WallpaperPackageType;

static bool WriteBytes(const fs::path& path, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

int main() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / "miaodesk-offline-locate";
    fs::remove_all(root, ec);
    const fs::path sourceDir = root / "src";
    fs::create_directories(sourceDir, ec);
    const char kClipBytes[] = {0, 0, 0, 0x18, 'f','t','y','p','m','p','4','2','m','e','d','i','a','-','p','a','y','l','o','a','d'};
    WriteBytes(sourceDir / "clip.mp4", std::string(kClipBytes, sizeof(kClipBytes)));

    const fs::path source = sourceDir / "clip.mp4";
    const fs::path package = root / "built-video.mdwall";

    std::printf("[1] exists           = %d\n", fs::exists(source, ec));
    std::printf("[2] is_regular_file  = %d\n", fs::is_regular_file(source, ec));
    const auto size = fs::file_size(source, ec);
    std::printf("[3] file_size        = %llu (ec=%d)\n",
                static_cast<unsigned long long>(size), ec ? 1 : 0);
    std::printf("[4] extension        = %ls\n", source.extension().wstring().c_str());
    std::printf("[5] create_dirs pkg  = %d\n", fs::create_directories(package, ec) ? 0 : 0);
    const fs::path assets = package / "assets";
    std::printf("[6] create assets    = %d (ec=%d)\n", fs::create_directories(assets, ec), ec ? 1 : 0);
    const fs::path entry = assets / "clip.mp4";
    fs::copy_file(source, entry, fs::copy_options::overwrite_existing, ec);
    std::printf("[7] copy_file        = ec=%d\n", ec ? 1 : 0);
    std::printf("    entry exists     = %d\n", fs::exists(entry, ec));

    std::wstring error;
    const bool created = WallpaperPackage::CreateVideo(package, L"Built Video", source,
                                                       L"self-test", L"MiaoDesk", &error);
    std::printf("[8] CreateVideo      = %d, error 长度 = %zu\n",
                created ? 1 : 0, error.size());
    // 只打印错误串的长度和前几个码元,避免宽字面量损坏干扰判断
    if (!created) {
        std::printf("    error 前 6 个码元:");
        for (std::size_t i = 0; i < error.size() && i < 6; ++i)
            std::printf(" U+%04X", static_cast<unsigned>(error[i]));
        std::printf("\n");
    }

    WallpaperPackageManifest manifest;
    const bool validated = WallpaperPackage::Validate(package, &manifest, &error);
    std::printf("[9] Validate         = %d\n", validated ? 1 : 0);
    if (validated) {
        std::printf("    type=%d(期望 %d) entry=%ls\n",
                    static_cast<int>(manifest.type),
                    static_cast<int>(WallpaperPackageType::Video),
                    manifest.entry.wstring().c_str());
    }

    fs::remove_all(root, ec);
    return 0;
}
