#include "miaodesk/TodayTaskStore.h"

#include "miaodesk/AppPaths.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::desktop {
namespace {

constexpr std::size_t kMaxTasks = 32;
constexpr std::size_t kMaxTitleLength = 160;
constexpr std::size_t kMaxDetailLength = 240;

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

std::wstring Trim(std::wstring value) {
    const auto notSpace = [](wchar_t ch) {
        return ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n';
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool SafeId(std::wstring_view value) {
    if (value.empty() || value.size() > 80) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_';
    });
}

bool ValidText(std::wstring_view value, std::size_t maxLength) {
    if (value.size() > maxLength) return false;
    return std::none_of(value.begin(), value.end(), [](wchar_t ch) { return ch == L'\0'; });
}

bool CreateUnicodeIni(const fs::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    constexpr unsigned char bom[2]{0xff, 0xfe};
    stream.write(reinterpret_cast<const char*>(bom), 2);
    return static_cast<bool>(stream);
}

std::wstring ReadText(
    const fs::path& path,
    std::wstring_view section,
    std::wstring_view key,
    std::wstring_view fallback = {}) {
    std::vector<wchar_t> buffer(4096);
    const std::wstring sectionText(section);
    const std::wstring keyText(key);
    const std::wstring fallbackText(fallback);
    GetPrivateProfileStringW(sectionText.c_str(), keyText.c_str(), fallbackText.c_str(),
                             buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

bool WriteText(
    const fs::path& path,
    std::wstring_view section,
    std::wstring_view key,
    std::wstring_view value) {
    const std::wstring sectionText(section);
    const std::wstring keyText(key);
    const std::wstring valueText(value);
    return WritePrivateProfileStringW(sectionText.c_str(), keyText.c_str(), valueText.c_str(), path.c_str()) != FALSE;
}

std::wstring TaskSection(std::size_t index) {
    return L"Task." + std::to_wstring(index);
}

TodayTaskItem Normalize(TodayTaskItem item, std::size_t index) {
    item.title = Trim(std::move(item.title));
    item.detail = Trim(std::move(item.detail));
    if (!SafeId(item.id)) item.id = L"task-" + std::to_wstring(index + 1);
    return item;
}

bool Validate(const std::vector<TodayTaskItem>& items, std::wstring* error) {
    if (items.size() > kMaxTasks) return Fail(error, L"Today Tasks 最多支持 32 条任务。");
    std::vector<std::wstring> ids;
    ids.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto item = Normalize(items[i], i);
        if (item.title.empty()) return Fail(error, L"Today Tasks 标题不能为空。");
        if (!ValidText(item.title, kMaxTitleLength)) return Fail(error, L"Today Tasks 标题过长。");
        if (!ValidText(item.detail, kMaxDetailLength)) return Fail(error, L"Today Tasks 详情过长。");
        if (!SafeId(item.id)) return Fail(error, L"Today Tasks id 无效。");
        if (std::find(ids.begin(), ids.end(), item.id) != ids.end())
            return Fail(error, L"Today Tasks id 重复：" + item.id);
        ids.push_back(item.id);
    }
    return true;
}

std::uint64_t GenerationFor(const fs::path& path) {
    std::error_code ec;
    const auto stamp = fs::last_write_time(path, ec);
    if (ec) return 0;
    const auto ticks = stamp.time_since_epoch().count();
    return static_cast<std::uint64_t>(ticks < 0 ? -ticks : ticks);
}

bool SaveToPath(const fs::path& path, const std::vector<TodayTaskItem>& source, std::wstring* error) {
    if (!Validate(source, error)) return false;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return Fail(error, L"无法创建 Today Tasks 状态目录。");

    fs::path temporary = path;
    temporary += L".tmp";
    fs::remove(temporary, ec);
    ec.clear();
    if (!CreateUnicodeIni(temporary)) return Fail(error, L"无法创建 Today Tasks 临时文件。");

    if (!WriteText(temporary, L"Tasks", L"Count", std::to_wstring(source.size()))) {
        fs::remove(temporary, ec);
        return Fail(error, L"无法写入 Today Tasks 索引。");
    }

    for (std::size_t i = 0; i < source.size(); ++i) {
        const TodayTaskItem item = Normalize(source[i], i);
        const std::wstring section = TaskSection(i);
        if (!WriteText(temporary, section, L"Id", item.id) ||
            !WriteText(temporary, section, L"Title", item.title) ||
            !WriteText(temporary, section, L"Detail", item.detail) ||
            !WriteText(temporary, section, L"Completed", item.completed ? L"1" : L"0")) {
            fs::remove(temporary, ec);
            return Fail(error, L"无法写入 Today Tasks 条目。");
        }
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());

    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED)) {
        const DWORD code = GetLastError();
        fs::remove(temporary, ec);
        return Fail(error, L"无法原子替换 Today Tasks 状态文件。Win32=" + std::to_wstring(code));
    }
    if (error) error->clear();
    return true;
}

