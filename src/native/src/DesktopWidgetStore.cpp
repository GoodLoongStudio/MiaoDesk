#include "turingdesk/DesktopWidgetStore.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

fs::path DefaultRoot() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path base = (length > 0 && length < std::size(local)) ? fs::path(local) : fs::temp_directory_path();
    return base / L"TuringDesk" / L"DesktopWidgets";
}

std::wstring ReadText(const fs::path& path, const std::wstring& section,
                      const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section.c_str(), key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

int ReadInt(const fs::path& path, const std::wstring& section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section.c_str(), key, static_cast<UINT>(fallback), path.c_str()));
}

float ReadFloat(const fs::path& path, const std::wstring& section, const wchar_t* key, float fallback) {
    wchar_t fallbackText[64]{};
    swprintf_s(fallbackText, L"%.6f", fallback);
    const std::wstring raw = ReadText(path, section, key, fallbackText);
    wchar_t* end = nullptr;
    const float value = std::wcstof(raw.c_str(), &end);
    return end == raw.c_str() ? fallback : value;
}

std::wstring FloatText(float value) {
    wchar_t text[64]{};
    swprintf_s(text, L"%.6f", value);
    return text;
}

bool SafeId(std::wstring_view id) {
    if (id.empty() || id.size() > 100) return false;
    for (wchar_t ch : id) {
        if (!(std::iswalnum(ch) || ch == L'-' || ch == L'_')) return false;
    }
    return true;
}

std::vector<std::wstring> SplitIds(std::wstring_view raw) {
    std::vector<std::wstring> result;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t separator = raw.find(L';', start);
        const std::size_t end = separator == std::wstring_view::npos ? raw.size() : separator;
        std::wstring id(raw.substr(start, end - start));
        if (SafeId(id)) result.push_back(std::move(id));
        if (separator == std::wstring_view::npos) break;
        start = separator + 1;
    }
    return result;
}

bool WriteText(const fs::path& path, const std::wstring& section,
               const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(section.c_str(), key, value.c_str(), path.c_str()) != FALSE;
}

bool IsHtmlSource(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return extension == L".html" || extension == L".htm";
}

bool WriteUtf8(const fs::path& path, std::string_view value) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(stream);
}

} // namespace

DesktopWidgetStore::DesktopWidgetStore() : root_(DefaultRoot()) {}
DesktopWidgetStore::DesktopWidgetStore(fs::path root) : root_(std::move(root)) {}

const std::vector<DesktopWidget>& DesktopWidgetStore::Items() const noexcept { return items_; }
const fs::path& DesktopWidgetStore::Root() const noexcept { return root_; }
fs::path DesktopWidgetStore::ManifestPath() const { return root_ / L"widgets.ini"; }
fs::path DesktopWidgetStore::PackageDirectory() const { return root_ / L"Packages"; }

const wchar_t* DesktopWidgetStore::KindKey(DesktopWidgetKind kind) noexcept {
    return kind == DesktopWidgetKind::Web ? L"web" : L"unknown";
}

DesktopWidgetKind DesktopWidgetStore::ParseKind(std::wstring_view value) noexcept {
    return _wcsicmp(std::wstring(value).c_str(), L"web") == 0 ? DesktopWidgetKind::Web : DesktopWidgetKind::Unknown;
}

DesktopWidget DesktopWidgetStore::Normalize(DesktopWidget widget) {
    widget.x = std::clamp(widget.x, 0.0f, 0.95f);
    widget.y = std::clamp(widget.y, 0.0f, 0.95f);
    widget.width = std::clamp(widget.width, 0.05f, 1.0f);
    widget.height = std::clamp(widget.height, 0.05f, 1.0f);
    if (widget.x + widget.width > 1.0f) widget.width = 1.0f - widget.x;
    if (widget.y + widget.height > 1.0f) widget.height = 1.0f - widget.y;
    widget.zIndex = std::clamp(widget.zIndex, -1000, 1000);
    if (widget.title.empty()) widget.title = L"Desktop Widget";
    return widget;
}

std::wstring DesktopWidgetStore::MakeId() {
    static std::atomic<unsigned long long> sequence{0};
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    return L"widget-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(ticks.QuadPart) + L"-" + std::to_wstring(++sequence);
}

std::optional<std::size_t> DesktopWidgetStore::FindIndex(std::wstring_view id) const noexcept {
    const std::wstring wanted(id);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (_wcsicmp(items_[i].id.c_str(), wanted.c_str()) == 0) return i;
    }
    return std::nullopt;
}

std::optional<DesktopWidget> DesktopWidgetStore::Find(std::wstring_view id) const {
    const auto index = FindIndex(id);
    return index ? std::optional<DesktopWidget>(items_[*index]) : std::nullopt;
}

