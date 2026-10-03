#include "miaodesk/ContentPackageValidator.h"

#include "miaodesk/JsonStringField.h"

#include "miaodesk/MiaoCapabilityCatalog.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace miaodesk::creator {
namespace {

// manifest.json 的必填字段。这些名字不是这里定的,是 MiaoContentPackage 的
// 加载器要的那一套 —— 校验器必须和加载器说同一种语言,否则"校验通过"到了
// 加载那一刻还是失败,而那一次失败发生在用户眼前。
constexpr const char* kRequiredManifestFields[] = {"id", "name", "version", "kind", "runtime", "entry"};

// 注意 key 必须带引号。用裸字段名去找的话,"kind":"id" 会让 `find("id")` 命中,
// 于是"id 缺失"被当成"id 在" —— 而 ExtractJsonString 那边找的是带引号的键,
// 两边的判据不是同一个。第一版就是这个 bug:必填检查传裸名、取值传带引号的键,
// 于是同一个文件在两处得出不同结论,而没有任何测试发现。
bool HasField(std::string_view json, std::string_view key) {
    return JsonHasStringKey(json, key);
}

// 必填的**字符串**字段:必须在,而且不能是空串。
//
// 只用 HasField 不够:"kind":"" 会被判成有这个字段,于是放过。而一个空 kind
// 到了加载器那里仍然是失败 —— 只不过那次失败发生在用户眼前。
// ExtractJsonString 分不出"没有这个键"和"值是空串"(两者都返回空串),所以这里
// 必须两个都查。
// 必填字段:传裸字段名进来,这里负责补上引号,让两处判据永远是同一个键。
bool HasNonEmptyField(std::string_view json, std::string_view bareKey) {
    const std::string key = std::string(miaodesk::kJsonQuote) + std::string(bareKey) +
                           miaodesk::kJsonQuote;
    if (!HasField(json, key)) return false;
    return !miaodesk::ExtractJsonString(json, key).empty();
}

bool IsAsciiStableId(const std::string& value) {
    if (value.empty() || value.size() > 256) return false;
    if (std::isalnum(static_cast<unsigned char>(value.front())) == 0) return false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-') continue;
        return false;
    }
    return true;
}

void AddIssue(content::ContentValidationResult* result,
              content::ContentValidationFailure failure, std::string file, std::string nodePath,
              std::string message, bool repairable) {
    content::ContentValidationIssue issue;
    issue.failure = failure;
    issue.file = std::move(file);
    issue.nodePath = std::move(nodePath);
    issue.message = std::move(message);
    issue.repairable = repairable;
    result->issues.push_back(std::move(issue));
}

} // namespace

const content::CandidatePart* FindPart(const std::vector<content::CandidatePart>& parts,
                                       content::CandidatePartRole role,
                                       const std::string& relPath) {
    for (const auto& part : parts) {
        if (part.role != role) continue;
        if (!relPath.empty() && part.relPath != relPath) continue;
        return &part;
    }
    return nullptr;
}

