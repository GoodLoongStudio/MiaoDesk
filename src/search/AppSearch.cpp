#include "miaodesk/AppSearch.h"
#include "miaodesk/SearchDedupPolicy.h"
#include "miaodesk/SearchTextScoring.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <iterator>
#include <utility>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

void AddPathEntries(const fs::path& root, std::vector<AppSearch::Entry>& entries) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        auto ext = Lower(it->path().extension().wstring());
        if (ext != L".lnk" && ext != L".url" && ext != L".exe") continue;
        auto name = it->path().stem().wstring();
        if (name.empty()) continue;
        entries.push_back({name, it->path().wstring(), it->path().parent_path().wstring()});
    }
}

void AddRegistryAppPaths(HKEY root, REGSAM view, std::vector<AppSearch::Entry>& entries) {
    HKEY key{};
    constexpr wchar_t kPath[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths";
    if (RegOpenKeyExW(root, kPath, 0, KEY_READ | view, &key) != ERROR_SUCCESS) return;

    DWORD index = 0;
    wchar_t subkeyName[512];
    while (true) {
        DWORD nameLen = static_cast<DWORD>(std::size(subkeyName));
        const auto status = RegEnumKeyExW(key, index++, subkeyName, &nameLen, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) continue;

        HKEY appKey{};
        if (RegOpenKeyExW(key, subkeyName, 0, KEY_READ | view, &appKey) != ERROR_SUCCESS) continue;
        wchar_t value[32768];
        DWORD type = 0;
        DWORD bytes = sizeof(value);
        if (RegQueryValueExW(appKey, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            std::wstring target(value, bytes / sizeof(wchar_t));
            while (!target.empty() && target.back() == L'\0') target.pop_back();
            if (type == REG_EXPAND_SZ) {
                wchar_t expanded[32768];
                const DWORD len = ExpandEnvironmentStringsW(target.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
                if (len > 0 && len < std::size(expanded)) target.assign(expanded);
            }
            std::wstring name(subkeyName, nameLen);
            if (Lower(name).ends_with(L".exe")) name.resize(name.size() - 4);
            entries.push_back({name, target, L"App Paths"});
        }
        RegCloseKey(appKey);
    }
    RegCloseKey(key);
}

} // namespace

void AppSearch::BuildIndex() {
    entries_.clear();

    wchar_t buffer[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH))
        AddPathEntries(fs::path(buffer) / L"Microsoft/Windows/Start Menu/Programs", entries_);
    if (GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH))
        AddPathEntries(fs::path(buffer) / L"Microsoft/Windows/Start Menu/Programs", entries_);

    AddRegistryAppPaths(HKEY_CURRENT_USER, 0, entries_);
    AddRegistryAppPaths(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, entries_);
    AddRegistryAppPaths(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, entries_);

    const std::pair<const wchar_t*, const wchar_t*> builtins[] = {
        {L"Explorer", L"explorer.exe"}, {L"Notepad", L"notepad.exe"},
        {L"Command Prompt", L"cmd.exe"}, {L"PowerShell", L"powershell.exe"},
        {L"Task Manager", L"taskmgr.exe"}, {L"Control Panel", L"control.exe"},
        {L"Windows Terminal", L"wt.exe"}
    };
    for (const auto& [name, target] : builtins) entries_.push_back({name, target, L"Windows"});

    // 去重规则住在 SearchDedupPolicy(纯逻辑,本机可测)。这里只做一次形态转换,
    // 与下面 Query 调 RankSearchEntries 是同一个形状:keywords 原样带着走,
    // 因为去重只按 name+target 判,而 keywords 是排序的输入,不能在这里丢。
    // 分开的理由见那个头的说明 —— 这行原先内联在下面,和排序是同一个失败类
    // (用户看得见),却因为住在一个要 windows.h 的文件里而本机一行都跑不到。
    std::vector<std::wstring> seen;
    seen.reserve(entries_.size());
    std::vector<Entry> deduped;
    deduped.reserve(entries_.size());
    for (auto& entry : entries_) {
        SearchDedupEntry candidate;
        candidate.name = entry.name;
        candidate.target = entry.target;
        if (!IsFirstSearchEntry(candidate, seen)) continue;
        seen.push_back(SearchDedupKey(candidate));
        deduped.push_back(std::move(entry));
    }
    entries_ = std::move(deduped);
}

std::vector<SearchResult> AppSearch::Query(const std::wstring& query, std::size_t maxResults) const {
    // 排序规则住在 SearchTextScoring(纯逻辑,本机可测)。这里只做一次形态转换:
    // 索引里一条 entry 就是一条可排序候选,语义上一一对应,不重排、不加权、不过滤。
    // 分开的理由见那个头的说明:规则原先在本机一行都跑不到,而它是用户感知最强的一段。
    std::vector<SearchRankEntry> rankable;
    rankable.reserve(entries_.size());
    for (const auto& entry : entries_) {
        SearchRankEntry item;
        item.name = entry.name;
        item.keywords = entry.keywords;
        item.target = entry.target;
        item.subtitle = entry.target;
        rankable.push_back(std::move(item));
    }
    return RankSearchEntries(rankable, query, maxResults);
}

} // namespace miaodesk