std::wstring EnvironmentValue(std::wstring_view name) {
    std::array<wchar_t, 32768> buffer{};
    const std::wstring key(name);
    const DWORD count = GetEnvironmentVariableW(key.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) return {};
    return std::wstring(buffer.data(), count);
}

} // namespace

fs::path TodayTaskStore::StorePath() {
    return paths::StateFile(L"today-tasks.ini");
}

bool TodayTaskStore::Load(TodayTaskSnapshot* snapshot, std::wstring* error) {
    if (!snapshot) return Fail(error, L"Today Tasks snapshot 输出不能为空。");
    *snapshot = {};
    const fs::path path = StorePath();
    if (path.empty()) return Fail(error, L"Today Tasks 状态路径不可用。");

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        if (ec) return Fail(error, L"无法检查 Today Tasks 状态文件。");
        snapshot->valid = true;
        if (error) error->clear();
        return true;
    }

    const std::wstring countText = ReadText(path, L"Tasks", L"Count", L"0");
    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(countText.c_str(), &end, 10);
    if (!end || *end != L'\0' || parsed > kMaxTasks)
        return Fail(error, L"Today Tasks 状态文件中的 Count 无效。");

    snapshot->items.reserve(static_cast<std::size_t>(parsed));
    for (std::size_t i = 0; i < static_cast<std::size_t>(parsed); ++i) {
        const std::wstring section = TaskSection(i);
        TodayTaskItem item;
        item.id = ReadText(path, section, L"Id");
        item.title = ReadText(path, section, L"Title");
        item.detail = ReadText(path, section, L"Detail");
        item.completed = ReadText(path, section, L"Completed", L"0") == L"1";
        item = Normalize(std::move(item), i);
        if (item.title.empty() || !ValidText(item.title, kMaxTitleLength) ||
            !ValidText(item.detail, kMaxDetailLength) || !SafeId(item.id))
            return Fail(error, L"Today Tasks 状态文件包含无效条目。");
        snapshot->items.push_back(std::move(item));
    }

    snapshot->total = snapshot->items.size();
    snapshot->completed = static_cast<std::size_t>(std::count_if(
        snapshot->items.begin(), snapshot->items.end(), [](const TodayTaskItem& item) { return item.completed; }));
    snapshot->pending = snapshot->total - snapshot->completed;
    snapshot->generation = GenerationFor(path);
    snapshot->valid = true;
    if (error) error->clear();
    return true;
}

bool TodayTaskStore::Replace(const std::vector<TodayTaskItem>& items, std::wstring* error) {
    const fs::path path = StorePath();
    if (path.empty()) return Fail(error, L"Today Tasks 状态路径不可用。");
    return SaveToPath(path, items, error);
}

bool TodayTaskStore::SetCompleted(std::wstring_view id, bool completed, std::wstring* error) {
    if (!SafeId(id)) return Fail(error, L"Today Tasks id 无效。");
    TodayTaskSnapshot snapshot;
    if (!Load(&snapshot, error)) return false;
    const auto found = std::find_if(snapshot.items.begin(), snapshot.items.end(), [&](const TodayTaskItem& item) {
        return item.id == id;
    });
    if (found == snapshot.items.end()) return Fail(error, L"没有找到 Today Tasks 条目：" + std::wstring(id));
    if (found->completed == completed) {
        if (error) error->clear();
        return true;
    }
    found->completed = completed;
    return Replace(snapshot.items, error);
}

bool TodayTaskStore::SelfTest() {
    const std::wstring oldLocalAppData = EnvironmentValue(L"LOCALAPPDATA");
    std::array<wchar_t, MAX_PATH> tempRoot{};
    if (GetTempPathW(static_cast<DWORD>(tempRoot.size()), tempRoot.data()) == 0) return false;
    const fs::path testRoot = fs::path(tempRoot.data()) /
        (L"MiaoDesk-TodayTasks-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::create_directories(testRoot, ec);
    if (ec) return false;

    SetEnvironmentVariableW(L"LOCALAPPDATA", testRoot.c_str());
    const auto restore = [&]() {
        if (oldLocalAppData.empty()) SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
        else SetEnvironmentVariableW(L"LOCALAPPDATA", oldLocalAppData.c_str());
        fs::remove_all(testRoot, ec);
    };

    std::wstring error;
    const std::vector<TodayTaskItem> tasks{
        {L"alpha", L"完成 Content TodayTasks 数据源", L"今天 10:00", false},
        {L"beta", L"验证完成状态持久化", L"今天 14:00", true},
    };
    if (!Replace(tasks, &error)) { restore(); return false; }

    TodayTaskSnapshot snapshot;
    if (!Load(&snapshot, &error) || !snapshot.valid || snapshot.total != 2 ||
        snapshot.pending != 1 || snapshot.completed != 1 || snapshot.items[0].title != tasks[0].title) {
        restore();
        return false;
    }
    if (!SetCompleted(L"alpha", true, &error)) { restore(); return false; }
    if (!Load(&snapshot, &error) || snapshot.pending != 0 || snapshot.completed != 2) {
        restore();
        return false;
    }
    restore();
    return true;
}

} // namespace miaodesk::desktop
