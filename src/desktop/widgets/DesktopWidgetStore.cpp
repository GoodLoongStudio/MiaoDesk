#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/AppPaths.h"
#include "miaodesk/MiaoWidgetContentCatalog.h"
#include "miaodesk/NativeWidgetPreset.h"

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

namespace miaodesk::wallpaper {
namespace {

fs::path DefaultRoot() {
    return paths::DesktopWidgetsRoot();
}

bool HasUtf16LeBom(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    unsigned char bom[2]{};
    stream.read(reinterpret_cast<char*>(bom), 2);
    return stream.gcount() == 2 && bom[0] == 0xff && bom[1] == 0xfe;
}

bool CreateUnicodeIni(const fs::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    const unsigned char bom[2]{0xff, 0xfe};
    stream.write(reinterpret_cast<const char*>(bom), 2);
    return static_cast<bool>(stream);
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

bool IsLostLegacyTitle(std::wstring_view title) {
    bool sawQuestion = false;
    for (const wchar_t ch : title) {
        if (ch == L'?') {
            sawQuestion = true;
            continue;
        }
        if (!std::iswspace(ch)) return false;
    }
    return sawQuestion;
}

constexpr wchar_t kWidgetStoreMutexName[] = L"Local\\MiaoDesk.DesktopWidgetStore.v1";

class WidgetStoreMutexGuard {
public:
    WidgetStoreMutexGuard() {
        handle_ = CreateMutexW(nullptr, FALSE, kWidgetStoreMutexName);
        if (!handle_) return;
        const DWORD wait = WaitForSingleObject(handle_, 5000);
        acquired_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    }
    ~WidgetStoreMutexGuard() {
        if (acquired_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
    }
    bool Acquired() const noexcept { return acquired_; }
private:
    HANDLE handle_{};
    bool acquired_{};
};

bool EnsureStoreLock(const WidgetStoreMutexGuard& guard, std::wstring* error) {
    if (guard.Acquired()) return true;
    if (error) *error = L"Desktop widget storage is busy; please retry.";
    return false;
}

std::wstring NativeSingletonKey(const DesktopWidget& widget) {
    if (widget.kind != DesktopWidgetKind::Native || !IsNativePresetSource(widget.source.wstring())) return {};
    std::wstring key = widget.source.wstring();
    key += L"|";
    key += widget.monitorId.empty() ? L"<primary>" : widget.monitorId;
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return key;
}

bool IsValidPersistedSource(const DesktopWidget& widget) {
    if (widget.kind == DesktopWidgetKind::Native) {
        return IsNativePresetSource(widget.source.wstring());
    }
    if (widget.kind == DesktopWidgetKind::Content) {
        std::wstring definitionId;
        return content::MiaoWidgetContentCatalog::ParseSource(widget.source.wstring(), &definitionId, nullptr);
    }
    return false;
}

} // namespace

DesktopWidgetStore::DesktopWidgetStore() : root_(DefaultRoot()) {}
DesktopWidgetStore::DesktopWidgetStore(fs::path root) : root_(std::move(root)) {}

const std::vector<DesktopWidget>& DesktopWidgetStore::Items() const noexcept { return items_; }
const fs::path& DesktopWidgetStore::Root() const noexcept { return root_; }
fs::path DesktopWidgetStore::ManifestPath() const { return root_ / L"widgets.ini"; }
fs::path DesktopWidgetStore::PackageDirectory() const { return root_ / L"Packages"; }

const wchar_t* DesktopWidgetStore::KindKey(DesktopWidgetKind kind) noexcept {
    switch (kind) {
    case DesktopWidgetKind::Native: return L"native";
    case DesktopWidgetKind::Content: return L"content";
    case DesktopWidgetKind::Unknown:
    default: return L"unknown";
    }
}

DesktopWidgetKind DesktopWidgetStore::ParseKind(std::wstring_view value) noexcept {
    const std::wstring kind(value);
    if (_wcsicmp(kind.c_str(), L"native") == 0) return DesktopWidgetKind::Native;
    if (_wcsicmp(kind.c_str(), L"content") == 0) return DesktopWidgetKind::Content;
    return DesktopWidgetKind::Unknown;
}

DesktopWidget DesktopWidgetStore::Normalize(DesktopWidget widget) {
    widget.x = std::clamp(widget.x, 0.0f, 0.95f);
    widget.y = std::clamp(widget.y, 0.0f, 0.95f);
    widget.width = std::clamp(widget.width, 0.05f, 1.0f);
    widget.height = std::clamp(widget.height, 0.05f, 1.0f);
    if (widget.x + widget.width > 1.0f) widget.width = 1.0f - widget.x;
    if (widget.y + widget.height > 1.0f) widget.height = 1.0f - widget.y;
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
    WidgetStoreMutexGuard storeLock;
    if (!EnsureStoreLock(storeLock, error)) return false;
    items_.clear();
    std::error_code ec;
    fs::create_directories(PackageDirectory(), ec);
    if (ec) {
        if (error) *error = L"Unable to create desktop widget storage.";
        return false;
    }

    const fs::path manifest = ManifestPath();
    if (!fs::exists(manifest, ec)) return true;
    const bool legacyAnsi = !HasUtf16LeBom(manifest);
    bool repairedText = false;

    const std::wstring ids = ReadText(manifest, L"Widgets", L"Ids", L"");
    for (const auto& id : SplitIds(ids)) {
        const std::wstring section = L"Widget." + id;
        DesktopWidget widget;
        widget.id = id;
        widget.kind = ParseKind(ReadText(manifest, section, L"Kind", L"native"));
        widget.title = ReadText(manifest, section, L"Title", L"Desktop Widget");
        widget.source = ReadText(manifest, section, L"Source", L"");
        widget.monitorId = ReadText(manifest, section, L"MonitorId", L"");
        widget.x = ReadFloat(manifest, section, L"X", 0.68f);
        widget.y = ReadFloat(manifest, section, L"Y", 0.05f);
        widget.width = ReadFloat(manifest, section, L"Width", 0.28f);
        widget.height = ReadFloat(manifest, section, L"Height", 0.18f);
        widget.enabled = ReadInt(manifest, section, L"Enabled", 1) != 0;

        NativeWidgetPreset nativePreset{};
        if (widget.kind == DesktopWidgetKind::Native &&
            ParseNativePreset(widget.source.wstring(), &nativePreset) &&
            IsLostLegacyTitle(widget.title)) {
            widget.title = NativePresetTitle(nativePreset);
            repairedText = true;
        }
        widget = Normalize(std::move(widget));
        if (widget.kind == DesktopWidgetKind::Unknown || widget.source.empty() || !IsValidPersistedSource(widget)) continue;

        const std::wstring singletonKey = NativeSingletonKey(widget);
        if (!singletonKey.empty()) {
            auto duplicate = std::find_if(items_.begin(), items_.end(), [&](const DesktopWidget& existing) {
                return NativeSingletonKey(existing) == singletonKey;
            });
            if (duplicate != items_.end()) {
                if (!duplicate->enabled && widget.enabled) *duplicate = std::move(widget);
                repairedText = true;
                continue;
            }
        }
        items_.push_back(std::move(widget));
    }

    if (legacyAnsi || repairedText) {
        std::wstring migrationError;
        if (!Save(&migrationError)) {
            if (error) *error = migrationError.empty() ? L"Unable to repair desktop widget storage." : migrationError;
            return false;
        }
    }
    return true;
}

bool DesktopWidgetStore::Save(std::wstring* error) const {
    if (error) error->clear();
    WidgetStoreMutexGuard storeLock;
    if (!EnsureStoreLock(storeLock, error)) return false;
    std::error_code ec;
    fs::create_directories(PackageDirectory(), ec);
    if (ec) {
        if (error) *error = L"Unable to create desktop widget storage.";
        return false;
    }

    const fs::path manifest = ManifestPath();
    fs::path temporary = manifest;
    temporary += L".tmp";
    DeleteFileW(temporary.c_str());
    if (!CreateUnicodeIni(temporary)) {
        if (error) *error = L"Unable to create temporary Unicode desktop widget manifest.";
        return false;
    }

    std::wstring ids;
    for (const auto& widget : items_) {
        if (!SafeId(widget.id)) continue;
        if (!ids.empty()) ids.push_back(L';');
        ids += widget.id;
    }
    if (!WriteText(temporary, L"Widgets", L"Ids", ids)) {
        DeleteFileW(temporary.c_str());
        if (error) *error = L"Unable to save desktop widget index.";
        return false;
    }

    for (const auto& raw : items_) {
        if (!SafeId(raw.id)) continue;
        const DesktopWidget widget = Normalize(raw);
        const std::wstring section = L"Widget." + widget.id;
        bool ok = true;
        ok = WriteText(temporary, section, L"Kind", KindKey(widget.kind)) && ok;
        ok = WriteText(temporary, section, L"Title", widget.title) && ok;
        ok = WriteText(temporary, section, L"Source", widget.source.wstring()) && ok;
        ok = WriteText(temporary, section, L"MonitorId", widget.monitorId) && ok;
        ok = WriteText(temporary, section, L"X", FloatText(widget.x)) && ok;
        ok = WriteText(temporary, section, L"Y", FloatText(widget.y)) && ok;
        ok = WriteText(temporary, section, L"Width", FloatText(widget.width)) && ok;
        ok = WriteText(temporary, section, L"Height", FloatText(widget.height)) && ok;
        ok = WriteText(temporary, section, L"Enabled", widget.enabled ? L"1" : L"0") && ok;
        if (!ok) {
            DeleteFileW(temporary.c_str());
            if (error) *error = L"Unable to save desktop widget: " + widget.id;
            return false;
        }
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());

    const DWORD moveFlags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED;
    if (!MoveFileExW(temporary.c_str(), manifest.c_str(), moveFlags)) {
        const DWORD moveCode = GetLastError();

        // Packaged/MSIX file-system redirection can make two paths that are
        // logically in the same directory resolve to different backing volumes.
        // MOVEFILE_COPY_ALLOWED should handle that, but keep an explicit
        // copy/delete fallback for environments or filter drivers that still
        // return ERROR_NOT_SAME_DEVICE.
        if (moveCode == ERROR_NOT_SAME_DEVICE) {
            if (CopyFileW(temporary.c_str(), manifest.c_str(), FALSE)) {
                DeleteFileW(temporary.c_str());
                return true;
            }

            const DWORD copyCode = GetLastError();
            DeleteFileW(temporary.c_str());
            if (error) {
                *error = L"Unable to replace desktop widget manifest across storage boundary. MoveWin32=" +
                         std::to_wstring(moveCode) + L", CopyWin32=" + std::to_wstring(copyCode);
            }
            return false;
        }

        DeleteFileW(temporary.c_str());
        if (error) *error = L"Unable to atomically replace desktop widget manifest. Win32=" + std::to_wstring(moveCode);
        return false;
    }
    return true;
}

std::optional<DesktopWidget> DesktopWidgetStore::Upsert(DesktopWidget widget, std::wstring* error) {
    if (error) error->clear();
    WidgetStoreMutexGuard storeLock;
    if (!EnsureStoreLock(storeLock, error)) return std::nullopt;
    std::wstring refreshError;
    if (!Load(&refreshError)) {
        if (error) *error = refreshError;
        return std::nullopt;
    }
    if (widget.id.empty()) widget.id = MakeId();
    if (!SafeId(widget.id)) {
        if (error) *error = L"Desktop widget id is invalid.";
        return std::nullopt;
    }
    widget = Normalize(std::move(widget));
    if (!IsValidPersistedSource(widget)) {
        if (error) *error = L"Desktop widget source is invalid.";
        return std::nullopt;
    }

    const std::wstring singletonKey = NativeSingletonKey(widget);
    if (!singletonKey.empty()) {
        const auto duplicate = std::find_if(items_.begin(), items_.end(), [&](const DesktopWidget& existing) {
            return _wcsicmp(existing.id.c_str(), widget.id.c_str()) != 0 &&
                   NativeSingletonKey(existing) == singletonKey;
        });
        if (duplicate != items_.end()) {
            if (error) *error = L"This native widget preset already exists on the target monitor.";
            return std::nullopt;
        }
    }

    const auto index = FindIndex(widget.id);
    if (index) items_[*index] = widget;
    else items_.push_back(widget);
    if (!Save(error)) return std::nullopt;
    return widget;
}

std::optional<DesktopWidget> DesktopWidgetStore::CreateManagedNative(
    NativeWidgetPreset preset, std::wstring title, std::wstring monitorId,
    float x, float y, float width, float height, std::wstring* error) {
    if (error) error->clear();

    DesktopWidget widget;
    widget.id = MakeId();
    widget.kind = DesktopWidgetKind::Native;
    widget.title = title.empty() ? NativePresetTitle(preset) : std::move(title);
    widget.source = fs::path(NativePresetSource(preset));
    widget.monitorId = std::move(monitorId);
    widget.x = x;
    widget.y = y;
    widget.width = width;
    widget.height = height;
    widget = Normalize(std::move(widget));
    return Upsert(widget, error);
}

bool DesktopWidgetStore::Remove(std::wstring_view id, std::wstring* error) {
    if (error) error->clear();
    WidgetStoreMutexGuard storeLock;
    if (!EnsureStoreLock(storeLock, error)) return false;
    std::wstring refreshError;
    if (!Load(&refreshError)) { if (error) *error = refreshError; return false; }
    const auto index = FindIndex(id);
    if (!index) {
        if (error) *error = L"Desktop widget was not found.";
        return false;
    }
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(*index));
    return Save(error);
}

bool DesktopWidgetStore::SelfTest() {
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-DesktopWidget-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return false;

    DesktopWidgetStore store(root);
    std::wstring error;
    const auto created = store.CreateManagedNative(
        NativeWidgetPreset::GlassClock, L"原生时钟", L"monitor-test", 0.2f, 0.3f, 0.25f, 0.18f, &error);
    bool ok = created.has_value() && created->kind == DesktopWidgetKind::Native &&
              HasUtf16LeBom(store.ManifestPath());
    const auto duplicateNative = store.CreateManagedNative(
        NativeWidgetPreset::GlassClock, L"重复原生时钟", L"monitor-test", 0.4f, 0.2f, 0.25f, 0.18f, &error);
    ok = ok && !duplicateNative.has_value();
    if (created) {
        DesktopWidget changed = *created;
        changed.x = 0.9f;
        changed.width = 0.5f;
        changed.enabled = false;
        const auto saved = store.Upsert(changed, &error);
        ok = ok && saved.has_value() && saved->width <= 0.1001f && !saved->enabled;

        DesktopWidget contentWidget;
        contentWidget.id = DesktopWidgetStore::MakeId();
        contentWidget.kind = DesktopWidgetKind::Content;
        contentWidget.title = L"Content Clock";
        contentWidget.source = fs::path(content::MiaoWidgetContentCatalog::MakeSource(L"com.goodloong.glass-clock"));
        contentWidget.monitorId = L"monitor-content";
        contentWidget.enabled = false;
        const auto savedContent = store.Upsert(contentWidget, &error);
        ok = ok && savedContent.has_value() && savedContent->kind == DesktopWidgetKind::Content;

        DesktopWidget invalidContent = contentWidget;
        invalidContent.id = DesktopWidgetStore::MakeId();
        invalidContent.source = fs::path(L"content:");
        ok = ok && !store.Upsert(invalidContent, &error).has_value();

        DesktopWidgetStore reloaded(root);
        ok = ok && reloaded.Load(&error);
        const auto persisted = reloaded.Find(created->id);
        ok = ok && persisted.has_value() && !persisted->enabled &&
             persisted->title == L"原生时钟" && persisted->monitorId == L"monitor-test";
        if (savedContent) {
            const auto persistedContent = reloaded.Find(savedContent->id);
            ok = ok && persistedContent.has_value() &&
                 persistedContent->kind == DesktopWidgetKind::Content &&
                 persistedContent->source == savedContent->source;
        }
        ok = ok && reloaded.Remove(created->id, &error);

        DesktopWidgetStore reloadedAfterRemove(root);
        ok = ok && reloadedAfterRemove.Load(&error) && !reloadedAfterRemove.Find(created->id).has_value();
    }

    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::wallpaper
