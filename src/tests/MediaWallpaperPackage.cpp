// A declarative media wallpaper is one manifest plus one asset. This gate pins the
// two properties that make that safe: the entry extension must match the declared
// type, and a package built from an arbitrary source path must never place an asset
// outside its own assets/ directory.
#include "miaodesk/WallpaperPackage.h"
#include "miaodesk/WallpaperLibrary.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using miaodesk::wallpaper::WallpaperPackage;
using miaodesk::wallpaper::WallpaperPackageManifest;
using miaodesk::wallpaper::WallpaperPackageType;

namespace {

int gFailures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++gFailures;
}

bool WriteBytes(const fs::path& path, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool WriteManifestRaw(const fs::path& package, const std::string& typeKey, const std::string& entry) {
    return WriteBytes(package / L"manifest.json",
                      "{\n  \"schema\": 1,\n  \"type\": \"" + typeKey + "\",\n" +
                          "  \"title\": \"Gate\",\n  \"author\": \"MiaoDesk\",\n  \"entry\": \"" + entry +
                          "\",\n  \"provenance\": \"self-test\",\n  \"fps_cap\": 30,\n  \"audio\": false\n}\n");
}

fs::path WorkRoot() {
    return fs::temp_directory_path() /
        (L"MiaoDesk-MediaPackage-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
}

} // namespace

int wmain() {
    const fs::path root = WorkRoot();
    const fs::path sourceDir = root / L"sources";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(sourceDir, ec);

    // A tiny but non-empty payload per extension is enough: Validate checks the
    // extension and existence, not the codec.
    //
    // The string literals are split on purpose: \x18f would otherwise be one hex
    // escape (f is a hex digit) instead of byte 0x18 followed by 'f'.
    WriteBytes(sourceDir / L"clip.mp4", "\x00\x00\x00\x18" "ftypmp42media-payload");
    WriteBytes(sourceDir / L"photo.png", "\x89PNG\r\n\x1a\npayload");
    WriteBytes(sourceDir / L"notes.txt", "not a media file");
    WriteBytes(sourceDir / L"empty.mp4", "");
    WriteBytes(sourceDir / L"..\\..\\evil.mp4", "traversal attempt");

    std::printf("Media wallpaper packages\n");

    std::printf("\n1. CreateVideo / CreateImage build valid packages\n");
    {
        const auto package = root / L"built-video.mdwall";
        std::wstring error;
        const bool created = WallpaperPackage::CreateVideo(package, L"Built Video", sourceDir / L"clip.mp4",
                                                           L"self-test", L"MiaoDesk", &error);
        Check(created, "CreateVideo succeeds");
        WallpaperPackageManifest manifest;
        Check(WallpaperPackage::Validate(package, &manifest, &error) &&
                  manifest.type == WallpaperPackageType::Video,
              "built video package validates as type video");
        Check(manifest.entry == fs::path(L"assets") / L"clip.mp4", "entry is assets/clip.mp4");

        const auto image = root / L"built-image.mdwall";
        const bool createdImage = WallpaperPackage::CreateImage(image, L"Built Image", sourceDir / L"photo.png",
                                                                L"self-test", L"MiaoDesk", &error);
        Check(createdImage, "CreateImage succeeds");
        Check(WallpaperPackage::Validate(image, &manifest, &error) &&
                  manifest.type == WallpaperPackageType::Image,
              "built image package validates as type image");
    }

    std::printf("\n2. The entry extension must match the declared type\n");
    {
        WallpaperPackageManifest manifest;
        std::wstring error;
        {
            // Previously accepted: a package could claim "video" while pointing at a
            // text file, and only fail later in the library with no usable diagnosis.
            const auto package = root / L"video-with-text-entry.mdwall";
            WriteBytes(package / L"assets" / L"notes.txt", "not a media file");
            Check(WriteManifestRaw(package, "video", "assets/notes.txt"), "hand-written manifest placed");
            const bool valid = WallpaperPackage::Validate(package, &manifest, &error);
            Check(!valid, "type video with a .txt entry is rejected");
        }
        {
            const auto package = root / L"image-with-video-entry.mdwall";
            WriteBytes(package / L"assets" / L"clip.mp4", "payload");
            Check(WriteManifestRaw(package, "image", "assets/clip.mp4"), "hand-written manifest placed");
            Check(!WallpaperPackage::Validate(package, &manifest, &error),
                  "type image with a .mp4 entry is rejected");
        }
        {
            const auto package = root / L"video-with-html-entry.mdwall";
            WriteBytes(package / L"assets" / L"index.html", "<html></html>");
            Check(WriteManifestRaw(package, "video", "assets/index.html"), "hand-written manifest placed");
            Check(!WallpaperPackage::Validate(package, &manifest, &error),
                  "type video with an .html entry is rejected");
        }
        {
            const auto package = root / L"web-with-video-entry.mdwall";
            WriteBytes(package / L"assets" / L"clip.mp4", "payload");
            Check(WriteManifestRaw(package, "web", "assets/clip.mp4"), "hand-written manifest placed");
            Check(!WallpaperPackage::Validate(package, &manifest, &error),
                  "type web with a .mp4 entry is still rejected (pre-existing rule)");
        }
    }

    std::printf("\n3. Source files that must be refused\n");
    {
        const auto package = root / L"from-text.mdwall";
        std::wstring error;
        Check(!WallpaperPackage::CreateVideo(package, L"Bad", sourceDir / L"notes.txt", L"t", L"a", &error),
              "CreateVideo refuses a .txt source");
        Check(!WallpaperPackage::CreateImage(package, L"Bad", sourceDir / L"clip.mp4", L"t", L"a", &error),
              "CreateImage refuses a .mp4 source");
        Check(!WallpaperPackage::CreateVideo(package, L"Bad", sourceDir / L"empty.mp4", L"t", L"a", &error),
              "CreateVideo refuses an empty source");
        Check(!WallpaperPackage::CreateVideo(package, L"Bad", sourceDir / L"missing.mp4", L"t", L"a", &error),
              "CreateVideo refuses a missing source");
        Check(!fs::exists(package / L"assets"), "no assets directory is left behind after refusal");
    }

    std::printf("\n4. An asset name can never escape the package\n");
    {
        const auto package = root / L"sanitised.mdwall";
        // The source stem carries separators and illegal characters; the built entry
        // must stay a single file inside assets/.
        const auto hostile = sourceDir / L"..\\..\\evil.mp4";
        std::wstring error;
        const bool created = WallpaperPackage::CreateVideo(package, L"Sanitised", hostile, L"t", L"a", &error);
        WallpaperPackageManifest manifest;
        const bool valid = created && WallpaperPackage::Validate(package, &manifest, &error);
        Check(valid, "package built from a hostile source path still validates");
        if (valid) {
            const bool contained = manifest.entry.is_relative() && !manifest.entry.has_root_name() &&
                                   *manifest.entry.begin() == L"assets";
            Check(contained, "entry begins with assets/");
            Check(fs::is_regular_file(package / manifest.entry, ec), "entry resolves inside the package");
            const auto outside = root / L"evil.mp4";
            Check(!fs::exists(outside, ec), "nothing was written outside the package");
        }
    }

    std::printf("\n5. The library imports a hand-written video package\n");
    {
        miaodesk::wallpaper::WallpaperLibrary library(root / L"Library");
        std::wstring error;
        Check(library.Load(&error), "empty library loads");
        const auto packages = library.PackageDirectory();
        std::error_code packageEc;
        fs::create_directories(packages, packageEc);
        const auto package = packages / L"hand-written-video.mdwall";
        WriteBytes(package / L"assets" / L"clip.mp4", "payload");
        Check(WriteManifestRaw(package, "video", "assets/clip.mp4"), "hand-written manifest placed in the library");
        Check(library.Load(&error), "library reloads after a package appears");
        Check(library.InferKind(package / L"assets" / L"clip.mp4") ==
                  miaodesk::wallpaper::LibraryWallpaperKind::Video,
              "library recognises the video extension");
        bool imported = false;
        miaodesk::wallpaper::LibraryWallpaperKind importedKind = miaodesk::wallpaper::LibraryWallpaperKind::Unknown;
        for (const auto& item : library.Items()) {
            if (item.kind == miaodesk::wallpaper::LibraryWallpaperKind::Video &&
                item.source.filename() == L"clip.mp4") {
                imported = true;
                importedKind = item.kind;
                break;
            }
        }
        Check(imported, "the video package becomes a Video library item");
        Check(importedKind == miaodesk::wallpaper::LibraryWallpaperKind::Video,
              "its kind is Video, not Unknown");
    }

    fs::remove_all(root, ec);

    std::printf("\n%s (%d failure(s))\n",
                gFailures ? "MEDIA PACKAGE GATE FAILED" : "MEDIA PACKAGE GATE PASSED", gFailures);
    return gFailures ? 1 : 0;
}
