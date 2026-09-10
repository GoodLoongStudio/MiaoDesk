#include "miaodesk/MiaoWidgetContentCatalog.h"

#include "miaodesk/AppPaths.h"
#include "miaodesk/MiaoContentDefinitionLoader.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool ValidDefinitionId(std::wstring_view value) noexcept {
    if (value.empty() || value.size() > 160) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-' || ch == L'_';
    });
}

bool IsMdWidgetDirectory(const fs::directory_entry& entry) {
    std::error_code ec;
    if (!entry.is_directory(ec) || ec) return false;
    return _wcsicmp(entry.path().extension().c_str(), L".mdwidget") == 0;
}

bool ResolveOneRoot(
    std::wstring_view definitionId,
    const fs::path& root,
    ResolvedWidgetContent* content,
    bool* found,
    std::wstring* error) {
    if (found) *found = false;
    if (root.empty()) return true;

    std::error_code ec;
    if (!fs::exists(root, ec)) return true;
    if (ec || !fs::is_directory(root, ec))
        return Fail(error, L"Widget content root is not a directory: " + root.wstring());

    std::optional<ResolvedWidgetContent> match;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!IsMdWidgetDirectory(entry)) continue;

        ContentDefinition definition;
        std::wstring loadError;
        if (!MiaoContentDefinitionLoader::Load(entry.path(), &definition, &loadError)) continue;
        if (definition.kind != ContentKind::Widget || definition.id != definitionId) continue;

        if (match)
            return Fail(error, L"Duplicate .mdwidget definition id in content root: " + std::wstring(definitionId));
        match = ResolvedWidgetContent{entry.path(), std::move(definition)};
    }
    if (ec) return Fail(error, L"Unable to enumerate Widget content root: " + root.wstring());
    if (!match) return true;

    if (content) *content = std::move(*match);
    if (found) *found = true;
    return true;
}

bool WriteText(const fs::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}

} // namespace

bool MiaoWidgetContentCatalog::IsContentSource(std::wstring_view source) noexcept {
    return source.size() > kSourcePrefix.size() && source.substr(0, kSourcePrefix.size()) == kSourcePrefix;
}

std::wstring MiaoWidgetContentCatalog::MakeSource(std::wstring_view definitionId) {
    return ValidDefinitionId(definitionId) ? std::wstring(kSourcePrefix) + std::wstring(definitionId) : std::wstring{};
}

bool MiaoWidgetContentCatalog::ParseSource(
    std::wstring_view source,
    std::wstring* definitionId,
    std::wstring* error) {
    if (!definitionId) return Fail(error, L"Widget content definition id output is null.");
    definitionId->clear();
    if (!IsContentSource(source)) return Fail(error, L"Widget source is not a content: source.");
    const std::wstring_view id = source.substr(kSourcePrefix.size());
    if (!ValidDefinitionId(id)) return Fail(error, L"Widget content definition id is invalid.");
    *definitionId = std::wstring(id);
    if (error) error->clear();
    return true;
}

bool MiaoWidgetContentCatalog::Resolve(
    std::wstring_view source,
    ResolvedWidgetContent* content,
    std::wstring* error) {
    const fs::path executable = paths::ExecutableDirectory();
    const fs::path stateRoot = paths::DesktopWidgetsRoot();
    const std::vector<fs::path> roots{
        executable.empty() ? fs::path{} : executable / L"Widgets",
        stateRoot.empty() ? fs::path{} : stateRoot / L"Packages",
    };
    return ResolveInRoots(source, roots, content, error);
}

bool MiaoWidgetContentCatalog::ResolveInRoots(
    std::wstring_view source,
    const std::vector<fs::path>& roots,
    ResolvedWidgetContent* content,
    std::wstring* error) {
    if (!content) return Fail(error, L"Resolved Widget content output is null.");
    *content = {};

    std::wstring definitionId;
    if (!ParseSource(source, &definitionId, error)) return false;

    for (const auto& root : roots) {
        bool found = false;
        ResolvedWidgetContent resolved;
        if (!ResolveOneRoot(definitionId, root, &resolved, &found, error)) return false;
        if (!found) continue;
        *content = std::move(resolved);
        if (error) error->clear();
        return true;
    }
    return Fail(error, L"Widget content package was not found: " + definitionId);
}

bool MiaoWidgetContentCatalog::SelfTest() {
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-WidgetCatalog-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const fs::path builtins = root / L"Builtins";
    const fs::path users = root / L"Users";
    const fs::path builtinPackage = builtins / L"Clock.mdwidget";
    const fs::path userPackage = users / L"ClockCopy.mdwidget";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(builtinPackage, ec);
    if (ec) return false;
    fs::create_directories(userPackage, ec);
    if (ec) { fs::remove_all(root, ec); return false; }

    constexpr std::string_view manifest = R"json({
      "schema":1,"id":"com.goodloong.catalog-clock","name":"Catalog Clock","author":"MiaoDesk","version":"1.0.0",
      "kind":"widget","runtime":"scene","entry":"scene.json","parameters":"parameters.json","capabilities":["clock.read"]
    })json";
    constexpr std::string_view scene = R"json({
      "schema":1,"id":"scene://catalog-clock","kind":"widget","profile":"widget","rootNodeId":"node://root",
      "nodes":[{"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]}],
      "assets":[],"shaders":[],"materials":[],"inputs":[],"bindings":[],"animations":[]
    })json";
    constexpr std::string_view parameters = R"json({"schema":1,"parameters":[]})json";

    bool ok = WriteText(builtinPackage / L"manifest.json", manifest) &&
              WriteText(builtinPackage / L"scene.json", scene) &&
              WriteText(builtinPackage / L"parameters.json", parameters) &&
              WriteText(userPackage / L"manifest.json", manifest) &&
              WriteText(userPackage / L"scene.json", scene) &&
              WriteText(userPackage / L"parameters.json", parameters);

    std::wstring error;
    const std::wstring source = MakeSource(L"com.goodloong.catalog-clock");
    ResolvedWidgetContent resolved;
    ok = ok && source == L"content:com.goodloong.catalog-clock" &&
         ResolveInRoots(source, {builtins, users}, &resolved, &error) &&
         resolved.packageRoot == builtinPackage &&
         resolved.definition.id == L"com.goodloong.catalog-clock" &&
         resolved.definition.kind == ContentKind::Widget;

    std::wstring parsedId;
    ok = ok && ParseSource(source, &parsedId, &error) && parsedId == L"com.goodloong.catalog-clock";
    ok = ok && !ParseSource(L"native:glass-clock", &parsedId, &error);
    ok = ok && !ResolveInRoots(L"content:missing.widget", {builtins, users}, &resolved, &error);

    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::content