bool DesktopWidgetStore::Load(std::wstring* error) {
    if (error) error->clear();
    items_.clear();
    std::error_code ec;
    fs::create_directories(PackageDirectory(), ec);
    if (ec) {
        if (error) *error = L"Unable to create desktop widget storage.";
        return false;
    }

    const fs::path manifest = ManifestPath();
    if (!fs::exists(manifest, ec)) return true;

    const std::wstring ids = ReadText(manifest, L"Widgets", L"Ids", L"");
    for (const auto& id : SplitIds(ids)) {
        const std::wstring section = L"Widget." + id;
        DesktopWidget widget;
        widget.id = id;
        widget.kind = ParseKind(ReadText(manifest, section, L"Kind", L"web"));
        widget.title = ReadText(manifest, section, L"Title", L"Desktop Widget");
        widget.source = ReadText(manifest, section, L"Source", L"");
        widget.monitorId = ReadText(manifest, section, L"MonitorId", L"");
        widget.x = ReadFloat(manifest, section, L"X", 0.68f);
        widget.y = ReadFloat(manifest, section, L"Y", 0.05f);
        widget.width = ReadFloat(manifest, section, L"Width", 0.28f);
        widget.height = ReadFloat(manifest, section, L"Height", 0.18f);
        widget.zIndex = ReadInt(manifest, section, L"ZIndex", 100);
        widget.enabled = ReadInt(manifest, section, L"Enabled", 1) != 0;
        widget.managedSource = ReadInt(manifest, section, L"ManagedSource", 0) != 0;
        widget = Normalize(std::move(widget));
        if (widget.kind == DesktopWidgetKind::Unknown || widget.source.empty()) continue;
        items_.push_back(std::move(widget));
    }
    return true;
}

bool DesktopWidgetStore::Save(std::wstring* error) const {
    if (error) error->clear();
    std::error_code ec;
    fs::create_directories(PackageDirectory(), ec);
    if (ec) {
        if (error) *error = L"Unable to create desktop widget storage.";
        return false;
    }

    const fs::path manifest = ManifestPath();
    DeleteFileW(manifest.c_str());

    std::wstring ids;
    for (const auto& widget : items_) {
        if (!SafeId(widget.id)) continue;
        if (!ids.empty()) ids.push_back(L';');
        ids += widget.id;
    }
    if (!WriteText(manifest, L"Widgets", L"Ids", ids)) {
        if (error) *error = L"Unable to save desktop widget index.";
        return false;
    }

    for (const auto& raw : items_) {
        if (!SafeId(raw.id)) continue;
        const DesktopWidget widget = Normalize(raw);
        const std::wstring section = L"Widget." + widget.id;
        bool ok = true;
        ok = WriteText(manifest, section, L"Kind", KindKey(widget.kind)) && ok;
        ok = WriteText(manifest, section, L"Title", widget.title) && ok;
        ok = WriteText(manifest, section, L"Source", widget.source.wstring()) && ok;
        ok = WriteText(manifest, section, L"MonitorId", widget.monitorId) && ok;
        ok = WriteText(manifest, section, L"X", FloatText(widget.x)) && ok;
        ok = WriteText(manifest, section, L"Y", FloatText(widget.y)) && ok;
        ok = WriteText(manifest, section, L"Width", FloatText(widget.width)) && ok;
        ok = WriteText(manifest, section, L"Height", FloatText(widget.height)) && ok;
        ok = WriteText(manifest, section, L"ZIndex", std::to_wstring(widget.zIndex)) && ok;
        ok = WriteText(manifest, section, L"Enabled", widget.enabled ? L"1" : L"0") && ok;
        ok = WriteText(manifest, section, L"ManagedSource", widget.managedSource ? L"1" : L"0") && ok;
        if (!ok) {
            if (error) *error = L"Unable to save desktop widget: " + widget.id;
            return false;
        }
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, manifest.c_str());
    return true;
}

std::optional<DesktopWidget> DesktopWidgetStore::Upsert(DesktopWidget widget, std::wstring* error) {
    if (error) error->clear();
    if (widget.id.empty()) widget.id = MakeId();
    if (!SafeId(widget.id)) {
        if (error) *error = L"Desktop widget id is invalid.";
        return std::nullopt;
    }
    widget = Normalize(std::move(widget));
    if (widget.kind != DesktopWidgetKind::Web || widget.source.empty() || !IsHtmlSource(widget.source)) {
        if (error) *error = L"Desktop widget currently requires a local HTML source.";
        return std::nullopt;
    }
    std::error_code ec;
    if (!fs::exists(widget.source, ec) || !fs::is_regular_file(widget.source, ec)) {
        if (error) *error = L"Desktop widget HTML source does not exist.";
        return std::nullopt;
    }

    const auto index = FindIndex(widget.id);
    if (index) items_[*index] = widget;
    else items_.push_back(widget);
    if (!Save(error)) return std::nullopt;
    return widget;
}