content::ContentValidationResult ValidateCandidatePackage(std::vector<content::CandidatePart> parts) {
    content::ContentValidationResult result;

    // 1. 清单本身必须完整且可用。摘要不可用时,后面每一条判断都建立在沙上。
    const auto digest = content::ComputeCandidateDigest(parts);
    if (!digest.UsableAsIdentity()) {
        result.ok = false;
        AddIssue(&result, content::ContentValidationFailure::SizeLimit, "",
                 "digest", digest.incompletenessReason.empty()
                               ? "候选摘要不可用:有声明过的部分读不到。"
                               : digest.incompletenessReason,
                 false);
        return result;
    }

    // 2. manifest.json 必须存在。
    const auto* manifest = FindPart(parts, content::CandidatePartRole::Manifest, "manifest.json");
    if (!manifest || manifest->bytes.empty()) {
        AddIssue(&result, content::ContentValidationFailure::MissingManifestField, "manifest.json",
                 "", "包里没有 manifest.json,或者它是空的。", true);
        result.ok = false;
        return result;
    }

    // 3. schema 版本。加载器只认 kSchemaVersion;校验器必须和它说同一个数,
    //    否则"校验通过"到了加载那一刻还是失败,而那一次失败发生在用户眼前。
    const auto schema = miaodesk::ExtractJsonInt(manifest->bytes, "\"schema\"");
    if (!schema) {
        AddIssue(&result, content::ContentValidationFailure::MissingManifestField, "manifest.json",
                 "schema", "manifest.json 缺少或读不出 schema。", true);
    } else if (*schema != 1) {
        AddIssue(&result, content::ContentValidationFailure::SchemaRejected, "manifest.json", "schema",
                 std::string("manifest.json 的 schema 必须是 1,现在是 ") + std::to_string(*schema) +
                     "。",
                 false);
    }

    // 4. 必填字段。
    bool missingField = false;
    for (const char* field : kRequiredManifestFields) {
        if (HasNonEmptyField(manifest->bytes, field)) continue;
        AddIssue(&result, content::ContentValidationFailure::MissingManifestField, "manifest.json",
                 field, std::string("manifest.json 缺少必填字段 ") + field + "。", true);
        missingField = true;
    }
    const std::string id = miaodesk::ExtractJsonString(manifest->bytes, "\"id\"");
    if (!id.empty() && !IsAsciiStableId(id)) {
        AddIssue(&result, content::ContentValidationFailure::MissingManifestField, "manifest.json",
                 "id", "manifest.json 的 id 只能由字母数字和 . _ - 组成,且不能以符号开头。", true);
    }

    // 5. 类型与运行时。
    const std::string kind = miaodesk::ExtractJsonString(manifest->bytes, "\"kind\"");
    if (!kind.empty() && kind != "wallpaper" && kind != "widget") {
        AddIssue(&result, content::ContentValidationFailure::KindMismatch, "manifest.json", "kind",
                 std::string("manifest.json 的 kind 必须是 wallpaper 或 widget,现在是 ") + kind + "。",
                 true);
    }
    const std::string runtime = miaodesk::ExtractJsonString(manifest->bytes, "\"runtime\"");
    if (!runtime.empty() && runtime != "scene" && runtime != "web") {
        AddIssue(&result, content::ContentValidationFailure::UnsupportedRuntime, "manifest.json",
                 "runtime",
                 std::string("manifest.json 的 runtime 必须是 scene 或 web,现在是 ") + runtime + "。",
                 true);
    }
    // Web 运行时不在本轮范围内:计划 §1 明确本轮只做声明式 Scene 内容与受控素材。
    // 这里必须显式拒绝而不是放过去 —— 放过去的话,后面每一步都会在一个
    // 本轮不支持的包上给出"通过"。
    if (runtime == "web") {
        AddIssue(&result, content::ContentValidationFailure::UnsupportedRuntime, "manifest.json",
                 "runtime",
                 "Web 运行时的内容包不在本轮范围内:声明式 Scene 内容才是本轮要做的。",
                 false);
    }

    // 5b. 清单里声明的能力必须在能力目录里。
    //
    // 这不是拼写检查。一个虚构的 capability 比一个缺失的更难发现:它通过全部校验,
    // 包里静悄悄地什么都不做,而作者要到桌面上才发现"我绑的数据没出来"。此前这里
    // 一层 capability 检查都没有 —— 只有加载器查过字符集。
    // 键要带引号,与这个文件里其它取值调用一致(见 kRequiredManifestFields 的注释)。
    const auto capabilities =
        miaodesk::ExtractJsonStringArray(manifest->bytes, "\"capabilities\"");
    if (capabilities) {
        for (const auto& capability : *capabilities) {
            if (content::IsCatalogCapability(capability)) continue;
            AddIssue(&result, content::ContentValidationFailure::UnknownCapability, "manifest.json",
                     "capabilities",
                     std::string("manifest.json 声明的能力 ") + capability +
                         " 不在能力目录里。声明一个没有任何实现的能力会被拒绝,不是被忽略。",
                     true);
        }
    }

    // 6. entry 指向的文件必须真的在包里。
    const std::string entry = miaodesk::ExtractJsonString(manifest->bytes, "\"entry\"");
    bool missingReferenced = false;
    if (!entry.empty()) {
        // entry 有两个都合法的位置,而这里原来只认一个:
        //   scene/scene.json —— 创作工作区的布局(CreatorWorkspacePolicy 认这一种);
        //   scene.json       —— 随产品发行的三个壁纸、三个组件与两个示例包的布局。
        // 只认前者的后果是校验器比**加载器**更严:加载器对两种布局都接受,而这里会把
        // 照发行包的样子写出来的候选全部拒掉,拒绝原因还是"必须指向 scene/ 下" ——
        // 作者对着自己的合法包看不出那句话错在哪。同一条原则本文件的 id 规则已经写过了:
        // "校验器比加载器更严的后果是拒绝它本来能加载的包"。
        //
        // 所以判据改成:一个安全的包内相对 .json 路径,且不是 manifest / parameters 本身。
        // 仍然拒绝 assets/a.json(资产目录不放 JSON)、manifest.json(入口不能是清单)、
        // scene/scene.png(不是 .json)与 ../x.json(越界)。工作区那套更严的布局规则由
        // CreatorWorkspacePolicy 继续负责,不在这里重复一遍。
        const bool looksJson = entry.size() > 5 && entry.compare(entry.size() - 5, 5, ".json") == 0;
        const bool escapes = entry.find("..") != std::string::npos ||
                             entry.find('\\') != std::string::npos || !entry.empty() && entry[0] == '/';
        const bool isMeta = entry == "manifest.json" || entry == "parameters.json";
        if (!looksJson || escapes || isMeta) {
            AddIssue(&result, content::ContentValidationFailure::InvalidBinding, "manifest.json",
                     "entry",
                     "manifest.json 的 entry 必须是包内的一个 .json 文件(例如 scene/scene.json "
                     "或 scene.json),不能是 manifest / parameters 本身,也不能越出包的根目录。",
                     true);
        } else if (!FindPart(parts, content::CandidatePartRole::Scene, entry) &&
                   !FindPart(parts, content::CandidatePartRole::Other, entry)) {
            AddIssue(&result, content::ContentValidationFailure::MissingReferencedFile, entry, "",
                     std::string("manifest.json 的 entry 指向 ") + entry + ",但包里没有这个文件。",
                     true);
            missingReferenced = true;
        }
    }

    // 7. scene/parameters 里的文件必须是 JSON 对象。空文件、或者一个数组,
    //    都说明这不是一份写好的内容。
    for (const auto& part : parts) {
        if (part.role != content::CandidatePartRole::Scene &&
            part.role != content::CandidatePartRole::Parameters) {
            continue;
        }
        if (part.bytes.empty()) {
            AddIssue(&result, content::ContentValidationFailure::SchemaRejected, part.relPath, "",
                     "这个文件是空的。", true);
            continue;
        }
        const std::size_t open = part.bytes.find_first_not_of(" \t\r\n");
        if (open == std::string::npos || part.bytes[open] != '{') {
            AddIssue(&result, content::ContentValidationFailure::SchemaRejected, part.relPath, "",
                     "这个文件必须是一个 JSON 对象(以 { 开头)。", true);
        }
    }

    // 8. 代码产物不该出现在清单里。能在封存这一步被抓到,比让它到了
    //    用户的桌面上才被抓到要好得多。
    for (const auto& part : parts) {
        const auto dot = part.relPath.find_last_of('.');
        if (dot == std::string::npos) continue;
        const auto extension = part.relPath.substr(dot);
        if (extension == ".js" || extension == ".ts" || extension == ".html" ||
            extension == ".css" || extension == ".exe" || extension == ".dll" ||
            extension == ".bat" || extension == ".cmd" || extension == ".ps1" ||
            extension == ".sh") {
            AddIssue(&result, content::ContentValidationFailure::UnsupportedRuntime, part.relPath, "",
                     std::string("声明式内容包里不接受 ") + extension + " 这类文件。", false);
        }
    }

    result.ok = !missingField && !missingReferenced && result.issues.empty();
    if (result.ok) {
        result.targetBackend = "scene";
    }
    return result;
}

} // namespace miaodesk::creator
