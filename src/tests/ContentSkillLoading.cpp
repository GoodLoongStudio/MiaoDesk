// content_skill_get must only ever serve files that ship with the product, and it
// must resolve them relative to the executable directory. A regression that turns
// the skill name into a path (instead of an allowlist lookup) would let a model
// reach arbitrary files, so this exercises the real dispatch path rather than a
// re-implementation of the logic.
#include "miaodesk/NativeTools.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr const wchar_t* kSkills[4] = {
    L"content-package-basics", L"wallpaper-content", L"widget-content", L"content-review",
};

// Marks the body of a real SKILL.md so a test can tell "a skill was served" apart
// from "the index was served".
constexpr const char* kSkillBodyMarker = "## 何时使用";

int gFailures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++gFailures;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
    return out;
}

bool WriteUtf8(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

// A skill body is a distinct marker string, never the README text, so the test can
// prove which file was served.
void PlantSkills(const fs::path& root) {
    for (const auto* skill : kSkills) {
        const std::string name(skill, skill + std::char_traits<wchar_t>::length(skill));
        const std::string body = std::string("---\nname: ") + name + "\n---\n\n" + kSkillBodyMarker + "\n";
        WriteUtf8(root / L"skills" / fs::path(name) / L"SKILL.md", body);
    }
    WriteUtf8(root / L"skills" / L"README.md",
              "---\nname: index\n---\n\nMiaoDesk content skills index: content-package-basics and friends.\n");
}

miaodesk::NativeToolResult Get(std::string_view arguments) {
    return miaodesk::ExecuteNativeToolRaw("content_skill_get", arguments);
}

bool ServedBody(const miaodesk::NativeToolResult& result) {
    return result.success && result.message.find(Utf8ToWide(kSkillBodyMarker)) != std::wstring::npos;
}

bool ServedIndex(const miaodesk::NativeToolResult& result) {
    return result.success && result.message.find(L"content skills index") != std::wstring::npos;
}

} // namespace

int wmain() {
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-ContentSkill-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::remove_all(root, ec);
    PlantSkills(root);

    // The tool resolves skills relative to the executable directory, so the plant
    // must live next to this test binary rather than next to the sources.
    wchar_t module[32768]{};
    const DWORD moduleLength = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (moduleLength == 0 || moduleLength >= std::size(module)) {
        std::printf("FAILED: unable to resolve the test executable path.\n");
        return 1;
    }
    const fs::path executableDir = fs::path(std::wstring(module, moduleLength)).parent_path();
    const fs::path skillsDir = executableDir / L"skills";
    const fs::path asideDir = executableDir / (L"skills-aside-" + std::to_wstring(GetCurrentProcessId()));
    const bool hadSkills = fs::exists(skillsDir, ec) && !ec;
    if (hadSkills) {
        fs::rename(skillsDir, asideDir, ec);
        if (ec) {
            std::printf("FAILED: unable to move the pre-existing skills directory aside.\n");
            return 1;
        }
    }
    fs::create_directories(skillsDir, ec);
    if (ec) {
        std::printf("FAILED: unable to create %ls\n", skillsDir.c_str());
        if (hadSkills) fs::rename(asideDir, skillsDir, ec);
        return 1;
    }
    fs::copy(root / L"skills", skillsDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::printf("FAILED: unable to stage skills next to the test executable.\n");
        fs::remove_all(skillsDir, ec);
        if (hadSkills) fs::rename(asideDir, skillsDir, ec);
        return 1;
    }

    std::printf("content_skill_get\n");

    std::printf("\n1. Every allowlisted skill is served\n");
    for (const auto* skill : kSkills) {
        const std::string name(skill, skill + std::char_traits<wchar_t>::length(skill));
        Check(ServedBody(Get("{\"name\":\"" + name + "\"}")), name);
    }

    std::printf("\n2. Omitting name serves the index, not a skill body\n");
    Check(ServedIndex(Get("{}")), "{} returns the index");
    Check(!ServedBody(Get("{}")), "{} does not leak a skill body");

    std::printf("\n3. The skill name is an allowlist, not a path\n");
    for (const auto* hostile : {"..", "../..", "../../Windows/System32", "skills",
                                "content-package-basics/../../README", "./content-package-basics",
                                "CONTENT-PACKAGE-BASICS", "content-package-basics ",
                                " content-package-basics", "content-package-basics\\..\\..\\README"}) {
        const auto result = Get(std::string("{\"name\":\"") + hostile + "\"}");
        Check(!ServedBody(result) && !ServedIndex(result), hostile);
    }

    std::printf("\n4. Unknown names fail with the valid set listed\n");
    {
        const auto result = Get("{\"name\":\"wallpaper-content-v2\"}");
        Check(!result.success, "unknown suffix rejected");
        Check(result.message.find(L"content-package-basics") != std::wstring::npos,
              "error message lists the valid set");
    }

    std::printf("\n5. Malformed arguments never leak a skill body\n");
    for (const auto* malformed : {"{\"name\":123}", "{\"name\":null}", "{\"name\":[]}", "{\"name\":{}}",
                                  "{\"name\":true}", "{\"name\":\"", "{\"name\":\"\\u0000\"}",
                                  "{\"name\":\"\\u0000content-package-basics\"}",
                                  "{\"name\":\"content-package-basics\\u0000\"}",
                                  "{\"name\":\"\\ud83d\\ude00\"}", "{\"name\":\"\\uZZZZ\"}",
                                  "{\"name\":\"\\q\"}", "{}", "", "not json", "[1,2,3]", "\"\""}) {
        const auto result = Get(malformed);
        Check(!ServedBody(result), malformed);
    }

    std::printf("\n6. A missing skill install is reported, not silently empty\n");
    {
        const fs::path removed = skillsDir / L"content-review" / L"SKILL.md";
        fs::remove(removed, ec);
        const auto result = Get("{\"name\":\"content-review\"}");
        Check(!result.success, "missing skill file reports failure");
        Check(result.message.find(removed.wstring()) != std::wstring::npos,
              "failure names the expected path");
        std::error_code restore;
        fs::copy(root / L"skills" / L"content-review" / L"SKILL.md", removed,
                 fs::copy_options::overwrite_existing, restore);
    }

    std::printf("\n7. Unknown tool name still routes to the generic error\n");
    {
        const auto result = miaodesk::ExecuteNativeToolRaw("content_skill_delete", "{}");
        Check(!result.success, "unknown tool rejected");
    }

    fs::remove_all(skillsDir, ec);
    if (hadSkills) fs::rename(asideDir, skillsDir, ec);
    fs::remove_all(root, ec);

    std::printf("\n%s (%d failure(s))\n", gFailures ? "CONTENT SKILL GATE FAILED" : "CONTENT SKILL GATE PASSED",
                gFailures);
    return gFailures ? 1 : 0;
}