std::optional<DesktopWidget> DesktopWidgetStore::CreateManagedWeb(
    std::wstring title, std::string_view htmlUtf8, std::wstring monitorId,
    float x, float y, float width, float height, std::wstring* error) {
    if (error) error->clear();
    if (htmlUtf8.empty()) {
        if (error) *error = L"Desktop widget HTML is empty.";
        return std::nullopt;
    }

    DesktopWidget widget;
    widget.id = MakeId();
    widget.kind = DesktopWidgetKind::Web;
    widget.title = title.empty() ? L"AI Desktop Widget" : std::move(title);
    widget.monitorId = std::move(monitorId);
    widget.x = x;
    widget.y = y;
    widget.width = width;
    widget.height = height;
    widget.managedSource = true;
    widget = Normalize(std::move(widget));

    std::error_code ec;
    const fs::path package = PackageDirectory() / (widget.id + L".tdwidget");
    fs::create_directories(package, ec);
    if (ec) {
        if (error) *error = L"Unable to create managed desktop widget package.";
        return std::nullopt;
    }
    widget.source = package / L"index.html";
    if (!WriteUtf8(widget.source, htmlUtf8)) {
        fs::remove_all(package, ec);
        if (error) *error = L"Unable to write desktop widget HTML.";
        return std::nullopt;
    }

    const auto saved = Upsert(widget, error);
    if (!saved) fs::remove_all(package, ec);
    return saved;
}

bool DesktopWidgetStore::UpdateManagedHtml(std::wstring_view id, std::string_view htmlUtf8, std::wstring* error) {
    if (error) error->clear();
    const auto index = FindIndex(id);
    if (!index) {
        if (error) *error = L"Desktop widget was not found.";
        return false;
    }
    const DesktopWidget& widget = items_[*index];
    if (!widget.managedSource || widget.source.empty() || htmlUtf8.empty()) {
        if (error) *error = L"Desktop widget HTML is not managed by TuringDesk or is empty.";
        return false;
    }

    fs::path temporary = widget.source;
    temporary += L".tmp";
    if (!WriteUtf8(temporary, htmlUtf8)) {
        if (error) *error = L"Unable to write updated desktop widget HTML.";
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), widget.source.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(temporary, ec);
        if (error) *error = L"Unable to replace desktop widget HTML.";
        return false;
    }
    return true;
}

bool DesktopWidgetStore::Remove(std::wstring_view id, bool deleteManagedSource, std::wstring* error) {
    if (error) error->clear();
    const auto index = FindIndex(id);
    if (!index) {
        if (error) *error = L"Desktop widget was not found.";
        return false;
    }
    const DesktopWidget removed = items_[*index];
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(*index));
    if (!Save(error)) return false;
    if (deleteManagedSource && removed.managedSource && SafeId(removed.id)) {
        std::error_code ec;
        fs::remove_all(PackageDirectory() / (removed.id + L".tdwidget"), ec);
    }
    return true;
}

bool DesktopWidgetStore::SelfTest() {
    const fs::path root = fs::temp_directory_path() /
        (L"TuringDesk-DesktopWidget-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return false;

    DesktopWidgetStore store(root);
    std::wstring error;
    const auto created = store.CreateManagedWeb(
        L"Widget Test", "<!doctype html><html><body>OK</body></html>", L"", 0.1f, 0.2f, 0.3f, 0.4f, &error);
    bool ok = created.has_value() && fs::exists(created->source, ec);
    if (created) {
        DesktopWidget changed = *created;
        changed.x = 0.9f;
        changed.width = 0.5f;
        changed.enabled = false;
        const auto saved = store.Upsert(changed, &error);
        ok = ok && saved.has_value() && saved->width <= 0.1001f && !saved->enabled;
        ok = ok && store.UpdateManagedHtml(created->id, "<!doctype html><html><body>UPDATED</body></html>", &error);

        DesktopWidgetStore reloaded(root);
        ok = ok && reloaded.Load(&error);
        const auto persisted = reloaded.Find(created->id);
        ok = ok && persisted.has_value() && !persisted->enabled && persisted->managedSource;
        ok = ok && reloaded.Remove(created->id, true, &error);
        ok = ok && !fs::exists(root / L"Packages" / (created->id + L".tdwidget"), ec);
    }

    fs::remove_all(root, ec);
    return ok;
}

} // namespace turingdesk::wallpaper
